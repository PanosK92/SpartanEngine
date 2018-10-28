// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D depthTexture 		: register(t0);
TextureCube environmentTex 	: register(t1);
SamplerState samplerLinear 	: register(s0);

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
PixelInputType mainVS(Vertex_PosUvTbn input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;	
	
	output.uv 			= input.uv;  
	output.position 	= mul(input.position, mWVP);
	output.positionWS 	= mul(input.position, mWorld);	
	output.gridPos 		= output.position;
	output.normal 		= normalize(mul(input.normal, 		(float3x3)mWorld)).xyz;
	output.tangent 		= normalize(mul(input.tangent, 		(float3x3)mWorld)).xyz;
	output.bitangent 	= normalize(mul(input.bitangent,	(float3x3)mWorld)).xyz;
		
	return output;
}

// Pixel Shader
float4 mainPS(PixelInputType input) : SV_TARGET
{
	float2 projectDepthMapTexCoord;
	projectDepthMapTexCoord.x = input.gridPos.x / input.gridPos.w / 2.0f + 0.5f;
	projectDepthMapTexCoord.y = -input.gridPos.y / input.gridPos.w / 2.0f + 0.5f;
	
	float transparentGeometryDepth 	= input.position.z;
	float opaqueGeometryDepth 		= depthTexture.Sample(samplerLinear, projectDepthMapTexCoord).r;
	
	if (opaqueGeometryDepth > transparentGeometryDepth)
		discard;
	
	float3 normal				= normalize(input.normal);
	float3 view 				= normalize(cameraPos - input.positionWS.xyz);
	float3 reflection 			= reflect(-view, normal);
	float3 environmentColor 	= ToLinear(environmentTex.Sample(samplerLinear, reflection)).rgb;

	// Intensity of the specular light
	float specularHardness 	= roughness;
	float3 H 				= normalize(lightDir + view);
	float NdotH 			= dot(normal, H);
	float intensity 		= pow(saturate(NdotH), specularHardness);
		
    float alpha         = color.a;
    float3 finalColor   = saturate(color.rgb + environmentColor * intensity);

    return float4(finalColor, alpha);
}