/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===========
#include "Common.hlsl"
#include "Velocity.hlsl"
//======================

static const float g_taa_blend_min = 0.0f;
static const float g_taa_blend_max = 0.4f;
#define LDS_HISTORY_CLIPPING 0 // slower on some GPUs, faster on others

#if LDS_HISTORY_CLIPPING
#define BORDER_SIZE         1
#define TILE_SIZE_X         (thread_group_count_x + 2 * BORDER_SIZE)
#define TILE_SIZE_Y         (thread_group_count_y + 2 * BORDER_SIZE)
#define TILE_PIXEL_COUNT    (TILE_SIZE_X * TILE_SIZE_Y)
groupshared float3 colors_current[TILE_SIZE_X][TILE_SIZE_Y];
#endif

float3 reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 reinhard_inverse(float3 sdr, float k = 1.0)
{
    return k * sdr / (k - sdr);
}

// Decrease blend factor when motion gets sub-pixel
float compute_factor_velocity(const float2 uv, const float2 velocity)
{
    const float threshold   = 0.5f;
    const float base        = 0.5f;
    const float gather      = 0.05f;
    
    float depth             = get_linear_depth(uv);
    float texel_vel_mag     = length(velocity * g_resolution_rt) * depth;
    float subpixel_motion   = saturate(threshold / (texel_vel_mag + FLT_MIN));
    return texel_vel_mag * base + subpixel_motion * gather;
}

// Decrease blend factor when contrast is high
float compute_factor_contrast(const float3 color_current, const float3 color_history)
{
    float luminance_history = luminance(color_history);
    float luminance_current = luminance(color_current);
    float unbiased_difference = abs(luminance_current - luminance_history) / ((max(luminance_current, luminance_history) + 0.3));
    return 1.0 - unbiased_difference;
}

// From "Temporal Reprojection Anti-Aliasing"
// https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
    float3 r = q - p;
    float3 rmax = aabb_max - p.xyz;
    float3 rmin = aabb_min - p.xyz;

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
float3 clip_history(uint2 thread_id, uint group_index, uint3 group_id, Texture2D tex_input, float3 color_history)
{
    #if LDS_HISTORY_CLIPPING
    
    uint2 pos_upper_left = group_id.xy * uint2(thread_group_count_x, thread_group_count_y) - BORDER_SIZE;
    [unroll]
    for (uint i = group_index; i < TILE_PIXEL_COUNT; i += thread_group_count)
    {
        uint2 pos_array = uint2(i % TILE_SIZE_X, i / TILE_SIZE_X);
        uint2 pos_tex   = pos_upper_left + pos_array;
        colors_current[pos_array.x][pos_array.y] = tex[pos_upper_left + pos_array].rgb;
    }
 
    GroupMemoryBarrierWithGroupSync();

    uint2 array_pos = uint2(group_index % thread_group_count_x, group_index / thread_group_count_x) + 1;
    
    float3 ctl = colors_current[array_pos.x - 1][array_pos.y - 1];
    float3 ctc = colors_current[array_pos.x][array_pos.y - 1];
    float3 ctr = colors_current[array_pos.x + 1][array_pos.y - 1];
    float3 cml = colors_current[array_pos.x - 1][array_pos.y];
    float3 cmc = colors_current[array_pos.x][array_pos.y];
    float3 cmr = colors_current[array_pos.x + 1][array_pos.y];
    float3 cbl = colors_current[array_pos.x - 1][array_pos.y + 1];
    float3 cbc = colors_current[array_pos.x][array_pos.y + 1];
    float3 cbr = colors_current[array_pos.x + 1][array_pos.y + 1];

    #else

    float3 ctl = tex_input[thread_id + uint2(-1, -1)].rgb;
    float3 ctc = tex_input[thread_id + uint2(0, -1)].rgb;
    float3 ctr = tex_input[thread_id + uint2(1, -1)].rgb;
    float3 cml = tex_input[thread_id + uint2(-1, 0)].rgb;
    float3 cmc = tex_input[thread_id].rgb;
    float3 cmr = tex_input[thread_id + uint2(1, 0)].rgb;
    float3 cbl = tex_input[thread_id + uint2(-1, 1)].rgb;
    float3 cbc = tex_input[thread_id + uint2(0, 1)].rgb;
    float3 cbr = tex_input[thread_id + uint2(1, 1)].rgb;

    #endif

    float3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
    float3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
    float3 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;

    return saturate_16(clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history));
}

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

