//= DEFINES =====================
#define CASCADES 3
#define PCF 2
#define PCF_DIM float(PCF) / 2.0f
//===============================

// = INCLUDES ========
#include "Common.hlsl"
#include "Vertex.hlsl"
//====================

//= TEXTURES ==================================
Texture2D texNormal 			: register(t0);
Texture2D texDepth 				: register(t1);
Texture2DArray lightDepthTex 	: register(t2);
//=============================================

//= SAMPLERS ==============================================
SamplerComparisonState  sampler_cmp_depth 	: register(s0);
SamplerState samplerLinear_clamp 			: register(s1);
//=========================================================

//= CONSTANT BUFFERS =====================
cbuffer DefaultBuffer : register(b1)
{
    matrix mViewProjectionInverse;
    matrix mLightViewProjection[CASCADES];
	float3 lightDir;
	float shadowMapResolution;
	float2 biases;
	float2 padding2;
};
//========================================

//= STRUCTS ========================
struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};
//==================================

float depthTest(float slice, float2 texCoords, float compare)
{
   return lightDepthTex.SampleCmpLevelZero(sampler_cmp_depth, float3(texCoords, slice), compare).r;
}

float random(float2 seed2) 
{
	float4 seed4 		= float4(seed2.x, seed2.y, seed2.y, 1.0f);
	float dot_product 	= dot(seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
    return frac(sin(dot_product) * 43758.5453);
}

float Technique_PCF(int cascadeIndex, float texel, float2 texCoords, float compare)
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
			amountLit 		+= depthTest(cascadeIndex, texCoords + offset, compare);
			count++;			
		}
	}
	return amountLit /= count;
}

float Technique_Poisson(int cascade, float2 texCoords, float compareDepth)
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
		uint index 	= uint(samples * random(texCoords * i)) % samples; // A pseudo-random number between 0 and 15, different for each pixel and each index
		amountLit 	+= depthTest(cascade, texCoords + (poissonDisk[index] / packing), compareDepth);
	}	

	amountLit /= (float)samples;
	return amountLit;
}

float ShadowMapping(int cascade, float4 positionCS, float texel, float3 normal, float3 lightDir, float bias)
{	
	// If the cascade is not covering this pixel, don't sample anything
	if( positionCS.x < -1.0f || positionCS.x > 1.0f || 
		positionCS.y < -1.0f || positionCS.y > 1.0f || 
		positionCS.z < 0.0f || positionCS.z > 1.0f ) return 1.0f;

	float2 texCoord 	= Project(positionCS);
	float compareDepth	= positionCS.z + bias;

	return Technique_PCF(cascade, texel, texCoord, compareDepth);
}

PixelInputType mainVS(Vertex_PosUv input)
{
    PixelInputType output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, g_mvp);
    output.uv 			= input.uv;
	
    return output;
}

float mainPS(PixelInputType input) : SV_TARGET
{
	// Compute some useful values
    float2 texCoord     		= input.uv;
    float3 normal       		= texNormal.Sample(samplerLinear_clamp, texCoord).rgb;
    float2 depthSample  		= texDepth.Sample(samplerLinear_clamp, texCoord).rg;
    float depth_cs      		= depthSample.g; 
	float bias					= biases.x;
	float normalBias			= biases.y;
	float texel          		= 1.0f / shadowMapResolution;
    float NdotL                 = dot(normal, lightDir);
    float cosAngle              = saturate(1.0f - NdotL);
    float3 scaledNormalOffset   = normal * normalBias * cosAngle * texel;
	float3 positionWS   		= ReconstructPositionWorld(depth_cs, mViewProjectionInverse, texCoord);
	float4 worldPos 			= float4(positionWS + scaledNormalOffset, 1.0f);
	
	// Compute clip space positions for each cascade
	float4 positonCS[3];
	positonCS[0] = mul(worldPos, mLightViewProjection[0]);
	positonCS[1] = mul(worldPos, mLightViewProjection[1]);
	positonCS[2] = mul(worldPos, mLightViewProjection[2]);
	
	// Compute position coordinates for each cascade
	float3 texCoords[3];
	texCoords[0] = positonCS[0].xyz * float3(0.5f, -0.5f, 0.5f) + 0.5f;
	texCoords[1] = positonCS[1].xyz * float3(0.5f, -0.5f, 0.5f) + 0.5f;
	texCoords[2] = positonCS[2].xyz * float3(0.5f, -0.5f, 0.5f) + 0.5f;
	
	// Determine cascade to use
	int cascade = -1;
	[unroll]
	for (int i = 2; i >= 0; i--)
	{
		cascade = any(texCoords[i] - saturate(texCoords[i])) ? cascade : i;
	}
	
	// If we are within a cascade, sample shadow maps
	[branch]
	if (cascade != -1)
	{
		float3 cascadeBlend = abs(texCoords[cascade] * 2 - 1);
		int2 cascades 		= int2(cascade, cascade + 1);
		float shadows[2] 	= { 1.0f, 1.0f };

		// Sample the main cascade	
		shadows[0] = ShadowMapping(cascades[0], positonCS[cascades[0]], texel, normal, lightDir, bias);
		
		[branch]
		if (cascades[1] <= 2)
		{
			shadows[1] = ShadowMapping(cascades[1], positonCS[cascades[1]], texel, normal, lightDir, bias);
		}

		// Blend cascades		
		return lerp(shadows[0], shadows[1], pow(max(cascadeBlend.x, max(cascadeBlend.y, cascadeBlend.z)), 4));
	}

    return 1.0f;
}