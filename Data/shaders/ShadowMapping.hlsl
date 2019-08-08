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
#define PCF_SAMPLES 2
#define PCF_DIM float(PCF_SAMPLES) / 2.0f
//=======================================

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

float Technique_Poisson(int cascade, float3 tex_coords, float compare)
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

float Technique_PCF_2d(int cascade, float texel, float2 tex_coords, float compare)
{
	float amountLit = 0.0f;
	float count 	= 0.0f;
	
	[unroll]
	for (float y = -PCF_DIM; y <= PCF_DIM; ++y)
	{
		[unroll]
		for (float x = -PCF_DIM; x <= PCF_DIM; ++x)
		{
			float2 offset 	= float2(x, y) * texel;
			
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

float ShadowMap_Directional(int cascade, float4 positionCS, float texel, float bias)
{	
	// If the cascade is not covering this pixel, don't sample anything
	if( positionCS.x < -1.0f || positionCS.x > 1.0f || 
		positionCS.y < -1.0f || positionCS.y > 1.0f || 
		positionCS.z < 0.0f || positionCS.z > 1.0f ) return 1.0f;

	float2 tex_coord 	= project(positionCS);
	float compare_depth	= positionCS.z + bias;

	return Technique_PCF_2d(cascade, texel, tex_coord, compare_depth);
}

float ShadowMap_Spot(float4 positionCS, float texel, float3 sample_direction, float bias)
{	
	// If the cascade is not covering this pixel, don't sample anything
	if( positionCS.x < -1.0f || positionCS.x > 1.0f || 
		positionCS.y < -1.0f || positionCS.y > 1.0f || 
		positionCS.z < 0.0f || positionCS.z > 1.0f ) return 1.0f;
		
	float compare_depth	= positionCS.z + bias;
	
	return Technique_PCF_2d(0, texel, sample_direction.xy, compare_depth);
}

float Shadow_Map(float3 normal, float depth, float3 world_pos, float bias, float normal_bias, Light light)
{
    float n_dot_l               = dot(normal, normalize(-light.direction));
    float cos_angle             = saturate(1.0f - n_dot_l);
    float3 scaled_normal_offset = normal * normal_bias * cos_angle * g_shadow_texel_size;
	float4 position_world   	= float4(world_pos + scaled_normal_offset, 1.0f);
	float self_shadow 			= saturate(n_dot_l);
	float shadow 				= 1.0f;
	
	// Determine cascade to use
	#if DIRECTIONAL
	{
		// Compute projection positions for each cascade
		float4 positonCS[3];
		positonCS[0] = mul(position_world, light_view_projection[0]);
		positonCS[1] = mul(position_world, light_view_projection[1]);
		positonCS[2] = mul(position_world, light_view_projection[2]);
		
		// Compute position coordinates for each cascade
		float3 tex_coords[3];
		tex_coords[0] = positonCS[0].xyz * float3(0.5f, -0.5f, 0.5f) + 0.5f;
		tex_coords[1] = positonCS[1].xyz * float3(0.5f, -0.5f, 0.5f) + 0.5f;
		tex_coords[2] = positonCS[2].xyz * float3(0.5f, -0.5f, 0.5f) + 0.5f;
		
		int cascade = -1;
		[unroll]
		for (int i = 2; i >= 0; i--)
		{
			cascade = any(tex_coords[i] - saturate(tex_coords[i])) ? cascade : i;
		}
		
		// If we are within a cascade, sample shadow maps
		[branch]
		if (cascade != -1)
		{
			float3 cascadeBlend = abs(tex_coords[cascade] * 2 - 1);

				float a = ShadowMap_Directional(0, positonCS[0], g_shadow_texel_size, bias);
				float b = ShadowMap_Directional(1, positonCS[1], g_shadow_texel_size, bias);
				float c = ShadowMap_Directional(2, positonCS[2], g_shadow_texel_size, bias);
	
				// Blend cascades		
				shadow = min(min(a, b),c);
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

	// Apply micro shadows
	shadow = min(shadow, self_shadow);
	shadow *= shadow / 0.02f;

	return shadow;
}