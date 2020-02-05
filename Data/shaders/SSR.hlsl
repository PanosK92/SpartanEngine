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

//= INCLUDES ============
#include "Common.hlsl"
#include "Dithering.hlsl"
//=======================

//= TEXTURES ==========================
Texture2D tex_normal    : register(t0);
Texture2D tex_depth     : register(t1);
//=====================================

static const uint g_ssr_max_steps               = 64;
static const uint g_ssr_binarySearchSteps       = 8;
static const float g_ssr_binarySearchThreshold  = 0.01f;
static const float g_ssr_ray_max_distance       = 20.0f;

bool binary_search(float3 ray_dir, inout float3 ray_pos, inout float2 ray_uv)
{
    float depth_buffer_z    = 0.0f;
    float depth_delta       = 1.0f;

    for (uint i = 0; i < g_ssr_binarySearchSteps; i++)
    {    
		ray_dir  *= 0.5f;
		ray_pos += -sign(depth_delta) * ray_dir;
        ray_uv    = project_uv(ray_pos, g_projection);

        depth_buffer_z  = get_linear_depth(tex_depth, ray_uv);
        depth_delta     = ray_pos.z - depth_buffer_z;
        
        if (abs(depth_delta) < g_ssr_binarySearchThreshold)
             return true;
    }

    return false;
}

bool ray_march(float3 ray_pos, float3 ray_dir, inout float2 ray_uv)
{
    for(uint i = 0; i < g_ssr_max_steps; i++)
    {
        // Step ray
        ray_pos += ray_dir;
        ray_uv  = project_uv(ray_pos, g_projection);

        float depth_buffer_z = get_linear_depth(tex_depth, ray_uv);

        [branch]
        if (ray_pos.z > depth_buffer_z)
            return binary_search(ray_dir, ray_pos, ray_uv);
    }

    return false;
}

float2 mainPS(Pixel_PosUv input) : SV_TARGET
{
    // Sample textures and compute world position
    float2 uv               = input.uv;
    float3 normal           = get_normal(tex_normal, uv);
    float depth             = get_depth(tex_depth, uv);    
    float3 position_world   = get_position_from_depth(depth, uv);
    
    // Compute reflection vector
    float3 normal_view  = normalize(mul(float4(normal, 0.0f), g_view).xyz);
    float3 ray_pos      = mul(float4(position_world, 1.0f), g_view).xyz;
    float3 ray_dir      = normalize(reflect(ray_pos, normal_view));
   
    // Reject if the reflection vector is pointing back at the viewer.
    // Attenuate reflections for angles between 60 degrees and 75 degrees, and drop all contribution beyond the (-60,60) degree range
	float3 camera_direction = normalize(mul(float4(g_camera_direction, 0.0f), g_view).xyz);
    float fade_camera       = 1 - smoothstep(0.25, 0.5, dot(-camera_direction, ray_dir));
    [branch]
	if (fade_camera <= 0)
		return 0.0f;

    // Compute ray step
    float step_length   = g_ssr_ray_max_distance / (float)g_ssr_max_steps;
    float3 ray_step     = ray_dir * step_length;
    
	// Apply dithering
	ray_pos += ray_step * dither_temporal_fallback(uv, 0.0f, 10.0f);
    
    float2 ray_hit_uv   = 0.0f;
    float fade_uv       = 0.0f;
    if (ray_march(ray_pos, ray_step, ray_hit_uv))
    {
        float2 edge_factor = float2(1, 1) - pow(saturate(abs(ray_hit_uv - float2(0.5f, 0.5f)) * 2), 8);
        fade_uv = ceil(saturate(min(edge_factor.x, edge_factor.y)));
    }
    
    // Reject if the reflection is pointing outside of the viewport
    ray_hit_uv *= fade_uv.x;
    
    return ray_hit_uv;
}