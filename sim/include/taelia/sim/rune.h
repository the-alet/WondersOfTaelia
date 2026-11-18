// Runes: a sign confines its contents (no passive diffusion, drift, fading, pressure conversion or manifestations).
// Completed signs deliberately regulate toward half their seal,
// drawing available matching/neutral magic below that target and returning excess above it (SPEC exception).
// Over the seal the excess leaks out by effusion (faster for lighter motes) as a jet in front of the sign; past its
// break point, or when the sign is damaged or its carrier falls apart, everything comes out at once and it is gone,
// the usual rules acting at once (fire past 675 comes out as lightning, past 1100 as light). First numbers: SPEC.md.
#pragma once
#include <cstdint>

#include "taelia/shared_constants.h"
#include "taelia/sim/magic.h"
#include "taelia/sim/magic_kinetics.h"
#include "taelia/sim/math.h"

namespace taelia {

enum class RuneMaterial : uint8_t { Wood, Stone, Metal };
enum class RunePlacement : uint8_t { Carrier, Surface, Air };

struct Rune {
    uint32_t id = 0;
    Aspect aspect = Aspect::Neutral;
    RuneMaterial material = RuneMaterial::Stone;
    RunePlacement placement = RunePlacement::Carrier;
    uint64_t support = 0;    // stable surface identity; 0 for ground or suspended signs
    dvec3 position;          // the sign's centre
    dvec3 normal{0, 0, 1};   // the way it faces: its jet leaves that way
    double size = 0.5;       // m across
    double contents[kAspects] = {};  // kg of each aspect inside
    double ground = 0;       // the ground under its sign (the world sets it): its carrier stands there
    double held = k::MAGIC_HELD;  // what its carrier holds of its material's aspect (kg/m3); it falls apart below its share's 5%
    bool broken = false;
    bool drawing = false;
    double drawProgress = 1.0, overcharge = 0.0;

    double volume() const;      // m3: 0.5 m across holds about 0.1 m3
    double seal() const;        // kg/m3 it holds shut: wood 400, stone 600, metal 900
    double breakPoint() const;  // 1.5 x the seal
    double mass() const;        // kg inside
    double density() const;     // kg/m3 inside
};

// What a rune is carved on: a block set into the ground with the sign on its front: a standing stone, a hewn wooden
// post, a cast metal plate. One shape for the physics (a box), the renderers and every front end.
struct RuneCarrier {
    dvec3 centre;            // the middle of the block
    dvec3 half;              // half its width (x), height (y) and thickness (z), in its own frame
    double yaw = 0;          // its front (+z) faces along the sign's normal (radians, +z turned toward +x)
    double rounding = 0.02;  // m: how round its edges are
    double signY = 0;        // the sign's centre above the block's middle (m); the sign is on its front face
};
RuneCarrier runeCarrier(const Rune& r);
// First intersection along a magic ball's step with the carrier's box expanded by its radius.
// Covers the whole segment, so a fast ball cannot skip a thin plate between fixed steps.
bool sweepRuneCarrier(const RuneCarrier& c, const dvec3& from, const dvec3& to, double radius, double& fraction);
// The aspect a carrier's material holds: stone and metal earth, wood life.
Aspect runeCarrierAspect(RuneMaterial m);
// Orthonormal frame of a sign, including signs on horizontal/sloped surfaces.
void runeBasis(const dvec3& normal, dvec3& right, dvec3& up);

// One fixed step of a rune with the field round it: what it draws in, what leaks over its seal, whether it breaks
// (then everything has gone out into the field and it is marked broken).
void stepRune(Rune& r, MagicField& field, double dt);
// Its sign damaged or its carrier gone: everything comes out at once.
void breakRune(Rune& r, MagicField& field);
// What an aspect comes out as from this density inside: fire past 675 as lightning, past 1100 as light; lightning past
// the light threshold as light; the rest as itself.
Aspect runeOutflowAspect(Aspect a, double densityInside);

}  // namespace taelia
