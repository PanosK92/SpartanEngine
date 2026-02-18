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

// 3D noise generation for volumetric clouds (Horizon Zero Dawn / GPU Pro 7 techniques)

#include "../common.hlsl"

// hash functions
float3 hash33(float3 p)
{
    p = float3(dot(p, float3(127.1, 311.7, 74.7)),
               dot(p, float3(269.5, 183.3, 246.1)),
               dot(p, float3(113.5, 271.9, 124.6)));
    return frac(sin(p) * 43758.5453123);
}

// worley noise - creates billowy cell structures for clouds
float worley3d(float3 p, float freq)
{
    p *= freq;
    float3 id = floor(p);
    float3 fd = frac(p);
    float min_dist = 1.0;

    [unroll] for (int z = -1; z <= 1; z++)
    [unroll] for (int y = -1; y <= 1; y++)
    [unroll] for (int x = -1; x <= 1; x++)
    {
        float3 offset = float3(x, y, z);
        float3 cell_point = hash33(id + offset);
        min_dist = min(min_dist, length(offset + cell_point - fd));
    }
    return min_dist;
}

float worley_fbm(float3 p, float freq)
{
    return (1.0 - worley3d(p, freq))       * 0.625 +
           (1.0 - worley3d(p, freq * 2.0)) * 0.250 +
           (1.0 - worley3d(p, freq * 4.0)) * 0.125;
}

// perlin noise - creates smooth flowing structures
float perlin3d(float3 p, float freq)
{
    p *= freq;
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0 - 2.0 * f);

    // 8 corner gradients
    float n[8];
    [unroll] for (int c = 0; c < 8; c++)
    {
        float3 corner = float3(c & 1, (c >> 1) & 1, (c >> 2) & 1);
        float3 grad = normalize(hash33(i + corner) * 2.0 - 1.0);
        n[c] = dot(grad, f - corner);
    }

    // trilinear interpolation
    float nx0 = lerp(lerp(n[0], n[1], u.x), lerp(n[2], n[3], u.x), u.y);
    float nx1 = lerp(lerp(n[4], n[5], u.x), lerp(n[6], n[7], u.x), u.y);
    return lerp(nx0, nx1, u.z) * 0.5 + 0.5;
}

float perlin_fbm(float3 p, float freq)
{
    return perlin3d(p, freq)       * 0.5000 +
           perlin3d(p, freq * 2.0) * 0.2500 +
           perlin3d(p, freq * 4.0) * 0.1250 +
           perlin3d(p, freq * 8.0) * 0.0625;
}

// perlin-worley hybrid - worley erodes into perlin for realistic cloud edges
float perlin_worley(float3 p, float freq)
{
    float perlin = perlin_fbm(p, freq);
    float worley = worley_fbm(p, freq);
    return saturate(worley + (perlin - worley) * perlin); // remap: perlin from [0,1] to [worley,1]
}

// shape noise (128^3): R=perlin-worley base, GBA=worley octaves for erosion
#ifdef SHAPE_NOISE
[numthreads(8, 8, 8)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint3 dims;
    tex3d_uav.GetDimensions(dims.x, dims.y, dims.z);
    if (any(tid >= dims)) return;

    float3 uvw = float3(tid) / float3(dims);
    static const float freq = 4.0;

    tex3d_uav[tid] = float4(perlin_worley(uvw, freq),
                            worley_fbm(uvw, freq),
                            worley_fbm(uvw, freq * 2.0),
                            worley_fbm(uvw, freq * 4.0));
}
#endif

// detail noise (32^3): high-freq worley for edge erosion
#ifdef DETAIL_NOISE
[numthreads(8, 8, 8)]
void main_cs(uint3 tid : SV_DispatchThreadID)
{
    uint3 dims;
    tex3d_uav.GetDimensions(dims.x, dims.y, dims.z);
    if (any(tid >= dims)) return;

    float3 uvw = float3(tid) / float3(dims);
    static const float freq = 8.0;

    tex3d_uav[tid] = float4(worley_fbm(uvw, freq),
                            worley_fbm(uvw, freq * 2.0),
                            worley_fbm(uvw, freq * 4.0),
                            1.0);
}
#endif
