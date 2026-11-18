// Shared queries for gaze placement and swept magic balls. No rendering or engine types.
#pragma once
#include <cstdint>
#include "taelia/sim/math.h"
#include "taelia/sim/rune.h"
namespace taelia {
enum class SurfaceKind : uint8_t { None, Ground, Water, Plant, Carrier, Mineral, Ball };
struct SurfaceHit {
    SurfaceKind kind = SurfaceKind::None;
    uint64_t id = 0;
    double fraction = 1;
    dvec3 point, normal;
    RuneMaterial material = RuneMaterial::Stone;
};
// Fraction along segment at first contact with a capsule. radius already includes the moving sphere's radius.
bool sweepCapsule(const dvec3& from, const dvec3& to, const dvec3& a, const dvec3& b,
                  double radius, double& fraction, dvec3& normal);
}
