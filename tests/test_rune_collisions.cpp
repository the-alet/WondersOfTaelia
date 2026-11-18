#include <cmath>

#include "taelia/shared_constants.h"
#include "taelia/sim/player.h"
#include "taelia/sim/rune.h"
#include "taelia/sim/terrain.h"
#include "test.h"

using namespace taelia;

TEST(player_inside_a_rune_carrier_leaves_its_nearest_face_in_one_step) {
    const Terrain terrain(1);
    for (const double yaw : {0.0, 0.7, 2.1}) {
        const PlayerObstacle box{{0, 100, 0}, {0.5, 2, 0.15}, yaw};
        const double cs = std::cos(yaw), sn = std::sin(yaw);
        Player p;
        p.mode = PlayerMode::Fly;
        p.position = box.centre + dvec3{0.05 * sn, 0, 0.05 * cs};
        stepPlayer(p, {}, terrain, {}, {box}, 1.0 / 60.0);
        const dvec3 q = p.position - box.centre;
        CHECK_NEAR(q.x * sn + q.z * cs, box.half.z + k::PLAYER_RADIUS, 1e-8);
        CHECK_NEAR(q.x * cs - q.z * sn, 0.0, 1e-8);
    }
}

TEST(a_fast_magic_ball_cannot_skip_a_thin_rotated_rune_carrier) {
    for (const double yaw : {0.0, 0.6, 1.7}) {
        RuneCarrier c;
        c.centre = {1e7, 120, -1e7};
        c.half = {0.3, 0.5, 0.02};
        c.yaw = yaw;
        const dvec3 front{std::sin(yaw), 0, std::cos(yaw)};
        const dvec3 side{std::cos(yaw), 0, -std::sin(yaw)};
        double t = -1;
        CHECK(sweepRuneCarrier(c, c.centre + front * 0.4, c.centre - front * 0.4, 0.05, t));
        CHECK_NEAR(t, (0.4 - 0.07) / 0.8, 1e-7);
        // A path beside the plate, and one above it, must both remain clear.
        CHECK(!sweepRuneCarrier(c, c.centre + side * 0.5 + front, c.centre + side * 0.5 - front, 0.05, t));
        CHECK(!sweepRuneCarrier(c, c.centre + dvec3{0, 1, 0} + front,
                               c.centre + dvec3{0, 1, 0} - front, 0.05, t));
        CHECK(sweepRuneCarrier(c, c.centre, c.centre + front, 0.05, t));
        CHECK_NEAR(t, 0.0, 1e-12);
        CHECK(!sweepRuneCarrier(c, c.centre + front, c.centre + front * 2.0, 0.05, t));
    }
}
