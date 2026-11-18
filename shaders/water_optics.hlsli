// Light in pond water, shared by the water surface (seen from above or from below) and the lighting pass (things seen
// from under the surface): the water takes red first and glows a little with the daylight it scatters, dimmer deeper.
#ifndef TAELIA_WATER_OPTICS_HLSLI
#define TAELIA_WATER_OPTICS_HLSLI

#include "common.hlsli"
#include "atmosphere.hlsli"

bool cameraUnderwater() { return g_frame.underwater.x > 0.0; }
float waterSurfaceAbove() { return g_frame.underwater.y; }  // how far the pond's surface is above the eye (m)

// The daylight falling on the water: the sky, and a quarter of the suns (the rest goes on down as their beams).
float3 waterDaylight() {
    float3 suns = 0;
    [unroll] for (uint s = 0; s < 2; ++s)
        suns += g_frame.sunLight[s].rgb * (1.0 - g_frame.suns[s].w) * sunTransmittance(g_frame.suns[s].xyz) * saturate(g_frame.suns[s].y);
    return skyIrradiance(float3(0, 1, 0)) + suns * 0.25;
}

// The water's own glow toward the eye at a depth under the surface.
float3 waterGlow(float depthBelow) {
    return K_WATER_COLOR * waterDaylight() * exp(-K_WATER_EXTINCTION * max(depthBelow, 0.0));
}

// What is seen through `path` metres of water at about this depth under the surface.
float3 throughWater(float3 color, float path, float depthBelow) {
    const float3 T = exp(-K_WATER_EXTINCTION * max(path, 0.0));
    return color * T + waterGlow(depthBelow) * (1.0 - T);
}

// Daylight left at a depth under the surface (things down there are lit by what the water above lets through).
float3 waterDepthLight(float depthBelow) { return exp(-K_WATER_EXTINCTION * max(depthBelow, 0.0)); }

#endif
