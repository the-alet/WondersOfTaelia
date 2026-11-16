// Runes (step 7): the block each rune is carved on (sim/rune.h runeCarrier: a standing stone, a hewn wooden post, a
// cast metal plate), meshed once per rune as a rounded box shaped by its material (the stone tapered and weathered with
// an arched top, the post with a slanted top, the plate an arched tablet), drawn into the G-buffer with the sign carved
// into its front (rune.hlsl), and one ray-tracing instance per block (shadows, water).
#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/terrain_renderer.h"
#include "rhi/rhi.h"
#include "taelia/sim/rune.h"

namespace taelia {

class RuneRenderer {
public:
    bool init(rhi::Device* device, std::string* error);
    void shutdown();
    // Once per frame, before the G-buffer pass: new runes get their blocks, broken ones lose them.
    void update(const std::vector<Rune>* runes, const dvec3& camera, const Frustum& frustum);
    void draw(rhi::CommandList* cmd, const rhi::Transient& frameConstants);
    void buildStructures(rhi::CommandList* cmd);
    void addInstances(std::vector<rhi::TlasInstance>& out) const;

private:
    struct Item {
        std::unique_ptr<rhi::Buffer> vertices;  // 16 bytes each: position (block frame, m), octahedral normal
        std::unique_ptr<rhi::AccelerationStructure> blas;
        RuneCarrier carrier;
        Rune rune;
        bool built = false, visible = false, seen = false;
        float place[4] = {};  // the block's middle relative to the camera, its yaw
    };
    rhi::Device* device_ = nullptr;
    std::unique_ptr<rhi::Pipeline> pipe_;
    std::unique_ptr<rhi::Buffer> indices_;
    uint32_t indexCount_ = 0, vertexCount_ = 0;
    std::unordered_map<uint32_t, Item> items_;
};

}  // namespace taelia
