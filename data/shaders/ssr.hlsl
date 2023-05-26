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

static const float g_ssr_max_distance       = 100.0f;
static const uint g_ssr_max_steps           = 45;
static const uint g_ssr_binary_search_steps = 24;
static const float g_ssr_thickness          = 0.0001f;

float compute_alpha(uint2 screen_pos, float2 hit_uv, float v_dot_r)
{
    float alpha = 1.0f;

    alpha *= screen_fade(hit_uv);

    // If the UV is invalid fade completely
    alpha *= all(hit_uv);

    return alpha;
}

float get_depth_from_ray(float2 ray_pos, float2 ray_start, float ray_length, float z_start, float z_end)
{
    float alpha = length(ray_pos - ray_start) / ray_length;
    return (z_start * z_end) / lerp(z_end, z_start, alpha);
}

bool intersect_depth_buffer(float2 ray_pos, float2 ray_start, float ray_length, float z_start, float z_end, out float depth_delta)
{
    float depth_ray  = get_depth_from_ray(ray_pos, ray_start, ray_length, z_start, z_end);
    float depth_real = get_linear_depth(ray_pos);
    depth_delta      = (depth_ray - depth_real);

    return depth_delta >= 0.0f;
}

float2 trace_ray(uint2 screen_pos, float3 ray_start_vs, float3 ray_dir_vs)
{
    // Compute ray end and start depth
    float3 ray_end_vs = ray_start_vs + ray_dir_vs * g_ssr_max_distance;
    float depth_end   = ray_end_vs.z;
    float depth_start = ray_start_vs.z;
    
    // Compute ray start and end (in UV space)
    float2 ray_start = view_to_uv(ray_start_vs);
    float2 ray_end   = view_to_uv(ray_end_vs);

    // Compute ray step
    float2 ray_start_to_end = ray_end - ray_start;
    float ray_length        = length(ray_start_to_end);
    float2 ray_step         =  (ray_start_to_end + FLT_MIN) / (float)(g_ssr_max_steps);
    float2 ray_pos          = ray_start;

    // Adjust position with some temporal noise (TAA will do some magic later)
    float offset = get_noise_interleaved_gradient(screen_pos);
    ray_pos      += ray_step * offset;
    
    // Ray-march
    for (uint i = 0; i < g_ssr_max_steps; i++)
    {
        // Early exit if the ray is out of screen
        if (!is_valid_uv(ray_pos))
            return 0.0f;
        
        // Intersect depth buffer
        float depth_delta = 0.0f;
        if (intersect_depth_buffer(ray_pos, ray_start, ray_length, depth_start, depth_end, depth_delta))
        {
            // Binary search
            float depth_delta_previous_sign = -1.0f;
            for (uint j = 0; j < g_ssr_binary_search_steps; j++)
            {
                // Depth test
                if (abs(depth_delta) <= g_ssr_thickness)
                    return ray_pos;

                // Half direction and flip (if necessary)
                if (sign(depth_delta) != depth_delta_previous_sign)
                {
                    ray_step *= -0.5f;
                    depth_delta_previous_sign = sign(depth_delta);
                }

                // Step ray
                ray_pos += ray_step;

                // Intersect depth buffer
                intersect_depth_buffer(ray_pos, ray_start, ray_length, depth_start, depth_end, depth_delta);
            }

            return 0.0f;
        }

        // Step ray
        ray_pos += ray_step;
    }

    return 0.0f;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= buffer_uber.resolution_rt.xy))
        return;

    // Construct surface
    Surface surface;
    surface.Build(thread_id.xy, true, false, false);

    float alpha = 0.0f;
    float3 color = 0.0f;

    bool early_exit_1 = !surface.is_opaque();
    bool early_exit_2 = surface.roughness >= g_ssr_roughness_threshold;
    if (early_exit_1 || early_exit_2)
    {
        tex_uav[thread_id.xy] = float4(color, alpha);
        return;
    }

    // Skip pixels which are fully rough
    float2 hit_uv = -1.0f;

    // Compute reflection direction in view space
    float3 normal          = world_to_view(surface.normal, false);
    float3 position        = world_to_view(surface.position, true);
    float3 camera_to_pixel = normalize(position);
    float3 reflection      = normalize(reflect(camera_to_pixel, normal));
    float v_dot_r          = dot(-camera_to_pixel, reflection);

    // Don't trace rays which are facing the camera
    if (v_dot_r < 0.0f)
    { 
        hit_uv = trace_ray(thread_id.xy, position, reflection);
        alpha  = compute_alpha(thread_id.xy, hit_uv, v_dot_r);
    }

    // Sample scene color
    hit_uv          -= tex_velocity.SampleLevel(sampler_bilinear_clamp, hit_uv, 0).xy; // reproject
    bool valid_uv    = hit_uv.x != - 1.0f;
    bool valid_alpha = alpha != 0.0f;
    color            = (valid_uv && valid_alpha) ? tex.SampleLevel(sampler_bilinear_clamp, hit_uv, 0).rgb : 0.0f;

    tex_uav[thread_id.xy] = float4(color, alpha);
}
