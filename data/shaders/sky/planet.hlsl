/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_PLANET
#define SPARTAN_PLANET

static const float planet_earth_radius      = 6360e3;
static const float planet_atmosphere_radius = 6460e3;
static const float3 planet_earth_center     = float3(0.0, -planet_earth_radius, 0.0);

// Shared by atmosphere and clouds so their extinction lookup cannot drift apart.
float2 planet_transmittance_uv(float radius, float cos_zenith)
{
    radius = clamp(radius, planet_earth_radius, planet_atmosphere_radius);
    float h = sqrt((radius - planet_earth_radius) / (planet_atmosphere_radius - planet_earth_radius));
    float rho = sqrt(max(0.0, (radius - planet_earth_radius) * (radius + planet_earth_radius)));
    float horizon = -rho / radius;
    // Square-root warp dedicates angular resolution to sunrise and sunset.
    float x = cos_zenith >= horizon
        ? 0.5 + 0.5 * sqrt(saturate((cos_zenith - horizon) / (1.0 - horizon)))
        : 0.5 - 0.5 * sqrt(saturate((horizon - cos_zenith) / (1.0 + horizon)));
    return float2(x, h);
}

float3 planet_transmittance(Texture2D lut, SamplerState samp, float radius, float cos_zenith)
{
    float2 uv = planet_transmittance_uv(radius, cos_zenith);
    if (uv.x < 0.5) return 0.0;
    float2 size;
    lut.GetDimensions(size.x, size.y);
    // Do not interpolate across the discontinuity into planet-occluded texels.
    uv.x = max(uv.x, 0.5 + 0.5 / size.x);
    return lut.SampleLevel(samp, uv, 0).rgb;
}

#endif
