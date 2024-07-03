/*
Copyright(c) 2016-2024 Panos Karabelas

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

static const uint   g_motion_blur_samples_max = 64;
static const uint   g_motion_blur_samples_min = 8;
static const float  g_velocity_scale          = 1.0f;  // adjust this to control overall blur intensity
static const float  g_velocity_threshold      = 0.01f; // threshold for skipping low-motion pixels
static const float  g_adaptive_threshold      = 0.1f;  // threshold for max adaptive sampling
static const float  g_depth_scale             = 1.0f;  // adjust this to control depth difference sensitivity

groupshared uint g_tile_max_velocity_sqr;

float2 get_velocity_3x3_average(float2 uv, float2 resolution_out)
{
    float2 texel_size     = 1.0f / resolution_out;
    float2 total_velocity = 0.0f;
    int    sample_count   = 0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset   = float2(x, y) * texel_size;
            float2 velocity = tex_velocity.SampleLevel(samplers[sampler_point_clamp_edge], uv + offset, 0).xy;
            total_velocity += velocity;
            ++sample_count;
        }
    }
    return total_velocity / float(sample_count);
}

uint get_adaptive_sample_count(float2 velocity)
{
    float velocity_length = length(velocity);
    float t = saturate(velocity_length / g_adaptive_threshold);
    return (uint)lerp(g_motion_blur_samples_min, g_motion_blur_samples_max, t);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint group_index : SV_GroupIndex)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uv      = (thread_id.xy + 0.5f) / resolution_out;
    float4 color   = tex[thread_id.xy];
    float2 velocity = get_velocity_3x3_average(uv, resolution_out);
    
    // compute motion blur strength from camera's shutter speed
    float camera_shutter_speed = pass_get_f3_value().x;
    float motion_blur_strength = saturate(camera_shutter_speed);
    
    // scale velocity by the motion blur strength, delta time, and additional scale factor
    velocity *= motion_blur_strength * g_velocity_scale / (buffer_frame.delta_time + FLT_MIN);
    
    // Compute max velocity squared for the tile
    if (group_index == 0)
    {
        g_tile_max_velocity_sqr = 0;
    }
    GroupMemoryBarrierWithGroupSync();
    
    uint velocity_sqr = (uint)(dot(velocity, velocity) * 1000000.0f); // Scale up and convert to integer
    InterlockedMax(g_tile_max_velocity_sqr, velocity_sqr);
    
    GroupMemoryBarrierWithGroupSync();
    
    // skip blur calculation for low-motion tiles
    if (sqrt(float(g_tile_max_velocity_sqr) / 1000000.0f) < g_velocity_threshold)
    {
        tex_uav[thread_id.xy] = color;
        return;
    }
    
    // skip blur calculation for low-motion pixels
    if (length(velocity) < g_velocity_threshold)
    {
        tex_uav[thread_id.xy] = color;
        return;
    }
    
    // determine adaptive sample count
    uint sample_count = get_adaptive_sample_count(velocity);
    
    float total_weight = 1.0f;
    float center_depth = get_linear_depth(uv);
    
    [loop] // for variable iteration count
    for (uint i = 1; i < sample_count; ++i)
    {
        float  t             = (float(i) / float(sample_count - 1) - 0.5f);
        float2 sample_offset = velocity * t;
        float2 sample_uv     = uv + sample_offset;
        
        float sample_depth = get_linear_depth(sample_uv);
        float depth_difference = abs(center_depth - sample_depth);
        float depth_weight = exp(-depth_difference * g_depth_scale);
        
        float4 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0);
        color += sample_color * depth_weight;
        total_weight += depth_weight;
    }
    
    // normalize the accumulated color
    color /= total_weight;
    tex_uav[thread_id.xy] = float4(color.rgb, 1.0f);
}
