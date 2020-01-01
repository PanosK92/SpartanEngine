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

// Updates once per frame
cbuffer FrameBuffer : register(b0)
{
	matrix g_view;
	matrix g_projection;
	matrix g_projectionOrtho;
	matrix g_viewProjection;
	matrix g_viewProjectionInv;
	matrix g_viewProjectionOrtho;
	matrix g_viewProjectionUnjittered;

	float g_delta_time;
	float g_time;
	float g_camera_near;
    float g_camera_far;
	
	float3 g_camera_position;
	float g_fxaa_subPix;
	
	float g_fxaa_edgeThreshold;
    float g_fxaa_edgeThresholdMin;
	float g_bloom_intensity;
	float g_sharpen_strength;
	
	float g_sharpen_clamp;
	float g_motionBlur_strength;
	float g_gamma;
	float g_toneMapping;
	
	float2 g_taa_jitterOffset;
	float g_exposure;	
	float g_directional_light_intensity;
	
	float g_ssr_enabled;
	float g_shadow_resolution;
	float g_ssao_scale;
	float g_padding;
};

// Updates multiple times per frame
cbuffer UberBuffer : register(b1)
{
	matrix g_transform; // can be anything	
	matrix g_wvp_current;
	matrix g_wvp_previous;

	float4 materialAlbedoColor;	

	float2 materialTiling;
	float2 materialOffset;

    float materialRoughness;
    float materialMetallic;
    float materialNormalStrength;
	float materialHeight;

	float materialShadingMode;
	float3 g_padding2;

	float4 g_color;
	
	float3 g_transform_axis;
	float g_blur_sigma;
	
	float2 g_blur_direction;
	float2 g_resolution;
};