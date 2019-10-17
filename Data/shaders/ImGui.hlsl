#include "Common.hlsl"

Texture2D texture0;

struct PS_INPUT
{
	float4 position : SV_POSITION;
	float4 color 	: COLOR;
	float2 uv  		: TEXCOORD;
};

PS_INPUT mainVS(Vertex_Pos2dUvColor input)
{
	PS_INPUT output;
	output.position = mul(g_transform, float4(input.position.xy, 0.f, 1.f));
	output.color 	= input.color;
	output.uv  		= input.uv;
	return output;
}

float4 mainPS(PS_INPUT input) : SV_Target
{
	return input.color * texture0.Sample(sampler_bilinear_wrap, input.uv);
}