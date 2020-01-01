/*
Copyright(c) 2016-2020 Panos Karabelas

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

// = INCLUDES ========
#include "Common.hlsl"
//====================

TextureCube tex_cubemap 	: register(t0);
Texture2D tex_depth			: register(t1);
SamplerState samplerLinear 	: register(s0);

cbuffer MiscBuffer : register(b0)
{
	matrix mTransform;
    matrix mViewProjectionInverse;
    float4 cameraPosWS;
};

struct PixelOutputType
{
	float4 albedo	: SV_Target0;
	float4 normal	: SV_Target1;
	float4 specular	: SV_Target2;
	float2 depth	: SV_Target3;
};

// Vertex Shader
Pixel_PosUv mainVS(Vertex_PosUv input)
{
    Pixel_PosUv output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, mTransform);
    output.uv 			= input.uv;
	
    return output;
}

// Pixel Shader
PixelOutputType mainPS(Pixel_PosUv input)
{
	PixelOutputType output;

	// Extract useful values out of those samples
	float depth 			= tex_depth.Sample(samplerLinear, input.uv).r;
	float3 position_world 	= ReconstructPositionWorld(depth, mViewProjectionInverse, input.uv);
    float3 camera_to_pixel 	= normalize(position_world.xyz - cameraPosWS.xyz);
	float4 color 			= ToLinear(tex_cubemap.Sample(samplerLinear, camera_to_pixel)); 

	output.albedo = color;
	return output;
}