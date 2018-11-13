// = INCLUDES ========
#include "Common.hlsl"
//====================

cbuffer MiscBuffer
{
	matrix mWorldView;
	matrix mWorldViewProjection;
	float farPlane;
	float3 padding;
};

struct VS_Output
{
    float4 position	: SV_POSITION;
	float4 position_view : POSITION_0;
};

VS_Output mainVS(Vertex_Pos input)
{
	VS_Output output;

	input.position.w 		= 1.0f;	
    output.position 		= mul(input.position, mWorldViewProjection);
	output.position_view	= mul(input.position, mWorldView);
	
	return output;
}

float mainPS(VS_Output input) : SV_TARGET
{
	return input.position_view.z / farPlane;
}