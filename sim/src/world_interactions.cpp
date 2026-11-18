#include "taelia/sim/world.h"
#include "taelia/sim/hash.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace taelia {
namespace {
double closest(const dvec3& p, const dvec3& a, const dvec3& b) {
    const dvec3 d = b - a;
    return std::clamp(dot(p - a, d) / std::max(dot(d, d), 1e-20), 0.0, 1.0);
}
bool sphere(const dvec3& p, const dvec3& d, const dvec3& c, double r, double& t) {
    const dvec3 q = p - c;
    const double aa = dot(d, d), bb = dot(q, d), cc = dot(q, q) - r * r;
    if (cc <= 0) { t = 0; return true; }
    const double det = bb * bb - aa * cc;
    if (aa < 1e-20 || det < 0) return false;
    t = (-bb - std::sqrt(det)) / aa;
    return t >= 0 && t <= 1;
}
const TreeSkeleton& skeleton(const TreeInstance& t) {
    static const auto all = [] {
        std::array<TreeSkeleton, size_t(TreeKind::Count) * kTreeVariants> a;
        for (size_t i = 0; i < a.size(); ++i)
            a[i] = growTree(TreeKind(i / kTreeVariants), treeSkeletonSeed(TreeKind(i / kTreeVariants), int(i % kTreeVariants)));
        return a;
    }();
    return all[size_t(t.kind) * kTreeVariants + t.variant];
}
uint64_t treeId(const TreeInstance& t) {
    return hashCombine(uint64_t(int64_t(std::floor(t.position.x / kTreeCell))),
                       uint64_t(int64_t(std::floor(t.position.z / kTreeCell))));
}
}

bool sweepCapsule(const dvec3& from, const dvec3& to, const dvec3& a, const dvec3& b,
                  double radius, double& fraction, dvec3& normal) {
    const dvec3 d = to - from, axis = b - a, q = from - a;
    const double aa = dot(axis, axis);
    double t = 2, cap;
    const dvec3 nearest = a + axis * closest(from, a, b);
    if (dot(from - nearest, from - nearest) <= radius * radius) t = 0;
    if (aa > 1e-20) {
        const dvec3 perpD = d - axis * (dot(d, axis) / aa);
        const dvec3 perpQ = q - axis * (dot(q, axis) / aa);
        const double A = dot(perpD, perpD), B = dot(perpQ, perpD), C = dot(perpQ, perpQ) - radius * radius;
        const double det = B * B - A * C;
        if (A > 1e-20 && det >= 0) {
            const double u = (-B - std::sqrt(det)) / A;
            const double h = dot(q + d * u, axis) / aa;
            if (u >= 0 && u <= 1 && h >= 0 && h <= 1) t = std::min(t, u);
        }
    }
    if (sphere(from, d, a, radius, cap)) t = std::min(t, cap);
    if (sphere(from, d, b, radius, cap)) t = std::min(t, cap);
    if (t > 1) return false;
    fraction = t;
    const dvec3 p = from + d * t, n = p - (a + axis * closest(p, a, b));
    normal = length(n) > 1e-9 ? normalize(n) : normalize(-d);
    return true;
}

