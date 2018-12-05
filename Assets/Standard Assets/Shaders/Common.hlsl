//= INCLUDES =========
#include "Buffer.hlsl"
//====================

/*------------------------------------------------------------------------------
								[GLOBALS]
------------------------------------------------------------------------------*/
#define PI 3.1415926535897932384626433832795
#define EPSILON 2.7182818284

/*------------------------------------------------------------------------------
							[STRUCTS]
------------------------------------------------------------------------------*/
struct Material
{
	float3 albedo;
	float roughness;
	float metallic;
	float3 padding;
	float emission;
	float3 F0;
	float alpha;
};

struct Light
{
	float3 color;
	float intensity;
	float3 direction;
	float padding;
};

/*------------------------------------------------------------------------------
							[GAMMA CORRECTION]
------------------------------------------------------------------------------*/
float4 Degamma(float4 color)
{
	return pow(abs(color), 2.2f);
}

float3 Degamma(float3 color)
{
	return pow(abs(color), 2.2f);
}

float4 Gamma(float4 color)
{
	return pow(abs(color), 1.0f / 2.2f); 
}

float3 Gamma(float3 color)
{
	return pow(abs(color), 1.0f / 2.2f); 
}

/*------------------------------------------------------------------------------
								[PROJECT]
------------------------------------------------------------------------------*/
float2 Project(float4 value)
{
	float2 projected;
	projected.x = (value.x / value.w) / 2.0f + 0.5f;
    projected.y = (-value.y / value.w) / 2.0f + 0.5f;
	
	return projected;
}

/*------------------------------------------------------------------------------
								[PACKING]
------------------------------------------------------------------------------*/
float3 Unpack(float3 value)
{
	return value * 2.0f - 1.0f;
}

float3 Pack(float3 value)
{
	return value * 0.5f + 0.5f;
}

float2 Unpack(float2 value)
{
	return value * 2.0f - 1.0f;
}

float2 Pack(float2 value)
{
	return value * 0.5f + 0.5f;
}

/*------------------------------------------------------------------------------
								[NORMALS]
------------------------------------------------------------------------------*/
float3 TangentToWorld(float3 normalMapSample, float3 normalW, float3 tangentW, float3 bitangentW, float intensity)
{
	// normal intensity
	intensity			= clamp(intensity, 0.01f, 1.0f);
	normalMapSample.r 	*= intensity;
	normalMapSample.g 	*= intensity;
	
	// construct TBN matrix
	float3 N 		= normalW;
	float3 T 		= tangentW;
	float3 B 		= bitangentW;
	float3x3 TBN 	= float3x3(T, B, N); 
	
	// transform from tangent space to world space
	float3 bumpedNormal = normalize(mul(normalMapSample, TBN)); 
	
    return bumpedNormal;
}

float3 GetNormal(Texture2D texNormal, SamplerState samplerState, float2 texCoord)
{
	float3 normal = texNormal.Sample(samplerState, texCoord).rgb;
	return normalize(Unpack(normal));
}

/*------------------------------------------------------------------------------
						[POSITION RECONSTRUCTION]
------------------------------------------------------------------------------*/
float3 ReconstructPositionWorld(float depth, matrix viewProjectionInverse, float2 texCoord)
{	
	float x 			= texCoord.x * 2.0f - 1.0f;
	float y 			= (1.0f - texCoord.y) * 2.0f - 1.0f;
	float z				= depth;
    float4 pos_clip 	= float4(x, y, z, 1.0f);
	float4 pos_world 	= mul(pos_clip, viewProjectionInverse);
	
    return pos_world.xyz / pos_world.w;  
}

/*------------------------------------------------------------------------------
								[VELOCITY]
------------------------------------------------------------------------------*/
float2 GetVelocity(float2 texCoord, Texture2D texture_velocity, SamplerState sampler_bilinear)
{	
	// Dilate
	float2 velocity_tl 		= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-2, -2)).xy;
	float2 velocity_tr		= texture_velocity.Sample(sampler_bilinear, texCoord + float2(2, -2)).xy;
	float2 velocity_bl		= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-2, 2)).xy;
	float2 velocity_br 		= texture_velocity.Sample(sampler_bilinear, texCoord + float2(2, 2)).xy;
	float2 velocity_ce 		= texture_velocity.Sample(sampler_bilinear, texCoord).xy;
	float2 velocity_average = (velocity_tl+ velocity_tr + velocity_bl + velocity_br + velocity_ce) / 5.0f;

	// Unpack
	float2 velocity = pow(velocity_average, abs(1.0 / 3.0));
	velocity 		= Unpack(velocity);
	
	// Get speed in texels
	velocity /= g_texelSize;

	return velocity;
}

/*------------------------------------------------------------------------------
								[DEPTH]
------------------------------------------------------------------------------*/
float LinerizeDepth(float depth, float near, float far)
{
	return (far / (far - near)) * (1.0f - (near / depth));
}