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

//= includes =========
#include "common.hlsl"
//====================

static const uint g_motion_blur_samples = 32;
static const float g_velocity_threshold = 0.005f;
static const float g_color_scale        = 0.5f;

groupshared uint g_tile_max_velocity_sqr;

// find maximum velocity in a 3x3 neighborhood (dilation)
// uses SampleLevel with UVs to handle resolution mismatches (e.g. 4K color vs 1080p velocity)
float2 get_velocity_dilated(float2 uv, float2 velocity_texel_size)
{
    float2 max_velocity = 0.0f;
    float max_len       = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * velocity_texel_size;
            float2 v      = tex_velocity.SampleLevel(samplers[sampler_point_clamp], uv + offset, 0).xy;
            float len     = length(v);

            if (len > max_len)
            {
                max_len      = len;
                max_velocity = v;
            }
        }
    }
    return max_velocity;
}

// simple interleaved gradient noise to reduce banding
float get_noise(uint2 pixel_coord)
{
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(float2(pixel_coord), magic.xy)));
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint group_index : SV_GroupIndex)
{
    // 1. get dimensions of the input color (e.g. 4k)
    float2 resolution_out;
    tex.GetDimensions(resolution_out.x, resolution_out.y);

    // 2. get dimensions of the velocity buffer (e.g. 1080p)
    float2 resolution_velocity;
    tex_velocity.GetDimensions(resolution_velocity.x, resolution_velocity.y);
    float2 velocity_texel_size = 1.0f / resolution_velocity;

    // setup coordinates
    uint2 pixel_coord = thread_id.xy;
    float2 uv         = (pixel_coord + 0.5f) / resolution_out;

    // 3. get velocities using UVs to handle the resolution mismatch
    // center uses point sampling to get the exact velocity for this screen area
    float2 center_velocity  = tex_velocity.SampleLevel(samplers[sampler_point_clamp], uv, 0).xy;
    float2 dilated_velocity = get_velocity_dilated(uv, velocity_texel_size);

    // physically based motion blur strength
    float shutter_speed        = pass_get_f3_value().x;
    float shutter_ratio        = shutter_speed / (buffer_frame.delta_time + FLT_MIN);
    float2 center_velocity_uv  = (center_velocity / 2.0f) * shutter_ratio;
    float2 dilated_velocity_uv = (dilated_velocity / 2.0f) * shutter_ratio;

    // compute max velocity for tile
    if (group_index == 0)
    {
        g_tile_max_velocity_sqr = 0;
    }
    GroupMemoryBarrierWithGroupSync();

    uint velocity_sqr = (uint)(dot(dilated_velocity_uv, dilated_velocity_uv) * 1000000.0f);
    InterlockedMax(g_tile_max_velocity_sqr, velocity_sqr);

    GroupMemoryBarrierWithGroupSync();

    // early exit if tile has insignificant motion
    if (sqrt(float(g_tile_max_velocity_sqr) / 1000000.0f) < g_velocity_threshold)
    {
        tex_uav[pixel_coord] = tex.Load(int3(pixel_coord, 0));
        return;
    }

    // classify pixel (background vs foreground)
    float center_speed  = length(center_velocity_uv);
    float dilated_speed = length(dilated_velocity_uv);
    bool is_background  = center_speed < (dilated_speed - g_velocity_threshold);

    // early exit if we are static and there is no fast neighbor
    if (!is_background && center_speed < g_velocity_threshold)
    {
         tex_uav[pixel_coord] = tex.Load(int3(pixel_coord, 0));
         return;
    }

    // reconstruction loop setup
    float4 center_color = tex.Load(int3(pixel_coord, 0));
    float center_depth  = get_linear_depth(uv);
    float4 accum_color  = center_color;
    float total_weight  = 1.0f;   
    float2 search_vector = dilated_velocity_uv;
    float noise          = get_noise(pixel_coord);
    [unroll]
    for (uint i = 1; i < g_motion_blur_samples; ++i)
    {
        float t           = (float(i) + noise) / float(g_motion_blur_samples) - 0.5f;
        float2 sample_uv  = uv + search_vector * t;

        float is_on_screen = step(0.0f, sample_uv.x) * step(sample_uv.x, 1.0f) * step(0.0f, sample_uv.y) * step(sample_uv.y, 1.0f);
        
        // bilinear sample for color (smoothness)
        float4 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0);
        float sample_depth  = get_linear_depth(sample_uv);

        // depth weighting logic
        float is_foreground_sample = step(sample_depth, center_depth - 0.01f);
        float depth_weight         = 1.0f;

        if (is_background)
        {
            depth_weight = is_foreground_sample;
        }
        else
        {
            float depth_diff = abs(center_depth - sample_depth);
            depth_weight     = exp(-depth_diff);
        }

        float color_diff   = length(center_color.rgb - sample_color.rgb);
        float color_weight = exp(-color_diff * g_color_scale);

        float weight  = is_on_screen * depth_weight * color_weight;
        accum_color  += sample_color * weight;
        total_weight += weight;
    }

    tex_uav[pixel_coord] = accum_color / total_weight;
}
