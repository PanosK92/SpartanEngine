/*------------------------------------------------------------------------------
								[GLOBALS]
------------------------------------------------------------------------------*/
#define PI 3.1415926535897932384626433832795
#define EPSILON 0.00000001

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
	float g_motionBlur_strength;
	float g_fps_current;	
	
	float g_fps_target;
	float g_packNormals;	
	float g_gamma;
	float padding;
};

static const float2 g_texelSize = float2(1.0f / g_resolution.x, 1.0f / g_resolution.y);

/*------------------------------------------------------------------------------
							[STRUCTS]
------------------------------------------------------------------------------*/
struct Material
{
	float3 albedo;
	float roughness;
	float metallic;
	float3 padding;
	float emission;
	float3 F0;
	float alpha;
};

struct Light
{
	float3 color;
	float intensity;
	float3 direction;
	float padding;
};

/*------------------------------------------------------------------------------
							[GAMMA CORRECTION]
------------------------------------------------------------------------------*/
float4 Degamma(float4 color)
{
	return pow(abs(color), g_gamma);
}

float3 Degamma(float3 color)
{
	return pow(abs(color), g_gamma);
}

float4 Gamma(float4 color)
{
	return pow(abs(color), 1.0f / g_gamma); 
}

float3 Gamma(float3 color)
{
	return pow(abs(color), 1.0f / g_gamma); 
}

/*------------------------------------------------------------------------------
								[PROJECT]
------------------------------------------------------------------------------*/
float2 Project(float4 value)
{
	return (value.xy / value.w) * float2(0.5f, -0.5f) + 0.5f;
}

/*------------------------------------------------------------------------------
								[PACKING]
------------------------------------------------------------------------------*/
float3 Unpack(float3 value)
{
	return value * 2.0f - 1.0f;
}

float3 Pack(float3 value)
{
	return value * 0.5f + 0.5f;
}

float2 Unpack(float2 value)
{
	return value * 2.0f - 1.0f;
}

float2 Pack(float2 value)
{
	return value * 0.5f + 0.5f;
}

/*------------------------------------------------------------------------------
								[NORMALS]
------------------------------------------------------------------------------*/
float3 TangentToWorld(float3 normalMapSample, float3 normalW, float3 tangentW, float3 bitangentW, float intensity)
{
	// normal intensity
	intensity			= clamp(intensity, 0.01f, 1.0f);
	normalMapSample.r 	*= intensity;
	normalMapSample.g 	*= intensity;
	
	// construct TBN matrix
	float3 N 		= normalW;
	float3 T 		= tangentW;
	float3 B 		= bitangentW;
	float3x3 TBN 	= float3x3(T, B, N); 
	
	// Transform from tangent space to world space
    return normalize(mul(normalMapSample, TBN));
}

float3 Normal_Decode(float3 normal)
{
	return normalize(g_packNormals == 1.0f ? Unpack(normal) : normal);
}

float3 Normal_Encode(float3 normal)
{
	return normalize(g_packNormals == 1.0f ? Pack(normal) : normal);
}

/*------------------------------------------------------------------------------
						[POSITION RECONSTRUCTION]
------------------------------------------------------------------------------*/
float3 ReconstructPositionWorld(float depth, matrix viewProjectionInverse, float2 texCoord)
{	
	float x 			= texCoord.x * 2.0f - 1.0f;
	float y 			= (1.0f - texCoord.y) * 2.0f - 1.0f;
	float z				= depth;
    float4 pos_clip 	= float4(x, y, z, 1.0f);
	float4 pos_world 	= mul(pos_clip, viewProjectionInverse);
	
    return pos_world.xyz / pos_world.w;  
}

/*------------------------------------------------------------------------------
								[VELOCITY]
------------------------------------------------------------------------------*/

// Return average velocity
float2 GetVelocity_Dilate_Average(float2 texCoord, Texture2D texture_velocity, SamplerState sampler_bilinear)
{
	float2 velocity_tl 	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-2, -2) * g_texelSize).xy;
	float2 velocity_tr	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(2, -2) * g_texelSize).xy;
	float2 velocity_bl	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(-2, 2) * g_texelSize).xy;
	float2 velocity_br 	= texture_velocity.Sample(sampler_bilinear, texCoord + float2(2, 2) * g_texelSize).xy;
	float2 velocity_ce 	= texture_velocity.Sample(sampler_bilinear, texCoord).xy;
	float2 velocity 	= (velocity_tl + velocity_tr + velocity_bl + velocity_br + velocity_ce) / 5.0f;	
	
	return velocity;
}

// Returns velocity with closest depth
float2 GetVelocity_Dilate_Depth(float2 texCoord, Texture2D texture_velocity, Texture2D texture_depth, SamplerState sampler_bilinear, out float closestDepth)
{	
	closestDepth			= 1.0f;
	float2 closestTexCoord 	= 0.0f;
	[unroll]
    for(int y = -1; y <= 1; ++y)
    {
		[unroll]
        for(int x = -1; x <= 1; ++x)
        {
			float2 texCoordNew 	= texCoord + float2(x, y) * g_texelSize;
			float depth			= texture_depth.Sample(sampler_bilinear, texCoordNew).r;
			if(depth < closestDepth)
			{
				closestDepth	= depth;
				closestTexCoord	= texCoordNew;
			}
        }
	}

	return texture_velocity.Sample(sampler_bilinear, closestTexCoord).xy;
}

/*------------------------------------------------------------------------------
								[DEPTH]
------------------------------------------------------------------------------*/
float LinerizeDepth(float depth, float near, float far)
{
	return (far / (far - near)) * (1.0f - (near / depth));
}

/*------------------------------------------------------------------------------
								[LUMINANCE]
------------------------------------------------------------------------------*/
float Luminance(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float Luminance(float4 color)
{
    return dot(color.rgb, float3(0.299f, 0.587f, 0.114f));
}

/*------------------------------------------------------------------------------
								[SKY SPHERE]
------------------------------------------------------------------------------*/
#define INV_PI 1.0 / PI;
float2 DirectionToSphereUV(float3 direction)
{
    float n 	= length(direction.xz);
    float2 uv 	= float2((n > 0.0000001) ? direction.x / n : 0.0, direction.y);
    uv 			= acos(uv) * INV_PI;
    uv.x 		= (direction.z > 0.0) ? uv.x * 0.5 : 1.0 - (uv.x * 0.5);
    uv.x 		= 1.0 - uv.x;
	
    return uv;
}