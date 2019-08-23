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

//= DEFINES =============================
#define PCF_SAMPLES 4
#define PCF_DIM float(PCF_SAMPLES) / 2.0f
//=======================================

//= INCLUDES ============
#include "Dithering.hlsl"
#include "SSCS.hlsl"
//=======================

float DepthTest_Directional(float slice, float2 tex_coords, float compare)
{
	return light_depth_directional.SampleCmpLevelZero(sampler_cmp_depth, float3(tex_coords, slice), compare).r;
}

float DepthTest_Point(float3 direction, float compare)
{
	return light_depth_point.SampleCmp(sampler_cmp_depth, direction, compare).r;
}

float DepthTest_Spot(float2 tex_coords, float compare)
{
	return light_depth_spot.SampleCmp(sampler_cmp_depth, tex_coords, compare).r;
}

float random(float2 seed2) 
{
	float4 seed4 		= float4(seed2.x, seed2.y, seed2.y, 1.0f);
	float dot_product 	= dot(seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
    return frac(sin(dot_product) * 43758.5453);
}

float Technique_Poisson(int cascade, float3 tex_coords, float compare, float2 dither)
{
	float packing = 700.0f; // how close together are the samples
	float2 poissonDisk[8] = 
	{
		float2(0.493393f, 0.394269f),
		float2(0.798547f, 0.885922f),
		float2(0.247322f, 0.92645f),
		float2(0.0514542f, 0.140782f),
		float2(0.831843f, 0.00955229f),
		float2(0.428632f, 0.0171514f),
		float2(0.015656f, 0.749779f),
		float2(0.758385f, 0.49617f)
	};
    
	uint samples 	= 16;
	float amountLit = 0.0f;
	[unroll]
	for (uint i = 0; i < samples; i++)
	{
		uint index 	= uint(samples * random(tex_coords.xy * i)) % samples; // A pseudo-random number between 0 and 15, different for each pixel and each index

		#if DIRECTIONAL
		amountLit 	+= DepthTest_Directional(cascade, tex_coords.xy + (poissonDisk[index] / packing), compare);
		#elif POINT
		amountLit 	+= DepthTest_Point(tex_coords, compare);
		#elif SPOT
		amountLit 	+= DepthTest_Spot(tex_coords.xy + (poissonDisk[index] / packing), compare);
		#endif
	}	

	amountLit /= (float)samples;
	return amountLit;
}

float Technique_PCF_2d(int cascade, float texel, float2 tex_coords, float compare, float2 dither)
{
	float amountLit 	= 0.0f;
	float count 		= 0.0f;
	float2 offset_scale = texel * dither;
	
	[unroll]
	for (float y = -PCF_DIM; y <= PCF_DIM; ++y)
	{
		[unroll]
		for (float x = -PCF_DIM; x <= PCF_DIM; ++x)
		{
			float2 offset 	= float2(x, y) * offset_scale;
			
			#if DIRECTIONAL
			amountLit 	+= DepthTest_Directional(cascade, tex_coords + offset, compare);
			#elif SPOT
			amountLit 	+= DepthTest_Spot(tex_coords + offset, compare);
			#endif
			
			count++;			
		}
	}
	return amountLit /= count;
}

float ShadowMap_Directional(int cascade, float4 positionCS, float texel, float bias, float2 dither)
{	
	// If the cascade is not covering this pixel, don't sample anything
	if( positionCS.x < -1.0f || positionCS.x > 1.0f || 
		positionCS.y < -1.0f || positionCS.y > 1.0f || 
		positionCS.z < 0.0f || positionCS.z > 1.0f ) return 1.0f;

	float2 tex_coord 	= project(positionCS);
	float compare_depth	= positionCS.z + bias;

	return Technique_PCF_2d(cascade, texel, tex_coord, compare_depth, dither);
}

float ShadowMap_Spot(float4 positionCS, float texel, float3 sample_direction, float bias, float2 dither)
{	
	// If the cascade is not covering this pixel, don't sample anything
	if( positionCS.x < -1.0f || positionCS.x > 1.0f || 
		positionCS.y < -1.0f || positionCS.y > 1.0f || 
		positionCS.z < 0.0f || positionCS.z > 1.0f ) return 1.0f;
		
	float compare_depth	= positionCS.z + bias;
	
	return Technique_PCF_2d(0, texel, sample_direction.xy, compare_depth, dither);
}

float Shadow_Map(float2 uv, float3 normal, float depth, float3 world_pos, float bias, float normal_bias, Light light)
{
    float n_dot_l               = dot(normal, normalize(-light.direction));
    float cos_angle             = saturate(1.0f - n_dot_l);
    float3 scaled_normal_offset = normal * cos_angle * g_shadow_texel_size * normal_bias * 10;
	float4 position_world   	= float4(world_pos + scaled_normal_offset, 1.0f);
	float shadow 				= 1.0f;
	float2 dither 				= Dither(uv + g_taa_jitterOffset).xy * 400;

	#if DIRECTIONAL
	{
		for (int cascade = 0; cascade < cascade_count; cascade++)
		{
			float4 pos 	= mul(position_world, light_view_projection[cascade]);
			float3 uv 	= pos.xyz * float3(0.5f, -0.5f, 0.5f) + 0.5f;
			
			// If a cascade was found, do shadow mapping
			[branch]
			if (is_saturated(uv))
			{	
				// Sample the primary cascade
				float shadow_primary = ShadowMap_Directional(cascade, pos, g_shadow_texel_size, bias, dither);

				// Edge threshold
				float edge = 0.8f; // 1.0f is where the cascade ends
				bool near_edge = !is_saturated(uv + (1.0f - edge));

				// Sample the secondary cascade
				float shadow_secondary = 1.0f;
				[branch]
				if (near_edge && cascade <= 2)
				{
					int cacade_secondary = cascade + 1;
					pos = mul(position_world, light_view_projection[cacade_secondary]);
					shadow_secondary = ShadowMap_Directional(cacade_secondary, pos, g_shadow_texel_size, bias, dither);
					
					// Blend cascades	
					shadow = min(shadow_primary, shadow_secondary);
				}
				else
				{
					shadow = shadow_primary;
				}

				break;
			}
		}
	}
	#elif POINT
	{
		float3 light_to_pixel_direction	= position_world.xyz - light.position;
		float light_to_pixel_distance = length(light_to_pixel_direction) / g_camera_far;
		
		[branch]
		if (light_to_pixel_distance < light.range)
		{
			float depth = light_depth_point.Sample(samplerLinear_clamp, light_to_pixel_direction).r;
			shadow = depth < light_to_pixel_distance ? 1.0f : 0.0f;

			//float compare = 1.0f - light_to_pixel_distance / range * (1.0f - bias); 
			//return light_depth_point.SampleCmpLevelZero(sampler_cmp_depth, light_to_pixel_direction, compare).r;
		}
	}
	#elif SPOT
	{
		shadow = 1.0f;
	}
	#endif

	// Screen space shadow
	if (screen_space_contact_shadows_enabled)
	{
		// Contact shadow
		float sscs = ScreenSpaceContactShadows(uv, light.direction);
		shadow = min(shadow, sscs);

		// Self shadow
		float self_shadow_smoothness = 15;
		float self_shadow = saturate(n_dot_l);
		shadow = min(shadow, 1.0f - pow(1.0f - self_shadow, self_shadow_smoothness));
	}
	


	return shadow;
}