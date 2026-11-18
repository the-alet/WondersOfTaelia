#include "taelia/sim/world.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "taelia/shared_constants.h"
#include "taelia/sim/hash.h"
#include "taelia/sim/sky.h"

namespace taelia {

namespace {

MagicOptions magicOptions(const WorldDesc& d) {
    MagicOptions o = d.magic;
    o.seed = d.seed;
    return o;
}

}  // namespace

World::World(const WorldDesc& d)
    : seed_(d.seed), clock_(d.startHour, d.startDay), settings_(d.settings), terrain_(d.seed), clouds_(d.seed),
      wind_(hashCombine(d.seed, 0x77196)), physics_(std::make_unique<Physics>(&terrain_, &wind_, d.seed)),
      magic_(std::make_unique<MagicField>(&terrain_, &wind_, magicOptions(d))) {
    wind_.setMean(wind());
    physics_->setMagicSampler([this](const dvec3& p, double& rho, dvec3& u, double& air) {
        const MagicSample s = magic_->sample(p);
        rho = s.rho.total();
        u = s.u;
        air = s.level >= 0 && rho > 1e-6 ? s.rho[Aspect::Air] / rho : 0.0;  // only where the field runs
    });
    // The human, the player (the user), stands at the spawn point looking along +z; a ball bounces off its capsule.
    player_.position = {k::CAMERA_SPAWN[0], 0.0, k::CAMERA_SPAWN[2]};
    player_.position.y = terrain_.height(player_.position.x, player_.position.z);
    physics_->addCapsule(1, player_.position + dvec3{0, 0.5 * k::HUMAN_HEIGHT, 0}, k::HUMAN_RADIUS, 0.5 * k::HUMAN_HEIGHT - k::HUMAN_RADIUS);
}

World::~World() = default;

int World::advance(double realSeconds) {
    const int n = clock_.advance(realSeconds);
    for (int i = 0; i < n; ++i) step();
    return n;
}

void World::step() {
    clock_.onStep();
    clouds_.setOverride(settings_.cloudCover);
    clouds_.setRain(settings_.rain);
    clouds_.step(WorldClock::stepSeconds(), wind(), clock_.seconds());
    wind_.setMean(wind());
    wind_.step(WorldClock::stepSeconds());
    stepPlayer();
    physics_->step(WorldClock::stepSeconds(), clock_.seconds());
    // Hits dent the ground (the user): IMPACT_PLASTIC of the energy each lost into it pushes it aside.
    impacts_.clear();
    physics_->takeImpacts(impacts_);
    for (const Impact& im : impacts_) dentGround(im.point, im.energy * k::IMPACT_PLASTIC, im.radius);
    // Water quenches the bed, wind feeds an existing fire, and added fire magic gives a short-lived boost.
    const dvec3 core = campfireCore();
    if (magic_->started()) {
        const MagicSample bed = magic_->sample(core);
        const double dt = WorldClock::stepSeconds();
        fireBoost_ *= std::exp(-dt / k::FIRE_MAGIC_BOOST_SECONDS);
        fire_ *= std::exp(-std::max(double(bed.rho[Aspect::Water]) - 0.5, 0.0) * k::FIRE_WATER_QUENCH * dt);
        if (fire_ > 0.01) {
            const double breeze = length(windAt(core));
            fire_ = std::min(double(k::CAMPFIRE_MAX), fire_ + dt * k::CAMPFIRE_FEED * (0.3 + breeze / 3.0));
        } else {
            fire_ -= fire_ * dt / double(k::CAMPFIRE_DIE_TAU);
        }
    }
    // Lightning: each strike leaves fire magic where it hits (whatever it sets alight, it does through that fire).
    for (const dvec3& s : clouds_.takeStrikes()) {
        if (k::MAGIC_LIGHTNING_ENABLED == 0) continue;
        const dvec3 at{s.x, terrain_.height(s.x, s.z), s.z};
        magic_->addMass(at + dvec3{0, 1.0, 0}, Aspect::Fire, k::WEATHER_STRIKE_FIRE * double(k::MAGIC_L0_CELL) * k::MAGIC_L0_CELL * k::MAGIC_L0_CELL_Y);
        strikes_.push_back({at, clock_.seconds()});
    }
    std::erase_if(strikes_, [&](const Strike& s) { return clock_.seconds() - s.seconds > 5.0; });
    magic_->setRain(clouds_.rainAt(focus_.x, focus_.z));
    // Hot things: the campfire, and the trees burning (each its own fire, fading as it burns out).
    std::vector<HeatSource> heat{HeatSource{core, fire_ + fireBoost_}};
    for (const PhysicalManifest& m : physical_)
        if (m.aspect == Aspect::Fire) heat.push_back({m.position,0.16 * std::max(1.0-m.age/5.0,0.0)});
    for (const BurningTree& b : burningTrees())
        if (b.strength > 0.01) heat.push_back({b.firePosition, k::PLANT_BURN_HEAT * b.strength / 2.2});
    magic_->setHeat(heat);
    // Light drops: each sun in the sky shoots them (fewer through cloud, none from a hidden disc); the planet sends a
    // little of what hits it back down.
    {
        const SkyState sky = Sky::at(clock_.seconds());
        const double clear = 1.0 - clouds_.cover(clock_.seconds());
        std::vector<DropSource> drops;
        double lit = 0;
        for (int i = 0; i < 2; ++i) {
            if (sky.sun[i].up) drops.push_back({sky.sun[i].dir, k::MAGIC_DROPS * (1.0 - sky.sun[i].coverage) * clear});
            lit += 0.25 * (1.0 - dot(sky.planetDir, sky.sun[i].dir));
        }
        if (sky.planetUp) drops.push_back({sky.planetDir, k::MAGIC_DROPS * k::MAGIC_PLANET_REFLECT * lit * clear});
        magic_->setDrops(drops);
    }
    magic_->step(clock_.steps());
    burnPlants();
<<<<<<< Updated upstream
    // Runes: what they draw in, what leaks over their seals; the burst ones are gone.
    for (Rune& r : runes_) stepRune(r, *magic_, WorldClock::stepSeconds());
    stepMagicBalls();
    std::erase_if(runes_, [](const Rune& r) { return r.broken; });
=======
    stepTreeRecovery();
    // Runes: what they draw in, what leaks over their seals; what their carriers hold evens out with the magic round
    // them, and a carrier falling apart breaks its rune; the burst ones are gone.
    for (Rune& r : runes_) {
        stepRune(r, *magic_, WorldClock::stepSeconds());
        if (r.broken) continue;
        if (r.placement == RunePlacement::Air) continue;
        r.held = magic::heldAfter(r.held, magic_->sample(r.position).rho.total(), WorldClock::stepSeconds());
        if (magic::fallsApart(r.held)) breakRune(r, *magic_);
    }
    stepMagicBalls();
    stepPhysicalManifestations();
    dropBrokenRunes();
>>>>>>> Stashed changes
    // What each ball holds evens out with the magic around it; in a void it falls apart. A ball is pure life magic:
    // necrotic magic it takes up eats its life (growing by MAGIC_NECROTIC_GAIN of it), and when it falls apart it
    // crumbles to dust, its necrotic magic going out into the field.
    {
        const double dt = WorldClock::stepSeconds();
        std::vector<uint32_t> gone;
        std::map<uint32_t, double> kept, keptNecrotic;
        for (const Ball& b : physics_->balls()) {
            const auto it = held_.find(b.id);
            double held = it == held_.end() ? double(k::MAGIC_HELD) : it->second;
            const auto nt = ballNecrotic_.find(b.id);
            double necrotic = nt == ballNecrotic_.end() ? 0.0 : nt->second;
            const MagicSample around = magic_->sample(b.position);
            held = magic::heldAfter(held, around.rho.total(), dt);
            necrotic = magic::heldToward(necrotic, around.rho[Aspect::Necrotic], dt);
            const double eaten = held - magic::lifeLeft(held, necrotic, dt);
            held -= eaten;
            necrotic += k::MAGIC_NECROTIC_GAIN * eaten;
            if (magic::fallsApart(held)) {
                gone.push_back(b.id);
                const double volume = 4.0 / 3.0 * kPi * k::BALL_RADIUS * k::BALL_RADIUS * k::BALL_RADIUS;
                crumble(b.position, k::BALL_RADIUS, necrotic * volume);
            } else {
                kept[b.id] = held;
                keptNecrotic[b.id] = necrotic;
            }
        }
        held_.swap(kept);
        ballNecrotic_.swap(keptNecrotic);
        for (const uint32_t id : gone) physics_->removeBall(id);
    }
    // The human's life magic (and the water magic it holds, the user: it is mostly water) evens out with the magic
    // round its chest (in ordinary places both stay near their shares). Necrotic magic it takes up from round it
    // slowly eats its life magic; holding water too, it does not crumble.
    {
        const double dt = WorldClock::stepSeconds();
        const MagicSample chest = magic_->sample(player_.position + dvec3{0, 1.3, 0});
        humanHeld_ = magic::heldAfter(humanHeld_, chest.rho.total(), dt);
        humanWater_ = magic::heldToward(humanWater_, std::min(double(k::HUMAN_WATER), chest.rho.total()), dt);
        humanNecrotic_ = magic::heldToward(humanNecrotic_, chest.rho[Aspect::Necrotic], dt);
        const double eaten = humanHeld_ - magic::lifeLeft(humanHeld_, humanNecrotic_, dt);
        humanHeld_ -= eaten;
        humanNecrotic_ += k::MAGIC_NECROTIC_GAIN * eaten;
    }
    // Dust settles and is gone.
    for (Dust& d : dust_) d.age += WorldClock::stepSeconds();
    std::erase_if(dust_, [](const Dust& d) { return d.age > k::MAGIC_DUST_SECONDS; });
    for (MagicImpact& impact : magicImpacts_) impact.age += WorldClock::stepSeconds();
    std::erase_if(magicImpacts_, [](const MagicImpact& impact) { return impact.age >= k::MAGIC_IMPACT_SECONDS; });
    // What the magic left (minerals, sprouts) holds its magic like any object; minerals are rock to a ball.
    {
        std::vector<Manifestation>& ms = magic_->manifestations();
        for (Manifestation& m : ms) {
            const MagicSample around = magic_->sample(m.position + dvec3{0, 0.3, 0});
            const double dt = WorldClock::stepSeconds();
            double held = magic::heldAfter(m.held, around.rho.total(), dt);
            if (m.kind == ManifestKind::Sprout) {  // pure life magic: necrotic magic it takes up eats it
                double necrotic = magic::heldToward(m.necrotic, around.rho[Aspect::Necrotic], dt);
                const double eaten = held - magic::lifeLeft(held, necrotic, dt);
                held -= eaten;
                necrotic += k::MAGIC_NECROTIC_GAIN * eaten;
                m.necrotic = float(necrotic);
            }
            m.held = float(held);
            if (m.kind == ManifestKind::Mineral && !rocks_.count(m.id)) {
                physics_->addRock(m.id, m.position + dvec3{0, 0.25, 0}, std::cbrt(3.0 * m.volume / (4.0 * kPi)));
                rocks_.insert(m.id);
            }
        }
        for (auto it = ms.begin(); it != ms.end();) {
            if (magic::fallsApart(it->held)) {
                // A sprout withering is a death (not by fire): the life it still held turns necrotic, with what it had
                // taken up (the user: necrotic magic comes from death; a burnt-out tree leaves none).
                if (it->kind == ManifestKind::Sprout) crumble(it->position + dvec3{0, 0.2, 0}, 0.25, double(it->necrotic + it->held) * it->volume);
                physics_->removeRock(it->id);
                rocks_.erase(it->id);
                it = ms.erase(it);
            } else {
                ++it;
            }
        }
    }
}


// Fire magic past the dew point: steam where it meets a pond, trees near it catch (and burn as hot things for a while,
// then stand charred), the grass under it burns away.
void World::burnPlants() {
    // Steam keeps rising a while after the water was heated: it thins over STEAM_SECONDS.
    const double thin = std::exp(-WorldClock::stepSeconds() / k::STEAM_SECONDS);
    for (Glow& g : steam_) g.strength *= thin;
    std::erase_if(steam_, [](const Glow& g) { return g.strength < 1.0; });
    const double now = clock_.seconds();
    for (auto it = burning_.begin(); it != burning_.end();) {
        if (now >= it->second.until) {
            burnt_.insert(it->first);  // fire consumed its life magic: it leaves no necrotic magic (the user)
            recovering_[it->first] = {it->second.tree, 0, 0};
            it = burning_.erase(it);
        } else {
            ++it;
        }
    }
    for (const HotSpot& h : magic_->hotSpots()) heatContact(h);
}

void World::heatContact(const HotSpot& h) {
    if (h.fire <= k::MAGIC_FIRE_STEAM) return;
    const double now = clock_.seconds();
    std::vector<TreeInstance> near;
    if (h.water) {
        // One column per couple of metres of water surface, as strong as the fire is past the dew point.
        const double strength = h.fire - k::MAGIC_FIRE_STEAM;
        Glow* column = nullptr;
        for (Glow& g : steam_)
            if (std::hypot(g.position.x - h.position.x, g.position.z - h.position.z) < 2.0) column = &g;
        if (column) column->strength = std::max(column->strength, strength);
        else if (steam_.size() < 64) steam_.push_back({h.position, strength});
        return;
    }
    const double ground = terrain_.height(h.position.x, h.position.z);
    if (h.position.y - ground < 3.0) {
        bool known = false;
        for (const Glow& g : scorches_) known = known || std::hypot(g.position.x - h.position.x, g.position.z - h.position.z) < 1.0;
        if (!known && scorches_.size() < 64) scorches_.push_back({{h.position.x, ground, h.position.z}, double(k::GRASS_SCORCH_RADIUS)});
    }
    near.clear();
    const double reach = k::PLANT_BURN_REACH + 6.0;
    treesInArea(terrain_, h.position.x - reach, h.position.z - reach, h.position.x + reach, h.position.z + reach, near);
    for (const TreeInstance& t : near) {
        const std::pair<int64_t, int64_t> key{int64_t(std::floor(t.position.x / kTreeCell)), int64_t(std::floor(t.position.z / kTreeCell))};
        if (burnt_.count(key) || burning_.count(key)) continue;
        // Near its trunk or under its crown (a crown spreads about 2.5 m per unit of scale), up to its top.
        const double d = std::hypot(t.position.x - h.position.x, t.position.z - h.position.z);
        const double above = h.position.y - t.position.y;
        if (d < k::PLANT_BURN_REACH + 2.5 * t.scale && above > -1.0 && above < 14.0 * t.scale) {
            const double height = growTree(t.kind, treeSkeletonSeed(t.kind,t.variant)).height * t.scale;
            const dvec3 ignition{h.position.x, std::clamp(h.position.y,t.position.y+0.15,t.position.y+height),h.position.z};
            burning_[key] = {t, now + k::PLANT_BURN_SECONDS, ignition, height};
        }
    }
}

std::vector<World::BurningTree> World::burningTrees() const {
    std::vector<BurningTree> out;
    for (const auto& [key, recovery] : recovering_)
        out.push_back({recovery.tree.position, 0.0, 1.0 - recovery.growth});
    for (const auto& [key, b] : burning_) {
        const double left = b.until - clock_.seconds();
        const double strength = std::clamp(left / 20.0, 0.0, 1.0) * 2.2 * b.tree.scale;
        const double burnt = std::clamp(1.0-left/k::PLANT_BURN_SECONDS,0.0,1.0);
        const double spread = std::clamp((k::PLANT_BURN_SECONDS-left)/k::TREE_FIRE_SPREAD_SECONDS,0.0,1.0);
        out.push_back({b.tree.position,strength,burnt,b.ignition});
        for (double height : {0.3*b.height,0.75*b.height}) {
            const dvec3 target = b.tree.position + dvec3{0,height,0};
            if (length(target-b.ignition) < 1.0) continue;
            out.push_back({b.tree.position,strength*spread,burnt,b.ignition+(target-b.ignition)*spread});
        }
    }
    return out;
}

void World::setPlayerInput(const PlayerInput& in) {
    // Pressed keys wait for the next step to take them.
    const bool dash = input_.dash || in.dash, fly = input_.fly || in.fly;
    input_ = in;
    input_.dash = dash;
    input_.fly = fly;
}

void World::placePlayer(const dvec3& feet, double yaw) {
    player_ = Player{};
    player_.position = feet;
    player_.yaw = yaw;
    const TerrainSample s = terrain_.sample(feet.x, feet.z);
    if (s.underwater() && s.waterLevel - s.height > k::PLAYER_SWIM_DEPTH) {
        player_.mode = PlayerMode::Swim;
        player_.position.y = s.waterLevel;
        player_.swim = 1.0;
    } else {
        player_.position.y = s.height;
    }
}

void World::stepPlayer() {
    const double dt = WorldClock::stepSeconds();
    std::vector<TreeInstance> trees;
    const dvec3 p = player_.position;
    treesInArea(terrain_, p.x - 4.0, p.z - 4.0, p.x + 4.0, p.z + 4.0, trees);
    const PlayerMode was = player_.mode;
    const double fall = -player_.velocity.y;
<<<<<<< Updated upstream
    taelia::stepPlayer(player_, input_, terrain_, trees, dt);
=======
    std::vector<PlayerObstacle> blocks;
    for (const Rune& r : runes_) {
        if (r.placement != RunePlacement::Carrier) continue;
        const RuneCarrier c = runeCarrier(r);
        if (std::hypot(c.centre.x - p.x, c.centre.z - p.z) < 6.0) blocks.push_back({c.centre, c.half, c.yaw});
    }
    taelia::stepPlayer(player_, input_, terrain_, trees, blocks, dt);
>>>>>>> Stashed changes
    // A hard landing presses its feet into soft ground (its legs take most of it).
    if (was == PlayerMode::Air && player_.mode == PlayerMode::Ground && fall > 3.0) {
        const double energy = 0.5 * k::HUMAN_MASS * fall * fall * k::LANDING_PLASTIC;
        const dvec3 side{std::cos(player_.yaw), 0.0, -std::sin(player_.yaw)};
        for (const double s : {-1.0, 1.0}) dentGround(player_.position + side * (0.11 * s), energy * 0.5, 0.09);
    }
    input_.dash = input_.fly = false;
    // Its capsule for balls: round its body, lower while it swims.
    const double centre = player_.mode == PlayerMode::Swim ? -0.4 : 0.5 * k::HUMAN_HEIGHT - 0.25 * player_.crouch;
    physics_->moveCapsule(1, player_.position + dvec3{0, centre, 0});
}

// Bursting: all it holds goes into the field where it is (a little back from what it hit, into the air).
void World::burst(MagicBall& b, const dvec3& at) {
    if (b.aspect == Aspect::Necrotic && b.kg > 0) {
        // Contact spreads the same mass across adjacent ground-cover cells instead of marooning it in one cell.
        const double radius = k::NECROTIC_CONTACT_RADIUS;
        double weights = 0;
        for (int z=-2; z<=2; ++z) for (int x=-2; x<=2; ++x) weights += std::exp(-0.5 * (x*x+z*z));
        for (int z=-2; z<=2; ++z) for (int x=-2; x<=2; ++x) {
            dvec3 p = at + dvec3{x*radius*0.5, 0, z*radius*0.5};
            p.y = std::max(p.y, terrain_.height(p.x,p.z)+0.15);
            magic_->addMovingMass(p, b.aspect, b.kg * std::exp(-0.5*(x*x+z*z))/weights, b.velocity);
        }
        b.kg = 0;
        return;
    }
    if (b.kg > 0.0 && !magic_->addMovingMass(at, b.aspect, b.kg, b.velocity))
        magic_->addMovingMass(at + dvec3{0, 1.0, 0}, b.aspect, b.kg, b.velocity);
    b.kg = 0.0;
}

uint32_t World::throwMagicBall(const dvec3& from, const dvec3& velocity, Aspect aspect, double kg) {
    if (!(kg > 0) || !std::isfinite(kg) || aspect == Aspect::Lightning) return 0;
    MagicBall b;
    b.id = nextMagicBall_++;
    b.position = from; b.velocity = velocity; b.aspect = aspect;
    b.kg = std::min(kg, double(k::MAGIC_BALL_FULL));
    magicBalls_.push_back(b);
    return b.id;
}

void World::stepMagicBalls() {
    const double dt = WorldClock::stepSeconds();
    if (!input_.gather) gatherBlocked_ = false;
    // Gathering between the hands: its aspect from the cells round them (nearer more), and neutral magic, slower: it
    // must take the aspect.
    const dvec3 hands = playerToWorld(player_, humanBallPoint(humanMotion()));
    if (input_.gather && !gatherBlocked_ && magic_->started()) {
        if (gathering_.kg <= 0.0) {
            gathering_ = MagicBall{};
            gathering_.id = nextMagicBall_++;
            gathering_.aspect = input_.aspect;
        }
        gathering_.position = hands;
        double room = double(k::MAGIC_BALL_FULL) - gathering_.kg;
        const double reach = k::MAGIC_BALL_REACH, stepM = 2.0;  // the fine cells' size
        for (double dy = -reach; dy <= reach + 1e-9 && room > 0.0; dy += stepM)
            for (double dz = -reach; dz <= reach + 1e-9 && room > 0.0; dz += stepM)
                for (double dx = -reach; dx <= reach + 1e-9 && room > 0.0; dx += stepM) {
                    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
                    if (dist > reach) continue;
                    const dvec3 q = hands + dvec3{dx, dy, dz};
                    MagicDensities d;
                    double volume = 0;
                    if (!magic_->cellAt(q, d, &volume) || volume > 9.0) continue;  // the fine level only
                    const double w = (1.0 - dist / reach) * dt * k::MAGIC_BALL_DRAW;
                    const Aspect a = gathering_.aspect;
                    double own = std::min(double(d[a]) * volume * w, room);
                    if (own > 0.0 && magic_->addMass(q, a, -own)) {
                        gathering_.kg += own;
                        room -= own;
                    }
                    if (a == Aspect::Neutral) continue;
                    double neutral = std::min(double(d[Aspect::Neutral]) * volume * w * k::MAGIC_BALL_NEUTRAL, room);
                    if (neutral > 0.0 && magic_->addMass(q, Aspect::Neutral, -neutral)) {
                        gathering_.kg += neutral;
                        room -= neutral;
                    }
                }
        if (gathering_.kg >= k::MAGIC_BALL_FULL - 1e-6 && !gathering_.manifested) manifestFullBall();
        if (gathering_.manifested && gathering_.kg >= k::MAGIC_BALL_FULL * 0.99) gathering_.fullHold += dt;
        else gathering_.fullHold = 0;
        if (gathering_.instability() >= 1.0) {
            burst(gathering_, hands);
            gathering_ = MagicBall{};
            gatherBlocked_ = true;
        }
    } else if (gathering_.kg > 0.0) {
        // Let go: thrown toward the aim, or (a click's worth) let go where it is.
        if (gathering_.kg >= k::MAGIC_BALL_MIN) {
            MagicBall b = gathering_;
            b.fullHold = 0;
            b.position = hands;
            dvec3 dir = input_.aim - hands;
            dir = length(dir) > 0.5 ? normalize(dir) : directionFromAngles(input_.yaw, input_.pitch);
            b.velocity = dir * double(k::MAGIC_BALL_SPEED) + player_.velocity;
            magicBalls_.push_back(b);
            player_.toss = 0.0;
        } else {
            burst(gathering_, hands);
        }
        gathering_ = MagicBall{};
    }
<<<<<<< Updated upstream
    // Flying: straight (magic is weightless) until it hits the ground, water or a trunk, or has gone its range.
    for (MagicBall& b : magicBalls_) {
        const dvec3 from = b.position, to = from + b.velocity * dt;
        const double r = b.radius();
        b.travelled += length(to - from);
        const TerrainSample s = terrain_.sample(to.x, to.z);
        if (to.y - r <= s.height) {
            burst(b, {to.x, std::max(to.y, s.height + 0.3), to.z});
            continue;
        }
        if (s.underwater() && to.y - r <= s.waterLevel) {
            burst(b, {to.x, s.waterLevel + 0.3, to.z});
            continue;
        }
        std::vector<TreeInstance> trees;
        treesInArea(terrain_, std::min(from.x, to.x) - 2.0, std::min(from.z, to.z) - 2.0, std::max(from.x, to.x) + 2.0,
                    std::max(from.z, to.z) + 2.0, trees);
        bool hit = false;
        for (const TreeInstance& t : trees) {
            if (to.y > t.position.y + 6.0 * t.scale || to.y < t.position.y) continue;
            // Closest approach of the step to the trunk's axis (in plan).
            const double ax = to.x - from.x, az = to.z - from.z, len2 = ax * ax + az * az;
            const double u = len2 > 1e-12 ? std::clamp(((t.position.x - from.x) * ax + (t.position.z - from.z) * az) / len2, 0.0, 1.0) : 0.0;
            const double cx = from.x + ax * u - t.position.x, cz = from.z + az * u - t.position.z;
            if (cx * cx + cz * cz < (0.35 * t.scale + r) * (0.35 * t.scale + r)) {
                burst(b, from);
                hit = true;
                break;
            }
        }
        if (hit) continue;
        b.position = to;
        if (b.travelled > k::MAGIC_BALL_RANGE) burst(b, to);
=======
    // Opposed coherent packets neutralize where their swept volumes first overlap, unless a surface blocks them.
    for (size_t i=0;i<magicBalls_.size();++i) for(size_t j=i+1;j<magicBalls_.size();++j) {
        MagicBall& a=magicBalls_[i]; MagicBall& b=magicBalls_[j];
        if (!((a.aspect==Aspect::Light && b.aspect==Aspect::Necrotic) ||
              (b.aspect==Aspect::Light && a.aspect==Aspect::Necrotic)) || a.kg<=0 || b.kg<=0) continue;
        double t; dvec3 n;
        const dvec3 relative=a.position-b.position;
        if(!sweepCapsule(relative,relative+(a.velocity-b.velocity)*dt,{},{},a.radius()+b.radius(),t,n)) continue;
        const dvec3 pa=a.position+a.velocity*(dt*t), pb=b.position+b.velocity*(dt*t);
        SurfaceHit block;
        if(traceEnvironment(a.position,pa,a.radius(),block) || traceEnvironment(b.position,pb,b.radius(),block)) continue;
        const double canceled=std::min(a.kg,b.kg);
        a.kg-=canceled; b.kg-=canceled;
        magic_->addMovingMass((pa+pb)*0.5,Aspect::Neutral,2*canceled,(a.velocity+b.velocity)*0.5);
>>>>>>> Stashed changes
    }
    // Sweep to the first contact or exhaustion point. Loss depends on distance, never the frame count.
    for (MagicBall& b : magicBalls_) {
        const dvec3 from = b.position;
        const double speed = length(b.velocity);
        const double distance = std::max(0.0, std::min({speed * dt, double(k::MAGIC_BALL_RANGE) - b.travelled,
                                            b.kg / double(k::MAGIC_BALL_LOSS_PER_METRE)}));
        const dvec3 to = from + normalize(b.velocity) * distance;
        const double r = b.radius();
        SurfaceHit hit;
        const bool contact = traceEnvironment(from, to, r, hit);
        const double flown = distance * (contact ? hit.fraction : 1.0);
        b.position = from + normalize(b.velocity) * flown;
        b.travelled += flown;
        // A sign can take its aspect (or neutral) from coherent magic passing through it, by the same intake law.
        for (Rune& rn : runes_) {
            if (rn.broken || rn.drawing || rn.drawProgress < 1.0 || (b.aspect != rn.aspect && b.aspect != Aspect::Neutral)) continue;
            double enter, reverse; dvec3 n;
            const double reach = rn.size * 0.5 + r;
            if (!sweepCapsule(from, b.position, rn.position, rn.position, reach, enter, n) ||
                !sweepCapsule(b.position, from, rn.position, rn.position, reach, reverse, n)) continue;
            const double duration = flown / std::max(speed, 1e-9) * std::max(1.0 - enter - reverse, 0.0);
            const double volume = 4.0 / 3.0 * kPi * r * r * r;
            const double past = std::max(b.kg / volume - k::MAGIC_DEW_POINT, 0.0);
            const double room = std::max(0.0, rn.seal() * 0.5 * rn.volume() - rn.mass());
            const double take = std::min({room, past * volume, double(k::RUNE_INTAKE) * past * rn.volume() * duration});
            rn.contents[int(rn.aspect)] += take;
            b.kg -= take;
            if (rn.density() > rn.breakPoint()) breakRune(rn, *magic_);
        }
        const double shed = std::min(b.kg, flown * double(k::MAGIC_BALL_LOSS_PER_METRE));
        // Split deposits along the travelled part, including partial final steps; no deposit behind an obstacle.
        const int pieces = std::max(1, int(std::ceil(flown / 0.25)));
        for (int i = 0; i < pieces; ++i) {
            const dvec3 p=from+(b.position-from)*((i+0.5)/pieces);
            magic_->addMovingMass(p,b.aspect,shed/pieces,b.velocity);
            if(b.aspect==Aspect::Light || b.aspect==Aspect::Necrotic) {
                const Aspect opposite=b.aspect==Aspect::Light?Aspect::Necrotic:Aspect::Light;
                MagicDensities ambient; double cellVolume;
                if(magic_->cellAt(p,ambient,&cellVolume)) {
                    const double volume=std::min(cellVolume,kPi*r*r*flown/pieces);
                    const double canceled=std::min(std::max(b.kg-shed,0.0),double(ambient[opposite])*volume);
                    if(canceled>0) {
                        magic_->addMass(p,opposite,-canceled);
                        magic_->addMass(p,Aspect::Neutral,2*canceled);
                        b.kg-=canceled;
                    }
                }
            }
        }
        b.kg -= shed;
        if (contact) {
            const bool weak = b.kg < k::MAGIC_IMPACT_MIN_KG;
            magicContact(b, hit);
            if (weak) {
                // A weak packet loses coherence harmlessly. Retaining its dense aspect here would bypass
                // the impact threshold on the following field tick (notably necrotic grass damage).
                b.aspect = Aspect::Neutral;
                b.velocity = {};
            }
            // Incoming momentum normal to a static surface has gone into that surface.
            const double vn = dot(b.velocity, hit.normal);
            if (vn < 0 && hit.kind != SurfaceKind::Ball) b.velocity = b.velocity - hit.normal * vn;
            burst(b, hit.point + hit.normal * (r + 0.04));
        } else if (b.travelled >= k::MAGIC_BALL_RANGE - 1e-8) burst(b, b.position);
    }
    std::erase_if(magicBalls_, [](const MagicBall& b) { return b.kg <= 1e-9; });
}

void World::dentGround(const dvec3& p, double energy, double radius) {
    if (!(energy > 0.0) || !(radius > 0.0)) return;
    const double dug = std::max(-terrain_.editAt(p.x, p.z), 0.0);
    const Stratum s = terrain_.stratumAt(p.x, p.z, dug + 0.01);
    const double volume = energy / s.hardness;
    if (volume < 2e-8) return;
    // Pressed in as a spherical cap of what hit it (V = pi d^2 (R - d/3)), the soil pushed into a bowl of that volume.
    double d = std::sqrt(volume / (kPi * radius));
    for (int i = 0; i < 4; ++i) d = std::sqrt(volume / (kPi * std::max(radius - d / 3.0, 0.4 * radius)));
    d = std::min(d, 2.0 * radius);
    // A paraboloid of that volume and depth, at least a cell and a half across the grid dents are kept on (shallower
    // then, the same volume).
    const double bowl = std::max(std::sqrt(2.0 * volume / (kPi * d)), 1.5 * double(k::TERRAIN_EDIT_CELL));
    d = 2.0 * volume / (kPi * bowl * bowl);
    terrain_.dent(p.x, p.z, bowl, d);
    if (bowl > 0.3 || d > 0.12) {  // deep enough to change the ground's shape for the physics and the renderers
        const double r = bowl * 1.7;
        physics_->groundChanged(p.x - r, p.z - r, p.x + r, p.z + r);
        dentAreas_.push_back({p.x - r, p.z - r, p.x + r, p.z + r});
    }
}

void World::takeDentAreas(std::vector<Area>& out) {
    out.insert(out.end(), dentAreas_.begin(), dentAreas_.end());
    dentAreas_.clear();
}

void World::crumble(const dvec3& at, double radius, double necroticKg) {
    dust_.push_back({at, radius, 0.0});
    if (necroticKg > 0.0) magic_->addMass(at, Aspect::Necrotic, necroticKg);
}

uint32_t World::addRune(Rune r) {
    r.id = nextRune_++;
    r.broken = false;
<<<<<<< Updated upstream
=======
    r.ground = terrain_.height(r.position.x, r.position.z);
    const RuneCarrier c = runeCarrier(r);
    if (r.placement == RunePlacement::Carrier)
        physics_->addBox(r.id, c.centre, c.half, c.yaw, r.material == RuneMaterial::Wood);
>>>>>>> Stashed changes
    runes_.push_back(r);
    return r.id;
}

void World::damageRune(uint32_t id) {
    for (Rune& r : runes_)
        if (r.id == id) breakRune(r, *magic_);
<<<<<<< Updated upstream
=======
    dropBrokenRunes();
}

void World::dropBrokenRunes() {
    for (Rune& r : runes_)
        if (r.placement == RunePlacement::Surface && r.support != 0) {
            const auto support = std::find_if(runes_.begin(), runes_.end(), [&](const Rune& s) { return s.id == r.support && !s.broken; });
            if (support == runes_.end()) breakRune(r, *magic_);
        }
    for (const Rune& r : runes_)
        if (r.broken && r.placement == RunePlacement::Carrier) physics_->removeBox(r.id);
>>>>>>> Stashed changes
    std::erase_if(runes_, [](const Rune& r) { return r.broken; });
}

dvec3 World::windAt(const dvec3& p) const {
    const TerrainSample s = terrain_.sample(p.x, p.z);
    const double base = s.underwater() ? s.waterLevel : s.height;
    const dvec3 air = wind_.at(p, p.y > base ? p.y - base : 0.0);
    // Moving air magic makes the wind: where it moves on its own (a whirlwind), it takes the air along by its share.
    const MagicSample m = magic_->sample(p);
    const double rho = m.rho.total();
    if (m.level < 0 || rho <= 1e-6) return air;
    return air + (m.u - air) * (double(k::MAGIC_WIND_SHARE) * m.rho[Aspect::Air] / rho);
}

dvec3 World::wind() const {
    // Blows toward the opposite of where it comes from.
    const double to = (settings_.windFromDeg + 180.0) * kDegToRad;
    return dvec3{std::sin(to), 0.0, std::cos(to)} * settings_.windSpeed;
}

uint64_t World::stateHash() const {
    auto bits = [](double v) {
        uint64_t u;
        std::memcpy(&u, &v, sizeof u);
        return u;
    };
    uint64_t h = mix64(seed_);
    h = hashCombine(h, static_cast<uint64_t>(clock_.steps()));
    h = hashCombine(h, bits(clock_.seconds()));
    h = hashCombine(h, bits(settings_.windSpeed));
    h = hashCombine(h, bits(settings_.windFromDeg));
    h = hashCombine(h, bits(clouds_.displacement().x));
    h = hashCombine(h, bits(clouds_.displacement().z));
    h = hashCombine(h, physics_->stateHash());
    h = hashCombine(h, wind_.stateHash());
    h = hashCombine(h, magic_->stateHash());
    h = hashCombine(h, clouds_.stateHash());
    for (const auto& [key, b] : burning_) {
        h = hashCombine(hashCombine(h, uint64_t(key.first)), uint64_t(key.second));
        for (double value : {b.until,b.ignition.x,b.ignition.y,b.ignition.z,b.height}) h = hashCombine(h,bits(value));
    }
    for (const auto& key : burnt_) h = hashCombine(hashCombine(h, uint64_t(key.first)), uint64_t(key.second));
    for (const auto& [key, recovery] : recovering_) {
        h = hashCombine(hashCombine(h, uint64_t(key.first)), uint64_t(key.second));
        h = hashCombine(hashCombine(h, bits(recovery.stored)), bits(recovery.growth));
    }
    for (const MagicImpact& impact : magicImpacts_) {
        h = hashCombine(hashCombine(h, impact.id), uint64_t(impact.aspect));
        for (double value : {impact.position.x, impact.position.y, impact.position.z, impact.radius, impact.age,
                             impact.normal.x,impact.normal.y,impact.normal.z}) h = hashCombine(h, bits(value));
    }
    for (const Glow& g : scorches_) h = hashCombine(hashCombine(h, bits(g.position.x)), bits(g.position.z));
    h = hashCombine(h, bits(fire_));
    h = hashCombine(h, bits(fireBoost_));
    for (const auto& [id, held] : held_) h = hashCombine(hashCombine(h, id), bits(held));
    h = hashCombine(h, bits(humanHeld_));
    h = hashCombine(h, terrain_.editsVersion());
    h = hashCombine(hashCombine(h, bits(humanWater_)), bits(humanNecrotic_));
    for (const auto& [id, n] : ballNecrotic_) h = hashCombine(hashCombine(h, id), bits(n));
    h = hashCombine(hashCombine(hashCombine(h, bits(player_.position.x)), bits(player_.position.y)), bits(player_.position.z));
    h = hashCombine(h, uint64_t(gatherBlocked_));
    h = hashCombine(hashCombine(h, nextMagicBall_), nextRune_);
    const auto hashBall = [&](const MagicBall& b) {
        h = hashCombine(hashCombine(h, b.id), uint64_t(b.aspect));
        h = hashCombine(h, uint64_t(b.manifested));
        for (const double value : {b.position.x, b.position.y, b.position.z, b.velocity.x, b.velocity.y, b.velocity.z, b.kg, b.travelled, b.fullHold})
            h = hashCombine(h, bits(value));
    };
    hashBall(gathering_);
    for (const MagicBall& b : magicBalls_) hashBall(b);
    for (const PhysicalManifest& m : physical_) {
        h = hashCombine(hashCombine(h, uint64_t(m.aspect)), uint64_t(m.settled));
        for (const double value : {m.position.x, m.position.y, m.position.z, m.velocity.x, m.velocity.y, m.velocity.z, m.radius, m.age, m.kg})
            h = hashCombine(h, bits(value));
    }
    for (const Rune& r : runes_) {
<<<<<<< Updated upstream
        h = hashCombine(h, r.id);
=======
        h = hashCombine(hashCombine(h, r.id), bits(r.held));
        h = hashCombine(hashCombine(h, uint64_t(r.placement)), r.support);
        h = hashCombine(hashCombine(h, uint64_t(r.drawing)), uint64_t(r.aspect));
        for (const double value : {r.position.x, r.position.y, r.position.z, r.normal.x, r.normal.y, r.normal.z, r.drawProgress, r.overcharge})
            h = hashCombine(h, bits(value));
>>>>>>> Stashed changes
        for (const double c : r.contents) h = hashCombine(h, bits(c));
    }
    for (const Manifestation& m : magic_->manifestations()) h = hashCombine(hashCombine(h, m.id), bits(m.held));
    return h;
}

}  // namespace taelia
