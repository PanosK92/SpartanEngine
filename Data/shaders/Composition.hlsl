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

//= TEXTURES ==============================
Texture2D tex_albedo 		: register(t0);
Texture2D tex_normal 		: register(t1);
Texture2D tex_depth 		: register(t2);
Texture2D tex_material 		: register(t3);
Texture2D tex_lightDiffuse 	: register(t4);
Texture2D tex_lightSpecular : register(t5);
Texture2D tex_ssr 			: register(t6);
Texture2D tex_environment 	: register(t7);
Texture2D tex_lutIbl		: register(t8);
//=========================================

//= SAMPLERS ======================================
SamplerState sampler_linear_clamp	: register(s0);
SamplerState sampler_trlinear_clamp	: register(s1);
SamplerState sampler_point_clamp	: register(s2);
//=================================================

// = INCLUDES ========
#include "Common.hlsl"
#include "IBL.hlsl"
//====================

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float2 uv		= input.uv;
    float3 color	= float3(0, 0, 0);
	
	// Sample from textures
	float4 sample_albedo 	= tex_albedo.Sample(sampler_point_clamp, uv);
	float4 sample_normal 	= tex_normal.Sample(sampler_point_clamp, uv);
	float4 sample_material  = tex_material.Sample(sampler_point_clamp, uv);
	float4 sample_diffuse 	= tex_lightDiffuse.Sample(sampler_point_clamp, uv);
	float3 sample_specular 	= tex_lightSpecular.Sample(sampler_point_clamp, uv).rgb;
	float3 sample_ssr 		= tex_ssr.Sample(sampler_point_clamp, uv).rgb;
	float sample_depth  	= tex_depth.Sample(sampler_point_clamp, uv).r;

	// Post-process samples
    float4 albedo				= degamma(sample_albedo);  
	float3 normal				= normal_decode(sample_normal.xyz);	
	float directional_shadow 	= sample_diffuse.a;
	float light_received 		= sample_diffuse.a;

	// Create material
    Material material;
    material.albedo     		= albedo.rgb;
    material.roughness  		= sample_material.r;
    material.metallic   		= sample_material.g;
    material.emissive   		= sample_material.b;
	material.F0 				= lerp(0.04f, material.albedo, material.metallic);

	// Compute common values  
    float3 worldPos 		= get_world_position_from_depth(sample_depth, g_viewProjectionInv, uv);
    float3 camera_to_pixel  = normalize(worldPos.xyz - g_camera_position.xyz);

	// Ambient light
	float light_ambient_min = 0.002f;
	float light_ambient		= g_directional_light_intensity * directional_shadow; // uber fake
	light_ambient			= clamp(light_ambient, light_ambient_min, 1.0f);
	
	// Sky
    if (sample_material.a == 0.0f)
    {
        color = tex_environment.Sample(sampler_linear_clamp, directionToSphereUV(camera_to_pixel)).rgb;
        color *= clamp(g_directional_light_intensity / 5.0f, 0.01f, 1.0f);
        return float4(color, 1.0f);
    }

	// IBL
	float3 light_image_based = ImageBasedLighting(material, normal, camera_to_pixel, tex_environment, tex_lutIbl, sampler_linear_clamp, sampler_trlinear_clamp) * light_ambient;

	// Emissive
	sample_specular += material.emissive * 10.0f;

	// Combine
	float3 light_sources = (sample_diffuse.rgb + sample_specular) * material.albedo;
	color = light_sources + light_image_based;

	// SSR
	float smoothness = 1.0f - material.roughness;
	color += sample_ssr * smoothness * light_received;

    return  float4(color, 1.0f);
}