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

static const float g_motion_blur_strength = 1.0f;
static const uint  g_motion_blur_samples  = 32;

float2 get_velocity_3x3_average(float2 uv, float2 resolution_out)
{
    float2 texel_size     = 1.0f / resolution_out;
    float2 total_velocity = 0.0f;
    int sample_count      = 0;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset = float2(x, y) * texel_size;
            float2 velocity = tex_velocity.SampleLevel(samplers[sampler_point_clamp_edge], uv + offset, 0).xy;

            total_velocity += velocity;
            ++sample_count;
        }
    }

    return total_velocity / float(sample_count);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    float2 uv       = (thread_id.xy + 0.5f) / resolution_out;
    float4 color    = tex[thread_id.xy];
    float2 velocity = get_velocity_3x3_average(uv, resolution_out);

    // compute motion blur strength from camera's shutter speed
    float camera_shutter_speed = pass_get_f3_value().x;
    float motion_blur_strength = saturate(camera_shutter_speed * g_motion_blur_strength);
    
    // scale velocity by the motion blur strength and delta time
    velocity *= motion_blur_strength / (buffer_frame.delta_time + FLT_MIN);
    
    [unroll]
    for (uint i = 1; i < g_motion_blur_samples; ++i)
    {
        float2 sample_offset = velocity * (float(i) / float(g_motion_blur_samples - 1) - 0.5f);
        color.rgb += tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + sample_offset, 0).rgb;
    }

    // normalize the accumulated color by the number of samples
    tex_uav[thread_id.xy] = float4(color.rgb / float(g_motion_blur_samples), 1.0f);
}
