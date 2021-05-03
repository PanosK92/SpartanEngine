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

// Settings
static const uint  g_sss_max_steps        = 8;     // Max ray steps, affects quality and performance.
static const float g_sss_ray_max_distance = 0.05f;  // Max shadow length, longer shadows are less accurate.
static const float g_sss_thickness        = 0.05f;  // Depth testing thickness.
static const float g_sss_step_length      = g_sss_ray_max_distance / (float)g_sss_max_steps;

//= INLUCES ==========
#include "Common.hlsl"
//====================

float ScreenSpaceShadows(Surface surface, Light light)
{
    // Compute ray position and direction (in view-space)
    float3 ray_pos = world_to_view(surface.position);
    float3 ray_dir = world_to_view(-light.direction, false);

    // Compute ray step
    float3 ray_step = ray_dir * g_sss_step_length;

    // Offset starting position with temporal interleaved gradient noise
    float offset = get_noise_interleaved_gradient(surface.uv * g_resolution_rt);
    ray_pos      += ray_step * offset;

    // Ray march towards the light
    float occlusion = 0.0;
    float2 ray_uv   = 0.0f;
    for (uint i = 0; i < g_sss_max_steps; i++)
    {
        // Step the ray
        ray_pos += ray_step;
        ray_uv  = view_to_uv(ray_pos);

        // Ensure the UV coordinates are inside the screen
        if (is_saturated(ray_uv))
        {
            // Compute the difference between the ray's and the camera's depth
            float depth_z       = get_linear_depth(ray_uv);
            float depth_delta   = ray_pos.z - depth_z;

            // Check if the camera can't "see" the ray (ray depth must be larger than the camera depth, so positive depth_delta)
            if ((depth_delta > 0.0f) && (depth_delta < g_sss_thickness))
            {
                // Mark as occluded
                occlusion = 1.0f;

                // Fade out as we approach the edges of the screen
                occlusion *= screen_fade(ray_uv);

                break;
            }
        }
    }

    // Convert to visibility
    return 1.0f - occlusion;
}


