// = INCLUDES ========
#include "Vertex.hlsl"
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

PixelInputType mainVS(Vertex_PosColor input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;
	output.linePos      = mul(input.position, view);
    output.position 	= mul(output.linePos, projection);
	output.color 		= input.color;
	
	return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
    float lineDepth     = input.linePos.z;
    float depthMapValue = depthTexture.Sample(samplerPoint, Project(input.linePos)).r;
	
	// If an object is in front of the grid, discard this grid pixel
    if (depthMapValue > lineDepth) 
        discard;
	
    return input.color;
}