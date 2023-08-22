/*
Copyright(c) 2016-2022 Panos Karabelas
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

//= INCLUDES =========
#include "common.hlsl"
//====================

/*------------------------------------------------------------------------------
                           NEIGHBOURHOOD OFFSETS
------------------------------------------------------------------------------*/

static const int2 kOffsets3x3[9] =
{
    int2(-1, -1),
    int2(0, -1),
    int2(1, -1),
    int2(-1, 0),
    int2(0, 0),
    int2(1, 0),
    int2(-1, 1),
    int2(0, 1),
    int2(1, 1),
};

/*------------------------------------------------------------------------------
                        THREAD GROUP SHARED MEMORY (LDS)
------------------------------------------------------------------------------*/

static const int kBorderSize     = 1;
static const int kGroupSize      = THREAD_GROUP_COUNT_X;
static const int kTileDimension  = kGroupSize + kBorderSize * 2;
static const int kTileDimension2 = kTileDimension * kTileDimension;

groupshared float3 tile_color[kTileDimension][kTileDimension];
groupshared float  tile_depth[kTileDimension][kTileDimension];

float3 load_color(uint2 group_thread_id)
{
    group_thread_id += kBorderSize;
    return tile_color[group_thread_id.x][group_thread_id.y];
}

void store_color(uint2 group_thread_id, float3 color)
{
    tile_color[group_thread_id.x][group_thread_id.y] = color;
}

float load_depth(uint2 group_thread_id)
{
    group_thread_id += kBorderSize;
    return tile_depth[group_thread_id.x][group_thread_id.y];
}

void store_depth(uint2 group_thread_id, float depth)
{
    tile_depth[group_thread_id.x][group_thread_id.y] = depth;
}

void store_color_depth(uint2 group_thread_id, uint2 thread_id)
{
    // out of bounds clamp
    thread_id = clamp(thread_id, uint2(0, 0), uint2(g_resolution_render) - uint2(1, 1));

    store_color(group_thread_id, tex2[thread_id].rgb);
    store_depth(group_thread_id, get_linear_depth(thread_id));
}

void populate_group_shared_memory(uint2 group_id, uint group_index)
{
    int2 group_top_left = group_id.xy * kGroupSize - kBorderSize;
    if (group_index < (kTileDimension2 >> 2))
    {
        int2 group_thread_id_1 = int2(group_index                             % kTileDimension, group_index                             / kTileDimension);
        int2 group_thread_id_2 = int2((group_index + (kTileDimension2 >> 2))  % kTileDimension, (group_index + (kTileDimension2 >> 2))  / kTileDimension);
        int2 group_thread_id_3 = int2((group_index + (kTileDimension2 >> 1))  % kTileDimension, (group_index + (kTileDimension2 >> 1))  / kTileDimension);
        int2 group_thread_id_4 = int2((group_index + kTileDimension2 * 3 / 4) % kTileDimension, (group_index + kTileDimension2 * 3 / 4) / kTileDimension);

        store_color_depth(group_thread_id_1, group_top_left + group_thread_id_1);
        store_color_depth(group_thread_id_2, group_top_left + group_thread_id_2);
        store_color_depth(group_thread_id_3, group_top_left + group_thread_id_3);
        store_color_depth(group_thread_id_4, group_top_left + group_thread_id_4);
    }

    // Wait for group threads to load store data.
    GroupMemoryBarrierWithGroupSync();
}

/*------------------------------------------------------------------------------
                                VELOCITY
------------------------------------------------------------------------------*/

void depth_test_min(uint2 pos, inout float min_depth, inout uint2 min_pos)
{
    float depth = load_depth(pos);

    if (depth < min_depth)
    {
        min_depth = depth;
        min_pos   = pos;
    }
}

// Returns velocity with closest depth (3x3 neighborhood)
void get_closest_pixel_velocity_3x3(in uint2 group_pos, uint2 group_top_left, out float2 velocity)
{
    float min_depth = 1.0f;
    uint2 min_pos   = group_pos;

    depth_test_min(group_pos + kOffsets3x3[0], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[1], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[2], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[3], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[4], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[5], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[6], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[7], min_depth, min_pos);
    depth_test_min(group_pos + kOffsets3x3[8], min_depth, min_pos);

    // Velocity out
    velocity = tex_velocity[group_top_left + min_pos].xy;
}

/*------------------------------------------------------------------------------
                              HISTORY SAMPLING
------------------------------------------------------------------------------*/

