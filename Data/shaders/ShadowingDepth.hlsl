// = INCLUDES ========
#include "Common.hlsl"
#include "Vertex.hlsl"
//====================

struct VS_Output
{
    float4 position	: SV_POSITION;
};

VS_Output mainVS(Vertex_Pos input)
{
	VS_Output output;

	input.position.w 	= 1.0f;	
    output.position 	= mul(input.position, g_mvp);
	
	return output;
}

float mainPS(VS_Output input) : SV_TARGET
{
	return input.position.z / input.position.w;
}