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

#ifndef SPARTAN_COMMON
#define SPARTAN_COMMON

//= INCLUDES =================
#include "Common_Struct.hlsl"
#include "Common_Vertex.hlsl"
#include "Common_Buffer.hlsl"
#include "Common_Sampler.hlsl"
#include "Common_Texture.hlsl"
//============================

/*------------------------------------------------------------------------------
    CONSTANTS
------------------------------------------------------------------------------*/
static const float PI           = 3.14159265f;
static const float PI2          = PI * 2;
static const float PI4          = PI * 4;
static const float INV_PI       = 0.31830988f;
static const float FLT_MIN      = 0.00000001f;
static const float FLT_MAX_10   = 65000.0f;
static const float FLT_MAX_11   = 65000.0f;
static const float FLT_MAX_14   = 65500.0f;
static const float FLT_MAX_16   = 65500.0f;

#define g_texel_size        float2(1.0f / g_resolution.x, 1.0f / g_resolution.y)
#define g_shadow_texel_size (1.0f / g_shadow_resolution)
#define thread_group_count_x    8
#define thread_group_count_y    8
#define thread_group_count      64

/*------------------------------------------------------------------------------
    MATH
------------------------------------------------------------------------------*/
inline float min2(float2 value) { return min(value.x, value.y); }
inline float min3(float3 value) { return min(min(value.x, value.y), value.z); }

inline float max2(float2 value) { return max(value.x, value.y); }
inline float max3(float3 value) { return max(max(value.x, value.y), value.z); }

inline bool is_saturated(float value)   { return value == saturate(value); }
inline bool is_saturated(float2 value)  { return is_saturated(value.x) && is_saturated(value.y); }
inline bool is_saturated(float3 value)  { return is_saturated(value.x) && is_saturated(value.y) && is_saturated(value.z); }
inline bool is_saturated(float4 value)  { return is_saturated(value.x) && is_saturated(value.y) && is_saturated(value.z) && is_saturated(value.w); }

/*------------------------------------------------------------------------------
    SATURATE
------------------------------------------------------------------------------*/
inline float    saturate_11(float x)    { return clamp(x, FLT_MIN, FLT_MAX_11); }
inline float2   saturate_11(float2 x)   { return clamp(x, FLT_MIN, FLT_MAX_11); }
inline float3   saturate_11(float3 x)   { return clamp(x, FLT_MIN, FLT_MAX_11); }
inline float4   saturate_11(float4 x)   { return clamp(x, FLT_MIN, FLT_MAX_11); }

inline float    saturate_16(float x)    { return clamp(x, FLT_MIN, FLT_MAX_16); }
inline float2   saturate_16(float2 x)   { return clamp(x, FLT_MIN, FLT_MAX_16); }
inline float3   saturate_16(float3 x)   { return clamp(x, FLT_MIN, FLT_MAX_16); }
inline float4   saturate_16(float4 x)   { return clamp(x, FLT_MIN, FLT_MAX_16); }

/*------------------------------------------------------------------------------
    GAMMA CORRECTION
------------------------------------------------------------------------------*/
inline float4 degamma(float4 color) { return pow(abs(color), g_gamma); }
inline float3 degamma(float3 color) { return pow(abs(color), g_gamma); }
inline float4 gamma(float4 color)   { return pow(abs(color), 1.0f / g_gamma); }
inline float3 gamma(float3 color)   { return pow(abs(color), 1.0f / g_gamma); }

/*------------------------------------------------------------------------------
    PACKING
------------------------------------------------------------------------------*/
inline float3 unpack(float3 value)  { return value * 2.0f - 1.0f; }
inline float3 pack(float3 value)    { return value * 0.5f + 0.5f; }
inline float2 unpack(float2 value)  { return value * 2.0f - 1.0f; }
inline float2 pack(float2 value)    { return value * 0.5f + 0.5f; }

/*------------------------------------------------------------------------------
    FAST MATH APPROXIMATIONS
------------------------------------------------------------------------------*/
inline float fast_sqrt(float v)
{
    int x = asint(v);
	x = 0x1FBD1DF5 + (x >> 1);
	return asfloat(x);
}

inline float fast_sin(float x)
{
    const float B = 4 / PI;
    const float C = -4 / PI2;
    const float P = 0.225;

    float y = B * x + C * x * abs(x);
    y = P * (y * abs(y) - y) + y;
    return y;
}

inline float fast_cos(float x)
{
   return abs(abs(x)  /PI2 % 4 - 2) - 1; 
}

