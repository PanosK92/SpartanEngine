/*
Copyright(c) 2025 George Bolba

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

#include "../common.hlsl"

#ifndef SPARTAN_COMMON_OCEAN
#define SPARTAN_COMMON_OCEAN

static const uint SPECTRUM_TEX_SIZE = 512;

RWTexture2D<float4> initial_spectrum         : register(u9);
RWTexture2D<float4> displacement_spectrum    : register(u10);
RWTexture2D<float4> slope_spectrum           : register(u11);
RWTexture2D<float4> displacement_map         : register(u12);
RWTexture2D<float4> slope_map                : register(u13);
RWTexture2D<float4> synthesised_displacement : register(u14);
RWTexture2D<float4> synthesised_slope        : register(u15);

float Dispersion(float kMag, float depth)
{
    return sqrt(G * kMag * tanh(min(kMag * depth, 20)));
}

float hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 78.233f);
    return frac(p.x * p.y);
}

float noise_2d(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float a = hash21(i);
    float b = hash21(i + float2(1.0f, 0.0f));
    float c = hash21(i + float2(0.0f, 1.0f));
    float d = hash21(i + float2(1.0f, 1.0f));
    float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float fbm_noise(float2 uv)
{
    float val = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    for (int i = 0; i < 5; i++)
    {
        val += amp * noise_2d(uv * freq);
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return val;
}

float worley(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float min_dist = 1.0f;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 neighbor = float2(x, y);
            float2 p = hash21(i + neighbor) * float2(1.0f, 1.0f);
            float2 diff = neighbor + p - f;
            float d = dot(diff, diff);
            min_dist = min(min_dist, d);
        }
    }
    return min_dist;
}

float compute_foam_noise(float2 uv, float time)
{
    float2 noise_uv = uv * 32.0f + time * float2(0.1f, 0.05f);

    float fbm = fbm_noise(noise_uv);
    float bubbles = 1.0f - saturate(sqrt(worley(noise_uv * 2.0f)));
    
    return saturate(fbm * bubbles);
}

#endif // SPARTAN_COMMON_OCEAN
