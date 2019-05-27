/*
Copyright(c) 2016-2019 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =========
#include "Common.hlsl"
//====================

Texture2D depthTexture 		: register(t0);
Texture2D environmentTex 	: register(t1);
SamplerState samplerLinear 	: register(s0);

cbuffer MiscBuffer : register(b1)
{
	matrix mWorld;
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
	float4 positionWS 	: POSITIONT0;
	float4 gridPos 		: POSITIONT1;
};

PixelInputType mainVS(Vertex_PosUvNorTan input)
{
    PixelInputType output;
    	
    input.position.w 	= 1.0f;	
	
	output.uv 			= input.uv;  
	output.position 	= mul(input.position, mWVP);
	output.positionWS 	= mul(input.position, mWorld);	
	output.gridPos 		= output.position;
	output.normal 		= normalize(mul(input.normal, 		(float3x3)mWorld)).xyz;
	output.tangent 		= normalize(mul(input.tangent, 		(float3x3)mWorld)).xyz;

	return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
	float3 camera_to_pixel 		= input.positionWS.xyz - cameraPos;
	float distance_transparent	= length(camera_to_pixel);
	camera_to_pixel 			= normalize(camera_to_pixel);
	float distance_opaque 		= depthTexture.Sample(samplerLinear, project(input.gridPos)).g;
	
	if (distance_opaque > distance_transparent)
		discard;
	
	float3 normal				= normalize(input.normal);
	float3 reflection 			= reflect(camera_to_pixel, normal);
	float3 environmentColor 	= environmentTex.Sample(samplerLinear, directionToSphereUV(reflection)).rgb;

	// Intensity of the specular light
	float specularHardness 	= 0;
	float3 H 				= normalize(lightDir - camera_to_pixel);
	float NdotH 			= dot(normal, H);
	float intensity 		= pow(saturate(NdotH), specularHardness);
		
    float alpha         = color.a;
    float3 finalColor   = saturate(color.rgb * intensity + environmentColor);

    return float4(environmentColor, alpha);
}