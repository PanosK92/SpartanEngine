// = INCLUDES ========
#include "Common.hlsl"
//====================

Pixel_Pos mainVS(Vertex_Pos input)
{
	Pixel_Pos output;

	input.position.w 	= 1.0f;	
    output.position 	= mul(input.position, g_mvp);
	
	return output;
}

float mainPS(Pixel_Pos input) : SV_TARGET
{
	return input.position.z / input.position.w;
}