bool World::traceEnvironment(const dvec3& from, const dvec3& to, double radius, SurfaceHit& hit) const {
    hit = {};
    const dvec3 delta = to - from;
    auto take = [&](double t, dvec3 n, SurfaceKind kind, uint64_t id = 0, RuneMaterial mat = RuneMaterial::Stone) {
        if (t < 0 || t > hit.fraction || (t == hit.fraction && hit.kind != SurfaceKind::None)) return;
        hit = {kind, id, t, from + delta * t - n * radius, n, mat};
    };
    // Bracket every crossed surface, then bisect; water is two-sided, terrain is solid below its surface.
    const int steps = std::max(1, int(std::ceil(length(delta) / 0.1)));
    const double waterSide = from.y >= terrain_.sample(from.x, from.z).waterLevel ? 1.0 : -1.0;
    for (const bool water : {false, true}) {
        auto clearance = [&](double t) {
            const dvec3 p = from + delta * t;
            if (!water) return p.y - terrain_.height(p.x, p.z) - radius;
            const TerrainSample s = terrain_.sample(p.x, p.z);
            return s.underwater() ? waterSide * (p.y - s.waterLevel) - radius : 1e20;
        };
        double prev = 0;
        for (int i = 0; i <= steps; ++i) {
            const double t = double(i) / steps;
            if (t > hit.fraction) break;
            if (clearance(t) <= 0) {
                double lo = prev, hi = t;
                for (int j = 0; j < 18; ++j) {
                    const double mid = (lo + hi) * 0.5;
                    (clearance(mid) > 0 ? lo : hi) = mid;
                }
                const dvec3 p = from + delta * hi;
                take(hi, water ? dvec3{0, waterSide, 0} : terrain_.sample(p.x, p.z).normal,
                     water ? SurfaceKind::Water : SurfaceKind::Ground);
                break;
            }
            prev = t;
        }
    }
    for (const Rune& r : runes_) {
        if (r.broken || r.placement != RunePlacement::Carrier) continue;
        const RuneCarrier c = runeCarrier(r);
        double t;
        if (!sweepRuneCarrier(c, from, to, radius, t)) continue;
        const double cs = std::cos(c.yaw), sn = std::sin(c.yaw);
        const dvec3 rel = from + delta * t - c.centre;
        const dvec3 local{rel.x * cs - rel.z * sn, rel.y, rel.x * sn + rel.z * cs};
        const double distances[] = {std::abs(std::abs(local.x) - c.half.x - radius),
                                    std::abs(std::abs(local.y) - c.half.y - radius),
                                    std::abs(std::abs(local.z) - c.half.z - radius)};
        const int axis = int(std::min_element(distances, distances + 3) - distances);
        const dvec3 n = axis == 0 ? dvec3{cs * (local.x >= 0 ? 1 : -1), 0, -sn * (local.x >= 0 ? 1 : -1)} :
                        axis == 1 ? dvec3{0, local.y >= 0 ? 1.0 : -1.0, 0} :
                                    dvec3{sn * (local.z >= 0 ? 1 : -1), 0, cs * (local.z >= 0 ? 1 : -1)};
        take(t, n, SurfaceKind::Carrier, r.id, r.material);
    }
    std::vector<TreeInstance> trees;
    // Include crowns whose trunks are outside the segment's bounding box.
    constexpr double reach = 18;
    treesInArea(terrain_, std::min(from.x, to.x) - reach, std::min(from.z, to.z) - reach,
                std::max(from.x, to.x) + reach, std::max(from.z, to.z) + reach, trees);
    for (const TreeInstance& tree : trees) {
        const TreeSkeleton& sk = skeleton(tree);
        if (std::min(from.y, to.y) - radius > tree.position.y + sk.height * tree.scale) continue;
        RuneCarrier bounds;
        bounds.centre = tree.position + dvec3{0, sk.height * tree.scale * 0.5, 0};
        bounds.half = {sk.crownRadius * tree.scale + 0.5, sk.height * tree.scale * 0.5 + 0.5, sk.crownRadius * tree.scale + 0.5};
        double broad;
        if (!sweepRuneCarrier(bounds, from, to, radius, broad)) continue;
        const double cs = std::cos(tree.yaw), sn = std::sin(tree.yaw);
        auto place = [&](const fvec3& v) {
            return tree.position + dvec3{v.x * cs + v.z * sn, v.y, -v.x * sn + v.z * cs} * tree.scale;
        };
        for (const TreeSegment& seg : sk.segments) {
            if (std::max(seg.ra, seg.rb) < 0.015f) continue;
            double t; dvec3 n;
            if (sweepCapsule(from, to, place(seg.a), place(seg.b), radius + std::max(seg.ra, seg.rb) * tree.scale, t, n))
                take(t, n, SurfaceKind::Plant, treeId(tree), RuneMaterial::Wood);
        }
    }
    for (const Manifestation& m : magic_->manifestations()) {
        if (m.kind != ManifestKind::Mineral) continue;
        const double r = std::cbrt(3.0 * m.volume / (4.0 * kPi));
        double t; dvec3 n;
        const dvec3 centre = m.position + dvec3{0, 0.25, 0}; // same centre as the physical mineral collider
        if (sweepCapsule(from, to, centre, centre, r + radius, t, n)) take(t, n, SurfaceKind::Mineral, m.id);
    }
    for (const Ball& b : physics_->balls()) {
        double t; dvec3 n;
        if (sweepCapsule(from, to, b.position, b.position, radius + k::BALL_RADIUS, t, n)) take(t, n, SurfaceKind::Ball, b.id);
    }
    return hit.kind != SurfaceKind::None;
}

