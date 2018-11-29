// = INCLUDES ========
#include "Vertex.hlsl"
//====================

cbuffer MiscBuffer : register(b0)
{
	matrix transform;
	float3 axis;
	float padding;
};

struct PixelInputType
{
	float4 position 	: SV_POSITION;
    float2 uv 			: TEXCOORD;
    float3 normal 		: NORMAL;
    float3 tangent 		: TANGENT;
	float3 bitangent 	: BITANGENT;
};

PixelInputType mainVS(Vertex_PosUvTbn input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;	
	output.position 	= mul(input.position, transform);
	output.normal 		= normalize(mul(float4(input.normal, 0.0f), transform)).xyz;	
	output.tangent 		= normalize(mul(float4(input.tangent, 0.0f), transform)).xyz;
	output.bitangent 	= normalize(mul(float4(input.bitangent, 0.0f), transform)).xyz;
    output.uv 			= input.uv;
	
	return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
	float4 color = float4(0, 0, 0, 1.0f);
	color.rgb = axis.xyz;
	
	return color;
}