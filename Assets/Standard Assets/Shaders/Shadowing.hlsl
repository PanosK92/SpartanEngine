//= DEFINES ======
#define CASCADES 3
//================

//= TEXTURES ===============================
Texture2D texNormal 		: register(t0);
Texture2D texDepth 			: register(t1);
Texture2D texNoise			: register(t2);
Texture2D lightDepthTex[3] 	: register (t3);
//==========================================

//= SAMPLERS ===============================
SamplerState samplerPoint 	: register (s0);
SamplerState samplerLinear 	: register (s1);
//==========================================

//= CONSTANT BUFFERS =====================
cbuffer DefaultBuffer : register(b0)
{
    matrix mWorldViewProjectionOrtho;
	matrix mViewProjectionInverse;
	matrix mLightViewProjection[CASCADES];
	float4 shadowSplits;	
	float3 lightDir;
	float shadowMapResolution;
    float2 resolution;
	float nearPlane;
    float farPlane;	
	float doShadowMapping;	
	float3 padding;
};
//========================================

// = INCLUDES ===============
#include "Common.hlsl"
#include "ShadowMapping.hlsl"
#include "SSAO.hlsl"
//===========================

//= STRUCTS ========================
struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};
//==================================

PixelInputType DirectusVertexShader(Vertex_PosUv input)
{
    PixelInputType output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mWorldViewProjectionOrtho);
    output.uv 			= input.uv;
	
    return output;
}

float2 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float2 texCoord 	= input.uv;
	float3 normal 		= texNormal.Sample(samplerPoint, texCoord).rgb;
	float3 depthSample 	= texDepth.Sample(samplerPoint, texCoord).rgb;
	float depth 		= texDepth.Sample(samplerPoint, texCoord).r;
	float3 positionWS 	= ReconstructPositionWorld(depth, mViewProjectionInverse, texCoord);
	
	
	//== SSAO =================================
	float ssao = SSAO(texCoord, samplerLinear);
	//=========================================
	
	//= SHADOW MAPPING ===========================================================================	
	float shadow 		= 1.0f;
	int cascadeIndex 	= 0; // assume 1st cascade as default
	if (doShadowMapping != 0.0f)
	{
		cascadeIndex += step(depth, shadowSplits.x); // test 2nd cascade
		cascadeIndex += step(depth, shadowSplits.y); // test 3rd cascade
		
		float cascadeCompensation	= (cascadeIndex + 1.0f) * 2.0f; // the further the cascade, the more ugly under sharp angles, damn
		float shadowTexel 			= 1.0f / shadowMapResolution;
		float bias 					= 2.0f * shadowTexel * cascadeCompensation;
		float normalOffset 			= 70.0f * cascadeCompensation;
		float NdotL 				= dot(normal, lightDir);
		float cosAngle 				= saturate(1.0f - NdotL);
		float3 scaledNormalOffset 	= normal * (normalOffset * cosAngle * shadowTexel);
		
		// Perform shadow mapping only if the polygons are back-faced
		// from the light, to avoid self-shadowing artifacts
		//if (NdotL < 0.0f)
		{
			if (cascadeIndex == 0)
			{
				float4 lightPos = mul(float4(positionWS + scaledNormalOffset, 1.0f), mLightViewProjection[0]);
				shadow	= ShadowMapping(lightDepthTex[0], samplerPoint, shadowMapResolution, lightPos, normal, lightDir, bias);
			}
			else if (cascadeIndex == 1)
			{
				float4 lightPos = mul(float4(positionWS + scaledNormalOffset, 1.0f), mLightViewProjection[1]);
				shadow	= ShadowMapping(lightDepthTex[1], samplerPoint, shadowMapResolution, lightPos, normal, lightDir, bias);
			}
			else if (cascadeIndex == 2)
			{
				float4 lightPos = mul(float4(positionWS + scaledNormalOffset, 1.0f), mLightViewProjection[2]);
				shadow	= ShadowMapping(lightDepthTex[2], samplerPoint, shadowMapResolution, lightPos, normal, lightDir, bias);
			}
		}
	}	
	//============================================================================================

    return float2(shadow, ssao);
}