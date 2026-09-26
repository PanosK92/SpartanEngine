// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#ifndef COMMON_PUDDLES_H
#define COMMON_PUDDLES_H

#include "common_road.hlsl"

// standing water, lagarde 2013 (water drop 2b), the ground is treated as a height field and a
// single water level rises through it with puddliness, so pools appear in the lowest spots first,
// grow and merge, instead of fading in everywhere at once
// the field is world space and independent of meshes, uvs and road segments, so a pool that
// straddles terrain, asphalt and paint keeps one shoreline across all three

// water level at puddliness 0 and 1, the basin field is roughly normal around 0.5 with a spread
// of about 0.12, so this runs from bone dry to a bit under half of all flat ground submerged
static const float puddle_level_dry      = 0.25f;
static const float puddle_level_flooded  = 0.49f;
// height units, how far below the level the water turns from a film into a full mirror
static const float puddle_shore_width    = 0.02f;
// height units, the glossy film either side of the shoreline where water wets but does not yet cover the detail
static const float puddle_film_width     = 0.03f;
// height units, the damp band above the shoreline where capillary water darkens the ground
static const float puddle_damp_width     = 0.09f;
// the whole surface is damp once water is standing on it, this is the share at puddliness 1
static const float puddle_damp_global    = 0.3f;
static const float puddle_roughness      = 0.03f;

// low frequency ground undulation, 8 m dips, 2.5 m dimples and sub metre ruts
float puddle_basin(float2 xz)
{
    return road_noise(xz * 0.12f) * 0.5f +
           road_noise(xz * 0.4f + float2(13.7f, 5.3f)) * 0.3f +
           road_noise(xz * 1.1f + float2(41.3f, 27.1f)) * 0.2f;
}

// porosity: 0 sealed (paint), 1 thirsty soil, decides how dark the ground gets when soaked
// relief_bias: extra depth where the surface itself is low, concave terrain, cracks, crevices
// footprint: metres per pixel, rolls sub pixel shoreline noise and ripples off before they alias
// puddliness: the level here, rain only fills what is under open sky
// returns how submerged the pixel is
float puddle_apply(
    float      puddliness,
    float3     position_world,
    float3     geometric_normal,
    float      porosity,
    float      relief_bias,
    float      footprint,
    inout float3 albedo,
    inout float3 normal,
    inout float  roughness,
    inout float  metalness,
    inout float  occlusion
)
{
    if (puddliness <= 0.0f)
        return 0.0f;

    // water only stands on flat ground, a road camber of a few percent still holds it
    float flatness = smoothstep(0.93f, 0.985f, geometric_normal.y);
    if (flatness <= 0.0f)
        return 0.0f;

    float2 xz = position_world.xz;

    // fine breakup keeps shorelines ragged rather than contour line smooth
    float resolved = 1.0f - saturate(footprint * 6.0f - 0.5f);
    float ragged   = lerp(0.5f, road_noise(xz * 4.3f + float2(7.1f, 3.9f)), resolved) * 0.05f;

    float height = puddle_basin(xz) + ragged - relief_bias;
    float level  = lerp(puddle_level_dry, puddle_level_flooded, puddliness);
    float depth  = (level - height) * flatness - (1.0f - flatness) * puddle_damp_width;

    // a pixel wide shore at a distance keeps the edge from crawling under taa
    float shore = puddle_shore_width + footprint * 0.02f;
    float fade  = smoothstep(0.0f, 0.15f, puddliness);
    float water = smoothstep(0.0f, shore, depth) * fade;
    float film  = smoothstep(-puddle_film_width, puddle_film_width, depth) * fade;
    float damp  = max(smoothstep(-puddle_damp_width, 0.0f, depth) * fade, puddle_damp_global * puddliness * flatness);
    damp        = saturate(max(damp, film));

    // soaked ground, pores fill so the diffuse darkens and the specular tightens
    albedo   *= lerp(1.0f, lerp(0.85f, 0.35f, saturate(porosity)), damp);
    roughness = lerp(roughness, min(roughness, lerp(0.35f, 0.18f, saturate(porosity))), damp);

    // the film glazes the ground ahead of the shoreline, so a pool bleeds into its surroundings
    roughness = lerp(roughness, min(roughness, 0.1f), film);

    // under the water, deeper reads darker as the column absorbs, and the detail normal drowns
    float deep  = saturate(depth / 0.08f);
    albedo     *= lerp(1.0f, lerp(0.9f, 0.7f, deep), water);
    roughness   = lerp(roughness, puddle_roughness, water);
    metalness   = lerp(metalness, 0.0f, water);
    occlusion   = lerp(occlusion, 1.0f, water * 0.5f);

    // a light wind keeps the surface from being a perfect mirror, a faint travelling chop
    float  wind_speed = length(buffer_frame.wind.xz);
    float2 wind_dir   = wind_speed > 0.001f ? buffer_frame.wind.xz / wind_speed : float2(1.0f, 0.0f);
    float  chop       = 0.02f * saturate(wind_speed / 8.0f) * resolved;
    float3 water_normal = geometric_normal;
    if (chop > 0.0f)
    {
        float2 p   = xz * 2.7f - wind_dir * (float)buffer_frame.time * 0.35f;
        float  n   = road_noise(p);
        float  nx  = road_noise(p + float2(0.15f, 0.0f));
        float  nz  = road_noise(p + float2(0.0f, 0.15f));
        water_normal = normalize(geometric_normal + float3(n - nx, 0.0f, n - nz) * (chop / 0.15f));
    }
    normal = normalize(lerp(normal, water_normal, water));
    return water;
}

#endif
