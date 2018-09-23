// = INCLUDES ========
#include "Common.hlsl"
//====================

cbuffer MiscBuffer
{
	matrix mTransform;
};

struct VS_Output
{
    float4 position	: SV_POSITION;
};

// Vertex Shader
VS_Output mainVS(Vertex_Pos input)
{
	input.position.w = 1.0f;
	VS_Output output;
    output.position = mul(input.position, mTransform);
	return output;
}

// Pixel Shader
float mainPS(VS_Output input) : SV_TARGET
{
	return input.position.z / input.position.w;
}