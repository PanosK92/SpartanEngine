// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D depthTexture 		: register(t0);
SamplerState samplerPoint 	: register(s0);

cbuffer MiscBuffer : register(b0)
{
	matrix mTransform;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 color 	: COLOR;
	float4 gridPos 	: GRID_POSITION;
};

// Vertex Shader
PixelInputType DirectusVertexShader(Vertex_PosColor input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
	output.gridPos 		= output.position;
	output.color 		= input.color;
	
	return output;
}

// Pixel Shader
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float2 projectDepthMapTexCoord;
	projectDepthMapTexCoord.x = input.gridPos.x / input.gridPos.w / 2.0f + 0.5f;
	projectDepthMapTexCoord.y = -input.gridPos.y / input.gridPos.w / 2.0f + 0.5f;
	
	float gridDepth = input.position.z;
	float depthMapValue = depthTexture.Sample(samplerPoint, projectDepthMapTexCoord).r;
	
	// If an object is in front of the grid, discard this grid pixel
	if (depthMapValue < gridDepth) 
		discard;
	
	float alpha = (1.0f - gridDepth) - 0.0025f;

	return float4(input.color.rgb, saturate(alpha));
}