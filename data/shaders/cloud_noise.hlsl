/*
Copyright(c) 2015-2025 Panos Karabelas

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

// ============================================================================
// 3D Noise Generation for Volumetric Clouds
// Based on techniques from:
// - Horizon Zero Dawn (Andrew Schneider, GDC 2015/2017)
// - GPU Pro 7: Real-Time Volumetric Cloudscapes
// - https://github.com/clayjohn/realtime_clouds
// ============================================================================

//= INCLUDES =========
#include "common.hlsl"
//====================

// Output texture: tex3d_uav (register u4) is defined in common_resources.hlsl
// We write to it for both shape and detail noise generation

// ============================================================================
// HASH FUNCTIONS (GPU-friendly pseudo-random)
// ============================================================================

float3 hash33(float3 p)
{
    p = float3(
        dot(p, float3(127.1, 311.7, 74.7)),
        dot(p, float3(269.5, 183.3, 246.1)),
        dot(p, float3(113.5, 271.9, 124.6))
    );
    return frac(sin(p) * 43758.5453123);
}

float hash31(float3 p)
{
    return frac(sin(dot(p, float3(127.1, 311.7, 74.7))) * 43758.5453123);
}

// ============================================================================
// WORLEY NOISE (Cellular/Voronoi noise)
// Creates billowy, cell-like structures perfect for clouds
// ============================================================================

float worley3d(float3 p, float frequency)
{
    p *= frequency;
    float3 id = floor(p);
    float3 fd = frac(p);
    
    float min_dist = 1.0;
    
    // Check 3x3x3 neighborhood
    [unroll]
    for (int z = -1; z <= 1; z++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            [unroll]
            for (int x = -1; x <= 1; x++)
            {
                float3 offset = float3(x, y, z);
                float3 neighbor = id + offset;
                
                // Random point within this cell
                float3 point_pos = hash33(neighbor);
                
                // Distance from fragment position to point
                float3 diff = offset + point_pos - fd;
                float dist = length(diff);
                
                min_dist = min(min_dist, dist);
            }
        }
    }
    
    return min_dist;
}

// Inverted Worley - high at cell centers (for cloud billows)
float worley3d_inv(float3 p, float frequency)
{
    return 1.0 - worley3d(p, frequency);
}

// Worley FBM - multiple octaves for detail
float worley_fbm(float3 p, float frequency)
{
    return worley3d_inv(p, frequency * 1.0) * 0.625 +
           worley3d_inv(p, frequency * 2.0) * 0.250 +
           worley3d_inv(p, frequency * 4.0) * 0.125;
}

// ============================================================================
// PERLIN NOISE (Gradient noise)
// Creates smooth, flowing structures
// ============================================================================

// 3D gradient function
float3 gradient3d(float3 p)
{
    float3 h = hash33(p);
    return normalize(h * 2.0 - 1.0);
}

float perlin3d(float3 p, float frequency)
{
    p *= frequency;
    float3 i = floor(p);
    float3 f = frac(p);
    
    // Smooth interpolation
    float3 u = f * f * (3.0 - 2.0 * f);
    
    // 8 corner gradients
    float n000 = dot(gradient3d(i + float3(0, 0, 0)), f - float3(0, 0, 0));
    float n100 = dot(gradient3d(i + float3(1, 0, 0)), f - float3(1, 0, 0));
    float n010 = dot(gradient3d(i + float3(0, 1, 0)), f - float3(0, 1, 0));
    float n110 = dot(gradient3d(i + float3(1, 1, 0)), f - float3(1, 1, 0));
    float n001 = dot(gradient3d(i + float3(0, 0, 1)), f - float3(0, 0, 1));
    float n101 = dot(gradient3d(i + float3(1, 0, 1)), f - float3(1, 0, 1));
    float n011 = dot(gradient3d(i + float3(0, 1, 1)), f - float3(0, 1, 1));
    float n111 = dot(gradient3d(i + float3(1, 1, 1)), f - float3(1, 1, 1));
    
    // Trilinear interpolation
    float nx00 = lerp(n000, n100, u.x);
    float nx10 = lerp(n010, n110, u.x);
    float nx01 = lerp(n001, n101, u.x);
    float nx11 = lerp(n011, n111, u.x);
    
    float nxy0 = lerp(nx00, nx10, u.y);
    float nxy1 = lerp(nx01, nx11, u.y);
    
    return lerp(nxy0, nxy1, u.z) * 0.5 + 0.5; // Remap to 0-1
}

// Perlin FBM
float perlin_fbm(float3 p, float frequency)
{
    return perlin3d(p, frequency * 1.0) * 0.5 +
           perlin3d(p, frequency * 2.0) * 0.25 +
           perlin3d(p, frequency * 4.0) * 0.125 +
           perlin3d(p, frequency * 8.0) * 0.0625;
}

// ============================================================================
// PERLIN-WORLEY HYBRID
// Key technique from Horizon Zero Dawn - combines smooth Perlin base with
// billowy Worley detail. The Worley erodes into the Perlin creating
// realistic cloud edges
// ============================================================================

// Remap helper
float remap_value(float value, float low1, float high1, float low2, float high2)
{
    return low2 + (value - low1) * (high2 - low2) / (high1 - low1);
}

float perlin_worley(float3 p, float frequency)
{
    float perlin = perlin_fbm(p, frequency);
    float worley = worley_fbm(p, frequency);
    
    // Remap Perlin-Worley: erode Perlin with Worley
    return saturate(remap_value(perlin, 0.0, 1.0, worley, 1.0));
}

// ============================================================================
// COMPUTE SHADER - SHAPE NOISE (128x128x128)
// R: Perlin-Worley (low freq base shape)
// G: Worley FBM (erosion octave 1)  
// B: Worley FBM (erosion octave 2)
// A: Worley FBM (erosion octave 3)
// ============================================================================

#ifdef SHAPE_NOISE
[numthreads(8, 8, 8)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint3 dims;
    tex3d_uav.GetDimensions(dims.x, dims.y, dims.z);
    
    if (any(thread_id >= dims))
        return;
    
    // Normalize to 0-1 with tiling
    float3 uvw = float3(thread_id) / float3(dims);
    
    // Base frequency (determines large-scale cloud size)
    float base_freq = 4.0;
    
    // Shape noise channels
    float perlin_worley_noise = perlin_worley(uvw, base_freq);
    float worley_fbm_0 = worley_fbm(uvw, base_freq * 1.0);
    float worley_fbm_1 = worley_fbm(uvw, base_freq * 2.0);
    float worley_fbm_2 = worley_fbm(uvw, base_freq * 4.0);
    
    tex3d_uav[thread_id] = float4(perlin_worley_noise, worley_fbm_0, worley_fbm_1, worley_fbm_2);
}
#endif

// ============================================================================
// COMPUTE SHADER - DETAIL NOISE (32x32x32)
// Higher frequency noise for cloud edge detail/erosion
// R: High freq Worley FBM
// G: Higher freq Worley FBM
// B: Highest freq Worley FBM
// A: Unused (could be curl noise for wispy effects)
// ============================================================================

#ifdef DETAIL_NOISE
[numthreads(8, 8, 8)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint3 dims;
    tex3d_uav.GetDimensions(dims.x, dims.y, dims.z);
    
    if (any(thread_id >= dims))
        return;
    
    float3 uvw = float3(thread_id) / float3(dims);
    
    // High frequency detail noise
    float base_freq = 8.0;
    
    float worley_0 = worley_fbm(uvw, base_freq * 1.0);
    float worley_1 = worley_fbm(uvw, base_freq * 2.0);
    float worley_2 = worley_fbm(uvw, base_freq * 4.0);
    
    tex3d_uav[thread_id] = float4(worley_0, worley_1, worley_2, 1.0);
}
#endif