float distance_squared(float2 a_to_b, float2 offset)
{
    float2 v = a_to_b ;
    return dot(v, v);
}

float get_sample_weight(float distance_squared)
{
    const float std_dev = 0.18f;
    const float dawa = 1.0f / (2.0f * std_dev * std_dev);
    return exp(distance_squared - dawa);
}

float3 get_input_sample(Texture2D tex_input, const uint2 pos_out)
{
    if (!is_taa_upsampling_enabled())
        return tex_input[pos_out].rgb;

    const float2 uv = (pos_out + 0.5f) / g_resolution_rt;
    const float2 jitter_offset_pixels = g_taa_jitter_offset * g_resolution_render;
    const float2 pos_input = uv * g_resolution_render;
    const float2 pos_input_center = floor(pos_input) + 0.5f ;
    const float2 pos_out_center = pos_out + 0.5f;
    const float2 in_to_out = pos_out_center - pos_input_center;

    // Compute sample weights
    float weights[9];
    float weight_sum = 0.0f;
    float weight_normaliser = 1.0f;
    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        weights[i] = get_sample_weight(distance_squared(pos_input_center, (float2)kOffsets3x3[i]));
        weight_sum += weights[i];
    }
   weight_normaliser /= weight_sum;
    
    // Fetch color samples
    float3 color = 0.0f;
    [unroll]
    for (uint j = 0; j < 9; j++)
    {
        float weigth = weights[j] * weight_normaliser;
        color += tex_input[pos_input + (float2)kOffsets3x3[j]].rgb * weigth;
    }

    return color;
}

float4 temporal_antialiasing(uint2 pos_out, uint group_index, uint3 group_id, Texture2D tex_history, Texture2D tex_input)
{
    const float2 uv         = (pos_out + 0.5f) / g_resolution_rt;
    const uint2 pos_input   = is_taa_upsampling_enabled() ? (uv * g_resolution_render) : pos_out;

    // Get history color (reprojection)
    float2 velocity         = get_velocity_closest_3x3(uv);
    float2 uv_reprojected   = uv - velocity;
    float3 color_history    = tex_history.SampleLevel(sampler_bilinear_clamp, uv_reprojected, 0).rgb;

    // Get input color
    float3 color_input = get_input_sample(tex_input, pos_out);

    // If re-projected UV is out of screen, converge to current color immediately
    if (!is_saturated(uv_reprojected))
        return float4(color_input, 1.0f);

    // Clip history to the neighbourhood of the current sample
    color_history = clip_history(pos_input, group_index, group_id, tex_input, color_history);

    // Compute blend factor
    float blend_factor = 1.0f;
    {
        // Decrease blend factor when motion gets sub-pixel
        blend_factor *= compute_factor_velocity(uv, velocity);

        // Decrease blend factor when contrast is high
        blend_factor *= compute_factor_contrast(color_input, color_history);

        // Clamp
        blend_factor = clamp(blend_factor, g_taa_blend_min, g_taa_blend_max);
    }

    // Resolve
    float3 color_resolved = 0.0f;
    {
        // Tonemap
        color_history = reinhard(color_history);
        color_input = reinhard(color_input);

        // Lerp/blend
        color_resolved = lerp(color_history, color_input, blend_factor);

        // Inverse tonemap
        color_resolved = reinhard_inverse(color_resolved);
    }

    return float4(color_resolved, 1.0f);
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex, uint3 group_id : SV_GroupID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;
    
    tex_out_rgba[thread_id.xy] = temporal_antialiasing(thread_id.xy, group_index, group_id, tex, tex2);
}
