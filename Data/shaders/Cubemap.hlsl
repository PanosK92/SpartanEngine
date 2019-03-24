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

struct PixelOutputType
{
	float4 albedo	: SV_Target0;
	float4 normal	: SV_Target1;
	float4 specular	: SV_Target2;
	float2 depth	: SV_Target3;
};

// Vertex Shader
Pixel_PosUv mainVS(Vertex_PosUv input)
{
    Pixel_PosUv output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
    output.uv 			= input.uv;
	
    return output;
}

// Pixel Shader
PixelOutputType mainPS(Pixel_PosUv input)
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