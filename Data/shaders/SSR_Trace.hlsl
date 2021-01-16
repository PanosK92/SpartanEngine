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
//====================

static const uint g_ssr_max_steps                   = 128;
static const uint g_ssr_binary_search_steps         = 16;
static const float g_ssr_binary_search_threshold    = 0.02f;
static const float g_ssr_ray_stride                 = 0.001f;
static const float g_roughness_threshold            = 1.0f;

inline float2 binary_search(float3 ray_dir, inout float3 ray_pos, inout float2 ray_uv)
{
    float depth_delta_previous_sign = -1.0f;

    [unroll]
    for (uint i = 0; i < g_ssr_binary_search_steps; i++)
    {
        // Test depth delta
        float depth_delta = ray_pos.z - get_linear_depth(ray_uv);
        if (abs(depth_delta) <= g_ssr_binary_search_threshold)
            return ray_uv;

        // Half direction
        if (sign(depth_delta) != depth_delta_previous_sign)
        {
            ray_dir *= -0.5f;
            depth_delta_previous_sign = sign(depth_delta);
        }

        // Step ray
        ray_pos += ray_dir;
        ray_uv  = project_uv(ray_pos, g_projection);
    }

    return 0.0f;
}

inline float2 trace_ray(int2 screen_pos, float3 ray_pos, float3 ray_dir)
{
    float3 ray_step     = ray_dir * g_ssr_ray_stride;
    float3 ray_step_h   = 0.0f;
    float2 ray_uv_hit   = 0.0f;
    
    // Adjust ray step using interleaved gradient noise, TAA will bring in more detail without any additional cost
    float offset = interleaved_gradient_noise(screen_pos);
    ray_step += ray_step * offset;
    
    // Ray-march
    float2 ray_uv = 0.0f;
    for (uint i = 0; i < g_ssr_max_steps; i++)
    {
        // Step ray
        ray_step_h  += ray_step; // hierarchical steps (as further reflections matter less)
        ray_pos     += ray_step_h;
        ray_uv      = project_uv(ray_pos, g_projection);
    
        float depth_buffer_z = get_linear_depth(ray_uv);
    
        [branch]
        if (ray_pos.z >= depth_buffer_z)
        {
            ray_uv_hit = binary_search(ray_dir, ray_pos, ray_uv);
            break;
        }
    }
    
    // Reject if the reflection is pointing outside of the viewport
    ray_uv_hit *= is_saturated(ray_uv_hit);

    return ray_uv_hit;
}

inline float compute_alpha(int2 screen_pos, float2 hit_uv, float3 view_direction, float3 reflection)
{
    float alpha = 1.0f;

    alpha *= screen_fade(hit_uv);

    // Reject if the reflection vector is pointing back at the viewer.
    alpha *= 1.0f - saturate(dot(-view_direction, reflection));

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
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // Compute reflection direction in view space
    float3 normal       = get_normal_view_space(thread_id.xy);
    float3 position     = get_position_view_space(thread_id.xy);
    float3 reflection   = normalize(reflect(position, normal));

    // Jitter reflection vector based on the surface normal
    {
        // Compute jitter
        float ign               = interleaved_gradient_noise(thread_id.xy);
        float2 uv               = (thread_id.xy + 0.5f) / g_resolution;
        float3 random_vector    = unpack(normalize(tex_normal_noise.SampleLevel(sampler_bilinear_wrap, uv * g_normal_noise_scale, 0)).xyz);
        float3 jitter           = reflect(hemisphere_samples[ign * 63], random_vector);

        // Get surface roughness
        float roughness = tex_material.Load(int3(thread_id.xy, 0)).r;
        roughness       *= roughness;
        roughness       = clamp(roughness, 0.0f, g_roughness_threshold); // limit max roughness as there is a limit to how much denoising TAA can do

        jitter *= roughness;                              // Adjust with roughness
        jitter *= sign(dot(normal, reflection + jitter)); // Flip if behind normal

        // Apply jitter to reflection
        reflection += jitter * 3;
    }

    float2 hit_uv               = trace_ray(thread_id.xy, position, reflection);
    float alpha                 = compute_alpha(thread_id.xy, hit_uv, position, reflection);
    tex_out_rgba[thread_id.xy]  = float4(hit_uv, alpha, 0.0f);
}

