/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= TEXTURES ==========================
Texture2D tex_normal 	: register(t0);
Texture2D tex_depth  	: register(t1);
Texture2D tex_material  : register(t2);
Texture2D tex_frame  	: register(t3);
//=====================================

//= SAMPLERS ======================================
SamplerState sampler_point_clamp 	: register(s0);
SamplerState sampler_linear_clamp 	: register(s1);
//=================================================


static const int g_steps 					= 8;
static const int g_binarySearchSteps 		= 8;
static const float g_binarySearchThreshold 	= 0.01f;

bool binary_search(float3 ray_dir, inout float3 ray_pos, inout float2 ray_uv)
{
	for (int i = 0; i < g_binarySearchSteps; i++)
	{	
		ray_uv 				= project(ray_pos, g_projection);
		float depth 		= get_linear_depth(tex_depth, sampler_linear_clamp, ray_uv);
		float depth_delta 	= ray_pos.z - depth;

		if (depth_delta <= 0.0f)
			ray_pos += ray_dir;

		ray_dir *= 0.5f;
		ray_pos -= ray_dir;
	}

	ray_uv 				= project(ray_pos, g_projection);
	float depth_sample 	= get_linear_depth(tex_depth, sampler_linear_clamp, ray_uv);
	float depth_delta 	= ray_pos.z - depth_sample;

	return abs(depth_delta) < g_binarySearchThreshold;
}

bool ray_march(float3 ray_pos, float3 ray_dir, inout float2 ray_uv)
{
	for(int i = 0; i < g_steps; i++)
	{
		// Step ray
		ray_pos += ray_dir;
		ray_uv 	= project(ray_pos, g_projection);

		// Compare depth
		float depth_sampled = get_linear_depth(tex_depth, sampler_linear_clamp, ray_uv);
		float depth_delta 	= ray_pos.z - depth_sampled;
		
		[branch]
		if (depth_delta > 0.0f)
			return binary_search(ray_dir, ray_pos, ray_uv);
	}

	return false;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
	// Sample textures and compute world position
    float2 uv				= input.uv;
	float3 normal 			= normal_decode(tex_normal.Sample(sampler_point_clamp, uv).xyz);	
	float roughness 		= tex_material.Sample(sampler_point_clamp, uv).r;
	float depth  			= tex_depth.Sample(sampler_point_clamp, uv).r;
    float3 position_world 	= get_world_position_from_depth(depth, g_viewProjectionInv, uv);

	// Convert everything to view space
	float3 view_pos			= mul(float4(position_world, 1.0f), g_view).xyz;
	float3 view_normal		= normalize(mul(float4(normal, 0.0f), g_view).xyz);
	float3 view_reflection 	= normalize(reflect(view_pos, view_normal));
	
	float2 ray_hit_uv = 0.0f;
	if (ray_march(view_pos, view_reflection, ray_hit_uv))
	{
		float2 edgeFactor = float2(1, 1) - pow(saturate(abs(ray_hit_uv - float2(0.5f, 0.5f)) * 2), 8);
		float fade_screen = saturate(min(edgeFactor.x, edgeFactor.y));
		return tex_frame.Sample(sampler_linear_clamp, ray_hit_uv) * fade_screen;
	}
	
	return 0.0f;
}