/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
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
