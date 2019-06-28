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

// = INCLUDES ========
#include "Common.hlsl"
//====================

Texture2D sourceTexture 	: register(t0);
SamplerState samplerState 	: register(s0);

struct VS_Output
{
    float4 position : SV_POSITION;
    float2 uv 		: TEXCOORD;
};

VS_Output mainVS(Vertex_PosUv input)
{
    VS_Output output;
	
    input.position.w 	= 1.0f;
    output.position 	= mul(input.position, g_viewProjectionOrtho);
    output.uv 			= input.uv;
	
    return output;
}

float4 mainPS(VS_Output input) : SV_TARGET
{
	float3 color = sourceTexture.Sample(samplerState, input.uv).rgb;
	
#if DEBUG_NORMAL
	color = pack(color);
#endif

#if DEBUG_VELOCITY
	color = abs(color) * 20.0f;
#endif

#if DEBUG_SSAO
	color = float3(color.r, color.r, color.r);
#endif

#if DEBUG_DEPTH
	float logDepth = color.r;
	color = float3(logDepth, logDepth, logDepth);
#endif

    return float4(color, 1.0f);
}