/*------------------------------------------------------------------------------
    PROJECT
------------------------------------------------------------------------------*/
inline float3 project(float3 position, matrix transform)
{
    float4 projectedCoords  = mul(float4(position, 1.0f), transform);
    projectedCoords.xyz     /= projectedCoords.w;
    projectedCoords.xy      = projectedCoords.xy * float2(0.5f, -0.5f) + 0.5f;

    return projectedCoords.xyz;
}

inline float2 project_uv(float3 position, matrix transform)
{
    return project(position, transform).xy;
}

inline float project_depth(float3 position, matrix transform)
{
    return project(position, transform).z;
}

/*------------------------------------------------------------------------------
    NORMAL
------------------------------------------------------------------------------*/
// No decoding required (just normalise)
inline float3 normal_decode(float3 normal)  { return normalize(normal); }
// No encoding required (just normalise)
inline float3 normal_encode(float3 normal)  { return normalize(normal); }

inline float3 get_normal(int2 pos)
{
    return tex_normal.Load(int3(clamp(pos, int2(0, 0), (int2)g_resolution), 0)).rgb;
}

inline float3 get_normal(float2 uv)
{
    return tex_normal.SampleLevel(sampler_point_clamp, uv, 0).r;
}

inline float3 get_normal_view_space(int2 pos)
{
    float3 normal_world_space = get_normal(pos);
    return normalize(mul(float4(normal_world_space, 0.0f), g_view).xyz);
}

inline float3 get_normal_view_space(float2 uv)
{
    float3 normal_world_space = get_normal(uv);
    return normalize(mul(float4(normal_world_space, 0.0f), g_view).xyz);
}

inline float3x3 makeTBN(float3 n, float3 t)
{
    // re-orthogonalize T with respect to N
    t = normalize(t - dot(t, n) * n);
    // compute bitangent
    float3 b = cross(n, t);
    // create matrix
    return float3x3(t, b, n); 
}

/*------------------------------------------------------------------------------
    DEPTH
------------------------------------------------------------------------------*/
inline float get_depth(int2 pos)
{
    return tex_depth.Load(int3(clamp(pos, int2(0, 0), (int2)g_resolution), 0)).r;
}

inline float get_depth(float2 uv)
{
    // effects like screen space shadows, can get artifacts if a point sampler is used
    return tex_depth.SampleLevel(sampler_bilinear_clamp, uv, 0).r;
}

inline float get_linear_depth(float z, float near, float far)
{
    float z_b = z;
    float z_n = 2.0f * z_b - 1.0f;
    return 2.0f * far * near / (near + far - z_n * (near - far));
}

inline float get_linear_depth(float z)
{
    return get_linear_depth(z, g_camera_near, g_camera_far);
}

inline float get_linear_depth(int2 pos)
{
    float depth = get_depth(pos);
    return get_linear_depth(depth);
}

inline float get_linear_depth(float2 uv)
{
    float depth = get_depth(uv);
    return get_linear_depth(depth);
}

/*------------------------------------------------------------------------------
    POSITION
------------------------------------------------------------------------------*/
inline float3 get_position(float z, float2 uv)
{
    // Reconstruct position from depth

    float x             = uv.x * 2.0f - 1.0f;
    float y             = (1.0f - uv.y) * 2.0f - 1.0f;
    float4 pos_clip     = float4(x, y, z, 1.0f);
    float4 pos_world    = mul(pos_clip, g_view_projection_inverted);
    return pos_world.xyz / pos_world.w;
}

inline float3 get_position(int2 pos)
{
    const float depth   = get_depth(pos);
    const float2 uv     = (pos + 0.5f) / g_resolution;
    return get_position(depth, uv);
}

inline float3 get_position(float2 uv)
{
    // Effects like ambient occlusion have visual discontinuities when they are not using filtered depth
    float depth = get_depth(uv);
    return get_position(depth, uv);
}

inline float3 get_position_view_space(int2 pos)
{
    float3 position_world_space = get_position(pos);
    return mul(float4(position_world_space, 1.0f), g_view).xyz;
}

inline float3 get_position_view_space(float2 uv)
{
    float3 position_world_space = get_position(uv);
    return mul(float4(position_world_space, 1.0f), g_view).xyz;
}

/*------------------------------------------------------------------------------
    VIEW DIRECTION
------------------------------------------------------------------------------*/
inline float3 get_view_direction(float depth, float2 uv)
{
    float3 position_world = get_position(depth, uv);
    return normalize(position_world - g_camera_position.xyz);
}

inline float3 get_view_direction(float2 uv)
{
    float depth = get_depth(uv);
    return get_view_direction(depth, uv);
}

