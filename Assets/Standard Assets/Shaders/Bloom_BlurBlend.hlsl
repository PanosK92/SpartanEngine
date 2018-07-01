// = INCLUDES ===============
#include "Common.hlsl"
#include "Common_Passes.hlsl"
//===========================

Texture2D sourceTexture 	: register(t0);
Texture2D brightTexture 	: register(t1);
SamplerState pointSampler 	: register(s0);

cbuffer MiscBuffer
{
	matrix mTransform;
	float2 resolution;
    float2 padding;
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
	float exposure 		= 1.0f;
	float2 texelSize 	= float2(1.0f / resolution.x, 1.0f / resolution.y);
	float4 color 		= sourceTexture.Sample(pointSampler, input.uv);
	float4 brightColor 	= BlurPass(input.uv, texelSize, 40, brightTexture, pointSampler);
	
	// Additive blending
	color += brightColor * exposure;
		
	return color;
}