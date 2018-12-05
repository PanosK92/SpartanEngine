cbuffer GlobalBuffer : register(b0)
{	
	matrix g_mvp;
	matrix g_view;
	matrix g_projection;	
	
	float g_camera_near;
    float g_camera_far;
    float2 g_resolution;
	
	float3 g_camera_position;	
	float g_fxaa_subPix;
	
	float g_fxaa_edgeThreshold;
    float g_fxaa_edgeThresholdMin;	
	float2 g_blur_direction;
	
	float g_blur_sigma;
	float g_bloom_intensity;
	float g_sharpen_strength;
	float g_sharpen_clamp;
	
	float2 g_taa_jitterOffset;
	float2 g_taa_jitterOffsetPrevious;
	
	float g_motionBlur_strength;
	float g_deltaTime;
	float2 padding;
};

static const float2 g_texelSize = float2(1.0f / g_resolution.x, 1.0f / g_resolution.y);