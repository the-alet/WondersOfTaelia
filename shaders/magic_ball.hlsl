// Magic balls (the user): condensed magic of one aspect between the player's hands or in flight. A camera-facing quad
// round each; the pixel's view ray is traced through the sphere: how much of it the ray crosses (thick in the middle,
// thin at the rim) times a swirl that moves inside, in the aspect's colour, added onto the lit scene. Fuller balls
// glow brighter; the magic view shows them brighter still. Each aspect has its own look.
#include "common.hlsli"
#include "magic.hlsli"
#include "sky.hlsli"

struct BallPush {
    uint pad0, instSrv, instOffset, pad1;
};
[[vk::push_constant]] ConstantBuffer<BallPush> g_push : register(b1, space0);

struct VSOut {
    float4 pos : SV_Position;
    float3 rel : TEXCOORD0;  // this point of the quad, relative to the camera
    nointerpolation float4 ball : TEXCOORD1;  // centre (relative to the camera), radius
    nointerpolation float4 info : TEXCOORD2;  // aspect, fill, seed, instability (<0 physical condensate)
    float2 corner : TEXCOORD3;
    nointerpolation float3 normal : TEXCOORD4;
};

VSOut VSMain(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    ByteAddressBuffer inst = g_buffers[g_push.instSrv];
    const uint base = g_push.instOffset + iid * 48;
    const float4 a = asfloat(inst.Load4(base)), b = asfloat(inst.Load4(base + 16));
    const float3 normal = asfloat(inst.Load3(base+32));
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    const float2 c = corners[vid];
    const bool impact = b.w >= 2.0;
    const float age = saturate(b.y / K_MAGIC_IMPACT_SECONDS);
    const float size = impact ? a.w * (1.5 + 5.0*age) : a.w * 2.4;
    // The quad stands in front of the ball (toward the eye), so nothing just behind its centre hides it.
    const float3 toEye = -normalize(a.xyz);
    const float3 centre = impact ? a.xyz + normal*age*a.w*1.4 : a.xyz + toEye * min(a.w, length(a.xyz) * 0.5);
    VSOut o;
    o.rel = centre + (g_frame.cameraRight.xyz * c.x + g_frame.cameraUp.xyz * c.y) * size;
    o.pos = mul(g_frame.viewProj, float4(o.rel, 1));
    o.ball = a;
    o.info = b;
    o.corner = c;
    o.normal = normal;
    return o;
}

float3 ballColor(uint aspect) {
    if (aspect == 7) return K_MAGIC_COLOR_NEUTRAL;
    if (aspect == 8) return K_MAGIC_COLOR_NECROTIC;
    return aspectColor(aspect + 1);
}

#include "magic_impact.hlsli"

