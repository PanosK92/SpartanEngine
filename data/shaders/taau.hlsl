/*
Copyright(c) 2016-2026 Panos Karabelas

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

// taau, temporal anti-aliasing with built-in upsampling
// current color (tex2) is at render resolution, jittered via the projection matrix
// history (tex) is at output resolution, written each frame by this pass
// output (tex_uav) is at output resolution
// velocity / depth are at render resolution

//= INCLUDES =========
#include "common.hlsl"
//====================

// push constants
// values[0].x = reset_history flag, non-zero on first frame, resolution change, teleport
float reset_history() { return pass_get_f3_value().x; }

/*------------------------------------------------------------------------------
    TONEMAPPING
------------------------------------------------------------------------------*/
float3 reinhard(const float3 color)
{
    return color / (color + 1.0f);
}

float3 reinhard_inverse(const float3 color)
{
    return color / max(1.0f - color, FLT_MIN);
}

/*------------------------------------------------------------------------------
    NEIGHBOURHOOD OFFSETS
------------------------------------------------------------------------------*/
static const int2 k_offsets_3x3[9] =
{
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1),
};

/*------------------------------------------------------------------------------
    VELOCITY (render res, closest depth in 3x3)
------------------------------------------------------------------------------*/
// returns velocity at the render-res pixel with the closest (max in reverse-z) depth in a 3x3 neighborhood
// px_render is in render-texture pixel space (already accounts for resolution_scale via the caller)
float2 get_closest_pixel_velocity_3x3(int2 px_render, int2 px_render_max)
{
    float closest_depth = -1.0f;
    int2 closest_pos    = px_render;

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        int2 p   = clamp(px_render + k_offsets_3x3[i], int2(0, 0), px_render_max);
        float d  = tex_depth[p].r;
        if (d > closest_depth)
        {
            closest_depth = d;
            closest_pos   = p;
        }
    }

    return tex_velocity[closest_pos].xy;
}

/*------------------------------------------------------------------------------
    CATMULL-ROM HISTORY SAMPLING (output res)
------------------------------------------------------------------------------*/
// 9-tap catmull-rom that collapses to 5 bilinear taps, reduces blur from naive bilinear history sampling
// based on https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
float3 sample_history_catmull_rom(float2 uv, float2 resolution)
{
    float2 sample_position = uv * resolution;
    float2 tex_pos_1       = floor(sample_position - 0.5f) + 0.5f;
    float2 f               = sample_position - tex_pos_1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12       = w1 + w2;
    float2 offset_12 = w2 / w12;

    float2 inv_res    = 1.0f / resolution;
    float2 tex_pos_0  = (tex_pos_1 - 1.0f) * inv_res;
    float2 tex_pos_3  = (tex_pos_1 + 2.0f) * inv_res;
    float2 tex_pos_12 = (tex_pos_1 + offset_12) * inv_res;

    float3 result = 0.0f.xxx;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_0.y),  0.0f).rgb * w0.x  * w0.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_0.y),  0.0f).rgb * w12.x * w0.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_0.y),  0.0f).rgb * w3.x  * w0.y;

    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_12.y), 0.0f).rgb * w0.x  * w12.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_12.y), 0.0f).rgb * w12.x * w12.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_12.y), 0.0f).rgb * w3.x  * w12.y;

    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_3.y),  0.0f).rgb * w0.x  * w3.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_3.y),  0.0f).rgb * w12.x * w3.y;
    result += tex.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_3.y),  0.0f).rgb * w3.x  * w3.y;

    return saturate_16(max(result, 0.0f.xxx));
}

/*------------------------------------------------------------------------------
    AABB CLIP (variance based)
------------------------------------------------------------------------------*/
// based on "temporal reprojection anti-aliasing" - https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q)
{
    float3 r    = q - p;
    float3 rmax = aabb_max - p;
    float3 rmin = aabb_min - p;

    const float eps = FLT_MIN;
    if (r.x > rmax.x + eps) r *= (rmax.x / r.x);
    if (r.y > rmax.y + eps) r *= (rmax.y / r.y);
    if (r.z > rmax.z + eps) r *= (rmax.z / r.z);
    if (r.x < rmin.x - eps) r *= (rmin.x / r.x);
    if (r.y < rmin.y - eps) r *= (rmin.y / r.y);
    if (r.z < rmin.z - eps) r *= (rmin.z / r.z);

    return p + r;
}

// build a render-res 3x3 neighborhood around the render-res sample that px_render lands on,
// derive variance bounds, then clip history into that volume
float3 clip_history_to_neighborhood(float3 color_history, int2 px_render, int2 px_render_max, float2 velocity)
{
    float3 m1 = 0.0f.xxx; // sum
    float3 m2 = 0.0f.xxx; // sum of squares

    [unroll]
    for (int i = 0; i < 9; ++i)
    {
        int2 p   = clamp(px_render + k_offsets_3x3[i], int2(0, 0), px_render_max);
        float3 s = tex2[p].rgb;
        m1 += s;
        m2 += s * s;
    }

    float3 mean     = m1 * RPC_9;
    float3 sigma    = sqrt(abs(m2 * RPC_9 - mean * mean));
    float box_size  = lerp(0.25f, 2.5f, smoothstep(0.02f, 0.0f, length(velocity)));
    float3 aabb_min = mean - sigma * box_size;
    float3 aabb_max = mean + sigma * box_size;

    float3 clipped = clip_aabb(aabb_min, aabb_max, clamp(mean, aabb_min, aabb_max), color_history);
    return saturate_16(clipped);
}

/*------------------------------------------------------------------------------
    MAIN
------------------------------------------------------------------------------*/
float3 taau(uint2 px_out)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uv_out = (px_out + 0.5f) / resolution_out;

    // the render textures (current, depth, velocity) are allocated at the unscaled render resolution,
    // but only their top-left (resolution_render * resolution_scale) region holds valid data,
    // so convert output uv into render-texture uv space via multiplication by resolution_scale
    float scale     = buffer_frame.resolution_scale;
    float2 uv_render = uv_out * scale;
    float2 active_render_size = buffer_frame.resolution_render * scale;

    // current color, render res, bilinear gather, jitter is baked into the projection
    float3 color_current = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv_render, 0.0f).rgb;
    color_current        = saturate_16(max(color_current, 0.0f.xxx));

    int2 px_render     = int2(floor(uv_render * buffer_frame.resolution_render));
    int2 px_render_max = max(int2(active_render_size) - 1, int2(0, 0));
    float2 velocity    = get_closest_pixel_velocity_3x3(px_render, px_render_max);

    // reproject history at output res
    float2 uv_reprojected = uv_out - velocity;

    // first frame, history wipe, or out of screen, snap to current
    bool history_invalid = reset_history() > 0.5f || !is_valid_uv(uv_reprojected);
    if (history_invalid)
        return color_current;

    float3 color_history = sample_history_catmull_rom(uv_reprojected, resolution_out);
    color_history        = clip_history_to_neighborhood(color_history, px_render, px_render_max, velocity);

    // base blend, ~1/8 because each output pixel mixes a freshly upscaled sample every frame
    float blend = 0.125f;

    // tonemap, lerp, inverse, the reinhard sandwich keeps fireflies from poisoning the accumulation
    color_history = reinhard(color_history);
    color_current = reinhard(color_current);
    float3 result = lerp(color_history, color_current, blend);
    result        = reinhard_inverse(result);

    return saturate_16(max(result, 0.0f.xxx));
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= int2(resolution_out)))
        return;

    tex_uav[thread_id.xy] = float4(taau(thread_id.xy), 1.0f);
}
