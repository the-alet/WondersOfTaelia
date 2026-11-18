#include "taelia/shared_constants.h"
#include "taelia/sim/world.h"
#include "test.h"

using namespace taelia;

namespace {

WorldDesc smallMagicWorld() {
    WorldDesc d;
    d.magic.columns = 16;
    d.magic.layers[0] = 8;
    d.magic.levels = 1;
    d.magic.threads = 1;
    return d;
}

}  // namespace

TEST(drawing_a_rune_reaches_three_metres_and_released_partial_strokes_stay_inert) {
    World w(smallMagicWorld());
    const dvec3 eye{0, 100, 0};
    const uint32_t id = w.drawRune(eye, {0, 0, 1}, Aspect::Fire);
    CHECK(id != 0);
    CHECK(w.runes().size() == 1u);
    if (w.runes().empty()) return;
    CHECK_NEAR(length(w.runes()[0].position - eye), 3.0, 1e-9);
    w.chargeRune(id, 0.5, true);
    CHECK_NEAR(w.runes()[0].drawProgress, 0.5, 1e-9);
    CHECK_NEAR(w.runes()[0].density(), 5.0, 1e-6);
    CHECK(!w.runes()[0].drawing);
    CHECK_NEAR(w.runes()[0].mass(), 5.0 * w.runes()[0].volume(), 1e-8);
    for (int i = 0; i < 60; ++i) w.step();
    CHECK_NEAR(w.runes()[0].mass(), 5.0 * w.runes()[0].volume(), 1e-6);
}

TEST(rune_charge_scales_to_full_density_seals_and_breaks_after_overcharge) {
    World full(smallMagicWorld());
    const uint32_t fullId = full.drawRune({0, 100, 0}, {0, 0, 1}, Aspect::Earth);
    full.chargeRune(fullId, 1.0, false);
    CHECK_NEAR(full.runes()[0].drawProgress, 1.0, 1e-9);
    CHECK_NEAR(full.runes()[0].density(), 10.0, 1e-6);
    full.chargeRune(fullId, 5.0, false);
    CHECK_NEAR(full.runes()[0].density(), 50.0, 1e-6);
    CHECK_NEAR(full.runes()[0].seal(), 150.0, 1e-9);

    MagicBall ball;
    ball.fullHold = 2.0;
    CHECK_NEAR(ball.instability(), 0.0, 1e-9);
    ball.fullHold = 3.5;
    CHECK_NEAR(ball.instability(), 0.5, 1e-9);
    ball.fullHold = 5.0;
    CHECK_NEAR(ball.instability(), 1.0, 1e-9);

    World overcharged(smallMagicWorld());
    const uint32_t overId = overcharged.drawRune({0, 100, 0}, {0, 0, 1}, Aspect::Water);
    overcharged.chargeRune(overId, 6.5, false);
    CHECK(overcharged.runes().size() == 1u);
    CHECK_NEAR(overcharged.runes()[0].overcharge, 0.5, 1e-9);
    overcharged.chargeRune(overId, 8.0, false);
    CHECK(overcharged.runes().empty());
}

TEST(breaking_a_charged_ground_rune_releases_its_mass_into_the_focused_field) {
    World w(smallMagicWorld());
    const double ground = w.groundHeight(0, 0);
    const dvec3 eye{0, ground + 1.0, 0};
    w.setFocus({0, ground + 1.0, 0});
    w.step();
    const uint32_t id = w.drawRune(eye, {0, -1, 0}, Aspect::Air);
    CHECK(id != 0);
    if (!id) return;
    w.chargeRune(id, 5.0, true);
    const Rune rune = w.runes().front();
    const double mass = rune.mass();
    const double before = w.magic().totalMass(Aspect::Air);
    w.damageRune(id);
    CHECK(w.runes().empty());
    CHECK_NEAR(w.magic().totalMass(Aspect::Air) - before, mass, 1e-5 * mass);
}

TEST(necrotic_field_grass_damage_persists_after_necrotic_magic_is_removed) {
    WorldDesc d = smallMagicWorld();
    d.magic.flatGround = true;
    d.magic.takingRule = false;
    World w(d);
    const dvec3 p{1, 1, 1};
    w.setFocus(p);
    w.step();
    CHECK(w.magic().addDensity(p, Aspect::Necrotic, 20.0));
    for (int i = 0; i < 120; ++i) w.step();
    const double damaged = w.magicAt(p).grassDamage;
    CHECK(damaged > 0.1);
    MagicDensities cell;
    double volume = 0;
    CHECK(w.magic().cellAt(p, cell, &volume));
    CHECK(w.magic().addMass(p, Aspect::Necrotic, -cell[Aspect::Necrotic] * volume));
    for (int i = 0; i < 5; ++i) w.step();
    CHECK(w.magicAt(p).rho[Aspect::Necrotic] < 1.0);
    CHECK(w.magicAt(p).grassDamage >= damaged);
}

