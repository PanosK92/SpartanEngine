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

// Settings
static const uint  g_sss_steps            = 16;     // Quality/performance
static const float g_sss_ray_max_distance = 0.05f; // Max shadow length
static const float g_sss_tolerance        = 0.02f; // Error in favor of reducing gaps
static const float g_sss_step_length      = g_sss_ray_max_distance / (float)g_sss_steps;

//= INLUCES ==========
#include "Common.hlsl"
//====================

float ScreenSpaceShadows(Surface surface, Light light)
{
    // Compute ray
    float3 ray_pos  = mul(float4(surface.position, 1.0f), g_view).xyz;
    float3 ray_dir  = mul(float4(-light.direction, 0.0f), g_view).xyz;
    float3 ray_step = ray_dir * g_sss_step_length;
	
	// Offset starting position with temporal interleaved gradient noise
    float offset = interleaved_gradient_noise(g_resolution * surface.uv) * 2.0f - 1.0;
    ray_pos      += ray_step * offset;

    // Ray march towards the light
    float occlusion = 0.0;
    float2 ray_uv   = 0.0f;
	[unroll]
    for (uint i = 0; i < g_sss_steps; i++)
    {
        // Step the ray
        ray_pos += ray_step;
        ray_uv  = project_uv(ray_pos, g_projection);
		
		[branch]
        if (is_saturated(ray_uv))
        {
			// Compute depth difference
			float depth_z     = get_linear_depth(ray_uv);
			float depth_delta = ray_pos.z - depth_z;

			// Occlusion test
			if (abs(g_sss_tolerance - depth_delta) < g_sss_tolerance)
			{
				occlusion = 1.0f;
				break;
			}
		}
    }

    // Fade out as we approach the edges of the screen
    occlusion *= screen_fade(ray_uv);
    
    return 1.0f - occlusion;
}
