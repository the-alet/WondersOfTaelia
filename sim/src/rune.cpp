#include "taelia/sim/rune.h"

#include <algorithm>
#include <cmath>

#include "taelia/shared_constants.h"

namespace taelia {

double Rune::volume() const { return k::RUNE_VOLUME_FACTOR * size * size * size; }

double Rune::seal() const {
    if (placement != RunePlacement::Carrier) return k::RUNE_DRAW_SEAL;
    switch (material) {
        case RuneMaterial::Wood: return k::RUNE_SEAL_WOOD;
        case RuneMaterial::Metal: return k::RUNE_SEAL_METAL;
        default: return k::RUNE_SEAL_STONE;
    }
}

double Rune::breakPoint() const { return k::RUNE_BREAK * seal(); }

<<<<<<< Updated upstream
=======
RuneCarrier runeCarrier(const Rune& r) {
    const double s = r.size;
    double width, thick, above, round;
    switch (r.material) {
        case RuneMaterial::Wood:  // a hewn post
            width = 1.3 * s, thick = std::max(0.10, 0.35 * s), above = 0.75 * s, round = 0.02;
            break;
        case RuneMaterial::Metal:  // a cast plate on its edge
            width = 1.2 * s, thick = 0.04, above = 0.70 * s, round = 0.008;
            break;
        default:  // a standing stone
            width = 1.5 * s, thick = std::max(0.18, 0.5 * s), above = 0.85 * s, round = 0.05;
            break;
    }
    dvec3 n{r.normal.x, 0.0, r.normal.z};
    n = length(n) > 1e-6 ? normalize(n) : dvec3{0, 0, 1};
    const double top = r.position.y + above;
    const double bottom = std::min(r.ground - 0.15, r.position.y - 0.6 * s);  // set into the ground
    RuneCarrier c;
    c.yaw = std::atan2(n.x, n.z);
    c.half = {0.5 * width, 0.5 * (top - bottom), 0.5 * thick};
    c.centre = {r.position.x - n.x * c.half.z, 0.5 * (top + bottom), r.position.z - n.z * c.half.z};
    c.rounding = std::min(round, 0.45 * std::min(c.half.x, c.half.z));
    c.signY = r.position.y - c.centre.y;
    return c;
}

Aspect runeCarrierAspect(RuneMaterial m) { return m == RuneMaterial::Wood ? Aspect::Life : Aspect::Earth; }

void runeBasis(const dvec3& normal, dvec3& right, dvec3& up) {
    const dvec3 n = normalize(normal);
    right = normalize(cross(std::abs(n.y) < 0.95 ? dvec3{0, 1, 0} : dvec3{0, 0, 1}, n));
    up = cross(n, right);
}

bool sweepRuneCarrier(const RuneCarrier& c, const dvec3& from, const dvec3& to, double radius, double& fraction) {
    const double cs = std::cos(c.yaw), sn = std::sin(c.yaw);
    const dvec3 p = from - c.centre, d = to - from;
    const double start[] = {p.x * cs - p.z * sn, p.y, p.x * sn + p.z * cs};
    const double delta[] = {d.x * cs - d.z * sn, d.y, d.x * sn + d.z * cs};
    const double half[] = {c.half.x + radius, c.half.y + radius, c.half.z + radius};
    double enter = 0.0, leave = 1.0;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::fabs(delta[axis]) < 1e-12) {
            if (std::fabs(start[axis]) > half[axis]) return false;
            continue;
        }
        double a = (-half[axis] - start[axis]) / delta[axis];
        double b = (half[axis] - start[axis]) / delta[axis];
        if (a > b) std::swap(a, b);
        enter = std::max(enter, a);
        leave = std::min(leave, b);
        if (enter > leave) return false;
    }
    fraction = enter;
    return true;
}

>>>>>>> Stashed changes
double Rune::mass() const {
    double m = 0;
    for (const double c : contents) m += c;
    return m;
}

