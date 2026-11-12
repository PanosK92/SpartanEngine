/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_CLOUD_SHADOW
#define SPARTAN_CLOUD_SHADOW

//= includes ============
#include "../common.hlsl"
//=======================

// the sampling half of the cloud shadow map, kept apart from clouds.hlsl so the lighting, fog
// and ray hit shaders that only read the map do not compile the cloud march or recompile with it

// altitude of the cumulus base, the plane the shadow map is indexed on
static const float cumulus_bottom_alt = 1500.0;

// cloud shadow map, cumulus transmittance toward the sun stored as a 2d map indexed by where
// the sun ray crosses the cloud base plane, baked by the CLOUD_SHADOW kernel in clouds.hlsl
// and sampled by the volumetric fog march so the sun shafts thread through the cloud gaps
// 8 km half extent over a 1024 map gives ~16 m texels, anything visible on the ground sits
// well inside that range and the border fade hands the rest of the world back to unshadowed
static const float cloud_shadow_half_extent = 8000.0;
static const float cloud_shadow_min_sun_y   = 0.087; // ~5 degrees, keeps the projection bounded at grazing sun

// projects a world position along the sun onto the cloud base plane, the crossing point is
// the shadow map's index so every altitude below the clouds gets correct shadow parallax.
// the map is centered on the camera's own crossing so nearby samples land mid map, both the
// bake and every consumer derive the mapping from camera and sun alone, no cpu plumbing
float2 cloud_shadow_plane_uv(float3 pos, float3 sun_dir, float3 cam_pos, float texel_size)
{
    float  sun_y    = max(sun_dir.y, cloud_shadow_min_sun_y);
    float2 slope    = sun_dir.xz / sun_y;
    float2 crossing = pos.xz     + slope * (cumulus_bottom_alt - pos.y);
    float2 anchor   = cam_pos.xz + slope * (cumulus_bottom_alt - cam_pos.y);
    anchor           = floor(anchor / texel_size + 0.5) * texel_size;
    return (crossing - anchor) / (2.0 * cloud_shadow_half_extent) + 0.5;
}

// transmittance toward the sun at a world position, fades to unshadowed at the map border so
// the clamped edge texel does not smear one shadow value across the world beyond the extent.
// the map is soft blurred after bake and carries a mip chain, so a single trilinear fetch is
// enough, distance based lod kills the screen space moire that sharp texel edges produced when
// the projected map was minified on the ground
float cloud_shadow_sample(Texture2D shadow_map, SamplerState samp, float3 pos, float3 sun_dir, float3 cam_pos)
{
    uint shadow_width;
    uint shadow_height;
    shadow_map.GetDimensions(shadow_width, shadow_height);
    float texel_size = 2.0 * cloud_shadow_half_extent / float(shadow_width);
    float2 uv     = cloud_shadow_plane_uv(pos, sun_dir, cam_pos, texel_size);
    float2 border = min(uv, 1.0 - uv);
    float  inside = smoothstep(0.0, 0.04, min(border.x, border.y));
    
    float lod   = log2(max(length(pos.xz - cam_pos.xz) * 0.0015, 1.0));
    float trans = shadow_map.SampleLevel(samp, uv, lod).r;
    return lerp(1.0, trans, inside);
}

#endif // SPARTAN_CLOUD_SHADOW
