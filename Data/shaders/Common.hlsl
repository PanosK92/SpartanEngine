#include "Common_Vertex.hlsl"

/*------------------------------------------------------------------------------
								[GLOBALS]
------------------------------------------------------------------------------*/
#define PI 3.1415926535897932384626433832795
#define INV_PI 1.0 / PI;
#define EPSILON 0.00000001

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
	float roughness_alpha;
};

struct Light
{
	float3 color;
	float intensity;
	float3 direction;
	float padding;
};

/*------------------------------------------------------------------------------
							[BUFFER]
------------------------------------------------------------------------------*/
cbuffer GlobalBuffer : register(b0)
{	
	matrix g_mvp;
	matrix g_view;
	matrix g_projection;
	matrix g_projectionOrtho;
	matrix g_viewProjection;
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

/*------------------------------------------------------------------------------
							[GAMMA CORRECTION]
------------------------------------------------------------------------------*/
inline float4 degamma(float4 color)	{ return pow(abs(color), g_gamma); }
inline float3 degamma(float3 color)	{ return pow(abs(color), g_gamma); }
inline float4 gamma(float4 color)	{ return pow(abs(color), 1.0f / g_gamma); }
inline float3 gamma(float3 color)	{ return pow(abs(color), 1.0f / g_gamma); }

/*------------------------------------------------------------------------------
								[PROJECT]
------------------------------------------------------------------------------*/
inline float2 project(float4 value)	{ return (value.xy / value.w) * float2(0.5f, -0.5f) + 0.5f; }
inline float2 project(float3 position, matrix transform)
{
	float4 projectedCoords 	= mul(float4(position, 1.0f), transform);
	projectedCoords.xy 		/= projectedCoords.w;
	projectedCoords.xy 		= projectedCoords.xy * float2(0.5f, -0.5f) + 0.5f;

	return projectedCoords.xy;
}

/*------------------------------------------------------------------------------
								[PACKING]
------------------------------------------------------------------------------*/
inline float3 unpack(float3 value)	{ return value * 2.0f - 1.0f; }
inline float3 pack(float3 value)	{ return value * 0.5f + 0.5f; }
inline float2 unpack(float2 value)	{ return value * 2.0f - 1.0f; }
inline float2 pack(float2 value)	{ return value * 0.5f + 0.5f; }

/*------------------------------------------------------------------------------
								[NORMALS]
------------------------------------------------------------------------------*/
inline float3x3 makeTBN(float3 n, float3 t)
{
	float3 b = cross(n, t);
	return float3x3(t, b, n); 
}
// No decoding required
inline float3 normal_Decode(float3 normal)	{ return normalize(normal); }
// No encoding required
inline float3 normal_Encode(float3 normal)	{ return normalize(normal); }

/*------------------------------------------------------------------------------
						[POSITION RECONSTRUCTION]
------------------------------------------------------------------------------*/
inline float3 reconstructPositionWorld(float depth, matrix viewProjectionInverse, float2 texCoord)
{	
	float x 			= texCoord.x * 2.0f - 1.0f;
	float y 			= (1.0f - texCoord.y) * 2.0f - 1.0f;
	float z				= depth;
    float4 pos_clip 	= float4(x, y, z, 1.0f);
	float4 pos_world 	= mul(pos_clip, viewProjectionInverse);
	
    return pos_world.xyz / pos_world.w;  
}

/*------------------------------------------------------------------------------
								[DEPTH]
------------------------------------------------------------------------------*/
inline float linerizeDepth(float depth, float near, float far)
{
	return (far / (far - near)) * (1.0f - (near / depth));
}

/*------------------------------------------------------------------------------
								[LUMINANCE]
------------------------------------------------------------------------------*/
static const float3 lumCoeff = float3(0.299f, 0.587f, 0.114f);

inline float luminance(float3 color)
{
    return max(dot(color, lumCoeff), 0.0001f);
}

inline float luminance(float4 color)
{
    return max(dot(color.rgb, lumCoeff), 0.0001f);
}

/*------------------------------------------------------------------------------
								[SKY SPHERE]
------------------------------------------------------------------------------*/
inline float2 directionToSphereUV(float3 direction)
{
    float n 	= length(direction.xz);
    float2 uv 	= float2((n > 0.0000001) ? direction.x / n : 0.0, direction.y);
    uv 			= acos(uv) * INV_PI;
    uv.x 		= (direction.z > 0.0) ? uv.x * 0.5 : 1.0 - (uv.x * 0.5);
    uv.x 		= 1.0 - uv.x;
	
    return uv;
}

/*------------------------------------------------------------------------------
								[RANDOM]
------------------------------------------------------------------------------*/
inline float randomize(float2 texcoord)
{
    float seed		= dot(texcoord, float2(12.9898, 78.233));
    float sine		= sin(seed);
    float noise		= frac(sine * 43758.5453);

    return noise;
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
// The Technical Art of Uncharted 4 - http://advances.realtimerendering.com/other/2016/naughty_dog/index.html
inline float micro_shadow(float ao, float3 N, float3 L, float shadow)
{
	float aperture 		= 2.0f * ao * ao;
	float microShadow 	= saturate(abs(dot(L, N)) + aperture - 1.0f);
	return shadow * microShadow;
}
inline bool is_saturated(float value) 	{ return value == saturate(value); }
inline bool is_saturated(float2 value) 	{ return is_saturated(value.x) && is_saturated(value.y); }