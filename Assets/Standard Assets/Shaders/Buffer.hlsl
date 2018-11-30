cbuffer GlobalBuffer : register(b0)
{	
	matrix mMVP;
	matrix mView;
	matrix mProjection;	
	
	float camera_near;
    float camera_far;
    float2 resolution;
	
	float3 camera_position;	
	float fxaa_subPix;
	
	float fxaa_edgeThreshold;
    float fxaa_edgeThresholdMin;	
	float2 blur_direction;
	
	float blur_sigma;
	float bloom_intensity;
	float sharpen_strength;
	float sharpen_clamp;
	
	float2 taa_jitterOffset;
	float motionBlur_strength;
	float padding;
};

static const float2 texelSize = float2(1.0f / resolution.x, 1.0f / resolution.y);