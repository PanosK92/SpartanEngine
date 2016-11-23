//= TEXTURES ===============================
Texture2D texNormal 		: register (t0);
Texture2D texDepth 			: register (t1);
Texture2D texNoise			: register (t2);
Texture2D lightDepthTex[3] 	: register (t3);
//==========================================

//= SAMPLERS =============================
SamplerState samplerAniso : register (s0);
//========================================

//= DEFINES ======
#define CASCADES 3
//================

//= BUFFERS ==================================
cbuffer DefaultDuffer : register(b0)
{
	matrix mWorld;
    matrix mWorldViewProjection;	
	matrix mLightViewProjection[CASCADES];
	float4 shadowSplits;		
	float3 lightDir;
	float shadowBias;
	float shadowMapResolution;
	float shadowMappingQuality;	
	float receiveShadows;
	float padding;
	float2 viewport;
	float2 padding2;
};
//===========================================

// = INCLUDES ===============
#include "Helper.hlsl"
#include "ShadowMapping.hlsl"
#include "SSAO.hlsl"
//===========================

//= STRUCTS =================================
struct VertexInputType
{
    float4 position : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};

struct PixelInputType
{
    float4 positionCS : SV_POSITION;
    float4 positionWS : POSITIONT1;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
};
//===========================================

PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
    
    input.position.w 	= 1.0f;	
	output.positionWS 	= mul(input.position, mWorld);
	output.positionCS 	= mul(input.position, mWorldViewProjection);	
	output.normal 		= normalize(mul(input.normal, mWorld));	
	
	return output;
}

float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float shadowing 		= 0.0f;
	int cascadeIndex 		= 0;	
	
	//= SHADOW MAPPING ===================================================================
	if (receiveShadows == 1.0f && shadowMappingQuality != 0.0f)
	{
		float3 normal = texNormal.Sample(samplerAniso, input.uv);
		normal = normalize(UnpackNormal(normal));

		float depth 			= input.positionCS.z / input.positionCS.w;
		
		int cascadeIndex 	= 0;		
		if (depth < shadowSplits.x)
			cascadeIndex = 2;		
		else if (depth < shadowSplits.y)
			cascadeIndex = 1;		
		
		if (cascadeIndex == 0)
		{
			float4 lightPos = mul(input.positionWS, mLightViewProjection[0]);
			shadowing		= ShadowMapping(lightDepthTex[0], samplerAniso, shadowMapResolution, shadowMappingQuality, lightPos, shadowBias, normal, lightDir);
		}
		else if (cascadeIndex == 1)
		{
			float4 lightPos = mul(input.positionWS, mLightViewProjection[1]);
			shadowing		= ShadowMapping(lightDepthTex[1], samplerAniso, shadowMapResolution, shadowMappingQuality, lightPos, shadowBias, normal, lightDir);
		}
		else if (cascadeIndex == 2)
		{
			float4 lightPos = mul(input.positionWS, mLightViewProjection[2]);
			shadowing		= ShadowMapping(lightDepthTex[2], samplerAniso, shadowMapResolution, shadowMappingQuality, lightPos, shadowBias, normal, lightDir);
		}
	}
	
	// Uncomment to vizualize cascade splits 
	/*if (cascadeIndex == 0)
		output.albedo		= float4(1,0,0,1);
	if (cascadeIndex == 1)
		output.albedo		= float4(0,1,0,1);
	if (cascadeIndex == 2)
		output.albedo		= float4(0,0,1,1);*/
		
	//= SSAO ==============================================================================
		
    return float4(shadowing, shadowing, shadowing, 1.0f);
}