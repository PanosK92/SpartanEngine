// = INCLUDES ========
#include "Common.hlsl"
#include "Vertex.hlsl"
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
	color = Pack(color);
#endif

#if DEBUG_VELOCITY
	color = abs(color) * 10.0f;
#endif

#if DEBUG_SSAO
	color = float3(color.r, color.r, color.r);
#endif

#if DEBUG_DEPTH
	float logDepth = color.g;
	color = float3(logDepth, logDepth, logDepth);
#endif

    return float4(color, 1.0f);
}