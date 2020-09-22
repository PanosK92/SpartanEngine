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

//= INCLUDES =========
#include "Common.hlsl"
//====================

static const uint g_ssr_max_steps               = 12;
static const uint g_ssr_binarySearchSteps       = 12;
static const float g_ssr_binarySearchThickness  = 0.001f;
static const float g_ssr_ray_max_distance       = 10.0f;

inline float2 binary_search(float3 ray_dir, inout float3 ray_pos, inout float2 ray_uv)
{
    float depth_buffer_z = 0.0f;
    float depth_delta = 1.0f;
    float2 hit_uv = 0.0f;

    [unroll]
    for (uint i = 0; i < g_ssr_binarySearchSteps; i++)
    {
        ray_dir *= 0.5f;
        ray_pos += -sign(depth_delta) * ray_dir;
        ray_uv = project_uv(ray_pos, g_projection);

        depth_buffer_z = get_linear_depth(ray_uv);
        depth_delta = ray_pos.z - depth_buffer_z;
        
        if (abs(g_ssr_binarySearchThickness - depth_delta) < g_ssr_binarySearchThickness)
        {
            hit_uv = ray_uv;
            break;
        }
    }

    return hit_uv;
}

inline float2 trace_ray(float2 uv, float3 ray_pos, float3 ray_dir)
{
    float step_length   = g_ssr_ray_max_distance / (float) g_ssr_max_steps;
    float3 ray_step     = ray_dir * step_length;
    float2 ray_uv_hit   = 0.0f;
    
    // Reject if the reflection vector is pointing back at the viewer.
    // Attenuate reflections for angles between 60 degrees and 75 degrees, and drop all contribution beyond the (-60,60) degree range
    float3 camera_direction = normalize(mul(float4(g_camera_direction, 0.0f), g_view).xyz);
    float fade_camera = 1 - smoothstep(0.25, 0.5, dot(-camera_direction, ray_dir));
    [branch]
    if (fade_camera > 0)
    {
        // Offseting with some temporal interleaved gradient noise, will capture more detail
        float offset = interleaved_gradient_noise(g_resolution * uv) * 2.0f - 1.0f;
        ray_pos += ray_step * offset;

        // Ray-march
        float2 ray_uv = 0.0f;
        for (uint i = 0; i < g_ssr_max_steps; i++)
        {
            // Step ray
            ray_pos += ray_step;
            ray_uv = project_uv(ray_pos, g_projection);

            float depth_buffer_z = get_linear_depth(ray_uv);

            [branch]
            if (ray_pos.z > depth_buffer_z)
            {
                ray_uv_hit = binary_search(ray_dir, ray_pos, ray_uv);
                break;
            }
        }

        // Reject if the reflection is pointing outside of the viewport
        ray_uv_hit *= is_saturated(ray_uv_hit);
    }

    return ray_uv_hit;
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    // Compute view-space ray
    float3 normal_view  = get_normal_view_space(thread_id.xy);
    float3 ray_pos      = get_position_view_space(thread_id.xy);
    float3 ray_dir      = normalize(reflect(ray_pos, normal_view));

    // Trace it
    tex_out_rg[thread_id.xy] = trace_ray(uv, ray_pos, ray_dir);
}

