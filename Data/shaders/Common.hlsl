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

//= INCLUDES ================
#include "Common_Vertex.hlsl"
#include "Common_Buffer.hlsl"
//===========================

/*------------------------------------------------------------------------------
								[GLOBALS]
------------------------------------------------------------------------------*/
static const float PI 		= 3.14159265f;
static const float INV_PI 	= 0.31830988f;
static const float EPSILON 	= 0.00000001f;
#define g_texel_size 		float2(1.0f / g_resolution.x, 1.0f / g_resolution.y)
#define g_shadow_texel_size	(1.0f / g_shadow_resolution)

/*------------------------------------------------------------------------------
							[STRUCTS]
------------------------------------------------------------------------------*/
struct Material
{
	float3 albedo;
	float roughness;
	float metallic;
	float3 padding;
	float emissive;
	float3 F0;
};

struct Light
{
	float 	index;
	float3	color;
	float	intensity;
	float3	position;
	float	range;
	float3	direction;
    float 	angle;
	float	bias;
	float 	normal_bias;
	float 	shadow_enabled;
	float 	volumetric_enabled;
};

/*------------------------------------------------------------------------------
							[GAMMA CORRECTION]
------------------------------------------------------------------------------*/
float4 degamma(float4 color)	{ return pow(abs(color), g_gamma); }
float3 degamma(float3 color)	{ return pow(abs(color), g_gamma); }
float4 gamma(float4 color)		{ return pow(abs(color), 1.0f / g_gamma); }
float3 gamma(float3 color)		{ return pow(abs(color), 1.0f / g_gamma); }

/*------------------------------------------------------------------------------
								[PROJECT]
------------------------------------------------------------------------------*/
float2 project(float4 value) { return (value.xy / value.w) * float2(0.5f, -0.5f) + 0.5f; }
float2 project(float3 position, matrix transform)
{
	float4 projectedCoords 	= mul(float4(position, 1.0f), transform);
	projectedCoords.xy 		/= projectedCoords.w;
	projectedCoords.xy 		= projectedCoords.xy * float2(0.5f, -0.5f) + 0.5f;

	return projectedCoords.xy;
}

/*------------------------------------------------------------------------------
								[PACKING]
------------------------------------------------------------------------------*/
float3 unpack(float3 value)	{ return value * 2.0f - 1.0f; }
float3 pack(float3 value)	{ return value * 0.5f + 0.5f; }
float2 unpack(float2 value)	{ return value * 2.0f - 1.0f; }
float2 pack(float2 value)	{ return value * 0.5f + 0.5f; }

/*------------------------------------------------------------------------------
								[NORMALS]
------------------------------------------------------------------------------*/
float3 get_normal(Texture2D _texture, float2 uv)
{
	return normalize(_texture.Load(int3(uv * g_resolution, 0)).rgb);
}

float3x3 makeTBN(float3 n, float3 t)
{
	// re-orthogonalize T with respect to N
	t = normalize(t - dot(t, n) * n);
	// compute bitangent
	float3 b = cross(n, t);
	// create matrix
	return float3x3(t, b, n); 
}
// No decoding required
float3 normal_decode(float3 normal)	{ return normalize(normal); }
// No encoding required
float3 normal_encode(float3 normal)	{ return normalize(normal); }

/*------------------------------------------------------------------------------
							[DEPTH/POS]
------------------------------------------------------------------------------*/
float get_depth(Texture2D _texture, float2 uv)
{
	return _texture.Load(int3(uv * g_resolution, 0)).r;
}

float get_linear_depth(float z)
{
	float z_b = z;
    float z_n = 2.0f * z_b - 1.0f;
	return 2.0f * g_camera_far * g_camera_near / (g_camera_near + g_camera_far - z_n * (g_camera_near - g_camera_far));
}

float get_linear_depth(Texture2D _texture, float2 uv)
{
	float depth = get_depth(_texture, uv);
	return get_linear_depth(depth);
}

float3 get_world_position_from_depth(float z, matrix viewProjectionInverse, float2 uv)
{	
	float x 			= uv.x * 2.0f - 1.0f;
	float y 			= (1.0f - uv.y) * 2.0f - 1.0f;
    float4 pos_clip 	= float4(x, y, z, 1.0f);
	float4 pos_world 	= mul(pos_clip, viewProjectionInverse);	
    return pos_world.xyz / pos_world.w;  
}

float3 get_world_position_from_depth(Texture2D _texture, float2 uv)
{
	float depth = get_depth(_texture, uv);
    return get_world_position_from_depth(depth, g_viewProjectionInv, uv);
}

float3 get_world_position_from_depth(float depth, float2 uv)
{
    return get_world_position_from_depth(depth, g_viewProjectionInv, uv);
}

/*------------------------------------------------------------------------------
								[LUMINANCE]
------------------------------------------------------------------------------*/
static const float3 lumCoeff = float3(0.299f, 0.587f, 0.114f);

float luminance(float3 color)
{
    return max(dot(color, lumCoeff), 0.0001f);
}

float luminance(float4 color)
{
    return max(dot(color.rgb, lumCoeff), 0.0001f);
}

/*------------------------------------------------------------------------------
								[SKY SPHERE]
------------------------------------------------------------------------------*/
float2 directionToSphereUV(float3 direction)
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
float randomize(float2 uv)
{
	return frac(sin(dot(uv ,float2(12.9898,78.233))) * 43758.5453);
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
// The Technical Art of Uncharted 4 - http://advances.realtimerendering.com/other/2016/naughty_dog/index.html
float micro_shadow(float ao, float3 N, float3 L, float shadow)
{
	float aperture 		= 2.0f * ao * ao;
	float microShadow 	= saturate(abs(dot(L, N)) + aperture - 1.0f);
	return shadow * microShadow;
}

float min2(float2 value) { return min(value.x, value.y); }
float min3(float3 value) { return min(min(value.x, value.y), value.z); }

float max2(float2 value) { return max(value.x, value.y); }
float max3(float3 value) { return max(max(value.x, value.y), value.z); }

bool is_saturated(float value) 	{ return value == saturate(value); }
bool is_saturated(float2 value) { return is_saturated(value.x) && is_saturated(value.y); }
bool is_saturated(float3 value) { return is_saturated(value.x) && is_saturated(value.y) && is_saturated(value.z); }
bool is_saturated(float4 value) { return is_saturated(value.x) && is_saturated(value.y) && is_saturated(value.z) && is_saturated(value.w); }