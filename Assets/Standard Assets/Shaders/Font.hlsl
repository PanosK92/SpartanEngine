// = INCLUDES ========
#include "Vertex.hlsl"
//====================

Texture2D textureAtlas 	: register(t0);
SamplerState texSampler : register(s0);

cbuffer MiscBuffer : register(b0)
{
	matrix mTransform;
	float4 color;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};

PixelInputType mainVS(Vertex_PosUv input)
{
    PixelInputType output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
    output.uv 			= input.uv;
	
    return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
	float4 finalColor = float4(0.0f, 0.0f, 0.0f, 1.0f);
	
	// Sample text from texture atlas
	finalColor.r = textureAtlas.Sample(texSampler, input.uv).r;
	finalColor.g = finalColor.r;
	finalColor.b = finalColor.r;
	finalColor.a = finalColor.r;
	
	// Color it
	finalColor *= color;
	
	return finalColor;
}