float3 sample_catmull_rom_9(Texture2D stex, float2 uv, float2 resolution)
{
    // Source: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
    // License: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae

    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 sample_pos = uv * resolution;
    float2 texPos1    = floor(sample_pos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = sample_pos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0 = texPos1 - 1.0f;
    float2 texPos3 = texPos1 + 2.0f;
    float2 texPos12 = texPos1 + offset12;

    texPos0  /= resolution;
    texPos3  /= resolution;
    texPos12 /= resolution;

    float3 result = float3(0.0f, 0.0f, 0.0f);

    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos0.x, texPos0.y), 0.0f).xyz * w0.x * w0.y;
    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos12.x, texPos0.y), 0.0f).xyz * w12.x * w0.y;
    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos3.x, texPos0.y), 0.0f).xyz * w3.x * w0.y;

    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos0.x, texPos12.y), 0.0f).xyz * w0.x * w12.y;
    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos12.x, texPos12.y), 0.0f).xyz * w12.x * w12.y;
    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos3.x, texPos12.y), 0.0f).xyz * w3.x * w12.y;

    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos0.x, texPos3.y), 0.0f).xyz * w0.x * w3.y;
    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos12.x, texPos3.y), 0.0f).xyz * w12.x * w3.y;
    result += stex.SampleLevel(sampler_bilinear_clamp, float2(texPos3.x, texPos3.y), 0.0f).xyz * w3.x * w3.y;

    return max(result, 0.0f);
}

/*------------------------------------------------------------------------------
                              HISTORY CLIPPING
------------------------------------------------------------------------------*/

// Based on "Temporal Reprojection Anti-Aliasing" - https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
    float3 r    = q - p;
    float3 rmax = (aabb_max - p.xyz);
    float3 rmin = (aabb_min - p.xyz);

    if (r.x > rmax.x + FLT_MIN)
        r *= (rmax.x / r.x);
    if (r.y > rmax.y + FLT_MIN)
        r *= (rmax.y / r.y);
    if (r.z > rmax.z + FLT_MIN)
        r *= (rmax.z / r.z);

    if (r.x < rmin.x - FLT_MIN)
        r *= (rmin.x / r.x);
    if (r.y < rmin.y - FLT_MIN)
        r *= (rmin.y / r.y);
    if (r.z < rmin.z - FLT_MIN)
        r *= (rmin.z / r.z);

    return p + r;
}

// Clip history to the neighbourhood of the current sample
float3 clip_history_3x3(uint2 group_pos, float3 color_history, float2 velocity_closest)
{
    // Sample a 3x3 neighbourhood
    float3 s1 = load_color(group_pos + kOffsets3x3[0]);
    float3 s2 = load_color(group_pos + kOffsets3x3[1]);
    float3 s3 = load_color(group_pos + kOffsets3x3[2]);
    float3 s4 = load_color(group_pos + kOffsets3x3[3]);
    float3 s5 = load_color(group_pos + kOffsets3x3[4]);
    float3 s6 = load_color(group_pos + kOffsets3x3[5]);
    float3 s7 = load_color(group_pos + kOffsets3x3[6]);
    float3 s8 = load_color(group_pos + kOffsets3x3[7]);
    float3 s9 = load_color(group_pos + kOffsets3x3[8]);

    // Compute min and max (with an adaptive box size, which greatly reduces ghosting)
    float3 color_avg  = (s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8 + s9) * RPC_9;
    float3 color_avg2 = ((s1 * s1) + (s2 * s2) + (s3 * s3) + (s4 * s4) + (s5 * s5) + (s6 * s6) + (s7 * s7) + (s8 * s8) + (s9 * s9)) * RPC_9;
    float box_size    = lerp(0.0f, 2.5f, smoothstep(0.02f, 0.0f, length(velocity_closest)));
    float3 dev        = sqrt(abs(color_avg2 - (color_avg * color_avg))) * box_size;
    float3 color_min  = color_avg - dev;
    float3 color_max  = color_avg + dev;

    // Variance clipping
    float3 color = clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history);

    // Clamp to prevent NaNs
    color = saturate_16(color);

    return color;
}

/*------------------------------------------------------------------------------
                            UPSAMPLING [WIP]
------------------------------------------------------------------------------*/

float distance_squared(float2 a_to_b, float2 offset)
{
    float2 v = a_to_b - offset;
    return dot(v, v);
}

float get_sample_weight(float distance_squared)
{
    const float std_dev = 0.18f;
    const float dawa = 1.0f / (2.0f * std_dev * std_dev);
    return exp(distance_squared - dawa);
}

