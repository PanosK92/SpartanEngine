// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D depthTexture 		: register(t0);
SamplerState samplerPoint 	: register(s0);

cbuffer MiscBuffer : register(b0)
{
	matrix view;
	matrix projection;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 color 	: COLOR;
    float4 linePos  : LINE_POSITION;
};

// Vertex Shader
PixelInputType mainVS(Vertex_PosColor input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;
	output.linePos      = mul(input.position, view);
    output.position 	= mul(output.linePos, projection);
	output.color 		= input.color;
	
	return output;
}

// Pixel Shader
float4 mainPS(PixelInputType input) : SV_TARGET
{
	float2 projectDepthMapTexCoord;
    projectDepthMapTexCoord.x = input.linePos.x / input.linePos.w / 2.0f + 0.5f;
    projectDepthMapTexCoord.y = -input.linePos.y / input.linePos.w / 2.0f + 0.5f;
	
    float lineDepth     = input.linePos.z;
    float depthMapValue = depthTexture.Sample(samplerPoint, projectDepthMapTexCoord).r;
	
	// If an object is in front of the grid, discard this grid pixel
    if (depthMapValue > lineDepth) 
        discard;
	
    return input.color;
}