inline float3 get_view_direction(int2 pos)
{
    const float depth = get_depth(pos);
    const float2 uv = (pos + 0.5f) / g_resolution;
    return get_view_direction(depth, uv);
}

inline float3 get_view_direction_view_space(float2 uv)
{
    float depth = get_depth(uv);
    const float3 view_direction = get_view_direction(depth, uv);
    return mul(float4(view_direction, 0.0f), g_view).xyz;
}

/*------------------------------------------------------------------------------
    DIRECTION UV
------------------------------------------------------------------------------*/
inline float2 direction_sphere_uv(float3 direction)
{
    float n = length(direction.xz);
    float2 uv = float2((n > 0.0000001) ? direction.x / n : 0.0, direction.y);
    uv = acos(uv) * INV_PI;
    uv.x = (direction.z > 0.0) ? uv.x * 0.5 : 1.0 - (uv.x * 0.5);
    uv.x = 1.0 - uv.x;
    
    return uv;
}

inline uint direction_to_cube_face_index(const float3 direction)
{
    float3 direction_abs = abs(direction);
    float max_coordinate = max3(direction_abs);
    
    if (max_coordinate == direction_abs.x)
    {
        return direction_abs.x == direction.x ? 0 : 1;
    }
    else if (max_coordinate == direction_abs.y)
    {
        return direction_abs.y == direction.y ? 2 : 3;
    }
    else
    {
        return direction_abs.z == direction.z ? 4 : 5;
    }
    
    return 0;
}

/*------------------------------------------------------------------------------
    LUMINANCE
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
    RANDOM/SAMPLING
------------------------------------------------------------------------------*/
inline float random(float2 uv)
{
    return frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453);
}

// Based on interleaved gradient function from Jimenez 2014 http://goo.gl/eomGso
inline float interleaved_gradient_noise(float2 position_screen)
{
    position_screen += g_frame * any(g_taa_jitter_offset); // temporal factor
    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(position_screen, magic.xy)));
}

// Based on Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
inline float noise_spatial_offset(float2 position_screen)
{
    int2 position = (int2)(position_screen);
    return 0.25 * (float)((position.y - position.x) & 3);
}

// Based on Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
static const float offsets[] = { 0.0f, 0.5f, 0.25f, 0.75f };
inline float noise_temporal_offset()
{
    return offsets[(g_frame / 6) % 4] * any(g_taa_jitter_offset);
}

// Based on Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
static const float rotations[] = { 60.0f, 300.0f, 180.0f, 240.0f, 120.0f, 0.0f };
inline float noise_temporal_direction()
{
    return (rotations[g_frame % 6] / 360.0f) * any(g_taa_jitter_offset);
}

/*------------------------------------------------------------------------------
    OCCLUSION/SHADOWING
------------------------------------------------------------------------------*/

// Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
inline float3 MultiBounceAO(float visibility, float3 albedo)
{
    float3 a = 2.0404 * albedo - 0.3324;
    float3 b = -4.7951 * albedo + 0.6417;
    float3 c = 2.7552 * albedo + 0.6903;
	
    float x = visibility;
    return max(x, ((x * a + b) * x + c) * x);
}

// The Technical Art of Uncharted 4 - http://advances.realtimerendering.com/other/2016/naughty_dog/index.html
float microw_shadowing_nt(float n_dot_l, float ao)
{
    float aperture = 2.0f * ao * ao;
    return saturate(abs(n_dot_l) + aperture - 1.0f);
}

// Chan 2018, "Material Advances in Call of Duty: WWII"
float microw_shadowing_cod(float n_dot_l, float visibility)
{
    float aperture = rsqrt(1.0 - visibility);
    float microShadow = saturate(n_dot_l * aperture);
    return microShadow * microShadow;
}

/*------------------------------------------------------------------------------
    MISC
------------------------------------------------------------------------------*/

inline float3 compute_diffuse_energy(float3 F, float metallic)
{
    float3 kS = F;          // The energy of light that gets reflected - Equal to Fresnel
    float3 kD = 1.0f - kS;  // Remaining energy, light that gets refracted          
    kD *= 1.0f - metallic;  // Multiply kD by the inverse metalness such that only non-metals have diffuse lighting
    
    return kD;
}

inline float screen_fade(float2 uv)
{
    float2 fade = max(12.0f * abs(uv - 0.5f) - 5.0f, 0.0f);
    return saturate(1.0 - dot(fade, fade));
}

static const float3 hemisphere_samples[64] =
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

#endif // SPARTAN_COMMON
