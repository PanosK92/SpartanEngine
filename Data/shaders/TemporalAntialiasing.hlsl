/*
Copyright(c) 2016-2020 Panos Karabelas

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

static const float g_taa_blend_min = 0.05f;
static const float g_taa_blend_max = 1.0f;
#define LDS_HISTORY_CLIPPING 1

float3 Reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 ReinhardInverse(float3 sdr, float k = 1.0)
{
    return k * sdr / (k - sdr);
}

// From "Temporal Reprojection Anti-Aliasing"
// https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
#if USE_OPTIMIZATIONS
    // note: only clips towards aabb center (but fast!)
    float3 p_clip = 0.5f * (aabb_max + aabb_min);
    float3 e_clip = 0.5f * (aabb_max - aabb_min) + EPSILON;

    float3 v_clip = q - p_clip;
    float3 v_unit = v_clip.xyz / e_clip;
    float3 a_unit = abs(v_unit);
    float ma_unit = max(a_unit.x, max(a_unit.y, a_unit.z));

    if (ma_unit > 1.0)
        return p_clip + v_clip / ma_unit;
    else
        return q;// point inside aabb
#else
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
#endif
}

#define BORDER_SIZE         1
#define TILE_SIZE_X         (thread_group_count_x + 2 * BORDER_SIZE)
#define TILE_SIZE_Y         (thread_group_count_y + 2 * BORDER_SIZE)
#define TILE_PIXEL_COUNT    (TILE_SIZE_X * TILE_SIZE_Y)

groupshared float3 colors_current[TILE_SIZE_X][TILE_SIZE_Y];

// Clip history to the neighbourhood of the current sample
float3 clip_history(uint2 thread_id, uint group_index, uint3 group_id, Texture2D tex, float3 color_history)
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
    
    uint2 du = uint2(1, 0);
    uint2 dv = uint2(0, 1);

    float3 ctl = tex[thread_id - dv - du].rgb;
    float3 ctc = tex[thread_id - dv].rgb;
    float3 ctr = tex[thread_id - dv + du].rgb;
    float3 cml = tex[thread_id - du].rgb;
    float3 cmc = tex[thread_id].rgb;
    float3 cmr = tex[thread_id + du].rgb;
    float3 cbl = tex[thread_id + dv - du].rgb;
    float3 cbc = tex[thread_id + dv].rgb;
    float3 cbr = tex[thread_id + dv + du].rgb;

    #endif
  
    float3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
    float3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
    float3 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;

    // Clip history to the neighbourhood of the current sample
    return saturate_16(clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history));
}

float4 TemporalAntialiasing(uint2 thread_id, uint group_index, uint3 group_id, Texture2D tex_accumulation, Texture2D tex_current)
{
    // Get history and current colors
    const float2 uv         = (thread_id + 0.5f) / g_resolution;
    float2 velocity         = GetVelocity_DepthMin(uv);
    float2 uv_reprojected   = uv - velocity;
    float3 color_history    = tex_accumulation.SampleLevel(sampler_bilinear_clamp, uv_reprojected, 0).rgb;
    float3 color_current    = tex_current[thread_id].rgb;

    // Clip history to the neighbourhood of the current sample
    color_history = clip_history(thread_id.xy, group_index, group_id, tex_current, color_history);

    // Compute blend factor
    float blend_factor = 1.0f;
    {   
        // Decrease blend factor when motion gets sub-pixel
        const float threshold   = 0.5f;
        const float base        = 0.5f;
        const float gather      = g_delta_time * 25.0f;
        float depth             = get_linear_depth(uv);
        float texel_vel_mag     = length(velocity / g_texel_size) * depth;
        float subpixel_motion   = saturate(threshold / (FLT_MIN + texel_vel_mag));
        blend_factor *= texel_vel_mag * base + subpixel_motion * gather;

        // Decrease blend factor when contrast is high
        float luminance_history = luminance(color_history);
        float luminance_current = luminance(color_current);
        float diff = 1.0 - abs(luminance_current - luminance_history) / (FLT_MIN + max(luminance_current, luminance_history));
        blend_factor *= diff * diff;

        // Clamp
        blend_factor = clamp(blend_factor, g_taa_blend_min, g_taa_blend_max);

        // Override blend factor if the re-projected uv is out of screen
        blend_factor = is_saturated(uv_reprojected) ? blend_factor : 1.0f;
    }
    
    // Tonemap
    color_history = Reinhard(color_history);
    color_current = Reinhard(color_current);
    
    // Resolve
    float3 resolved = lerp(color_history, color_current, blend_factor);
    
    // Inverse tonemap
    resolved = ReinhardInverse(resolved);
    
    return float4(resolved, 1.0f);
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex, uint3 group_id : SV_GroupID)
{
    tex_out_rgba[thread_id.xy] = TemporalAntialiasing(thread_id.xy, group_index, group_id, tex, tex2);
}

