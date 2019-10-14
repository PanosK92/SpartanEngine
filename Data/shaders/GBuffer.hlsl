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

//= INCLUDES ==================
#include "Common.hlsl"
#include "ParallaxMapping.hlsl"
//=============================

//= TEXTURES ===========================
Texture2D texAlbedo 	: register (t0);
Texture2D texRoughness 	: register (t1);
Texture2D texMetallic 	: register (t2);
Texture2D texNormal 	: register (t3);
Texture2D texHeight 	: register (t4);
Texture2D texOcclusion 	: register (t5);
Texture2D texEmission 	: register (t6);
Texture2D texMask 		: register (t7);
//======================================

//= SAMPLERS =============================
SamplerState samplerAniso : register (s0);
//========================================

cbuffer MaterialBuffer : register(b2)
{
	float4 materialAlbedoColor;	
	float2 materialTiling;
	float2 materialOffset;
    float materialRoughness;
    float materialMetallic;
    float materialNormalStrength;
	float materialHeight;
	float materialShadingMode;
	float3 padding2;
};

cbuffer ObjectBuffer : register(b3)
{		
	matrix mModel;
	matrix mMVP_current;
	matrix mMVP_previous;
};

struct PixelInputType
{
    float4 positionCS 			: SV_POSITION;
    float2 uv 					: TEXCOORD;
    float3 normal 				: NORMAL;
    float3 tangent 				: TANGENT;
	float4 positionVS 			: POSITIONT0;
    float4 positionWS 			: POSITIONT1;
	float4 positionCS_Current 	: SCREEN_POS;
	float4 positionCS_Previous 	: SCREEN_POS_PREVIOUS;
};

struct PixelOutputType
{
	float4 albedo	: SV_Target0;
	float4 normal	: SV_Target1;
	float4 material	: SV_Target2;
	float2 velocity	: SV_Target3;
};

PixelInputType mainVS(Vertex_PosUvNorTan input)
{
    PixelInputType output;
    
    input.position.w 			= 1.0f;	
	output.positionWS 			= mul(input.position, mModel);
    output.positionVS   		= mul(output.positionWS, g_view);
    output.positionCS   		= mul(output.positionVS, g_projection);
	output.positionCS_Current 	= mul(input.position, mMVP_current);
	output.positionCS_Previous 	= mul(input.position, mMVP_previous);
	output.normal 				= normalize(mul(input.normal, (float3x3)mModel)).xyz;	
	output.tangent 				= normalize(mul(input.tangent, (float3x3)mModel)).xyz;
    output.uv 					= input.uv;
	
	return output;
}

PixelOutputType mainPS(PixelInputType input)
{
	PixelOutputType g_buffer;

	float2 texCoords 		= float2(input.uv.x * materialTiling.x + materialOffset.x, input.uv.y * materialTiling.y + materialOffset.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= materialRoughness;
	float metallic 			= materialMetallic;
	float3 normal			= input.normal.xyz;
	float normal_intensity	= clamp(materialNormalStrength, 0.012f, materialNormalStrength);
	float emission			= 0.0f;
	float occlusion			= 1.0f;	
	
	//= VELOCITY ==============================================================================
	float2 position_current 	= (input.positionCS_Current.xy / input.positionCS_Current.w);
	float2 position_previous 	= (input.positionCS_Previous.xy / input.positionCS_Previous.w);
	float2 position_delta		= position_current - position_previous;
    float2 velocity 			= (position_delta - g_taa_jitterOffset) * float2(0.5f, -0.5f);
	//=========================================================================================

	// Make TBN
	float3x3 TBN = makeTBN(input.normal, input.tangent);

	#if HEIGHT_MAP
		// Parallax Mapping
		float height_scale 		= materialHeight * 0.04f;
		float3 camera_to_pixel 	= normalize(g_camera_position - input.positionWS.xyz);
		texCoords 				= ParallaxMapping(texHeight, samplerAniso, texCoords, camera_to_pixel, TBN, height_scale);
	#endif
	
	float mask_threshold = 0.6f;
	
	#if MASK_MAP
		float3 maskSample = texMask.Sample(samplerAniso, texCoords).rgb;
		if (maskSample.r <= mask_threshold && maskSample.g <= mask_threshold && maskSample.b <= mask_threshold)
			discard;
	#endif
	
	#if ALBEDO_MAP
		float4 albedo_sample = texAlbedo.Sample(samplerAniso, texCoords);
		if (albedo_sample.a <= mask_threshold)
			discard;
			
		albedo *= albedo_sample;
	#endif
	
	#if ROUGHNESS_MAP
		roughness *= texRoughness.Sample(samplerAniso, texCoords).r;
	#endif
	
	#if METALLIC_MAP
		metallic *= texMetallic.Sample(samplerAniso, texCoords).r;
	#endif
	
	#if NORMAL_MAP
		// Get tangent space normal and apply intensity
		float3 tangent_normal 	= normalize(unpack(texNormal.Sample(samplerAniso, texCoords).rgb));
		tangent_normal.xy 		*= saturate(normal_intensity);
		normal 					= normalize(mul(tangent_normal, TBN).xyz); // Transform to world space
	#endif

	#if OCCLUSION_MAP
		occlusion = texOcclusion.Sample(samplerAniso, texCoords).r;
	#endif
	
	#if EMISSION_MAP
		emission = texEmission.Sample(samplerAniso, texCoords).r;
	#endif

	// Write to G-Buffer
	g_buffer.albedo		= albedo;
	g_buffer.normal 	= float4(normal_encode(normal), occlusion);
	g_buffer.material	= float4(roughness, metallic, emission, materialShadingMode);
	g_buffer.velocity	= velocity;

    return g_buffer;
}