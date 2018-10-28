// = INCLUDES ========
#include "Common.hlsl"
//====================

TextureCube tex_cubemap 	: register(t0);
Texture2D tex_depth			: register(t1);
SamplerState samplerLinear 	: register(s0);

cbuffer MiscBuffer : register(b0)
{
	matrix mTransform;
    matrix mViewProjectionInverse;
    float4 cameraPosWS;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};

struct PixelOutputType
{
	float4 albedo	: SV_Target0;
	float4 normal	: SV_Target1;
	float4 specular	: SV_Target2;
	float2 depth	: SV_Target3;
};

// Vertex Shader
PixelInputType mainVS(Vertex_PosUv input)
{
    PixelInputType output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
    output.uv 			= input.uv;
	
    return output;
}

// Pixel Shader
PixelOutputType mainPS(PixelInputType input)
{
	PixelOutputType output;

	// Extract useful values out of those samples
	float depth_expo 	= tex_depth.Sample(samplerLinear, input.uv).g;
	float3 worldPos 	= ReconstructPositionWorld(depth_expo, mViewProjectionInverse, input.uv);
    float3 viewDir 		= normalize(cameraPosWS.xyz - worldPos.xyz);
	float4 color 		= ToLinear(tex_cubemap.Sample(samplerLinear, -viewDir)); 

	output.albedo = color;
	return output;
}