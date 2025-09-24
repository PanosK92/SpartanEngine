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

// Inspired by Acerola's Implementation:
// https://github.com/GarrettGunnell/Water/blob/main/Assets/Shaders/FFTWater.compute

#include "common_ocean.hlsl"

struct VSOUT
{
    float4 pos : SV_Position;
    float2 uv : TEXCOORD;
};

// hash
float hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 78.233f);
    return frac(p.x * p.y);
}

// value noise
float noise2D(float2 p)
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

// fractal Brownian motion (multiple octaves)
float fbmNoise(float2 uv)
{
    float val = 0.0f;
    float amp = 0.5f;
    float freq = 1.0f;
    for (int i = 0; i < 5; i++)
    { // 5 octaves
        val += amp * noise2D(uv * freq);
        freq *= 2.0f;
        amp *= 0.5f;
    }
    return val;
}

// worley noise (optional bubble-like pattern)
float worley(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float minDist = 1.0f;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float2 neighbor = float2(x, y);
            float2 p = hash21(i + neighbor) * float2(1.0f, 1.0f);
            float2 diff = neighbor + p - f;
            float d = dot(diff, diff);
            minDist = min(minDist, d);
        }
    }
    return minDist;
}

float computeFoamNoise(float2 uv, float time)
{
    // high frequency UV, animate over time
    float2 noiseUV = uv * 32.0f + time * float2(0.1f, 0.05f);

    float fbm = fbmNoise(noiseUV);
    float bubbles = 1.0f - saturate(sqrt(worley(noiseUV * 2.0f)));

    // combine fbm + bubbles for frothy look
    return saturate(fbm * bubbles);
}

VSOUT main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    VSOUT vs_out;

    float2 map_flags = pass_get_f2_value();
    
    float3 displaced_pos = input.position.xyz + tex.SampleLevel(samplers[sampler_point_clamp], input.uv, 0).xyz * GetMaterial().ocean_parameters.displacementScale;
    float3 wpos = mul(float4(displaced_pos, 1.0f), buffer_pass.transform).xyz;
    if (any(map_flags) == 1.0f) // If debugging displacement/slope map, then dont apply displacement map
        wpos = mul(float4(input.position.xyz, 1.0f), buffer_pass.transform).xyz;
    vs_out.pos = mul(float4(wpos, 1.0f), buffer_frame.view_projection);
    vs_out.uv = input.uv;

    return vs_out;
}

float4 main_ps(VSOUT vertex) : SV_Target0
{
    const float4 foam_color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    const float foam_mask = tex2.Sample(samplers[sampler_trilinear_clamp], vertex.uv).a * 1.0f;
    
    const float foam_noise = computeFoamNoise(vertex.uv, buffer_frame.time);
    
    float2 map_flags = pass_get_f2_value();

    if (map_flags.x == 1.0f) // Displacement Map
        return float4(tex.Sample(samplers[sampler_point_clamp], vertex.uv).rgb, 1.0f);
    if (map_flags.y == 1.0f) // Slope Map
        return float4(tex2.Sample(samplers[sampler_trilinear_clamp], vertex.uv).rgb, 1.0f);
    
    return float4(foam_noise.xxxx) * foam_mask;
}
