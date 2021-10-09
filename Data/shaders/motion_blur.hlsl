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

//= INCLUDES =========
#include "common.hlsl"
//====================

static const uint g_motion_blur_samples = 16;

// Returns max velocity (3x3 neighborhood)
float2 get_velocity_max_3x3(uint2 pos)
{
    float2 max_velocity = 0.0f;
    float max_length2   = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 velocity = tex_velocity.Load(int3(pos + float2(x, y), 0)).xy;
            float length2   = dot(velocity, velocity);

            if (length2 > max_length2)
            {
                max_velocity = velocity;
                max_length2  = length2;
            }
        }
    }

    return max_velocity;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= g_resolution_rt.xy))
        return;
    
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution_rt;
    float3 color    = tex[thread_id.xy].rgb;
    float2 velocity = get_velocity_max_3x3(thread_id.xy);

    // Compute motion blur strength from camera's shutter speed
    float motion_blur_strength = saturate(g_camera_shutter_speed * 1.0f);
    
    // Scale with delta time
    motion_blur_strength /= g_delta_time + FLT_MIN;
    
    // Scale velocity
    velocity *= motion_blur_strength;
    
    // Early exit
    if (abs(velocity.x) + abs(velocity.y) < FLT_MIN)
        tex_out_rgb[thread_id.xy] = color;
    
    [unroll]
    for (uint i = 1; i < g_motion_blur_samples; ++i)
    {
        float2 offset = velocity * (float(i) / float(g_motion_blur_samples - 1) - 0.5f);
        color += tex.SampleLevel(sampler_bilinear_clamp, uv + offset, 0).rgb;
    }

    tex_out_rgb[thread_id.xy] = color / float(g_motion_blur_samples);
}
