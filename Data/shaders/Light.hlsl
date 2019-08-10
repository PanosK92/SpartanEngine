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
							 
//= TEXTURES ==========================================
Texture2D tex_normal 					: register(t0);
Texture2D tex_material 					: register(t1);
Texture2D tex_depth 					: register(t2);
Texture2D tex_ssao 						: register(t3);
Texture2DArray light_depth_directional 	: register(t4);
TextureCube light_depth_point 			: register(t5);
Texture2D light_depth_spot 				: register(t6);
//=====================================================

//= SAMPLERS ==============================================
SamplerState sampler_point_clamp 			: register(s0);
SamplerComparisonState  sampler_cmp_depth 	: register(s1);
SamplerState samplerLinear_clamp 			: register(s2);
//=========================================================

//= BUFFERS ================================
#define cascade_count 4
cbuffer LightBuffer : register(b1)
{
	matrix 	light_view_projection[cascade_count];
	float3	color;
	float	intensity;
	float3	position;
	float	range;
	float3	direction;
    float 	angle;
	float 	bias;
	float 	normal_bias;
	float 	shadow_enabled;	
	float	padding_2;
};
//==========================================

//= INCLUDES ================
#include "Common.hlsl"       
#include "BRDF.hlsl"                 
#include "ShadowMapping.hlsl"
//===========================

struct PixelOutputType
{
	float4 diffuse	: SV_Target0;
	float4 specular	: SV_Target1;
};

PixelOutputType mainPS(Pixel_PosUv input)
{
	PixelOutputType light_out;
	light_out.diffuse 		= float4(0.0f, 0.0f, 0.0f, 0.0f);
	light_out.specular 		= float4(0.0f, 0.0f, 0.0f, 0.0f);
	float2 uv 				= input.uv;
	
	// Sample textures
	float4 normal_sample 	= tex_normal.Sample(sampler_point_clamp, uv);
	float4 material_sample  = tex_material.Sample(sampler_point_clamp, uv);
	float depth_sample   	= tex_depth.Sample(sampler_point_clamp, uv).r;
	float ssao_sample 		= tex_ssao.Sample(sampler_point_clamp, uv).r;
	
	// Ignore sky
    if (material_sample.a == 0.0f)
    {
		return light_out;
    }
	
	// Post-proces samples	
	float3 normal	= normal_decode(normal_sample.xyz);
	float occlusion = normal_sample.w;
	occlusion 		= min(occlusion, ssao_sample);
	float metallic 	= material_sample.g;

	// Compute camera to pixel vector
    float3 position_world 	= get_world_position_from_depth(depth_sample, g_viewProjectionInv, input.uv);
    float3 camera_to_pixel  = normalize(position_world - g_camera_position.xyz);
	
	// Fill in light struct
	Light light;
	light.color 	= color;
	light.position 	= position;
	light.direction	= direction;
	light.intensity = intensity;
	light.range 	= range;
	light.angle 	= angle;
	
	// Shadow
	float shadow = 1.0f;
	if (shadow_enabled)
	{
		shadow = Shadow_Map(uv, normal, depth_sample, position_world, bias, normal_bias, light);
	}

	// Mix shadow with ssao and modulate light's intensity
	shadow = min(shadow, occlusion);
	light.intensity *= shadow;
		
	#if DIRECTIONAL
		// Save shadows in the diffuse's alpha channel (used to modulate IBL later)
		light_out.diffuse.a = shadow;
	#endif
	
	#if POINT
		// Compute light direction
		light.direction = normalize(position_world - light.position);
	
        // Attunate
        float dist         = length(position_world - light.position);
        float attenuation  = saturate(1.0f - dist / range);
        light.intensity    *= attenuation * attenuation;
		
		// Erase light if there is no need to compute it
		light.intensity *=step(dist, range);
	#endif
	
	#if SPOT
		// Compute light direction
		float3 direction = normalize(position_world - light.position);
		
		// Attunate
        float cutoffAngle   = 1.0f - light.angle;      
        float dist          = length(position_world - light.position);
        float theta         = dot(direction, light.direction);
        float epsilon       = cutoffAngle - cutoffAngle * 0.9f;
        float attenuation 	= saturate((theta - cutoffAngle) / epsilon); // atteunate when approaching the outer cone
        attenuation         *= saturate(1.0f - dist / light.range);
        light.intensity 	*= attenuation * attenuation;
		
		// Erase light if there is no need to compute it
		light.intensity *= step(theta, cutoffAngle);
	#endif

	// Diffuse color for BRDFs which will allow for diffuse and specular light to be multiplied by albedo later
	float3 diffuse_color = float3(1,1,1);
	
	// Create material
	Material material;
	material.roughness  		= material_sample.r;
	material.metallic   		= material_sample.g;
	material.emissive   		= material_sample.b;
	material.F0 				= lerp(0.04f, diffuse_color, material.metallic);

	// Reflectance equation
	if (light.intensity > 0.0f)
	{
		// Compute some stuff
		float3 l		= -light.direction;
		float3 v 		= -camera_to_pixel;
		float3 h 		= normalize(v + l);
		float v_dot_h 	= saturate(dot(v, h));
		float n_dot_v 	= saturate(dot(normal, v));
		float n_dot_l 	= saturate(dot(normal, l));
		float n_dot_h 	= saturate(dot(normal, h));
		float3 radiance	= light.color * light.intensity * n_dot_l;

		// BRDF components
		float3 F 			= 0.0f;
		float3 cDiffuse 	= BRDF_Diffuse(diffuse_color, material, n_dot_v, n_dot_l, v_dot_h);	
		float3 cSpecular 	= BRDF_Specular(material, n_dot_v, n_dot_l, n_dot_h, v_dot_h, F);
				
		// Ensure energy conservation
		float3 kS 	= F;							// The energy of light that gets reflected - Equal to Fresnel
		float3 kD 	= 1.0f - kS; 					// Remaining energy, light that gets refracted			
		kD 			*= 1.0f - material.metallic; 	// Multiply kD by the inverse metalness such that only non-metals have diffuse lighting		
		
		light_out.diffuse.rgb	= kD * cDiffuse * radiance;
		light_out.specular.rgb	= cSpecular * radiance;
	}

	// Accumulate total light amount hitting that pixel (used to modulate ssr later)
	light_out.diffuse.a = light.intensity;

	return light_out;
}