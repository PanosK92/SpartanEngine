// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D depthTexture 		: register(t0);
SamplerState samplerPoint 	: register(s0);

cbuffer MiscBuffer : register(b0)
{
	matrix mWorld;
	matrix mView;
	matrix mProjection;
	matrix mWVP;
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
	float4 gridPos 		: POSITIONT1;
};

// Vertex Shader
PixelInputType DirectusVertexShader(Vertex_PosUvTbn input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;	
	output.uv 			= input.uv;
	  
	output.position 	= mul(input.position, mWVP);
	output.positionWS 	= mul(input.position, mWVP);	
	output.gridPos 		= output.position;
	output.normal 		= normalize(mul(float4(input.normal, 0.0f), mWVP)).xyz;
	output.tangent 		= normalize(mul(float4(input.tangent, 0.0f), mWVP)).xyz;
	output.bitangent 	= normalize(mul(float4(input.bitangent, 0.0f), mWVP)).xyz;
		
	return output;
}

// Pixel Shader
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float2 projectDepthMapTexCoord;
	projectDepthMapTexCoord.x = input.gridPos.x / input.gridPos.w / 2.0f + 0.5f;
	projectDepthMapTexCoord.y = -input.gridPos.y / input.gridPos.w / 2.0f + 0.5f;
	
	float transparentGeometryDepth 	= input.position.z;
	float opaqueGeometryDepth 		= depthTexture.Sample(samplerPoint, projectDepthMapTexCoord).r;
	
	if (opaqueGeometryDepth < transparentGeometryDepth)
		discard;
		
	float3 viewDir 	= normalize(cameraPos - input.positionWS.xyz);
	float3 H 		= normalize(lightDir + viewDir);

	//Intensity of the specular light
	float specularHardness 	= roughness;
	float NdotH 			= dot(input.normal, H);
	float intensity 		= pow(saturate(NdotH), specularHardness);
		
	return color + float4(intensity, intensity, intensity, 0.0f);
}