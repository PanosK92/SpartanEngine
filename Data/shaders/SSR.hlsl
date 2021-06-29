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
#include "Common.hlsl"
//===================

static const float g_ssr_max_distance               = 80.0f;
static const uint g_ssr_max_steps                   = 64;
static const uint g_ssr_binary_search_steps         = 32;
static const float g_ssr_thickness                  = 0.0001f;
static const float g_ssr_camera_facing_threshold    = 0.8f; // Higher values allow for more camera facing rays to be traced.

bool intersect_depth_buffer(float2 ray_pos, float2 ray_start, float ray_length, float z_start, float z_end, out float depth_delta)
{
    float alpha     = length(ray_pos - ray_start) / ray_length;
    float ray_depth = (z_start * z_end) / lerp(z_end, z_start, alpha);
    depth_delta     = ray_depth - get_linear_depth(ray_pos);

    return depth_delta >= 0.0f;
}

float2 trace_ray(uint2 screen_pos, float3 ray_start_vs, float3 ray_dir_vs)
{
    // Compute ray to UV space
    float3 ray_end_vs       = ray_start_vs + ray_dir_vs * g_ssr_max_distance;
    float2 ray_start        = view_to_uv(ray_start_vs);
    float2 ray_end          = view_to_uv(ray_end_vs);
    float2 ray_start_to_end = ray_end - ray_start;
    float ray_length        = length(ray_start_to_end);
    float2 ray_step         = (ray_start_to_end) / (float)g_ssr_max_steps;
    float2 ray_pos          = ray_start;
    
    // Adjust position with some temporal noise (TAA will do some magic later)
    float offset = get_noise_interleaved_gradient(screen_pos);
    ray_pos += ray_step * offset;
    
    // Ray-march
    for (uint i = 0; i < g_ssr_max_steps; i++)
    {
        // Step ray
        ray_pos += ray_step;

        // Intersect depth buffer
        float depth_delta = 0.0f;
        if (intersect_depth_buffer(ray_pos, ray_start, ray_length, ray_start_vs.z, ray_end_vs.z, depth_delta))
        {
            // Binary search
            float depth_delta_previous_sign = -1.0f;
            for (uint j = 0; j < g_ssr_binary_search_steps; j++)
            {
                // Depth test
                if (abs(depth_delta) <= g_ssr_thickness)
                    return ray_pos;

                // Half direction and flip (if necessery)
                if (sign(depth_delta) != depth_delta_previous_sign)
                {
                    ray_step *= -0.5f;
                    depth_delta_previous_sign = sign(depth_delta);
                }

                // Step ray
                ray_pos += ray_step;

                // Intersect depth buffer
                intersect_depth_buffer(ray_pos, ray_start, ray_length, ray_start_vs.z, ray_end_vs.z, depth_delta);
            }

            return 0.0f;
        }
    }

    return 0.0f;
}

inline float compute_alpha(uint2 screen_pos, float2 hit_uv, float v_dot_r)
{
    float alpha = 1.0f;

    alpha *= screen_fade(hit_uv);

    // Reject if the reflection vector is pointing back at the viewer.
    alpha *= saturate(g_ssr_camera_facing_threshold - v_dot_r);

    // Fade out as the material roughness increases.
    // This is because reflections do get rougher by getting jittered but there is a threshold before they start to look too noisy.
    float roughness = tex_material[screen_pos].r;
    alpha *= 1.0f - roughness;

    // If the UV is invalid fade completely
    alpha *= all(hit_uv);

    return alpha;
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    float2 hit_uv   = 0.0f;
    float alpha     = 0.0f;

    // Skip pixels which are beyond the roughness threshold we handle (and not noticable anyway)
    float roughness = tex_material.Load(int3(thread_id.xy, 0)).r;
    if (roughness < 1.0f)
    {
        // Compute reflection direction in view space
        float3 normal           = get_normal_view_space(thread_id.xy);
        float3 position         = get_position_view_space(thread_id.xy);
        float3 camera_to_pixel  = normalize(position);
        float3 reflection       = normalize(reflect(camera_to_pixel, normal));

        // Compute this dot product now as it's used in two places
        float v_dot_r = dot(-camera_to_pixel, reflection);

        // Don't trace rays which are almost facing the camera
        if (v_dot_r < g_ssr_camera_facing_threshold)
        { 
            // Jitter reflection vector based on the surface normal
            {
                // Compute jitter
                float random    = get_noise_interleaved_gradient(thread_id.xy);
                float3 jitter   = hemisphere_samples[random * 63];
                jitter          *= roughness * roughness; // Adjust with roughness

                // Apply jitter to reflection
                reflection += jitter;
            }

            hit_uv  = trace_ray(thread_id.xy, position, reflection);
            alpha   = compute_alpha(thread_id.xy, hit_uv, v_dot_r);
        }
    }

    tex_out_rgba[thread_id.xy]  = float4(hit_uv, alpha, 0.0f);
}
