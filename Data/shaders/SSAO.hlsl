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
#include "Dithering.hlsl"
//====================

//= TEXTURES ======================
Texture2D texNormal : register(t0);
Texture2D texDepth  : register(t1);
Texture2D texNoise  : register(t2);
//=================================

//= SAMPLERS ===================================
SamplerState samplerLinear_clamp : register(s0);
SamplerState samplerLinear_wrap : register(s1);
//==============================================

static const float3 sampleKernel[64] =
{
    float3(0.04977, -0.04471, 0.04996),
	float3(0.01457, 0.01653, 0.00224),
	float3(-0.04065, -0.01937, 0.03193),
	float3(0.01378, -0.09158, 0.04092),
	float3(0.05599, 0.05979, 0.05766),
	float3(0.09227, 0.04428, 0.01545),
	float3(-0.00204, -0.0544, 0.06674),
	float3(-0.00033, -0.00019, 0.00037),
	float3(0.05004, -0.04665, 0.02538),
	float3(0.03813, 0.0314, 0.03287),
	float3(-0.03188, 0.02046, 0.02251),
	float3(0.0557, -0.03697, 0.05449),
	float3(0.05737, -0.02254, 0.07554),
	float3(-0.01609, -0.00377, 0.05547),
	float3(-0.02503, -0.02483, 0.02495),
	float3(-0.03369, 0.02139, 0.0254),
	float3(-0.01753, 0.01439, 0.00535),
	float3(0.07336, 0.11205, 0.01101),
	float3(-0.04406, -0.09028, 0.08368),
	float3(-0.08328, -0.00168, 0.08499),
	float3(-0.01041, -0.03287, 0.01927),
	float3(0.00321, -0.00488, 0.00416),
	float3(-0.00738, -0.06583, 0.0674),
	float3(0.09414, -0.008, 0.14335),
	float3(0.07683, 0.12697, 0.107),
	float3(0.00039, 0.00045, 0.0003),
	float3(-0.10479, 0.06544, 0.10174),
	float3(-0.00445, -0.11964, 0.1619),
	float3(-0.07455, 0.03445, 0.22414),
	float3(-0.00276, 0.00308, 0.00292),
	float3(-0.10851, 0.14234, 0.16644),
	float3(0.04688, 0.10364, 0.05958),
	float3(0.13457, -0.02251, 0.13051),
	float3(-0.16449, -0.15564, 0.12454),
	float3(-0.18767, -0.20883, 0.05777),
	float3(-0.04372, 0.08693, 0.0748),
	float3(-0.00256, -0.002, 0.00407),
	float3(-0.0967, -0.18226, 0.29949),
	float3(-0.22577, 0.31606, 0.08916),
	float3(-0.02751, 0.28719, 0.31718),
	float3(0.20722, -0.27084, 0.11013),
	float3(0.0549, 0.10434, 0.32311),
	float3(-0.13086, 0.11929, 0.28022),
	float3(0.15404, -0.06537, 0.22984),
	float3(0.05294, -0.22787, 0.14848),
	float3(-0.18731, -0.04022, 0.01593),
	float3(0.14184, 0.04716, 0.13485),
	float3(-0.04427, 0.05562, 0.05586),
	float3(-0.02358, -0.08097, 0.21913),
	float3(-0.14215, 0.19807, 0.00519),
	float3(0.15865, 0.23046, 0.04372),
	float3(0.03004, 0.38183, 0.16383),
	float3(0.08301, -0.30966, 0.06741),
	float3(0.22695, -0.23535, 0.19367),
	float3(0.38129, 0.33204, 0.52949),
	float3(-0.55627, 0.29472, 0.3011),
	float3(0.42449, 0.00565, 0.11758),
	float3(0.3665, 0.00359, 0.0857),
	float3(0.32902, 0.0309, 0.1785),
	float3(-0.08294, 0.51285, 0.05656),
	float3(0.86736, -0.00273, 0.10014),
	float3(0.45574, -0.77201, 0.00384),
	float3(0.41729, -0.15485, 0.46251),
	float3(-0.44272, -0.67928, 0.1865)
};

static const int sample_count		= 16;
static const float radius			= 0.5f;
static const float intensity    	= 2.0f;
static const float2 noiseScale  	= float2(g_resolution.x / 128.0f, g_resolution.y / 128.0f);

float mainPS(Pixel_PosUv input) : SV_TARGET
{
	float2 uv				= input.uv;  
    float depth      		= texDepth.Sample(samplerLinear_clamp, uv).r;
    float3 center_pos       = get_world_position_from_depth(depth, uv);
    float3 center_normal    = normal_decode(texNormal.Sample(samplerLinear_clamp, uv).xyz);
	float3 noise			= unpack(texNoise.Sample(samplerLinear_wrap, uv * noiseScale).xyz);		
	float occlusion_acc     = 0.0f;
    float3 color            = float3(0.0f, 0.0f, 0.0f);

	// Compute range based dithered radius
	float dither 		= Dither(uv + g_taa_jitterOffset).x * 500;
	float radius_depth	= get_linear_depth(depth) / (1.0f / (radius)) * dither;

	// Construct TBN
	float3 tangent	= normalize(noise - center_normal * dot(noise, center_normal));
	float3x3 TBN	= makeTBN(center_normal, tangent);

    // Occlusion
    for (int i = 0; i < sample_count; i++)
    {	
		// Compute sample uv
		float3 offset 	= mul(sampleKernel[i], TBN);
		float3 ray_pos	= center_pos + offset * radius_depth;
		float2 ray_uv 	= project(ray_pos, g_viewProjection);
		
		// Acquire/Compute sample data
        float3 sample_pos      			= get_world_position_from_depth(texDepth, samplerLinear_clamp, ray_uv);
        float3 center_to_sample			= sample_pos - center_pos;
		float center_to_sample_distance	= length(center_to_sample);
		float3 center_to_sample_dir 	= normalize(center_to_sample);
		
		// Accumulate
		float3 sampled_normal   = normal_decode(texNormal.Sample(samplerLinear_clamp, ray_uv).xyz);  
		float occlusion			= dot(center_normal, center_to_sample_dir);
		float rangeCheck		= center_to_sample_distance <= radius_depth;
		occlusion_acc 			+= occlusion * rangeCheck * intensity;
    }
    occlusion_acc /= (float)sample_count;

	return saturate(1.0f - occlusion_acc);
}