double Rune::density() const { return mass() / volume(); }

Aspect runeOutflowAspect(Aspect a, double densityInside) {
    if (k::MAGIC_LIGHTNING_ENABLED == 0) return a;
    using magic::Conversion;
    const double theta = magic::theta0();
    if (a == Aspect::Fire) {
        const Conversion c = magic::fireConversion(densityInside, theta);
        return c == Conversion::ToLight ? Aspect::Light : (c == Conversion::ToLightning ? Aspect::Lightning : Aspect::Fire);
    }
    if (a == Aspect::Lightning && magic::lightningConversion(densityInside, theta) == Conversion::ToLight) return Aspect::Light;
    return a;
}

namespace {

// Out into the field in front of the sign, as what it turns into at the density it had inside.
void release(const Rune& r, MagicField& field, Aspect a, double kg, double insideDensity, double reach) {
    if (kg <= 0) return;
    field.addMass(r.position + r.normal * reach, runeOutflowAspect(a, insideDensity), kg);
}

}  // namespace

void breakRune(Rune& r, MagicField& field) {
    if (r.broken) return;
    for (int a = 0; a < kAspects; ++a) {
        const double inside = r.contents[a] / r.volume();
        release(r, field, Aspect(a), r.contents[a], inside, 0.1);
        r.contents[a] = 0;
    }
    r.broken = true;
}

void stepRune(Rune& r, MagicField& field, double dt) {
    if (r.broken) return;
    if (r.drawing || r.drawProgress < 1.0) return;
    if (!(dt > 0.0)) return;
    const double v = r.volume();
    // Check rupture before regulation: a large time step must never rescue an already broken seal.
    if (r.density() > r.breakPoint()) { breakRune(r, field); return; }
    const double target = 0.5 * r.seal() * v;
    const double fraction = -std::expm1(-dt / k::RUNE_BALANCE_SECONDS);
    MagicDensities around; double cellVolume = 0;
    if (r.mass() < target && field.cellAt(r.position, around, &cellVolume)) {
        double wanted = (target - r.mass()) * fraction;
        for (const Aspect a : {r.aspect, Aspect::Neutral}) {
            const double take = std::min(wanted, double(around[a]) * cellVolume);
            if (take > 0 && field.addMass(r.position, a, -take)) {
                r.contents[int(r.aspect)] += take;
                wanted -= take;
            }
            if (r.aspect == Aspect::Neutral) break;
        }
    } else if (r.mass() > target) {
        const double total = r.mass(), excess = (total - target) * fraction;
        for (int a = 0; a < kAspects; ++a) {
            const double out = excess * r.contents[a] / total;
            if (out > 0 && field.addMass(r.position + r.normal * 0.3, Aspect(a), out)) r.contents[a] -= out;
        }
    }
    // Past its break point it bursts.
    if (r.density() > r.breakPoint()) {
        breakRune(r, field);
        return;
    }
    // Over its seal the excess leaks by effusion: per kg/m3 of excess, sqrt(Theta / 2 pi m) through its vent, so the
    // lighter the motes the sooner it vents.
    const double over = r.density() - r.seal();
    if (over > 0) {
        const double total = r.mass(), inside = r.density();
        for (int a = 0; a < kAspects; ++a) {
            if (r.contents[a] <= 0) continue;
            const Aspect asp = Aspect(a);
            const double share = r.contents[a] / total;
            const double excess = over * share;  // kg/m3 of this aspect over the seal
            const double flux = magic::effusionFlux(asp, magic::partialPressure(asp, excess, magic::theta0()), magic::theta0()) * magic::moteMass(asp);
            const double out = std::min(flux * k::RUNE_LEAK_AREA * dt, excess * v);
            r.contents[a] -= out;
            release(r, field, asp, out, inside, 0.3);  // a jet in front of the sign
        }
    }
}

}  // namespace taelia
