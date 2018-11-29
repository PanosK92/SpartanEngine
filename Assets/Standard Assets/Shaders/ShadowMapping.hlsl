//= DEFINES ==============
#define CASCADES 3
#define PCF 2
#define PCF_DIM PCF / 2
#define PCF_UNROLL PCF * 2
//========================

// = INCLUDES ========
#include "Common.hlsl"
#include "Vertex.hlsl"
//====================

//= TEXTURES =============================
Texture2D texNormal : register(t0);
Texture2D texDepth : register(t1);
Texture2D lightDepthTex[3] : register(t2);
//========================================

//= SAMPLERS ===================================
SamplerState samplerPoint_clamp : register(s0);
SamplerState samplerLinear_clamp : register(s1);
//==============================================

//= CONSTANT BUFFERS =====================
cbuffer DefaultBuffer : register(b0)
{
    matrix mWorldViewProjectionOrtho;
    matrix mViewProjectionInverse;
	matrix mLightView;
    matrix mLightViewProjection[CASCADES];
    float2 shadowSplits;
	float2 biases;
    float3 lightDir;
    float shadowMapResolution;
    float farPlane;
    float doShadowMapping;
    float2 padding;
};
//========================================

//= STRUCTS ========================
struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};
//==================================

float2 texOffset(float2 shadowMapSize, int x, int y)
{
    return float2(x * 1.0f / shadowMapSize.x, y * 1.0f / shadowMapSize.y);
}

float depthTest(Texture2D shadowMap, SamplerState samplerState, float2 texCoords, float compare)
{
    float depth = shadowMap.Sample(samplerState, texCoords).r;
    return step(compare, depth);
}

float sampleShadowMap(Texture2D shadowMap, SamplerState samplerState, float2 size, float2 texCoords, float compare)
{
    float2 texelSize = float2(1.0f, 1.0f) / size;
    float2 f = frac(texCoords * size + 0.5f);
    float2 centroidUV = floor(texCoords * size + 0.5f) / size;

    float lb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 0.0f), compare);
    float lt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(0.0f, 1.0f), compare);
    float rb = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 0.0f), compare);
    float rt = depthTest(shadowMap, samplerState, centroidUV + texelSize * float2(1.0f, 1.0f), compare);
    float a = lerp(lb, lt, f.y);
    float b = lerp(rb, rt, f.y);
    float c = lerp(a, b, f.x);
	
    return c;
}

float random(float2 seed2) 
{
	float4 seed4 = float4(seed2.x, seed2.y, seed2.y, 1.0f);
	float dot_product = dot(seed4, float4(12.9898f, 78.233f, 45.164f, 94.673f));
    return frac(sin(dot_product) * 43758.5453);
}

float Technique_PCF(Texture2D shadowMap, SamplerState samplerState, float2 size, float2 texCoords, float compare)
{
	float amountLit = 0.0f;
	float count = 0.0f;
	
	[unroll(PCF_UNROLL)]
	for (float y = -PCF_DIM; y <= PCF_DIM; ++y)
	{
		[unroll(PCF_UNROLL)]
		for (float x = -PCF_DIM; x <= PCF_DIM; ++x)
		{
			amountLit += sampleShadowMap(shadowMap, samplerState, size, texCoords + texOffset(size, x, y), compare);
			count++;			
		}
	}
	return amountLit /= count;
}

float Technique_Poisson(Texture2D shadowMap, SamplerState samplerState, float4 pos, float compareDepth)
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
    
	uint samples = 8;
	float amountLit = 0.0f;
	[unroll(samples)]
	for (uint i = 0; i < samples; i++)
	{
		uint index = uint(samples * random(pos.xy * i)) % samples; // A pseudo-random number between 0 and 15, different for each pixel and each index
		amountLit += depthTest(shadowMap, samplerState, pos.xy + (poissonDisk[index] / packing), compareDepth);
	}	
	amountLit /= (float)samples;
}


float ShadowMapping(Texture2D shadowMap, SamplerState samplerState, float shadowMapResolution, float4 pos, float3 normal, float3 lightDir, float compareDepth)
{	
	// Re-homogenize position after interpolation
    pos.xyz /= pos.w;
	
	// If position is not visible to the light, dont illuminate it
    if( pos.x < -1.0f || pos.x > 1.0f ||
        pos.y < -1.0f || pos.y > 1.0f ||
        pos.z < 0.0f  || pos.z > 1.0f ) return 1.0f;

	// Transform clip space coords to texture space coords (-1:1 to 0:1)
	pos.x = pos.x / 2.0f + 0.5f;
	pos.y = pos.y / -2.0f + 0.5f;

	return Technique_PCF(shadowMap, samplerState, shadowMapResolution, pos.xy, compareDepth);
}

PixelInputType mainVS(Vertex_PosUv input)
{
    PixelInputType output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mWorldViewProjectionOrtho);
    output.uv 			= input.uv;
	
    return output;
}

float mainPS(PixelInputType input) : SV_TARGET
{
    float2 texCoord     = input.uv;
    float3 normal       = texNormal.Sample(samplerLinear_clamp, texCoord).rgb;
    float2 depthSample  = texDepth.Sample(samplerLinear_clamp, texCoord).rg;
    float depth_linear  = depthSample.r * farPlane;
    float depth_cs      = depthSample.g;
    float3 positionWS   = ReconstructPositionWorld(depth_cs, mViewProjectionInverse, texCoord);
	float bias			= biases.x;
	float normalBias	= biases.y;
		
    float shadow = 1.0f;
    if (doShadowMapping != 0.0f)
    {
		// Determine cascade to use
		int cascadeIndex 	= 2;
        cascadeIndex 		-= step(depth_linear, shadowSplits.x);
        cascadeIndex 		-= step(depth_linear, shadowSplits.y);
		
        float shadowTexel           = 1.0f / shadowMapResolution;
        float NdotL                 = dot(normal, lightDir);
        float cosAngle              = saturate(1.0f - NdotL);
		float biasCascadeFactor		= cascadeIndex + 1;
        float3 scaledNormalOffset   = normal * (normalBias * biasCascadeFactor * biasCascadeFactor) * cosAngle * shadowTexel;
		float4 worldPos 			= float4(positionWS + scaledNormalOffset, 1.0f);
		float compareDepth			= mul(worldPos, mLightView).z / farPlane;
		float4 lightPos 			= mul(worldPos, mLightViewProjection[cascadeIndex]) - bias;

        if (cascadeIndex == 0)
        {      
            shadow = ShadowMapping(lightDepthTex[0], samplerPoint_clamp, shadowMapResolution, lightPos, normal, lightDir, compareDepth);
        }
        else if (cascadeIndex == 1)
        {
            shadow = ShadowMapping(lightDepthTex[1], samplerPoint_clamp, shadowMapResolution, lightPos, normal, lightDir, compareDepth);
        }
        else if (cascadeIndex == 2)
        {
            shadow = ShadowMapping(lightDepthTex[2], samplerPoint_clamp, shadowMapResolution, lightPos, normal, lightDir, compareDepth);
        }
    }

    return shadow;
}