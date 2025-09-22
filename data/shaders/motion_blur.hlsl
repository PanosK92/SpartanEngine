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

//= INCLUDES =========
#include "common.hlsl"
//====================

static const uint g_motion_blur_samples = 32;
static const float g_velocity_scale     = 1.0f;
static const float g_velocity_threshold = 0.005f;
static const float g_depth_scale        = 1.0f;
static const float g_color_scale        = 0.5f;

groupshared uint g_tile_max_velocity_sqr;

// use dilated velocity to capture the maximum motion in a 3x3 neighborhood
float2 get_velocity_dilated(float2 uv, float2 resolution_out)
{
    float2 texel_size   = 1.0f / resolution_out;
    float2 max_velocity = 0.0f;
    float max_len       = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texel_size;
            float2 v      = tex_velocity.SampleLevel(samplers[sampler_point_clamp], (uv + offset) * buffer_frame.resolution_scale, 0).xy;
            float len     = length(v);
            if (len > max_len)
            {
                max_len = len;
                max_velocity = v;
            }
        }
    }
    return max_velocity;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint group_index : SV_GroupIndex)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uv           = (thread_id.xy + 0.5f) / resolution_out;
    float4 center_color = tex[thread_id.xy];
    float2 velocity     = get_velocity_dilated(uv, resolution_out);

    // convert velocity from NDC to UV space
    float2 velocity_UV = velocity / 2.0f;

    // compute motion blur strength from camera's shutter speed
    float camera_shutter_speed = pass_get_f3_value().x;
    float motion_blur_strength = saturate(camera_shutter_speed * 1.5f);

    // scale velocity by motion blur strength, delta time, and additional scale factor
    velocity_UV *= motion_blur_strength * g_velocity_scale / (buffer_frame.delta_time + FLT_MIN);

    // compute max velocity squared for the tile
    if (group_index == 0)
    {
        g_tile_max_velocity_sqr = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint velocity_sqr = (uint) (dot(velocity_UV, velocity_UV) * 1000000.0f);
    InterlockedMax(g_tile_max_velocity_sqr, velocity_sqr);

    GroupMemoryBarrierWithGroupSync();

    // early exit for low-motion tiles
    if (sqrt(float(g_tile_max_velocity_sqr) / 1000000.0f) < g_velocity_threshold)
    {
        tex_uav[thread_id.xy] = center_color;
        return;
    }

    // early exit for low-motion pixels
    if (length(velocity_UV) < g_velocity_threshold)
    {
        tex_uav[thread_id.xy] = center_color;
        return;
    }

    float4 color            = center_color;
    float total_weight      = 1.0f;
    float center_depth      = get_linear_depth(uv);
    float3 center_color_rgb = center_color.rgb;

    [unroll]
    for (uint i = 1; i < g_motion_blur_samples; ++i)
    {
        float t              = (float(i) / float(g_motion_blur_samples - 1)) - 0.5f;
        float2 sample_offset = velocity_UV * t;
        float2 sample_uv     = uv + sample_offset;

        // zero out weight for off-screen samples without branching (allows full unroll)
        float is_on_screen = step(0.0f, sample_uv.x) * step(sample_uv.x, 1.0f) * step(0.0f, sample_uv.y) * step(sample_uv.y, 1.0f);

        float sample_depth     = get_linear_depth(sample_uv);
        float depth_difference = abs(center_depth - sample_depth);
        float depth_weight     = exp(-depth_difference * g_depth_scale);

        float4 sample_color     = tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0);
        float3 sample_color_rgb = sample_color.rgb;
        float color_difference  = length(center_color_rgb - sample_color_rgb);
        float color_weight      = exp(-color_difference * g_color_scale);

        float weight  = depth_weight * color_weight * is_on_screen;
        color        += sample_color * weight;
        total_weight += weight;
    }

    // normalize the accumulated color
    color /= total_weight + FLT_MIN;

    tex_uav[thread_id.xy] = float4(color.rgb, 1.0f);
}
