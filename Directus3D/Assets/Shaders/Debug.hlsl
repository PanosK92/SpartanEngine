Texture2D depthTexture 			: register(t0);
SamplerState samplerAnisoWrap 	: register(s0);

#include "Helper.hlsl"

//= Constant Buffers ===============
cbuffer MiscBuffer : register(b0)
{
	matrix mWorldViewProjection;
	matrix mViewProjection;
};

//= Structs ========================
struct VertexInputType
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
	float4 colliderPos : WHATEVER;
};

//= Vertex Shader ======================================================================================
PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
    	
    input.position.w = 1.0f;
    output.position = mul(input.position, mWorldViewProjection);
	output.colliderPos = mul(input.position, mViewProjection);
	output.color = input.color;
	
	return output;
}

//= Pixel Shader =======================================================================================
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float2 projectDepthMapTexCoord;
	projectDepthMapTexCoord.x = input.colliderPos.x / input.colliderPos.w / 2.0f + 0.5f;
	projectDepthMapTexCoord.y = -input.colliderPos.y / input.colliderPos.w / 2.0f + 0.5f;
	
	float colliderDepthValue = input.position.z / input.position.w;
	float depthMapValue = depthTexture.Sample(samplerAnisoWrap, projectDepthMapTexCoord).r;
	
	if (depthMapValue >= colliderDepthValue) // If an object is in front of the collider
		discard;
	
	return input.color;
}