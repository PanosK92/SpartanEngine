// = INCLUDES ========
#include "Common.hlsl"
#include "Vertex.hlsl"
//====================

cbuffer defaultBuffer : register(b1)
{
	matrix world;
	float3 axis;
	float padding2;
};

struct PixelInputType
{
	float4 position 	: SV_POSITION;
    float2 uv 			: TEXCOORD;
	float3 normal 		: NORMAL;
    float3 positionWS 	: POSITIONT_WS;
};

PixelInputType mainVS(Vertex_PosUvNorTan input)
{
    PixelInputType output;
    	
	input.position.w 	= 1.0f;
	output.positionWS 	= mul(input.position, world).xyz;
	output.position 	= mul(float4(output.positionWS, 1.0f), g_viewProjection);
	output.normal 		= mul(input.normal, (float3x3)world);
    output.uv 			= input.uv;
	
	return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
	float3 color_diffuse	= axis.xyz;
	float3 color_ambient 	= color_diffuse * 0.3f;
	float3 color_specular	= 1.0f;
	float3 lightPos 		= float3(10.0f, 10.0f, 10.0f);
	float3 normal 			= normalize(input.normal);
	float3 lightDir 		= normalize(lightPos - input.positionWS);
	float lambertian 		= max(dot(lightDir, normal), 0.0f);
	float specular 			= 0.0f;
	
	if(lambertian > 0.0f) 
	{
		// Blinn phong
		float3 viewDir	= normalize(g_camera_position - input.positionWS);
		float3 halfDir 	= normalize(lightDir + viewDir);
		float specAngle = max(dot(halfDir, normal), 0.0f);
		specular 		= pow(specAngle, 16.0f); 
	}
	
	return float4(color_ambient + lambertian * color_diffuse + color_specular * specular, 1.0f);
}