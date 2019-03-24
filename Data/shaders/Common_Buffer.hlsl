cbuffer GlobalBuffer : register(b0)
{	
	matrix g_mvp;
	matrix g_view;
	matrix g_projection;
	matrix g_projectionOrtho;
	matrix g_viewProjection;
	matrix g_viewProjectionInv;
	matrix g_viewProjectionOrtho;
	
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
	
	float g_motionBlur_strength;
	float g_fps_current;		
	float g_fps_target;
	float g_gamma;
	
	float2 g_taa_jitterOffset;
	float g_toneMapping;
	float g_exposure;
};

#define g_texelSize float2(1.0f / g_resolution.x, 1.0f / g_resolution.y)