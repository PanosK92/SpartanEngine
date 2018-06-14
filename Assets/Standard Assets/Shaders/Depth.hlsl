// = INCLUDES ========
#include "Common.hlsl"
//====================

cbuffer MiscBuffer : register(b0)
{
	matrix mTransform;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
};

// Vertex Shader
PixelInputType DirectusVertexShader(Vertex_Pos input)
{
	PixelInputType output;
     
    input.position.w = 1.0f;
    output.position = mul(input.position, mTransform);
	
	return output;
}

// Pixel Shader
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	return input.position.z / input.position.w;
}