uint32_t World::drawRune(const dvec3& eye, const dvec3& gaze, Aspect aspect) {
    if (length(gaze) < 1e-9) return 0;
    const dvec3 dir = normalize(gaze), end = eye + dir * double(k::RUNE_DRAW_REACH);
    Rune r;
    r.aspect = aspect; r.size = k::RUNE_DRAW_SIZE;
    r.drawing = true; r.drawProgress = 0;
    r.placement = RunePlacement::Air; r.position = end; r.normal = -dir;
    SurfaceHit surface;
    if (traceEnvironment(eye, end, 0, surface)) {
        bool fits = surface.kind != SurfaceKind::Water && surface.kind != SurfaceKind::Ball;
        double relief = 0;
        dvec3 right, up; runeBasis(surface.normal, right, up);
        for (int y = -2; y <= 2 && fits; ++y)
            for (int x = -2; x <= 2 && fits; ++x) {
                const dvec3 p = surface.point + (right * double(x) + up * double(y)) * (r.size * 0.25);
                SurfaceHit probe;
                fits = traceEnvironment(p + surface.normal * 0.12, p - surface.normal * 0.12, 0, probe) &&
                       probe.kind == surface.kind && probe.id == surface.id &&
                       std::abs(dot(probe.point - p, surface.normal)) < 0.06 && dot(probe.normal, surface.normal) > 0.90;
                if (fits) relief = std::max(relief, dot(probe.point - p, surface.normal));
            }
        if (fits) {
            r.placement = RunePlacement::Surface; r.normal = surface.normal;
            r.position = surface.point + r.normal * (relief + 0.018); r.material = surface.material;
            r.support = surface.kind == SurfaceKind::Carrier ? surface.id : 0;
        }
    }
    if (r.placement == RunePlacement::Air) {
        // Sweep a sphere enclosing the WHOLE sign, not only its centre. This also catches obstacles touched
        // by a corner when the centre ray missed, and oblique surfaces protruding into the suspended plane.
        SurfaceHit clearance;
        const double bound = r.size * std::sqrt(0.5) + 0.025;
        if (traceEnvironment(eye, end, bound, clearance))
            r.position = eye + dir * std::max(0.0, clearance.fraction * k::RUNE_DRAW_REACH - 0.025);
        if (length(r.position - eye) < bound) return 0; // no room even at the eye
    }
    // Drawing over an existing sign reuses it instead of stacking coplanar geometry.
    for (const Rune& old : runes_)
        if (!old.broken && old.aspect == r.aspect && length(old.position - r.position) < r.size * 0.5 && dot(old.normal, r.normal) > 0.9)
            return 0; // never overwrite the contents of an existing sign by drawing again
    return addRune(r);
}

void World::chargeRune(uint32_t id, double heldSeconds, bool finished) {
    for (Rune& r : runes_) {
        if (r.id != id || r.broken || !r.drawing) continue;
        const double seconds = std::max(heldSeconds, 0.0);
        r.drawProgress = std::clamp(seconds / k::RUNE_DRAW_MIN_SECONDS, 0.0, 1.0);
        r.contents[int(r.aspect)] = k::RUNE_DRAW_DENSITY * std::clamp(seconds / k::RUNE_DRAW_MAX_SECONDS, 0.0, 1.0) * r.volume();
        r.overcharge = std::clamp((seconds - k::RUNE_DRAW_MAX_SECONDS) / k::RUNE_OVERCHARGE_SECONDS, 0.0, 1.0);
        if (r.overcharge >= 1.0) breakRune(r, *magic_);
        if (finished) { r.drawing = false; r.overcharge = 0; }
        break;
    }
    dropBrokenRunes();
}

