// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D texHistory	: register(t0);
Texture2D texCurrent	: register(t1);
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
	float3 color_history 	= texHistory.Sample(texSampler, input.uv).rgb;
	float3 color_current 	= texCurrent.Sample(texSampler, input.uv).rgb;
	float3 color_result		= lerp(color_history, color_current, 0.05f);
	
	// Re-compute luma for FXAA
    float luma = dot(color_result, float3(0.299f, 0.587f, 0.114f));

	return float4(color_result, luma);
}