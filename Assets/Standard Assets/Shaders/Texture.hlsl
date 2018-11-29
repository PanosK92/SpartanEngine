// = INCLUDES ========
#include "Vertex.hlsl"
//====================

Texture2D tex			: register(t0);
SamplerState texSampler : register(s0);

cbuffer MiscBuffer : register(b0)
{
	matrix mTransform;
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
	return tex.Sample(texSampler, input.uv);
}