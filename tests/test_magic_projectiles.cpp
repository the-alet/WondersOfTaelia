#include "test.h"
#include "taelia/sim/world.h"
#include <cmath>
using namespace taelia;

namespace {
void gather(World& w, Aspect aspect, int steps = 90) {
    w.setFocus(w.humanPosition() + dvec3{0, 1.6, 0});
    w.step();
    PlayerInput in; in.gather = true; in.face = true; in.aspect = aspect;
    w.setPlayerInput(in);
    for (int i = 0; i < steps; ++i) w.step();
    CHECK(w.gathering().kg > 4.0);
}
WorldDesc smallMagicWorld() {
    WorldDesc d;
    d.magic.levels = 1; d.magic.columns = 16; d.magic.layers[0] = 16;
    d.magic.threads = 1;
    return d;
}
}

TEST(magic_ball_loss_is_one_kg_per_actual_metre_and_its_radius_shrinks) {
    World w(smallMagicWorld());
    gather(w, Aspect::Earth);
    const double gathered = w.gathering().kg;
    PlayerInput in; in.face = true;
    in.aim = w.gathering().position + dvec3{0, 12, 30};
    w.setPlayerInput(in);
    for (int i = 0; i < 12; ++i) {
        w.step();
        CHECK(w.magicBalls().size() == 1u);
        if (w.magicBalls().empty()) return;
        const MagicBall& b = w.magicBalls().front();
        CHECK_NEAR(gathered - b.kg, b.travelled * k::MAGIC_BALL_LOSS_PER_METRE, 1e-7);
        CHECK_NEAR(b.radius(), k::MAGIC_BALL_RADIUS * std::cbrt(std::max(b.fill(), 0.04)), 1e-8);
    }
    MagicBall full; full.kg = k::MAGIC_BALL_FULL;
    CHECK_NEAR(full.radius(), 0.24, 1e-7);
}

TEST(moving_magic_deposit_keeps_mass_and_mixes_its_momentum) {
    MagicOptions opt;
    opt.levels = 1; opt.columns = 16; opt.layers[0] = 8; opt.flatGround = true;
    opt.takingRule = false; opt.threads = 1;
    MagicField f(nullptr, nullptr, opt);
    f.setFocus({1, 5, 1}); f.step(0);
    const dvec3 p{1, 5, 1};
    MagicDensities before; double volume;
    CHECK(f.cellAt(p, before, &volume));
    const auto old = f.sample(p);
    const double mass = f.totalMass(Aspect::Fire);
    CHECK(f.addMovingMass(p, Aspect::Fire, 4, {3, 0, 0}));
    CHECK_NEAR(f.totalMass(Aspect::Fire) - mass, 4, 1e-5);
    const double expected = (before.total() * volume * old.u.x + 12) / (before.total() * volume + 4);
    CHECK_NEAR(f.sample(p).u.x, expected, 1e-5);
}

TEST(a_magic_ball_contacts_a_physical_ball_before_the_ground_behind_it) {
    World w(smallMagicWorld());
    gather(w, Aspect::Earth);
    const dvec3 target = w.gathering().position + dvec3{0, 0, 1.2};
    const uint32_t id = w.throwBall(target, {0, 0, 0});
    PlayerInput in; in.face = true; in.aim = target;
    w.setPlayerInput(in);
    double pushed = 0;
    for (int i = 0; i < 12; ++i) {
        w.step();
        for (const Ball& b : w.balls()) if (b.id == id) pushed = std::max(pushed, b.velocity.z);
    }
    CHECK(w.magicBalls().empty());
    CHECK(pushed > 0.5);
}

TEST(a_dense_fire_ball_scorches_without_denting_the_ground_at_contact) {
    World w(smallMagicWorld());
    gather(w, Aspect::Fire);
    const dvec3 from = w.gathering().position;
    const dvec3 target{from.x, w.groundHeight(from.x, from.z), from.z};
    const double before = w.terrain().editAt(target.x, target.z);
    PlayerInput in; in.face = true; in.aim = target;
    w.setPlayerInput(in);
    for (int i = 0; i < 30; ++i) w.step();
    CHECK(w.magicBalls().empty());
    CHECK_NEAR(w.terrain().editAt(target.x, target.z), before, 1e-9);
    bool scorched = false;
    for (const World::Glow& s : w.scorches())
        scorched = scorched || std::hypot(s.position.x - target.x, s.position.z - target.z) < 0.5;
    CHECK(scorched);
}

TEST(a_suspended_rune_takes_matching_magic_from_a_ball_that_passes_through) {
    World w(smallMagicWorld());
    gather(w, Aspect::Earth);
    const dvec3 from = w.gathering().position;
    Rune r; r.placement = RunePlacement::Air; r.aspect = Aspect::Earth;
    r.position = from + dvec3{0, 0.3, 1};
    const uint32_t id = w.addRune(r);
    PlayerInput in; in.face = true; in.aim = from + dvec3{0, 3, 10};
    w.setPlayerInput(in);
    for (int i = 0; i < 5; ++i) w.step();
    CHECK(!w.magicBalls().empty()); // the sign is not a solid carrier
    double contents = 0;
    for (const Rune& rune : w.runes()) if (rune.id == id) contents = rune.mass();
    CHECK(contents > 0.05);
}

TEST(an_earth_ball_can_dent_ground_while_other_aspects_cannot) {
    for (const Aspect aspect : {Aspect::Earth, Aspect::Water, Aspect::Air, Aspect::Light, Aspect::Life, Aspect::Necrotic}) {
        World w(smallMagicWorld());
        const dvec3 from = w.humanPosition()+dvec3{0,2,0};
        w.setFocus(from); w.step();
        const dvec3 target{from.x, w.groundHeight(from.x, from.z), from.z};
        const uint64_t before = w.terrain().editsVersion();
        w.throwMagicBall(from,normalize(target-from)*20.0,aspect,60);
        for (int i=0;i<30;++i) w.step();
        CHECK(w.magicBalls().empty());
        const uint64_t after = w.terrain().editsVersion();
        if(aspect==Aspect::Earth) CHECK(after > before);
        else CHECK(after == before);
    }
}
