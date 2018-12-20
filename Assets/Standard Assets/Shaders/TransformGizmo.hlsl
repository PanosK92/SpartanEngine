// = INCLUDES ========
#include "Vertex.hlsl"
//====================

cbuffer defaultBuffer : register(b0)
{
	matrix transform;	
	float3 axis;
	float padding;
};

struct PixelInputType
{
	float4 position 	: SV_POSITION;
    float2 uv 			: TEXCOORD;
};

PixelInputType mainVS(Vertex_PosUvTbn input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;	
	output.position 	= mul(input.position, transform);
    output.uv 			= input.uv;
	
	return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
	float4 color = float4(0, 0, 0, 1.0f);
	color.rgb = axis.xyz;
	
	return color;
}