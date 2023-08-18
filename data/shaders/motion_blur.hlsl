/*
Copyright(c) 2016-2023 Panos Karabelas

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

static const uint g_motion_blur_samples = 16;

// returns max velocity (3x3 neighborhood)
float2 get_velocity_max_3x3(float2 uv)
{
    // the render target can be larger than the g-buffer (including the velocity), so we have to sample with UVs.

    float2 max_velocity = 0.0f;
    float max_length2   = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 offset   = float2(x, y) * get_rt_texel_size();
            float2 velocity = tex_velocity.SampleLevel(samplers[sampler_point_clamp], uv + offset, 0).xy;
            float length2   = dot(velocity, velocity);

            if (length2 > max_length2)
            {
                max_velocity = velocity;
                max_length2 = length2;
            }
        }
    }

    return max_velocity;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / pass_get_resolution_out();
    float4 color    = tex[thread_id.xy];
    float2 velocity = get_velocity_max_3x3(uv);

    // compute motion blur strength from camera's shutter speed
    float camera_shutter_speed = pass_get_f3_value().x;
    float motion_blur_strength = saturate(camera_shutter_speed * 0.5f);
    
    // scale with delta time
    motion_blur_strength /= buffer_frame.delta_time + FLT_MIN;
    
    // scale velocity
    velocity *= motion_blur_strength;
    
    // early exit
    if (abs(velocity.x) + abs(velocity.y) < FLT_MIN)
        tex_uav[thread_id.xy] = color;
    
    [unroll]
    for (uint i = 1; i < g_motion_blur_samples; ++i)
    {
        float2 offset = velocity * (float(i) / float(g_motion_blur_samples - 1) - 0.5f);
        color.rgb += tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + offset, 0).rgb;
    }

    tex_uav[thread_id.xy] = float4(color.rgb / float(g_motion_blur_samples), 1.0f);
}
