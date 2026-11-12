/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_SKY_VIEW
#define SPARTAN_SKY_VIEW

//= includes ============
#include "../common.hlsl"
//=======================

// =====================================================================
// sky view lut (hillaire 2020)
// the per panorama pixel atmosphere integration is replaced by a small lut baked once per
// frame, the sky is azimuthally symmetric about the light's vertical plane so u only needs
// the [0, pi] azimuth difference, v is elevation with a square root warp that concentrates
// texels at the horizon where the gradient is steepest, the texture packs two half luts
// vertically, rows [0, h) hold the sun sky and rows [h, 2h) hold the moonlit night sky.
// the mapping lives here so the panorama bake and the clouds read the same sky
// =====================================================================

static const float2 sky_view_lut_size = float2(192.0, 108.0); // resolution of one half

// horizontal unit vector of a direction, falls back to +x when degenerate
float2 sky_view_horizontal(float3 dir)
{
    float2 h   = float2(dir.x, dir.z);
    float  len = length(h);
    return len > 1e-5 ? h / len : float2(1.0, 0.0);
}

// forward mapping used when sampling, unit uv in [0,1]^2 for the upper hemisphere
float2 sky_view_dir_to_unit(float3 view_dir, float3 light_dir)
{
    float elevation = asin(saturate(view_dir.y));
    float v         = sqrt(elevation / (PI * 0.5));
    float cos_az    = clamp(dot(sky_view_horizontal(view_dir), sky_view_horizontal(light_dir)), -1.0, 1.0);
    float u         = acos(cos_az) / PI;
    return float2(u, v);
}

// inverse mapping used by the bake kernel, texel centers span the full unit range
float3 sky_view_unit_to_dir(float2 unit, float3 light_dir)
{
    float elevation = unit.y * unit.y * (PI * 0.5);
    float azimuth   = unit.x * PI;
    float2 fwd      = sky_view_horizontal(light_dir);
    float2 side     = float2(-fwd.y, fwd.x);
    float2 dir_h    = fwd * cos(azimuth) + side * sin(azimuth);
    float  cos_el   = cos(elevation);
    return float3(dir_h.x * cos_el, sin(elevation), dir_h.y * cos_el);
}

// bilinear fetch from the packed lut at a unit coordinate, x is the azimuth away from the light
// over pi and y the warped elevation, half_index 0 samples the sun half, 1 the moon half
float3 sample_sky_view_lut_unit(Texture2D lut, SamplerState samp, float2 unit, float half_index)
{
    float2 uv;
    uv.x = (unit.x * (sky_view_lut_size.x - 1.0) + 0.5) / sky_view_lut_size.x;
    uv.y = (unit.y * (sky_view_lut_size.y - 1.0) + 0.5) / (sky_view_lut_size.y * 2.0) + half_index * 0.5;
    return lut.SampleLevel(samp, uv, 0).rgb;
}

// bilinear fetch from the packed lut along a direction
float3 sample_sky_view_lut(Texture2D lut, SamplerState samp, float3 view_dir, float3 light_dir, float half_index)
{
    return sample_sky_view_lut_unit(lut, samp, sky_view_dir_to_unit(view_dir, light_dir), half_index);
}

// weight of the night terms, the sun half of the lut needs none since the earth shadow in the
// transmittance lut fades it on its own
float sky_view_night_factor(float sun_elevation)
{
    return 1.0 - 1.0 / (1.0 + exp(-sun_elevation * 20.0));
}

// night atmosphere, uses the shared night radiometric levels from common.hlsl
float3 night_compute_atmosphere(float3 view_dir, float3 moon_dir, Texture2D sky_view_lut, SamplerState samp)
{
    float3 base = night_sky_radiance(view_dir.y);
    
    float dy = view_dir.y - 0.04;
    float airglow_band = exp(-(dy * dy) / 0.0050);
    float3 airglow = night_airglow_rad * airglow_band;
    
    float moon_elev = moon_dir.y;
    float3 moon_scatter = float3(0, 0, 0);
    if (moon_elev > -0.10)
    {
        float3 lum  = sample_sky_view_lut(sky_view_lut, samp, view_dir, moon_dir, 1.0);
        float fade  = smoothstep(-0.05, 0.10, moon_elev);
        moon_scatter = lum * night_moon_to_sun * fade * buffer_frame.celestial_moon.w;
    }
    
    return base + airglow + moon_scatter;
}

// radiance of the atmosphere alone along a direction, the sky without the sun disc, the moon
// and the stars, directions below the horizon read the horizon row
float3 sky_view_radiance(Texture2D sky_view_lut, SamplerState samp, float3 view_dir, float3 sun_dir)
{
    view_dir.y      = max(view_dir.y, 0.0);
    float3 radiance = sample_sky_view_lut(sky_view_lut, samp, view_dir, sun_dir, 0.0);
    float night     = sky_view_night_factor(sun_dir.y);
    if (night > 0.001)
    {
        radiance += night_compute_atmosphere(view_dir, buffer_frame.celestial_moon.xyz, sky_view_lut, samp) * night;
    }
    return radiance;
}

#endif // SPARTAN_SKY_VIEW
