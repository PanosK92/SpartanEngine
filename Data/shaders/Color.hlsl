// = INCLUDES ========
#include "Common.hlsl"
//====================

Pixel_PosColor mainVS(Vertex_PosColor input)
{
    Pixel_PosColor output;
    	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, g_mvp);
	output.color 		= input.color;
	
	return output;
}

float4 mainPS(Pixel_PosColor input) : SV_TARGET
{
    return input.color;
}