// = INCLUDES ========
#include "Common.hlsl"
//====================

cbuffer MiscBuffer : register(b0)
{
	matrix transform;
	matrix world;
	float4 color;
	float3 cameraPos;
	float roughness;
	float3 lightDir;
	float padding2;
};

struct PixelInputType
{
	float4 position 	: SV_POSITION;
    float2 uv 			: TEXCOORD;
    float3 normal 		: NORMAL;
    float3 tangent 		: TANGENT;
	float3 bitangent 	: BITANGENT;
	float4 positionWS 	: POSITIONT0;
};

// Vertex Shader
PixelInputType DirectusVertexShader(Vertex_PosUvTbn input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;	
	output.position 	= mul(input.position, transform);
	output.positionWS 	= mul(input.position, world);
	output.normal 		= normalize(mul(float4(input.normal, 0.0f), transform)).xyz;	
	output.tangent 		= normalize(mul(float4(input.tangent, 0.0f), transform)).xyz;
	output.bitangent 	= normalize(mul(float4(input.bitangent, 0.0f), transform)).xyz;
    output.uv 			= input.uv;
	
	return output;
}

// Pixel Shader
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float3 viewDir 	= normalize(cameraPos - input.positionWS.xyz);
	float3 H 		= normalize(lightDir + viewDir);

	//Intensity of the specular light
	float specularHardness 	= roughness;
	float NdotH 			= dot(input.normal, H);
	float intensity 		= pow(saturate(NdotH), specularHardness);
		
	return color + float4(intensity, intensity, intensity, 0.0f);
}