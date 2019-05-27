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