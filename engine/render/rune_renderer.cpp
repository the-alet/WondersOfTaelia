#include "render/rune_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "render/gbuffer.h"
#include "taelia/shared_constants.h"
#include "taelia/sim/noise.h"

namespace taelia {

namespace {

constexpr uint32_t kRuneInstance = 0xFFFFF8;  // rt.hlsli RUNE_INSTANCE
constexpr int kRound = 4, kFlat = 12, kSide = 2 * kRound + kFlat + 1;  // points across a face: round, flat, round
constexpr double kFlatTo = 0.5;  // |cube coordinate| up to which a face is flat; past it, it rounds over the edge

struct RunePush {
    uint32_t vbSrv, aspect, material, seed;
    float place[4];  // the block's middle relative to the camera, its yaw
    float sign[4];   // the sign's centre above the block's middle, its size, the front face (z), unused
    float magic[4];  // how full (density / seal), what the block holds (0..1 of its share), 1 over its seal, unused
    float right[4], normal[4]; // drawn signs can lie on any surface
};
static_assert(sizeof(RunePush) == 96, "rune.hlsl push constants");

// Octahedral (gbuffer.hlsli decodeOctNormal: y is the octahedron's up axis).
uint32_t octNormal(double x, double y, double z) {
    const double s = std::fabs(x) + std::fabs(y) + std::fabs(z);
    if (s < 1e-12) return octNormal(0, 1, 0);
    x /= s;
    y /= s;
    z /= s;
    double ex = x, ez = z;
    if (y < 0) {
        ex = (1.0 - std::fabs(z)) * (x >= 0 ? 1.0 : -1.0);
        ez = (1.0 - std::fabs(x)) * (z >= 0 ? 1.0 : -1.0);
    }
    const uint32_t u = uint32_t(std::lround((ex * 0.5 + 0.5) * 65535.0)), v = uint32_t(std::lround((ez * 0.5 + 0.5) * 65535.0));
    return u | (v << 16);
}

// Where along one axis of a face the grid's points lie (cube coordinates): the rounded edge, the flat middle, the other edge.
double gridPoint(int i) {
    if (i <= kRound) return -1.0 + (1.0 - kFlatTo) * double(i) / kRound;
    if (i >= kRound + kFlat) return kFlatTo + (1.0 - kFlatTo) * double(i - kRound - kFlat) / kRound;
    return -kFlatTo + 2.0 * kFlatTo * double(i - kRound) / kFlat;
}

double smooth(double x, double a, double b) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// A point of the unit cube's surface on the rounded box: each axis is flat in the middle (blended across the inner box)
// and rounds over the edge (a quarter circle of the rounding radius), so edges and corners are round and the faces flat.
dvec3 roundedBox(const dvec3& c, const dvec3& half, double r, dvec3& normal) {
    auto axis = [&](double x, double h, double& raw) {
        const double inner = std::max(h - r, 0.0);
        if (std::fabs(x) <= kFlatTo) {
            raw = 0.0;
            return x / kFlatTo * inner;
        }
        const double s = x < 0 ? -1.0 : 1.0;
        raw = s * (std::fabs(x) - kFlatTo) / (1.0 - kFlatTo);
        return s * inner;
    };
    dvec3 raw, blend;
    blend.x = axis(c.x, half.x, raw.x);
    blend.y = axis(c.y, half.y, raw.y);
    blend.z = axis(c.z, half.z, raw.z);
    normal = normalize(raw);
    return blend + normal * r;
}

// Its material's shape on the rounded box (the block's frame: +z its front, y up): the stone tapers, arches and
// weathers, the post's top slants, the plate is an arched tablet. Smooth everywhere, so its normals come from its slope.
dvec3 shaped(const dvec3& q, const RuneCarrier& c, RuneMaterial m, uint64_t seed) {
    const double t = (q.y + c.half.y) / (2.0 * c.half.y), u = q.x / c.half.x;
    dvec3 p = q;
    switch (m) {
        case RuneMaterial::Stone: {
            const double lean = (hashToUnit(mix64(seed * 7 + 1)) - 0.5) * 0.3;
            p.x *= 1.0 - 0.10 * t;
            p.z *= 1.0 - 0.18 * t;
            p.y -= c.half.x * (0.30 * u * u + lean * u) * smooth(t, 0.55, 1.0);
            // Weathered: low lumps over all of it, outward.
            const dvec3 out = normalize(dvec3{q.x / c.half.x, 0.3 * q.y / c.half.y, q.z / c.half.z});
            const double lump = gradientNoise3(q.x, q.y, q.z, 0.45, seed) * 0.7 + gradientNoise3(q.x, q.y, q.z, 0.17, seed + 1) * 0.3;
            p = p + out * (lump * 0.014);
            break;
        }
        case RuneMaterial::Wood:
            p.y -= c.half.x * 0.22 * std::sqrt(u * u + 0.01) * smooth(t, 0.7, 1.0);
            break;
        case RuneMaterial::Metal:
            p.y -= c.half.x * 0.55 * u * u * smooth(t, 0.5, 1.0);
            break;
    }
    return p;
}

}  // namespace

bool RuneRenderer::init(rhi::Device* device, std::string* error) {
    device_ = device;
    // One grid per face of the cube; every block shares it.
    std::vector<uint32_t> idx;
    for (int face = 0; face < 6; ++face) {
        const uint32_t base = uint32_t(face * kSide * kSide);
        for (int j = 0; j + 1 < kSide; ++j)
            for (int i = 0; i + 1 < kSide; ++i) {
                const uint32_t a = base + uint32_t(j * kSide + i), b = a + 1, c = a + uint32_t(kSide), d = c + 1;
                idx.insert(idx.end(), {a, b, d, a, d, c});
            }
    }
    vertexCount_ = uint32_t(6 * kSide * kSide);
    indexCount_ = uint32_t(idx.size());
    indices_ = device_->createBuffer({idx.size() * 4, rhi::BufferUsage::Index | rhi::BufferUsage::Storage | rhi::BufferUsage::AccelerationInput,
                                      rhi::Memory::GpuOnly, "rune.indices"}, idx.data());
    rhi::GraphicsPipelineDesc d;
    d.vs = "rune.VSMain";
    d.ps = "rune.PSMain";
    gbuffer::fillFormats(d);
    d.cull = rhi::Cull::None;
    d.name = "runes";
    pipe_ = device_->createGraphicsPipeline(d, error);
    return pipe_ && indices_;
}

void RuneRenderer::shutdown() {
    items_.clear();
    pipe_.reset();
    indices_.reset();
}

void RuneRenderer::update(const std::vector<Rune>* runes, const dvec3& cam, const Frustum& f) {
    for (auto& [id, it] : items_) it.seen = false;
    if (runes)
        for (const Rune& r : *runes) {
            Item& it = items_[r.id];
            it.seen = true;
            it.rune = r;
            if (!it.vertices && r.placement == RunePlacement::Carrier) {
                // Its block, meshed once: the rounded box, shaped by its material, normals from the shape's slope.
                it.carrier = runeCarrier(r);
                const RuneCarrier& c = it.carrier;
                const uint64_t seed = mix64(r.id * 0x9e3779b97f4a7c15ull + 17);
                std::vector<uint32_t> data;
                data.reserve(size_t(vertexCount_) * 4);
                for (int face = 0; face < 6; ++face) {
                    const int a = face / 2;
                    const double s = face % 2 == 0 ? 1.0 : -1.0;
                    for (int j = 0; j < kSide; ++j)
                        for (int i = 0; i < kSide; ++i) {
                            double cube[3];
                            cube[a] = s;
                            cube[(a + 1) % 3] = gridPoint(i) * s;  // flipped on the far faces so every face winds the same way
                            cube[(a + 2) % 3] = gridPoint(j);
                            dvec3 n;
                            const dvec3 q = roundedBox({cube[0], cube[1], cube[2]}, c.half, c.rounding, n);
                            const dvec3 p = shaped(q, c, r.material, seed);
                            // The normal through the shape: n times the cofactors of its Jacobian (found numerically).
                            const double e = 1e-4;
                            const dvec3 jx = (shaped(q + dvec3{e, 0, 0}, c, r.material, seed) - shaped(q - dvec3{e, 0, 0}, c, r.material, seed)) * (0.5 / e);
                            const dvec3 jy = (shaped(q + dvec3{0, e, 0}, c, r.material, seed) - shaped(q - dvec3{0, e, 0}, c, r.material, seed)) * (0.5 / e);
                            const dvec3 jz = (shaped(q + dvec3{0, 0, e}, c, r.material, seed) - shaped(q - dvec3{0, 0, e}, c, r.material, seed)) * (0.5 / e);
                            const dvec3 cx = cross(jy, jz), cy = cross(jz, jx), cz = cross(jx, jy);  // rows of the cofactor matrix
                            const dvec3 nn = normalize(cx * n.x + cy * n.y + cz * n.z);
                            float pos[3] = {float(p.x), float(p.y), float(p.z)};
                            uint32_t bits[3];
                            std::memcpy(bits, pos, sizeof bits);
                            data.insert(data.end(), {bits[0], bits[1], bits[2], octNormal(nn.x, nn.y, nn.z)});
                        }
                }
                it.vertices = device_->createBuffer({data.size() * 4, rhi::BufferUsage::Storage | rhi::BufferUsage::AccelerationInput,
                                                     rhi::Memory::GpuOnly, "rune.block"}, data.data());
            }
            const RuneCarrier& c = it.carrier;
            const bool carrier = r.placement == RunePlacement::Carrier;
            const dvec3 rel = (carrier ? c.centre : r.position) - cam;
            const double reach = carrier ? length(c.half) + 0.1 : r.size;
            it.visible = f.boxVisible(rel - dvec3{reach, reach, reach}, rel + dvec3{reach, reach, reach});
            it.place[0] = float(rel.x);
            it.place[1] = float(rel.y);
            it.place[2] = float(rel.z);
            it.place[3] = float(c.yaw);
        }
    std::erase_if(items_, [](const auto& kv) { return !kv.second.seen; });  // broken: gone (the device keeps them until the GPU is done)
}

void RuneRenderer::draw(rhi::CommandList* cmd, const rhi::Transient& fc) {
    if (items_.empty() || !pipe_) return;
    cmd->beginMarker("runes");
    cmd->setIndexBuffer(indices_.get(), 0, rhi::IndexType::U32);
    cmd->setPipeline(pipe_.get());
    cmd->setFrameConstants(fc);
    for (const auto& [id, it] : items_) {
        if (!it.visible) continue;
        const Rune& r = it.rune;
        RunePush p{};
        p.vbSrv = it.vertices ? it.vertices->srv() : 0;
        p.aspect = uint32_t(r.aspect);
        p.material = uint32_t(r.material);
        p.seed = uint32_t(id * 2654435761u);
        std::memcpy(p.place, it.place, sizeof p.place);
        p.sign[0] = float(it.carrier.signY);
        p.sign[1] = float(r.size);
        p.sign[2] = float(it.carrier.half.z);
        p.sign[3] = float(r.placement);
        dvec3 right, up; runeBasis(r.normal, right, up);
        p.right[0] = float(right.x); p.right[1] = float(right.y); p.right[2] = float(right.z);
        const dvec3 normal = normalize(r.normal);
        p.normal[0] = float(normal.x); p.normal[1] = float(normal.y); p.normal[2] = float(normal.z);
        p.magic[0] = float(r.density() / r.seal());
        p.magic[1] = float(std::clamp(r.held / k::MAGIC_HELD, 0.0, 1.0));
        p.magic[2] = float(std::max(r.density() > r.seal() ? 1.0 : 0.0, r.overcharge));
        p.magic[3] = float(r.drawProgress);
        cmd->pushConstants(&p, sizeof p);
        if (r.placement == RunePlacement::Carrier) cmd->drawIndexed(indexCount_, 1, 0);
        else cmd->draw(6, 1);
    }
    cmd->endMarker();
}

void RuneRenderer::buildStructures(rhi::CommandList* cmd) {
    if (!device_->rayTracingSupported()) return;
    for (auto& [id, it] : items_) {
        if (it.built || !it.vertices) continue;
        cmd->barrier(it.vertices.get(), rhi::State::ShaderRead);
        cmd->barrier(indices_.get(), rhi::State::ShaderRead);
        rhi::BlasDesc desc;
        desc.geometries.push_back({it.vertices.get(), 0, 16, vertexCount_, indices_.get(), 0, indexCount_, rhi::IndexType::U32, true});
        desc.name = "rune.blas";
        it.blas = device_->createBlas(desc);
        if (it.blas) cmd->buildBlas(it.blas.get(), desc);
        it.built = true;
    }
}

void RuneRenderer::addInstances(std::vector<rhi::TlasInstance>& out) const {
    for (const auto& [id, it] : items_) {
        if (!it.blas) continue;
        const float c = std::cos(it.place[3]), s = std::sin(it.place[3]);
        rhi::TlasInstance inst;
        const float xf[12] = {c, 0, s, it.place[0], 0, 1, 0, it.place[1], -s, 0, c, it.place[2]};
        std::memcpy(inst.transform, xf, sizeof xf);
        inst.customIndex = kRuneInstance;
        inst.forceOpaque = true;
        inst.blas = it.blas.get();
        out.push_back(inst);
    }
}

}  // namespace taelia
