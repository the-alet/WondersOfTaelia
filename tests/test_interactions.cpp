#include <cmath>

#include "taelia/shared_constants.h"
#include "taelia/sim/interaction.h"
#include "taelia/sim/physics.h"
#include "taelia/sim/rune.h"
#include "taelia/sim/world.h"
#include "test.h"

using namespace taelia;

namespace {

WorldDesc smallWorld() {
    WorldDesc d;
    d.magic.levels = 1;
    d.magic.columns = 16;
    d.magic.layers[0] = 8;
    return d;
}

}  // namespace

TEST(capsule_sweep_handles_fast_crossing_tangency_and_inside_at_large_coordinates) {
    const dvec3 offset{1e7, 1e7, -1e7};
    const dvec3 a = offset + dvec3{0, -1, 0}, b = offset + dvec3{0, 1, 0};
    double fraction = -1;
    dvec3 normal;
    CHECK(sweepCapsule(offset + dvec3{-100, 0, 0}, offset + dvec3{100, 0, 0}, a, b,
                       0.25, fraction, normal));
    CHECK_NEAR(fraction, 0.49875, 1e-8);
    CHECK_NEAR(length(normal), 1.0, 1e-9);
    CHECK(sweepCapsule(offset + dvec3{-2, 0, 0.25}, offset + dvec3{2, 0, 0.25}, a, b,
                       0.25, fraction, normal));
    CHECK_NEAR(fraction, 0.5, 1e-8);
    CHECK(sweepCapsule(offset, offset + dvec3{4, 0, 0}, a, b, 0.25, fraction, normal));
    CHECK_NEAR(fraction, 0.0, 1e-12);
}

TEST(drawing_into_clear_sky_places_an_air_rune_at_three_metres_facing_the_eye) {
    World w(smallWorld());
    const dvec3 eye{0, 100, 0}, gaze = normalize(dvec3{1, 0.3, 0.2});
    const uint32_t id = w.drawRune(eye, gaze, Aspect::Fire);
    CHECK(id != 0);
    CHECK(w.runes().size() == 1u);
    const Rune& r = w.runes().front();
    CHECK(r.placement == RunePlacement::Air);
    CHECK_NEAR(length(r.position - eye), 3.0, 1e-9);
    CHECK(dot(r.normal, -gaze) > 1.0 - 1e-12);
}

TEST(drawing_down_to_nearby_ground_places_a_surface_rune_with_a_fitting_sign) {
    World w(smallWorld());
    const double ground = w.groundHeight(0, 0);
    const dvec3 eye{0, ground + 1.0, 0};
    const uint32_t id = w.drawRune(eye, {0, -1, 0}, Aspect::Earth);
    CHECK(id != 0);
    CHECK(w.runes().size() == 1u);
    const Rune& r = w.runes().front();
    CHECK(r.placement == RunePlacement::Surface);
    CHECK_NEAR(r.size, 0.5, 1e-12);
    CHECK_NEAR(length(r.normal), 1.0, 1e-9);
    CHECK(r.position.y >= w.groundHeight(r.position.x, r.position.z) - 0.02);
}

TEST(a_narrow_metal_plate_keeps_its_collider_and_draws_the_sign_in_front) {
    World w(smallWorld());
    Rune carrier;
    carrier.material = RuneMaterial::Metal;
    carrier.placement = RunePlacement::Carrier;
    carrier.size = 0.15;
    carrier.normal = {0, 0, 1};
    carrier.position = {0, w.groundHeight(0, 0) + 0.5, 0};
    carrier.ground = w.groundHeight(0, 0);
    const uint32_t carrierId = w.addRune(carrier);
    const dvec3 eye = carrier.position + dvec3{0, 0, 1};
    const uint32_t drawnId = w.drawRune(eye, {0, 0, -1}, Aspect::Water);
    CHECK(drawnId != 0 && drawnId != carrierId);
    CHECK(w.runes().size() == 2u);
    const Rune* drawn = nullptr;
    for (const Rune& r : w.runes()) if (r.id == drawnId) drawn = &r;
    CHECK(drawn != nullptr);
    if (drawn) {
        CHECK(drawn->placement == RunePlacement::Air);
        CHECK(drawn->position.z > carrier.position.z);
    }
    SurfaceHit hit;
    CHECK(w.traceEnvironment(eye, carrier.position - dvec3{0, 0, 1}, 0, hit));
    CHECK(hit.kind == SurfaceKind::Carrier);
    CHECK(hit.id == carrierId);
}

TEST(dense_magic_wakes_a_settled_ball_and_pushes_it_without_wind) {
    auto settledBall = [](double density) {
        auto physics = std::make_unique<Physics>(nullptr, nullptr, 1);
        physics->throwBall({0, k::BALL_RADIUS + 0.001, 0}, {0, 0, 0});
        double seconds = 0;
        for (int i = 0; i < 600 && !physics->balls()[0].resting; ++i)
            physics->step(1.0 / 60.0, seconds += 1.0 / 60.0);
        physics->setMagicSampler([density](const dvec3&, double& rho, dvec3& u, double& airShare) {
            rho = density;
            u = {6, 0, 0};
            airShare = 0;
        });
        return physics;
    };
    auto thin = settledBall(50);
    auto dense = settledBall(120);
    CHECK(thin->balls()[0].resting);
    CHECK(dense->balls()[0].resting);
    const double thinX = thin->balls()[0].position.x, denseX = dense->balls()[0].position.x;
    double thinTime = 10, denseTime = 10;
    for (int i = 0; i < 5 * 60; ++i) {
        thin->step(1.0 / 60.0, thinTime += 1.0 / 60.0);
        dense->step(1.0 / 60.0, denseTime += 1.0 / 60.0);
    }
    CHECK(thin->balls()[0].resting);
    CHECK(std::abs(thin->balls()[0].position.x - thinX) < 0.01);
    CHECK(!dense->balls()[0].resting);
    CHECK(dense->balls()[0].position.x > denseX + 0.1);
}

TEST(an_air_rune_clears_the_entire_sign_when_only_a_corner_meets_an_obstacle) {
    World w(smallWorld());
    Rune plate; plate.size=0.15; plate.material=RuneMaterial::Metal;
    plate.position={0.24,w.groundHeight(0,0)+0.7,0}; plate.ground=w.groundHeight(0,0);
    plate.normal={0,0,1}; w.addRune(plate);
    const dvec3 eye{0,plate.position.y,1};
    SurfaceHit centre;
    CHECK(!w.traceEnvironment(eye,eye+dvec3{0,0,-1},0,centre));
    const uint32_t id=w.drawRune(eye,{0,0,-1},Aspect::Air);
    CHECK(id!=0);
    for(const Rune& r:w.runes()) if(r.id==id) {
        CHECK(r.placement==RunePlacement::Air);
        CHECK(r.position.z>plate.position.z+0.1);
        dvec3 right,up; runeBasis(r.normal,right,up);
        for(int y=-2;y<=2;++y) for(int x=-2;x<=2;++x) {
            const dvec3 p=r.position+(right*double(x)+up*double(y))*(r.size*0.25);
            SurfaceHit hit;
            CHECK(!w.traceEnvironment(p+r.normal*0.01,p-r.normal*0.01,0,hit));
        }
    }
}
