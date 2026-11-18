// Runes (step 7): the block a rune is carved on (engine/render/rune_renderer.cpp meshes it), drawn into the G-buffer.
// Plain sight sees only the carving; the magic view shows the sign in its aspect's colour, brighter the fuller, and
// flickering past its seal, with the block glowing in what its material holds.
#include "common.hlsli"
#include "gbuffer.hlsli"
#include "magic.hlsli"

struct RunePush {
    uint vbSrv, aspect, material, seed;
    float4 place;  // xyz the block's middle relative to the camera, w its yaw
    float4 sign;   // x the sign's centre above the middle, y the rune's size, z the front face's half thickness
    float4 magic;  // x how full (density / seal), y what the block holds (0..1 of its share), z 1 past the seal
    float4 right, normal;
};
[[vk::push_constant]] ConstantBuffer<RunePush> g_push : register(b1, space0);

struct VSOut {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float3 local : TEXCOORD0;  // the block's frame: +z its front, y up
};

VSOut VSMain(uint vid : SV_VertexID) {
    if (g_push.sign.w > 0.5) {
        static const float2 corners[6] = {float2(-1,-1), float2(1,-1), float2(1,1),
                                        float2(-1,-1), float2(1,1), float2(-1,1)};
        VSOut o;
        o.local = float3(corners[vid] * (g_push.sign.y * 0.5), 0);
        o.normal = g_push.normal.xyz;
        const float3 up = cross(o.normal, g_push.right.xyz);
        const float3 rel = g_push.place.xyz + g_push.right.xyz * o.local.x + up * o.local.y;
        o.pos = mul(g_frame.viewProj, float4(rel, 1));
        return o;
    }
    const uint4 raw = g_buffers[g_push.vbSrv].Load4(vid * 16);  // xyz position (float bits), octahedral normal
    VSOut o;
    o.local = asfloat(raw.xyz);
    const float c = cos(g_push.place.w), s = sin(g_push.place.w);
    const float3 rel = float3(c * o.local.x + s * o.local.z, o.local.y, -s * o.local.x + c * o.local.z) + g_push.place.xyz;
    const float3x3 turn = float3x3(c, 0.0, s, 0.0, 1.0, 0.0, -s, 0.0, c);
    o.normal = mul(turn, decodeOctNormal(raw.w));
    o.pos = mul(g_frame.viewProj, float4(rel, 1));
    return o;
}

#include "rune_glyphs.hlsli"

