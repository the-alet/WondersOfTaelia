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
};
[[vk::push_constant]] ConstantBuffer<RunePush> g_push : register(b1, space0);

struct VSOut {
    float4 pos : SV_Position;
    float3 normal : NORMAL;
    float3 local : TEXCOORD0;  // the block's frame: +z its front, y up
};

VSOut VSMain(uint vid : SV_VertexID) {
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

// Signed distances to a stroke's centre line, in the sign's square (-1..1 across).
float sdSeg(float2 p, float2 a, float2 b) {
    const float2 pa = p - a, ba = b - a;
    const float h = saturate(dot(pa, ba) / max(dot(ba, ba), 1e-6));
    return length(pa - ba * h);
}
float sdRing(float2 p, float2 c, float r) { return abs(length(p - c) - r); }

// One invented sign per aspect, as strokes.
float glyphDist(uint aspect, float2 p) {
    float d = 1e3;
    if (aspect == 0) {  // earth: a square on legs, a bar through it
        d = min(d, sdSeg(p, float2(-0.5, -0.3), float2(0.5, -0.3)));
        d = min(d, sdSeg(p, float2(0.5, -0.3), float2(0.5, 0.4)));
        d = min(d, sdSeg(p, float2(0.5, 0.4), float2(-0.5, 0.4)));
        d = min(d, sdSeg(p, float2(-0.5, 0.4), float2(-0.5, -0.3)));
        d = min(d, sdSeg(p, float2(0.0, -0.3), float2(0.0, 0.4)));
        d = min(d, sdSeg(p, float2(0.0, 0.4), float2(0.0, 0.8)));
    } else if (aspect == 1) {  // water: three nested bowls
        d = min(d, sdSeg(p, float2(-0.6, 0.55), float2(0.0, 0.3)));
        d = min(d, sdSeg(p, float2(0.0, 0.3), float2(0.6, 0.55)));
        d = min(d, sdSeg(p, float2(-0.42, 0.1), float2(0.0, -0.12)));
        d = min(d, sdSeg(p, float2(0.0, -0.12), float2(0.42, 0.1)));
        d = min(d, sdSeg(p, float2(-0.24, -0.35), float2(0.0, -0.52)));
        d = min(d, sdSeg(p, float2(0.0, -0.52), float2(0.24, -0.35)));
    } else if (aspect == 2) {  // life: a sprout, a ring over its stem
        d = min(d, sdSeg(p, float2(0.0, -0.7), float2(0.0, 0.0)));
        d = min(d, sdSeg(p, float2(0.0, 0.0), float2(-0.45, 0.25)));
        d = min(d, sdSeg(p, float2(0.0, 0.0), float2(0.45, 0.25)));
        d = min(d, sdRing(p, float2(0.0, 0.42), 0.24));
    } else if (aspect == 3) {  // fire: a spiral with a tail
        [unroll]
        for (int k = 0; k < 12; ++k) {
            const float t0 = float(k) / 12.0, t1 = float(k + 1) / 12.0;
            const float2 a = float2(cos(t0 * 9.42), sin(t0 * 9.42)) * (0.14 + 0.55 * t0);
            const float2 b = float2(cos(t1 * 9.42), sin(t1 * 9.42)) * (0.14 + 0.55 * t1);
            d = min(d, sdSeg(p, a, b));
        }
        d = min(d, sdSeg(p, float2(0.0, -0.2), float2(0.0, -0.75)));
    } else if (aspect == 4) {  // air: two arcs opening outward over a bar
        d = min(d, sdSeg(p, float2(-0.15, -0.45), float2(-0.5, 0.0)));
        d = min(d, sdSeg(p, float2(-0.5, 0.0), float2(-0.15, 0.45)));
        d = min(d, sdSeg(p, float2(0.15, -0.45), float2(0.5, 0.0)));
        d = min(d, sdSeg(p, float2(0.5, 0.0), float2(0.15, 0.45)));
        d = min(d, sdSeg(p, float2(-0.55, -0.72), float2(0.55, -0.72)));
    } else if (aspect == 5) {  // lightning: a ring a bolt falls through
        d = min(d, sdRing(p, float2(0.0, 0.1), 0.55));
        d = min(d, sdSeg(p, float2(0.35, 0.85), float2(-0.1, 0.18)));
        d = min(d, sdSeg(p, float2(-0.1, 0.18), float2(0.18, 0.1)));
        d = min(d, sdSeg(p, float2(0.18, 0.1), float2(-0.28, -0.7)));
    } else if (aspect == 6) {  // light: a ring with eight rays round a dot
        d = min(d, sdRing(p, float2(0.0, 0.0), 0.32));
        d = min(d, length(p) - 0.05);
        [unroll]
        for (int k = 0; k < 8; ++k) {
            const float a = 6.2831853 * float(k) / 8.0;
            const float2 dir = float2(cos(a), sin(a));
            d = min(d, sdSeg(p, dir * 0.48, dir * 0.75));
        }
    } else if (aspect == 7) {  // neutral: two rings, one within the other
        d = min(d, sdRing(p, float2(0.0, 0.0), 0.62));
        d = min(d, sdRing(p, float2(0.0, 0.0), 0.28));
    } else {  // necrotic: the sprout withered, its stem bent down
        d = min(d, sdSeg(p, float2(0.0, 0.6), float2(0.1, -0.1)));
        d = min(d, sdSeg(p, float2(0.1, -0.1), float2(-0.35, -0.55)));
        d = min(d, sdSeg(p, float2(0.1, -0.1), float2(0.5, -0.45)));
        d = min(d, sdSeg(p, float2(-0.55, 0.35), float2(0.55, 0.55)));
    }
    return d;
}

GBufferOut PSMain(VSOut i) {
    const float3 n = normalize(i.normal);
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
        albedo = lerp(float3(0.28, 0.28, 0.27), float3(0.44, 0.42, 0.39), mot);
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
    if (i.local.z > g_push.sign.z * 0.5) {
        const float2 gp = float2(i.local.x, i.local.y - g_push.sign.x) / max(g_push.sign.y * 0.62, 1e-3);
        carve = 1.0 - smoothstep(0.05, 0.10, glyphDist(g_push.aspect, gp));
        albedo *= 1.0 - 0.55 * carve;
        rough = saturate(rough + 0.1 * carve);
    }
    GBufferOut o;
    if (magicView()) {
        // The block glows with what it holds; the sign with the rune's own aspect, brighter the fuller, flickering
        // past the seal.
        float3 glow = heldMagicGlow(carrier1, g_push.magic.y, i.local.y * 0.7, 0.3 + 0.4 * carve);
        const float flick = g_push.magic.z > 0.5 ? 0.6 + 0.4 * sin(g_frame.fireInfo.x * 9.0 + float(g_push.seed & 15u)) : 1.0;
        const float full = saturate(g_push.magic.x);
        glow += aspectColor(g_push.aspect + 1) * (0.25 + 1.25 * full) * flick * (0.12 + 2.2 * carve) * K_MAGIC_SURFACE_GLOW;
        o.albedo = magicViewAlbedo(albedo, glow);
    } else {
        o.albedo = float4(albedo, 1);
    }
    o.normal = float4(n, rough);
    o.material = float4(0, spec, 0, packMagic(carrier1, g_push.magic.y, false));
    o.motion = 0;
    return o;
}