float4 PSMain(VSOut i) : SV_Target0 {
    if (i.info.w >= 2.0) return impactColor(i.corner,i.info,i.normal);
    const float3 dir = normalize(i.rel);
    const float3 c = i.ball.xyz;
    const float unstable = max(i.info.w, 0.0);
    const float r = i.ball.w * (1.0 + unstable * 0.10 * sin(g_frame.fireInfo.x * 37.0 + i.info.z));
    const uint aspect = uint(i.info.x + 0.5);
    const float fill = i.info.y, seed = i.info.z;
    const float t = g_frame.fireInfo.x;
    // The ray through the sphere: closest approach, how much of it lies inside.
    const float along = dot(c, dir);
    const float3 closest = dir * along - c;
    const float d2 = dot(closest, closest), r2 = r * r;
    const float inside = d2 < r2 ? 2.0 * sqrt(r2 - d2) / (2.0 * r) : 0.0;  // 0 at the rim .. 1 through the middle
    const float rim = sqrt(d2) / r;                                         // 1 at the edge
    if (i.info.w < 0.0) {
        clip(r2 - d2);
        const float3 hit = dir * (along - sqrt(max(r2-d2,0.0)));
        const float3 n = normalize(hit-c);
        const float3 env = skyColor(reflect(dir,n));
        const float fresnel = 0.02 + 0.98 * pow(1.0-saturate(dot(n,-dir)),5.0);
        float3 color;
        float alpha = 1;
        if (aspect == 1) {
            color = lerp(float3(0.04,0.12,0.15),env,fresnel);
            alpha = 0.38+0.6*fresnel;
        } else if (aspect == 0) color = float3(0.17,0.13,0.09) * skyAmbient(n);
        else if (aspect == 4) { color = env*0.6; alpha = 0.12 * inside; }
        else { color = K_MAGIC_COLOR_NECROTIC*skyAmbient(n); alpha = 0.35*inside; }
        [unroll] for(uint sun=0;sun<2;++sun) {
            const float3 h = normalize(g_frame.suns[sun].xyz-dir);
            color += g_frame.sunLight[sun].rgb*pow(saturate(dot(n,h)),aspect==1?180.0:12.0)*0.25;
        }
        if(magicView()) color = aspectColor(aspect+1)*K_MAGIC_SURFACE_GLOW;
        return float4(color,alpha);
    }
    const float halo = exp(-max(rim - 1.0, 0.0) * 12.0) * (1.0 - smoothstep(1.0, 1.45, rim));
    // A swirl moving inside: the noise sampled where the ray is nearest the centre, turning with time.
    const float3 q = (closest / r) * 2.2;
    const float spin = t * (aspect == 3 ? 2.6 : (aspect == 4 ? 3.2 : 1.4));
    const float cs = cos(spin), sn = sin(spin);
    const float3 qr = float3(q.x * cs - q.z * sn, q.y + t * 0.3, q.x * sn + q.z * cs) + seed;
    const float n1 = latticeNoise3(qr * 1.7), n2 = latticeNoise3(qr * 3.9 + 7.0);
    float swirl = 0.12 + 1.6 * smoothstep(0.30, 0.70, n1 * 0.7 + n2 * 0.3);
    float3 col = ballColor(aspect);
    float glow = 2.2;
    if (aspect == 3) {  // fire: a hot flickering core
        col = lerp(float3(1.0, 0.35, 0.05), float3(1.0, 0.85, 0.45), saturate(inside * 1.2 * swirl));
        glow = 4.0 * (0.8 + 0.2 * sin(t * 23.0 + seed * 10.0));
    } else if (aspect == 5) {  // lightning: crackling threads
        swirl = 0.35 + 2.5 * pow(saturate(1.0 - abs(n1 * 2.0 - 1.0) * 6.0), 3.0) * (0.6 + 0.4 * sin(t * 40.0 + seed));
        glow = 3.0;
    } else if (aspect == 6) {  // light: blazing white
        glow = 5.0;
    } else if (aspect == 4) {  // air: faint streaks
        swirl = 0.3 + 0.9 * pow(saturate(n2), 2.0);
        glow = 1.4;
    } else if (aspect == 0) {  // earth: smouldering dust
        glow = 1.2;
    } else if (aspect == 8) {  // necrotic: grey-violet smoke
        swirl = 0.4 + 0.6 * n1;
        glow = 1.1;
    }
    float transmittance = 1.0;
    float radiance = 0;
    // Integrate through the sphere: overlapping moving filaments keep its shape readable instead of a flat noise spot.
    [unroll] for (int step = 0; step < 8; ++step) {
        const float alongSphere = ((step + 0.5) / 8.0 * 2.0 - 1.0) * inside;
        const float3 samplePoint = closest / r + dir * alongSphere;
        const float3 flow = float3(samplePoint.x * cs - samplePoint.z * sn, samplePoint.y - t * 0.18,
                                  samplePoint.x * sn + samplePoint.z * cs);
        const float noise = latticeNoise3(flow * 5.0 + seed);
        const float strands = smoothstep(0.36, 0.66, noise);
        const float absorption = 1.0 - exp(-(0.75 + 3.5 * strands) * inside * 0.25);
        radiance += transmittance * absorption * (0.18 + 0.55 * strands + 0.15 * swirl);
        transmittance *= 1.0 - absorption;
    }
    float3 light = col * (radiance + halo * 0.025) * glow * (0.35 + 0.65 * fill);
    light *= 1.0 + unstable * (0.45 * sin(t*45.0+seed) + 1.3 * pow(saturate(n2),9.0));
    if (magicView()) light *= 2.5;  // magic glows by its own light
    // Dense swirls attenuate the scene as well as emitting: the ball retains a readable volume at arm's length.
    const float opacity = (1.0 - transmittance) * (aspect == 6 ? 0.2 : 0.85);
    return float4(light, opacity);
}

// ---------------------------------------------------------------- dust: a thing of pure life magic crumbled

struct DustOut {
    float4 pos : SV_Position;
    float2 corner : TEXCOORD0;
    nointerpolation float3 info : TEXCOORD1;  // age (0..1 of its life), seed, radius
};

DustOut DustVS(uint vid : SV_VertexID, uint iid : SV_InstanceID) {
    ByteAddressBuffer inst = g_buffers[g_push.instSrv];
    const uint base = g_push.instOffset + iid * 48;
    const float4 a = asfloat(inst.Load4(base)), b = asfloat(inst.Load4(base + 16));
    const float age = saturate(b.x / K_MAGIC_DUST_SECONDS);
    const float2 corners[6] = {float2(-1, -1), float2(1, -1), float2(1, 1), float2(-1, -1), float2(1, 1), float2(-1, 1)};
    // It spreads and settles as it thins.
    const float size = a.w * (1.6 + 4.0 * sqrt(age));
    const float3 centre = a.xyz + float3(0, -0.3 * age * a.w * 4.0, 0);
    DustOut o;
    o.corner = corners[vid];
    o.pos = mul(g_frame.viewProj, float4(centre + (g_frame.cameraRight.xyz * o.corner.x + g_frame.cameraUp.xyz * o.corner.y) * size, 1));
    o.info = float3(age, b.z, a.w);
    return o;
}

float4 DustPS(DustOut i) : SV_Target0 {
    const float r2 = dot(i.corner, i.corner);
    if (r2 > 1.0) discard;
    const float age = i.info.x;
    const float3 q = float3(i.corner * 2.5, i.info.y + age * 1.5);
    const float n = latticeNoise3(q * 1.7) * 0.65 + latticeNoise3(q * 4.1 + 3.0) * 0.35;
    const float cover = saturate((1.0 - r2) * 1.4) * smoothstep(0.25, 0.75, n) * pow(1.0 - age, 1.5) * 0.85;
    // Dry grey-brown, lit by the sky and the suns.
    float3 light = skyAmbient(float3(0, 1, 0));
    [unroll] for (uint s = 0; s < 2; ++s) light += g_frame.sunLight[s].rgb * (1.0 - g_frame.suns[s].w) * sunTransmittance(g_frame.suns[s].xyz) * saturate(g_frame.suns[s].y) * 0.6;
    float3 col = float3(0.42, 0.38, 0.33) * light;
    if (magicView()) col = magicDim(col) + K_MAGIC_COLOR_NECROTIC * 0.08;  // what it held is gone
    return float4(col, cover);
}