TEST(light_and_necrotic_magic_cancel_equal_amounts_without_losing_mass) {
    for (const float light : {3.0f,7.0f,12.0f}) {
        MagicDensities d; d[Aspect::Light]=light; d[Aspect::Necrotic]=7;
        const double total=d.total();
        magic::neutralizeLight(d);
        CHECK(d[Aspect::Light]==0 || d[Aspect::Necrotic]==0);
        CHECK_NEAR(d[Aspect::Light],std::max(light-7.0f,0.0f),1e-6);
        CHECK_NEAR(d[Aspect::Necrotic],std::max(7.0f-light,0.0f),1e-6);
        CHECK_NEAR(d.total(),total,1e-6);
    }
}

TEST(necrotic_retention_is_longest_in_earth_then_water_fire_and_air) {
    double previous=1e10;
    for(const Aspect a : {Aspect::Earth,Aspect::Water,Aspect::Fire,Aspect::Air}) {
        MagicDensities d; d[a]=3; d[Aspect::Necrotic]=20;
        const double retention=magic::necroticRetention(d);
        CHECK(retention>0 && retention<previous);
        previous=retention;
    }
}

TEST(magic_field_ticks_cancel_light_against_necrotic_without_losing_their_total) {
    WorldDesc d = smallMagicWorld();
    d.magic.flatGround = true;
    d.magic.takingRule = false;
    d.magic.closed = true;
    MagicField field(nullptr, nullptr, d.magic);
    field.setFocus({1, 1, 1});
    field.step(0);
    const dvec3 at{1, 1, 1};
    CHECK(field.addDensity(at, Aspect::Light, 7.0));
    CHECK(field.addDensity(at, Aspect::Necrotic, 3.0));
    const auto mass = [&]() { double sum=0; for(int a=0;a<kAspects;++a) sum+=field.totalMass(Aspect(a)); return sum; };
    const double before = mass();
    field.step(2);
    CHECK_NEAR(mass(), before, 1e-5 * before);
    MagicDensities cell;
    CHECK(field.cellAt(at, cell));
    CHECK(cell[Aspect::Light] > 1e-5);  // the three-unit necrotic amount was cancelled
    for (int x = -7; x <= 7; x += 2)
        for (int z = -7; z <= 7; z += 2)
            for (int y = 1; y <= 7; y += 2) {
                CHECK(field.cellAt({double(x), double(y), double(z)}, cell));
                CHECK(!(cell[Aspect::Light] > 1e-5 && cell[Aspect::Necrotic] > 1e-5));
            }
}

TEST(full_magic_ball_manifests_once_life_is_exempt_and_overcharge_blocks_regathering) {
    for (const Aspect aspect : {Aspect::Earth, Aspect::Life}) {
        WorldDesc d = smallMagicWorld();
        World w(d);
        w.placePlayer({40, w.groundHeight(40,-40), -40}, 0.0);
        w.setFocus(w.humanPosition() + dvec3{0, 1.6, 0});
        w.step();
        const dvec3 supply = w.humanPosition() + dvec3{0, 1, 0};
        CHECK(w.magic().addMass(supply, Aspect::Neutral, 20000.0));
        PlayerInput in;
        in.gather = true;
        in.aspect = aspect;
        w.setPlayerInput(in);
        for (int i = 0; i < 1200 && !w.gathering().manifested; ++i) w.step();
        CHECK(w.gathering().manifested);
        if (!w.gathering().manifested) continue;
        if (aspect == Aspect::Earth) {
            CHECK(w.physicalManifestations().size() == 1u);
            CHECK(w.physicalManifestations()[0].aspect == Aspect::Earth);
        } else {
            CHECK(w.physicalManifestations().empty());
        }
        for (int i = 0; i < 360; ++i) w.step();
        CHECK(w.gathering().kg == 0.0);
        CHECK(w.physicalManifestations().empty());
        size_t createdMinerals = 0;
        for (const auto& m : w.magic().manifestations()) if (m.id >= 0x70000000u) ++createdMinerals;
        CHECK(createdMinerals == (aspect == Aspect::Earth ? 1u : 0u));
        for (int i = 0; i < 120; ++i) w.step();
        CHECK(w.gathering().kg == 0.0);  // still held: gather remains blocked after bursting
        in.gather = false;
        w.setPlayerInput(in);
        w.step();
        in.gather = true;
        w.setPlayerInput(in);
        w.step();
        CHECK(w.gathering().kg > 0.0);  // releasing resets the block
    }
}
