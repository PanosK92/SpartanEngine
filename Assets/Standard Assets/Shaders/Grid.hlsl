Texture2D depthTexture 			: register(t0);
SamplerState samplerAnisoWrap 	: register(s0);

#include "Helper.hlsl"

//= Constant Buffers ===============
cbuffer MiscBuffer : register(b0)
{
	matrix mWVP;
};

//= Structs ========================
struct VertexInputType
{
    float4 position : POSITION;
    float4 color : COLOR;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
	float4 gridPos : POSITION;
};

//= Vertex Shader ======================================================================================
PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
    	
    input.position.w = 1.0f;
    output.position = mul(input.position, mWVP);
	output.gridPos = output.position;
	output.color = input.color;
	
	return output;
}

//= Pixel Shader =======================================================================================
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float2 projectDepthMapTexCoord;
	projectDepthMapTexCoord.x = input.gridPos.x / input.gridPos.w / 2.0f + 0.5f;
	projectDepthMapTexCoord.y = -input.gridPos.y / input.gridPos.w / 2.0f + 0.5f;
	
	float gridDepth = input.position.z / input.position.w;
	float depthMapValue = depthTexture.Sample(samplerAnisoWrap, projectDepthMapTexCoord).r;
	
	// If an object is in front of the grid, discard this grid pixel
	if (depthMapValue >= gridDepth) 
		discard;
	
	return float4(input.color.rgb, gridDepth - 0.01f);
}