float3 get_input_sample(uint2 group_top_left, uint2 group_pos)
{
    uint2 thread_id               = group_top_left + group_pos;
    const float2 uv               = (group_top_left + group_pos + 0.5f) / g_resolution_rt;
    const float2 pos_input        = uv * g_resolution_render;
    const float2 pos_input_center = floor(pos_input) + 0.5f;
    
    // Compute sample weights
    float weights[9];
    float weight_sum = 0.0f;
    float weight_normaliser = 1.0f;
    for (uint i = 0; i < 9; i++)
    {
        weights[i] = get_sample_weight(distance_squared(group_pos, (float2)kOffsets3x3[i]));
        weight_sum += weights[i];
    }
    weight_normaliser /= weight_sum;
    
    // Fetch color samples
    float3 color = 0.0f;
    for (uint j = 0; j < 9; j++)
    {
        float weight = weights[j] * weight_normaliser;
        color += load_color(group_pos + (float2)kOffsets3x3[j]); // * weight;
    }
    
    return color;
}

/*------------------------------------------------------------------------------
                                    TAA
------------------------------------------------------------------------------*/

float get_factor_luminance(uint2 pos, Texture2D tex_history, float3 color_input)
{
    float luminance_history   = luminance(tex_history[pos].rgb);
    float luminance_current   = luminance(color_input);
    float unbiased_difference = abs(luminance_current - luminance_history) / (max(luminance_current, luminance_history) + FLT_MIN);

    return saturate(unbiased_difference);
}

float get_factor_dissoclusion(float2 uv_reprojected, float2 velocity)
{

    float2 velocity_previous = tex_velocity_previous[uv_reprojected * g_resolution_render].xy;
    float dissoclusion       = length(velocity_previous - velocity) - 0.0001f;

    return saturate(dissoclusion * 2000.0f);
}

float3 temporal_antialiasing(uint2 thread_id, uint2 pos_group_top_left, uint2 pos_group, uint2 pos_screen, float2 uv, Texture2D tex_history)
{
    // Get the velocity of the current pixel
    float2 velocity = tex_velocity[thread_id].xy;

    // Get reprojected uv
    float2 uv_reprojected = uv - velocity;

    // Get input color
    float3 color_input = get_input_sample(pos_group_top_left, pos_group);

    // Get history color (catmull-rom reduces a lot of the blurring that you get under motion)
    float3 color_history = sample_catmull_rom_9(tex_history, uv_reprojected, g_resolution_rt).rgb;
    
    // Clip history to the neighbourhood of the current sample (fixes a lot of the ghosting).
    float2 velocity_closest = 0.0f; // This is best done by using the velocity with the closest depth.
    get_closest_pixel_velocity_3x3(pos_group, pos_group_top_left, velocity_closest);
    color_history = clip_history_3x3(pos_group, color_history, velocity_closest);

    // Compute blend factor
    float blend_factor = RPC_16; // We want to be able to accumulate as many jitter samples as we generated, that is, 16.
    {
        // If re-projected UV is out of screen, converge to current color immediately
        float factor_screen = !is_saturated(uv_reprojected);

        // Increase blend factor when there is dissoclusion (fixes a lot of the remaining ghosting).
        float factor_dissoclusion = get_factor_dissoclusion(uv_reprojected, velocity);

        // Add to the blend factor
        blend_factor = saturate(blend_factor + factor_screen + factor_dissoclusion);
    }

    // Resolve
    float3 color_resolved = 0.0f;
    {
        // Tonemap
        color_history = reinhard(color_history);
        color_input   = reinhard(color_input);

        // Lerp/blend
        color_resolved = lerp(color_history, color_input, blend_factor);

        // Inverse tonemap
        color_resolved = reinhard_inverse(color_resolved);
    }

    return color_resolved;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint3 group_id : SV_GroupID, uint group_index : SV_GroupIndex)
{
    populate_group_shared_memory(group_id.xy, group_index);

    // Out of bounds check
    if (any(int2(thread_id.xy) >= g_resolution_rt.xy))
        return;

    const uint2 pos_group          = group_thread_id.xy;
    const uint2 pos_group_top_left = group_id.xy * kGroupSize - kBorderSize;
    const uint2 pos_screen         = pos_group_top_left + pos_group;
    const float2 uv                = (thread_id.xy + 0.5f) / g_resolution_rt;

    tex_out_rgb[thread_id.xy] = temporal_antialiasing(thread_id.xy, pos_group_top_left, pos_group, pos_screen, uv, tex);
}
