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

//= TEXTURES ===================================
Texture2D tex_albedo 			: register(t0);
Texture2D tex_normal 			: register(t1);
Texture2D tex_depth 			: register(t2);
Texture2D tex_material 			: register(t3);
Texture2D tex_lightDiffuse 		: register(t4);
Texture2D tex_lightSpecular 	: register(t5);
Texture2D tex_lightVolumetric 	: register(t6);
Texture2D tex_ssr 				: register(t7);
Texture2D tex_environment 		: register(t8);
Texture2D tex_lutIbl			: register(t9);
Texture2D tex_ssao				: register(t10);
//==============================================

// = INCLUDES ======
#include "BRDF.hlsl"
//==================

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float2 uv		= input.uv;
    float3 color	= float3(0, 0, 0);
	
	// Sample from textures
	float4 sample_albedo 		= tex_albedo.Sample(sampler_point_clamp, uv);
	float4 sample_normal 		= tex_normal.Sample(sampler_point_clamp, uv);
	float4 sample_material  	= tex_material.Sample(sampler_point_clamp, uv);
	float4 sample_diffuse 		= tex_lightDiffuse.Sample(sampler_point_clamp, uv);
	float4 sample_specular 		= tex_lightSpecular.Sample(sampler_point_clamp, uv);
	float4 sample_ssr 			= tex_ssr.Sample(sampler_point_clamp, uv);
	float sample_depth  		= tex_depth.Sample(sampler_point_clamp, uv).r;
	float sample_ssao			= tex_ssao.Sample(sampler_point_clamp, uv).r;
	float3 light_volumetric 	= tex_lightVolumetric.Sample(sampler_point_clamp, uv).rgb;

	// Post-process samples
    float4 albedo				= degamma(sample_albedo);  
	float3 normal				= normal_decode(sample_normal.xyz);	
	float directional_shadow 	= sample_diffuse.a;
	float light_received 		= sample_specular.a;
	bool ssr_available			= sample_ssr.a != 0.0f;
	bool is_sky 				= sample_material.a == 0.0f;

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
	float light_ambient_min = sample_ssao * g_directional_light_intensity * 0.025f; // no global illumination, so ambient light of the poor it is...
	float light_ambient		= g_directional_light_intensity * directional_shadow;
	light_ambient			= clamp(light_ambient, light_ambient_min, 1.0f);
	
	// Sky
	[branch]
    if (is_sky)
    {
        color += tex_environment.Sample(sampler_bilinear_clamp, directionToSphereUV(camera_to_pixel)).rgb;
        color *= clamp(g_directional_light_intensity / 5.0f, 0.01f, 1.0f);
		color += light_volumetric;
    }
	else
	{
		// Volumetric lighting
		color += light_volumetric;

		// IBL
		float3 reflectivity = 0.0f;
		float3 light_image_based = ImageBasedLighting(material, normal, camera_to_pixel, tex_environment, tex_lutIbl, reflectivity) * light_ambient;
	
		// SSR
		color += sample_ssr.rgb * reflectivity * light_received;
		
		// Emissive
		sample_specular.rgb += material.emissive * 200.0f;
	
		// Combine
		float3 light_sources = (sample_diffuse.rgb + sample_specular.rgb) * material.albedo;
		color += light_sources + light_image_based;	
	}
	
    return  float4(color, 1.0f);
}