GBufferOut PSMain(VSOut i) {
    if (g_push.sign.w > 0.5) {
        const float2 p = i.local.xy / max(g_push.sign.y * 0.62, 1e-4);
        const float dist = glyphDist(g_push.aspect, p);
        const float aa = max(fwidth(dist), 0.002);
        const bool suspended = g_push.sign.w > 1.5;
        const float width = suspended ? 0.026 : 0.045;
        const float stroke = 1.0 - smoothstep(width - aa, width + aa, dist);
        clip(stroke - 0.35); // just the strokes; no rectangle or solid slab behind the sign
        const float instability = saturate(g_push.magic.z);
        const float pulse = sin(g_frame.fireInfo.x * 19.0 + float(g_push.seed & 15u));
        // Fine interruptions open along the drawn strokes as the seal destabilizes, in either view.
        const float fracture = abs(sin(p.x * 39.0 + p.y * 23.0 + pulse * 0.35));
        clip(fracture - instability * instability * 0.35);
        float3 n = normalize(i.normal);
        const float rounded = sqrt(saturate(1.0 - (dist / width) * (dist / width)));
        if (suspended) {
            const float eps = 0.002;
            const float2 grad = float2(glyphDist(g_push.aspect, p + float2(eps, 0)) - glyphDist(g_push.aspect, p - float2(eps, 0)),
                                      glyphDist(g_push.aspect, p + float2(0, eps)) - glyphDist(g_push.aspect, p - float2(0, eps))) / (2 * eps);
            const float3 up = cross(n, g_push.right.xyz);
            n = normalize(n * max(rounded, 0.15) + (g_push.right.xyz * grad.x + up * grad.y) * saturate(dist / width));
        }
        const float flicker = 1.0 - instability * (0.3 - 0.3 * pulse);
        const float3 pigment = (suspended ? float3(0.20, 0.27, 0.30) * (0.5 + 0.5 * rounded) : float3(0.035, 0.028, 0.023)) * flicker;
        const float3 glow = (float3(0.12, 0.12, 0.12) + aspectColor(g_push.aspect + 1) *
                            (3.3 * clamp(g_push.magic.x, 0.0, 1.5) * flicker)) * K_MAGIC_SURFACE_GLOW;
        GBufferOut o;
        o.albedo = magicView() ? magicViewAlbedo(pigment, glow) : float4(pigment, 1);
        o.normal = float4(n, suspended ? 0.3 : 0.75);
        o.material = float4(0, suspended ? 0.45 : 0.1, 0, 0);
        o.motion = 0;
        return o;
    }
    float3 n = normalize(i.normal);
    const float cs = cos(g_push.place.w), sn = sin(g_push.place.w);
    const float3 right = float3(cs, 0, -sn), front = float3(sn, 0, cs);
    // What it is made of, weathered a little.
    const float off = float(g_push.seed & 63u);
    float3 albedo;
    float rough, spec;
    uint carrier1;
    if (g_push.material == 0) {  // wood: a hewn post, grain down its length
        const float grain = latticeNoise3(float3(i.local.x * 7.0, i.local.y * 0.9, i.local.z * 7.0) + off);
        albedo = float3(0.27, 0.18, 0.11) * (0.75 + 0.5 * grain);
        rough = 0.8;
        spec = 0.1;
        carrier1 = MAGIC_HELD_LIFE;
    } else if (g_push.material == 1) {  // stone: grey, mottled
        const float mot = latticeNoise3(i.local * 2.2 + off) * 0.6 + latticeNoise3(i.local * 7.0 - off) * 0.4;
        const float grain = latticeNoise3(i.local * 95.0 + off);
        albedo = lerp(float3(0.10, 0.105, 0.11), float3(0.22, 0.21, 0.19), mot) * (0.8 + 0.4 * grain);
        rough = 0.85;
        spec = 0.2;
        carrier1 = MAGIC_HELD_EARTH;
    } else {  // metal: a cast bronze plate
        const float mot = latticeNoise3(i.local * 3.0 + off);
        albedo = lerp(float3(0.20, 0.16, 0.10), float3(0.36, 0.30, 0.19), mot);
        rough = 0.45;
        spec = 0.55;
        carrier1 = MAGIC_HELD_EARTH;
    }
    // The sign, carved into the front face: its strokes sunk and darkened.
    float carve = 0;
    const float scale = max(g_push.sign.y * 0.62, 1e-3);
    const float2 gp = float2(i.local.x, i.local.y - g_push.sign.x) / scale;
    const float dist = glyphDist(g_push.aspect, gp);
    const float aa = max(fwidth(dist), 0.003);
    if (i.local.z > g_push.sign.z * 0.5 && dot(n, front) > 0.5) {
        carve = 1.0 - smoothstep(0.06 - aa, 0.06 + aa, dist);
        // A recessed channel: its sloping walls respond to each sun, rather than just darkening the colour.
        const float eps = 0.004;
        const float2 gradient = float2(glyphDist(g_push.aspect, gp + float2(eps, 0)) - glyphDist(g_push.aspect, gp - float2(eps, 0)),
                                      glyphDist(g_push.aspect, gp + float2(0, eps)) - glyphDist(g_push.aspect, gp - float2(0, eps))) / (2.0 * eps);
        const float ramp = saturate((dist - 0.018) / 0.065);
        const float slope = (0.004 / scale) * 6.0 * ramp * (1.0 - ramp) / 0.065;
        const float3 tilt = (right * gradient.x + float3(0, gradient.y, 0)) * slope;
        n = normalize(n - (tilt - n * dot(tilt, n)));
        albedo *= 1.0 - 0.40 * carve;
        rough = saturate(rough + 0.1 * carve);
    }
    GBufferOut o;
    if (magicView()) {
        // The block glows with what it holds; the sign with the rune's own aspect, brighter the fuller, flickering
        // past the seal.
        float3 glow = heldMagicGlow(carrier1, g_push.magic.y, i.local.y * 0.7, 0.3 + 0.4 * carve);
        const float flick = g_push.magic.z > 0.5 ? 0.6 + 0.4 * sin(g_frame.fireInfo.x * 9.0 + float(g_push.seed & 15u)) : 1.0;
        const float full = clamp(g_push.magic.x, 0.0, 1.5);
        glow += aspectColor(g_push.aspect + 1) * (1.5 * full) * flick * (2.2 * carve) * K_MAGIC_SURFACE_GLOW;
        o.albedo = magicViewAlbedo(albedo, glow);
    } else {
        o.albedo = float4(albedo, 1);
    }
    o.normal = float4(n, rough);
    o.material = float4(0, spec, 0, packMagic(carrier1, g_push.magic.y, false));
    o.motion = 0;
    return o;
}
