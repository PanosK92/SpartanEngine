// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D sourceTexture 	: register(t0);
SamplerState pointSampler 	: register(s0);

cbuffer MiscBuffer
{
	matrix mTransform;
};

struct VS_Output
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};

// Vertex Shader
VS_Output DirectusVertexShader(Vertex_PosUv input)
{
	VS_Output output;
     
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
	output.uv 			= input.uv;
	
	return output;
}

// Pixel Shader
float4 DirectusPixelShader(VS_Output input) : SV_TARGET
{
	float4 color 		= sourceTexture.Sample(pointSampler, input.uv);
    float brightness 	= dot(color.rgb, float3(0.299f, 0.587f, 0.114f));	
	return brightness > 0.8f ? color : float4(0.0f, 0.0f, 0.0f, 0.0f);
}