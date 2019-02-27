// = INCLUDES ========
#include "Vertex.hlsl"
#include "Common.hlsl"
//====================

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 color 	: COLOR;
};

PixelInputType mainVS(Vertex_PosColor input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, g_mvp);
	output.color 		= input.color;
	
	return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
    return input.color;
}