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

static const uint g_motion_blur_samples = 16;

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;
    
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    float4 color    = tex[thread_id.xy];
    float2 velocity = GetVelocity_Max(uv, tex_velocity, tex_depth);

    // Compute motion blur strength from camera's shutter speed
    float motion_blur_strength = saturate(g_camera_shutter_speed * 1.0f);
	
	// Scale with delta time
    motion_blur_strength /= g_delta_time + FLT_MIN;
	
	// Scale velocity
    velocity *= motion_blur_strength;
    
    // Early exit
    if (abs(velocity.x) + abs(velocity.y) < FLT_MIN)
        tex_out_rgba[thread_id.xy] = color;
    
    [unroll]
    for (uint i = 1; i < g_motion_blur_samples; ++i)
    {
        float2 offset = velocity * (float(i) / float(g_motion_blur_samples - 1) - 0.5f);
        color += tex.SampleLevel(sampler_bilinear_clamp, uv + offset, 0);
    }

    tex_out_rgba[thread_id.xy] = color / float(g_motion_blur_samples);
}