void World::magicContact(MagicBall& b, const SurfaceHit& hit, bool magicPacket) {
    if (magicPacket && b.kg < k::MAGIC_IMPACT_MIN_KG) return;
    const double radius = b.radius(), volume = 4.0 / 3.0 * kPi * radius * radius * radius;
    const double density = b.kg / std::max(volume, 1e-12);
    if (magicPacket && b.kg > 1e-6) {
        if (magicImpacts_.size() >= 128) magicImpacts_.erase(magicImpacts_.begin());
        magicImpacts_.push_back({hit.point + hit.normal * 0.04, b.aspect, std::max(radius,0.12), 0, b.id, hit.normal});
    }
    if (b.aspect == Aspect::Life) restoreTrees(hit.point, b.kg);
    if (b.kg <= 1e-9) return;
    if (length(hit.point - campfireCore()) < 2.5) {
        if (b.aspect == Aspect::Water) {
            fire_ = std::max(0.0, fire_ - b.kg * k::FIRE_WATER_QUENCH);
            fireBoost_ = 0;
        } else if (b.aspect == Aspect::Fire && density > k::MAGIC_FIRE_STEAM) {
            // The wood is still present after quenching: fresh ignition can restart a cold bed.
            if (fire_ <= 0.01) fire_ = 0.25;
            fireBoost_ = std::min(double(k::FIRE_MAGIC_BOOST), fireBoost_ + b.kg * 0.02);
        } else if (b.aspect == Aspect::Air && fire_ > 0.01) {
            fire_ = std::min(double(k::CAMPFIRE_MAX), fire_ + b.kg * 0.015);
        }
    }
    if (b.aspect == Aspect::Water) {
        for (auto it = burning_.begin(); it != burning_.end();) {
            const auto& burning = it->second;
            const dvec3 base = burning.tree.position, top = base+dvec3{0,burning.height,0};
            const dvec3 axis = base+(top-base)*closest(hit.point,base,top);
            if ((length(hit.point-axis)<3.0 || length(hit.point-burning.ignition)<3.0) && b.kg>0.5) it = burning_.erase(it);
            else ++it;
        }
    }
    // Resolve contact at the packet's actual density before averaging its released mass onto the field grid.
    // Fire uses the same heat-contact rule as field hot spots; it is not a scripted explosion.
    if (b.aspect == Aspect::Fire && density > k::MAGIC_FIRE_STEAM)
        heatContact({hit.point, density, hit.kind == SurfaceKind::Water});
    if (density <= k::MAGIC_EFFECT_THRESHOLD) return;
    const double coupled = 1.0 - k::MAGIC_EFFECT_THRESHOLD / density;
    const double inward = std::max(-dot(b.velocity, hit.normal), 0.0);
    if (hit.kind == SurfaceKind::Ground && b.aspect == Aspect::Earth)
        dentGround(hit.point, 0.5 * b.kg * inward * inward * coupled * k::IMPACT_PLASTIC, radius);
    if (hit.kind == SurfaceKind::Ball) {
        const auto& balls = physics_->balls();
        const auto found = std::find_if(balls.begin(), balls.end(), [&](const Ball& v) { return v.id == hit.id; });
        if (found == balls.end()) return;
        const double closing = std::max(-dot(b.velocity - found->velocity, hit.normal), 0.0);
        const double intercepted = b.kg * std::min(1.0, double(k::BALL_RADIUS) * k::BALL_RADIUS / (radius * radius));
        const double reduced = intercepted * k::BALL_MASS / (intercepted + k::BALL_MASS);
        const dvec3 impulse = -hit.normal * (reduced * closing * coupled);
        physics_->impulseBall(uint32_t(hit.id), impulse);
        b.velocity = b.velocity - impulse * (1.0 / b.kg);
    }
}
}
