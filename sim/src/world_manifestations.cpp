#include "taelia/sim/world.h"
#include <algorithm>
#include <cmath>
namespace taelia {
void World::restoreTrees(const dvec3& position, double& lifeKg) {
    for (auto& [key, recovery] : recovering_) {
        const TreeInstance& tree = recovery.tree;
        if (std::hypot(position.x-tree.position.x, position.z-tree.position.z) > 2.5*tree.scale + 1.0 ||
            position.y < tree.position.y-1 || position.y > tree.position.y+14*tree.scale) continue;
        const double need = k::TREE_REVIVE_KG * tree.scale * tree.scale * tree.scale;
        const double take = std::min(lifeKg, std::max(need-recovery.stored,0.0));
        recovery.stored += take;
        lifeKg -= take;
        if (lifeKg <= 0) break;
    }
}

void World::stepTreeRecovery() {
    const double dt = WorldClock::stepSeconds();
    for (auto it = recovering_.begin(); it != recovering_.end();) {
        auto& recovery = it->second;
        const TreeInstance& tree = recovery.tree;
        const double need = k::TREE_REVIVE_KG * tree.scale * tree.scale * tree.scale;
        const dvec3 at = tree.position + dvec3{0,0.6,0};
        MagicDensities around; double volume = 0;
        if (magic_->cellAt(at, around, &volume) && around[Aspect::Life] > around[Aspect::Necrotic]) {
            const double available = std::max(double(around[Aspect::Life])-double(k::MAGIC_HELD),0.0)*volume;
            const double take = std::min({std::max(need-recovery.stored,0.0), available, need*dt/k::TREE_REVIVE_SECONDS});
            if (take > 0 && magic_->addMass(at,Aspect::Life,-take)) recovery.stored += take;
        }
        recovery.growth = std::min(recovery.stored/need, recovery.growth + dt/k::TREE_REVIVE_SECONDS);
        if (recovery.growth >= 1.0-1e-9) {
            burnt_.erase(it->first);
            it = recovering_.erase(it);
        } else ++it;
    }
}

void World::manifestFullBall() {
    MagicBall& b = gathering_;
    b.manifested = true;
    if (b.aspect == Aspect::Life || b.aspect == Aspect::Lightning) return;
    const double mass = std::min(double(k::MAGIC_FULL_MANIFEST_KG), b.kg);
    PhysicalManifest m;
    m.aspect = b.aspect; m.position = b.position + dvec3{0, -b.radius(), 0}; m.kg = mass;
    const double density = b.aspect == Aspect::Earth ? 1600.0 : 1000.0;
    m.radius = std::cbrt(mass * 3.0 / (4.0 * kPi * density));
    if (b.aspect == Aspect::Fire) m.radius = 0.12;
    if (b.aspect == Aspect::Air) m.radius = 0.4;
    if (b.aspect == Aspect::Light) m.radius = 0.025;
    if (b.aspect == Aspect::Necrotic) m.radius = 0.18;
    physical_.push_back(m);
    b.kg -= mass;
}

void World::stepPhysicalManifestations() {
    const double dt = WorldClock::stepSeconds();
    for (PhysicalManifest& m : physical_) {
        m.age += dt;
        if ((m.aspect == Aspect::Water || m.aspect == Aspect::Earth) && !m.settled) {
            m.velocity.y -= 9.81 * dt;
            SurfaceHit hit;
            const dvec3 to = m.position + m.velocity * dt;
            if (traceEnvironment(m.position, to, m.radius, hit)) {
                m.position = hit.point + hit.normal * m.radius;
                m.settled = true;
                if (m.aspect == Aspect::Water) {
                    MagicBall contact; contact.aspect = Aspect::Water; contact.kg = m.kg;
                    contact.position = m.position; contact.velocity = m.velocity;
                    magicContact(contact,hit,false); // condensed liquid quenches physically; no magic-packet flash
                }
            } else m.position = to;
        }
        if (m.aspect == Aspect::Air && m.age <= dt * 1.1)
            magic_->addMovingMass(m.position, Aspect::Air, m.kg, {3,1,0});
        if (m.aspect == Aspect::Light) m.position.y += dt * 0.08;
    }
    // Earth persists as an ordinary mineral collider; other small manifestations evaporate back into the field.
    for (const PhysicalManifest& m : physical_) {
        if (m.aspect == Aspect::Earth && m.settled) {
            auto& all = magic_->manifestations();
            Manifestation rock;
            uint32_t id = 0x70000000u;
            for (const auto& r : all) id = std::max(id,r.id+1);
            rock.id=id; rock.kind=ManifestKind::Mineral; rock.position=m.position-dvec3{0,0.25,0};
            rock.volume=float(4.0/3.0*kPi*m.radius*m.radius*m.radius); rock.held=float(k::MAGIC_HELD); rock.seed=id;
            all.push_back(rock);
        } else if (m.age > 5.0 && m.aspect != Aspect::Air) {
            magic_->addMass(m.position,m.aspect,m.kg);
        }
    }
    std::erase_if(physical_,[](const PhysicalManifest& m) { return m.age>5.0 || (m.aspect==Aspect::Earth && m.settled); });
}
}
