/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ====================
#include "common_resources.hlsl"
#include "common_colorspace.hlsl"
//===============================

/*-----------------------------------------------------------------------------
    CONSTANTS
------------------------------------------------------------------------------*/
static const float PI                   = 3.14159265f;
static const float PI2                  = 6.28318530f;
static const float PI4                  = 12.5663706f;
static const float INV_PI               = 0.31830988f;
static const float PI_HALF              = PI * 0.5f;
static const float FLT_MIN              = 0.00000001f;
static const float FLT_MAX_16           = 32767.0f;
static const float FLT_MAX_16U          = 65504.0f;
static const float RPC_9                = 0.11111111111f;
static const float RPC_16               = 0.0625f;
static const float RPC_32               = 0.03125f;
static const uint  THREAD_GROUP_COUNT_X = 8;
static const uint  THREAD_GROUP_COUNT_Y = 8;
static const uint  THREAD_GROUP_COUNT   = 64;
static const float DEG_TO_RAD           = PI / 180.0f;
static const float LUMINOUS_EFFICACY_MAX = 683.0f;

float radiometric_to_photometric(float value)
{
    return value * LUMINOUS_EFFICACY_MAX;
}

float3 radiometric_to_photometric(float3 value)
{
    return value * LUMINOUS_EFFICACY_MAX;
}

float photometric_to_radiometric(float value)
{
    return value / LUMINOUS_EFFICACY_MAX;
}

float3 photometric_to_radiometric(float3 value)
{
    return value / LUMINOUS_EFFICACY_MAX;
}

/*------------------------------------------------------------------------------
    SUN RADIANCE
------------------------------------------------------------------------------*/
// single source of truth for every sun energy term in the engine, the directional
// sun is always at slot 0 in the light parameters buffer, light.color holds the
// temperature derived chromaticity (~1.0, 0.95, 0.90 at 5778 K) and light.intensity
// holds the authored lux already converted to radiometric on the cpu via /683
//
// every consumer (direct surface lighting, sky scattering, sun disc, cloud direct
// lighting, fog haze, ibl through the baked panorama) goes through these helpers
// so the entire scene is locked to one calibration, changing the directional light
// color or intensity in the editor propagates to every visual term coherently
float3 get_sun_color()
{
    return light_parameters[0].color.rgb;
}

float get_sun_intensity()
{
    return light_parameters[0].intensity;
}

float3 get_sun_radiance()
{
    return get_sun_color() * get_sun_intensity();
}

// chromaticity preserving hdr clamp, the engine sky panorama is clamped to keep huge sun
// radiance values inside the 16 bit storage range, a channel wise min(color, cap) would
// saturate every channel to the cap whenever any single channel exceeded it which is the
// case for the sun disc at every preset (a warm tinted (146, 136, 127) at the day preset
// for example clips to a flat (100, 100, 100) and looks like a laboratory grade flat white
// even though the source color carries the directional light's temperature), this helper
// scales the whole color by a single factor when its peak channel exceeds the cap so the
// warm or cool tint from the directional light survives the hdr to ldr range reduction
float3 hdr_clamp_chroma(float3 color, float max_value)
{
    float peak = max(color.r, max(color.g, color.b));
    return (peak > max_value) ? color * (max_value / peak) : color;
}

/*------------------------------------------------------------------------------
    SATURATE
------------------------------------------------------------------------------*/
float  saturate_16(float x)  { return clamp(x, 0.0f, FLT_MAX_16U); }
float2 saturate_16(float2 x) { return clamp(x, 0.0f, FLT_MAX_16U); }
float3 saturate_16(float3 x) { return clamp(x, 0.0f, FLT_MAX_16U); }
float4 saturate_16(float4 x) { return clamp(x, 0.0f, FLT_MAX_16U); }

/*------------------------------------------------------------------------------
    VALIDATE (used because AMD SSSR is full of issues)
------------------------------------------------------------------------------*/
// nan propagates through addition so a single any check on the combined value catches the bad
// case in one shot instead of running isnan and a select per component
float validate_output(float value)
{
    value = saturate_16(value);
    return isnan(value) ? 1.0f : value;
}

float2 validate_output(float2 value)
{
    value = saturate_16(value);
    return any(isnan(value)) ? float2(1.0f, 1.0f) : value;
}

float3 validate_output(float3 value)
{
    value = saturate_16(value);
    return any(isnan(value)) ? float3(1.0f, 1.0f, 1.0f) : value;
}

float4 validate_output(float4 value)
{
    value = saturate_16(value);
    return any(isnan(value)) ? float4(1.0f, 1.0f, 1.0f, 1.0f) : value;
}

/*------------------------------------------------------------------------------
    PACK/UNPACK
------------------------------------------------------------------------------*/
float3 unpack(float3 value) { return value * 2.0f - 1.0f; }
float3 pack(float3 value)   { return value * 0.5f + 0.5f; }
float2 unpack(float2 value) { return value * 2.0f - 1.0f; }
float2 pack(float2 value)   { return value * 0.5f + 0.5f; }
float  unpack(float value)  { return value * 2.0f - 1.0f; }
float  pack(float value)    { return value * 0.5f + 0.5f; }

/*------------------------------------------------------------------------------
    FAST MATH APPROXIMATIONS
------------------------------------------------------------------------------*/
float fast_sqrt(float x)
{
    return (float)(asfloat(0x1fbd1df5 + (asint(x) >> 1)));
}

float fast_length(float3 v)
{
    float length_squared = dot(v, v);
    return fast_sqrt(length_squared);
}

float fast_sin(float x)
{
    const float B = 4 / PI;
    const float C = -4 / PI2;
    const float P = 0.225;

    float y = B * x + C * x * abs(x);
    y = P * (y * abs(y) - y) + y;
    
    return y;
}

float fast_cos(float x)
{
   return abs(abs(x) / PI2 % 4 - 2) - 1;
}

float fast_acos(float in_x)
{
    float x   = abs(in_x);
    float res = -0.156583f * x + PI_HALF;
    res      *= fast_sqrt(1.0f - x);
    return (in_x >= 0) ? res : PI - res;
}

/*------------------------------------------------------------------------------
    TRANSFORMATIONS
------------------------------------------------------------------------------*/
float3 world_to_view(float3 x, bool is_position = true)
{
    return mul(float4(x, (float)is_position), get_view()).xyz;
}

float3 world_to_ndc(float3 x, bool is_position = true)
{
    float4 ndc = mul(float4(x, (float)is_position), get_view_projection());
    return ndc.xyz / ndc.w;
}

float3 world_to_ndc(float3 x, float4x4 transform) // shadow mapping
{
    float4 ndc = mul(float4(x, 1.0f), transform);
    return ndc.xyz / ndc.w;
}

float3 view_to_ndc(float3 x, bool is_position = true)
{
    float4 ndc = mul(float4(x, (float)is_position), get_projection());
    return ndc.xyz / ndc.w;
}

float2 world_to_uv(float3 x, bool is_position = true)
{
    float4 uv = mul(float4(x, (float)is_position), get_view_projection());
    return (uv.xy / uv.w) * float2(0.5f, -0.5f) + 0.5f;
}

float3 view_to_world(float3 x, bool is_position = true)
{
     return mul(float4(x, (float)is_position), get_view_inverted()).xyz;
}

float2 view_to_uv(float3 x, bool is_position = true)
{
    float4 uv = mul(float4(x, (float)is_position), get_projection());
    return (uv.xy / uv.w) * float2(0.5f, -0.5f) + 0.5f;
}

float2 ndc_to_uv(float2 x)
{
    return x * float2(0.5f, -0.5f) + 0.5f;
}

float2 ndc_to_uv(float3 x)
{
    return x.xy * float2(0.5f, -0.5f) + 0.5f;
}

float2 uv_to_ndc(float2 uv)
{
    return float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f); // flip y for dx style
}

// rotate uv in 90 degree increments: 0 = 0, 1 = 90, 2 = 180, 3 = 270
float2 rotate_uv_90(float2 uv, float rotation_index)
{
    uint r = uint(rotation_index) & 3;
    float2 centered = uv - 0.5f;
    float2 rotated  = centered;
    if (r == 1)      rotated = float2(-centered.y,  centered.x); // 90 ccw
    else if (r == 2) rotated = float2(-centered.x, -centered.y); // 180
    else if (r == 3) rotated = float2( centered.y, -centered.x); // 270 ccw
    return rotated + 0.5f;
}

float2 compute_world_space_uv(float3 position_world, float3 normal_world)
{
    float3 normal = normalize(normal_world);
    float3 axis_v = float3(0.0f, 1.0f, 0.0f) - normal * dot(float3(0.0f, 1.0f, 0.0f), normal);

    // horizontal surfaces need a second reference axis because world up collapses onto the normal.
    if (dot(axis_v, axis_v) < 1e-4f)
        axis_v = float3(0.0f, 0.0f, -1.0f) - normal * dot(float3(0.0f, 0.0f, -1.0f), normal);

    axis_v = normalize(axis_v);

    // keep v aligned with world up on walls and slopes, while u follows the surface plane.
    float3 axis_u = normalize(cross(axis_v, normal));

    return float2(dot(position_world, axis_u), dot(position_world, axis_v));
}

/*------------------------------------------------------------------------------
    NORMAL
------------------------------------------------------------------------------*/
// reconstruct normal z, x and y components have to be in a -1 to 1 range.
float3 reconstruct_normal_z(float2 normal)
{
    float z = sqrt(saturate(1.0f - dot(normal, normal)));
    return float3(normal, z);
}

float3 get_normal(uint2 pos)
{
    // load returns 0 for any value accessed out of bounds, so clamp.
    pos.x = clamp(pos.x, 0, buffer_frame.resolution_render.x);
    pos.y = clamp(pos.y, 0, buffer_frame.resolution_render.y);
    
    return tex_normal[pos].xyz;
}

float3 get_normal(float2 uv)
{
    return tex_normal.SampleLevel(samplers[sampler_point_clamp], uv, 0).xyz;
}

float3 get_normal_view_space(uint2 pos)
{
    return normalize(mul(float4(get_normal(pos), 0.0f), get_view()).xyz);
}

float3 get_normal_view_space(float2 uv)
{
    return normalize(mul(float4(get_normal(uv), 0.0f), get_view()).xyz);
}

float3x3 make_tangent_to_world_matrix(float3 n, float3 t)
{
    // re-orthogonalize T with respect to N
    t = normalize(t - dot(t, n) * n);
    // compute bitangent
    float3 b = cross(n, t);
    // create matrix
    return float3x3(t, b, n); 
}

float3x3 make_world_to_tangent_matrix(float3 n, float3 t)
{
    return transpose(make_tangent_to_world_matrix(n, t));
}

/*------------------------------------------------------------------------------
    DEPTH - REVERSE-Z
------------------------------------------------------------------------------*/
float get_depth(const uint2 position)
{
    return tex_depth[position].r;
}

float get_depth(const float2 uv)
{
    return tex_depth.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).r;
}

float linearize_depth(const float z)
{
    float near = buffer_frame.camera_near;
    float far  = buffer_frame.camera_far;
    float z_b  = z;
    float z_n  = 2.0f * z_b - 1.0f;
    return 2.0f * far * near / (near + far - z_n * (near - far));
}

float get_linear_depth(const uint2 pos)
{
    return linearize_depth(get_depth(pos));
}

float get_linear_depth(const float2 uv)
{
    return linearize_depth(get_depth(uv));
}

/*------------------------------------------------------------------------------
    POSITION
------------------------------------------------------------------------------*/
float3 get_position(float z, float2 uv)
{
    float x          = uv.x * 2.0f - 1.0f;
    float y          = (1.0f - uv.y) * 2.0f - 1.0f;
    float4 pos_clip  = float4(x, y, z, 1.0f);
    float4 pos_world = mul(pos_clip, get_view_projection_inverted());
    return pos_world.xyz / pos_world.w;
}

// explicit-view overload: used by raster pixel shaders in multiview passes where the per-fragment
// eye must be selected via SV_ViewID rather than the (static) buffer_pass.eye_index push constant.
float3 get_position_for_view(float z, float2 uv, uint view_id)
{
    float x          = uv.x * 2.0f - 1.0f;
    float y          = (1.0f - uv.y) * 2.0f - 1.0f;
    float4 pos_clip  = float4(x, y, z, 1.0f);
    float4 pos_world = mul(pos_clip, get_view_projection_inverted_for_view(view_id));
    return pos_world.xyz / pos_world.w;
}

float3 get_position(float2 uv)
{
    return get_position(get_depth(uv), uv / buffer_frame.resolution_scale);
}

float3 get_position(uint2 pos)
{
    const float2 uv = (pos + 0.5f) / (buffer_frame.resolution_render * buffer_frame.resolution_scale);
    return get_position(get_depth(pos), uv);
}

float3 get_position_view_space(uint2 pos)
{
    return mul(float4(get_position(pos), 1.0f), get_view()).xyz;
}

float3 get_position_view_space(float2 uv)
{
    return mul(float4(get_position(get_depth(uv), uv / buffer_frame.resolution_scale), 1.0f), get_view()).xyz;
}

/*------------------------------------------------------------------------------
    VELOCITY
------------------------------------------------------------------------------*/
float2 get_velocity_uv(uint2 pos)
{
    return tex_velocity[pos].xy;
}

float2 get_velocity_uv(float2 uv)
{
    return tex_velocity.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).xy;
}

/*------------------------------------------------------------------------------
    VIEW DIRECTION
------------------------------------------------------------------------------*/
float3 get_view_direction(float3 position_world)
{
    return normalize(position_world - get_camera_position());
}

float3 get_view_direction(float depth, float2 uv)
{
    return get_view_direction(get_position(depth, uv));
}

float3 get_view_direction(float2 uv)
{
    return get_view_direction(get_position(get_depth(uv), uv / buffer_frame.resolution_scale));
}

float3 get_view_direction(uint2 pos, float2 resolution)
{
    const float2 uv = (pos + 0.5f) / resolution;
    return get_view_direction(uv);
}

float3 get_view_direction_view_space(float2 uv)
{
    return mul(float4(get_view_direction(get_position(get_depth(uv), uv / buffer_frame.resolution_scale)), 0.0f), get_view()).xyz;
}

float3 get_view_direction_view_space(uint2 pos, float2 resolution)
{
    const float2 uv = (pos + 0.5f) / resolution;
    return get_view_direction_view_space(uv);
}

float3 get_view_direction_view_space(float3 position_world)
{
    return mul(float4(get_view_direction(position_world), 0.0f), get_view()).xyz;
}

/*------------------------------------------------------------------------------
    DIRECTION UV
------------------------------------------------------------------------------*/
float2 direction_sphere_uv(float3 direction)
{
    float u = 0.5f + atan2(direction.z, direction.x) / PI2;
    float v = 0.5f - asin(direction.y) / PI;

    return float2(u, v);
}

/*------------------------------------------------------------------------------
    LUMINANCE
------------------------------------------------------------------------------*/
static const float3 srgb_color_space_coefficient = float3(0.299f, 0.587f, 0.114f);

float luminance(float3 color)
{
    return max(dot(color, srgb_color_space_coefficient), FLT_MIN);
}

float luminance(float4 color)
{
    return max(dot(color.rgb, srgb_color_space_coefficient), FLT_MIN);
}

/*------------------------------------------------------------------------------
    HASHES & NOISE
------------------------------------------------------------------------------*/
// fast 1d hash
float hash(float p)
{
    // scale input, convert to uint for bit manipulation
    uint u = asuint(p * 3141592653.0f);
    
    // mix with multiply and xor, normalize to [0,1)
    return float(u * u * 3141592653u) / 4294967295.0f;
}

// fast 2d hash
float hash(float2 p)
{
    // scale each component, convert to uint2
    uint2 u = asuint(p * float2(141421356.0f, 2718281828.0f));
    
    // combine with xor, mix, normalize to [0,1)
    return float((u.x ^ u.y) * 3141592653u) / 4294967295.0f;
}

float noise_perlin(float x)
{
    float scale = 0.1f;
    return tex_perlin.SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), float2(x * scale, 0.5f), 0).r * 2.0f - 1.0f;
}

float noise_perlin(float2 x)
{
    float scale = 0.1f;
    return tex_perlin.SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), x * scale, 0).r * 2.0f - 1.0f;
}

// spartan take on the interleaved gradient function from Jimenez 2014 http://goo.gl/eomGso
float noise_interleaved_gradient(float2 screen_pos, bool temporal = true)
{
    // temporal factor
    float animate      = saturate((float)is_taa_enabled() + (float)temporal);
    float frame_count  = (float)buffer_frame.frame;
    float frame_step   = float(frame_count % 16) * RPC_16 * animate;
    screen_pos.x      += frame_step * 4.7526;
    screen_pos.y      += frame_step * 3.1914;

    float3 magic = float3(0.06711056f, 0.00583715f, 52.9829189f);
    return frac(magic.z * frac(dot(screen_pos, magic.xy)));
}

// remaps perceptual roughness to a ggx alpha, matches D_GGX_Alpha in brdf.hlsl (call of duty
// wwii remap) so the reflection ray cone tracks the exact same specular lobe the surface shading
// uses, a plain roughness*roughness mapping is several times wider here and washes glossy
// reflections into a matte sheen
float ggx_alpha_from_roughness(float roughness)
{
    float gloss = 1.0f - roughness;
    return sqrt(2.0f / (1.0f + pow(2.0f, 18.0f * gloss)));
}

// ggx visible normal distribution sampler, heitz 2018, ve is the view direction expressed in
// the local frame where z is the surface normal, returns a microfacet normal in that same frame,
// at alpha 0 it collapses to the surface normal so mirror surfaces stay perfectly sharp
float3 ggx_vndf_sample(float3 ve, float2 xi, float alpha)
{
    float a = max(alpha, 1e-3f);

    float3 vh = normalize(float3(a * ve.x, a * ve.y, ve.z));

    float  lensq = vh.x * vh.x + vh.y * vh.y;
    float3 t1    = (lensq > 0.0f) ? (float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq)) : float3(1.0f, 0.0f, 0.0f);
    float3 t2    = cross(vh, t1);

    float r        = sqrt(xi.x);
    float phi      = 2.0f * PI * xi.y;
    float t1_coeff = r * cos(phi);
    float t2_coeff = r * sin(phi);
    float s        = 0.5f * (1.0f + vh.z);
    t2_coeff       = (1.0f - s) * sqrt(1.0f - t1_coeff * t1_coeff) + s * t2_coeff;

    float3 nh = t1_coeff * t1 + t2_coeff * t2 + sqrt(max(0.0f, 1.0f - t1_coeff * t1_coeff - t2_coeff * t2_coeff)) * vh;

    return normalize(float3(a * nh.x, a * nh.y, max(0.0f, nh.z)));
}

/*------------------------------------------------------------------------------
    OCCLUSION/SHADOWING
------------------------------------------------------------------------------*/
// The Technical Art of Uncharted 4 - http://advances.realtimerendering.com/other/2016/naughty_dog/index.html
float microw_shadowing_nt(float n_dot_l, float ao)
{
    float aperture = 2.0f * ao * ao;
    return saturate(abs(n_dot_l) + aperture - 1.0f);
}

// chan 2018, material advances in call of duty wwii
// canonical paper form, the prior rsqrt(1 - visibility) aperture exploded for visibility close
// to one and the n_dot_l multiplication then crushed grazing angles to near zero with even a
// tiny amount of ssao occlusion, which is what made flat ground at sunset go almost black the
// moment gtao bent normals were enabled, the canonical form below stays at one for visibility
// equal to one, only bites near the terminator, and collapses to zero only when visibility and
// n_dot_l vanish together, matching the curves in the cod wwii slides
float microw_shadowing_cod(float n_dot_l, float visibility)
{
    float aperture    = 2.0f * visibility;
    float microShadow = saturate(n_dot_l + aperture - 1.0f);
    return microShadow * microShadow;
}

/*------------------------------------------------------------------------------
    MISC
------------------------------------------------------------------------------*/
bool is_valid_uv(float2 value) { return (value.x >= 0.0f && value.x <= 1.0f) && (value.y >= 0.0f && value.y <= 1.0f); }

float screen_fade(float2 uv)
{
    float2 fade = max(0.0f, 12.0f * abs(uv - 0.5f) - 5.0f);
    return saturate(1.0f - dot(fade, fade));
}

// find good arbitrary axis vectors to represent U and V axes of a plane, given just the normal. ported from UnMath.h
void find_best_axis_vectors(float3 In, out float3 Axis1, out float3 Axis2)
{
    const float3 N = abs(In);

    // find best basis vector
    if (N.z > N.x && N.z > N.y)
    {
        Axis1 = float3(1, 0, 0);
    }
    else
    {
        Axis1 = float3(0, 0, 1);
    }

    Axis1 = normalize(Axis1 - In * dot(Axis1, In));
    Axis2 = cross(Axis1, In);
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

float get_alpha_threshold(float3 position_world)
{
    static const float ALPHA_THRESHOLD_DEFAULT = 0.6f;
    static const float ALPHA_MAX_DISTANCE      = 700.0f;
    static const float ALPHA_MAX_DISTANCE_SQ = ALPHA_MAX_DISTANCE * ALPHA_MAX_DISTANCE;

    // beyond max distance, no alpha testing (threshold = 0)
    float3 offset           = position_world - get_camera_position();
    float pixel_distance_sq = dot(offset, offset);
    float distance_factor   = step(ALPHA_MAX_DISTANCE_SQ, pixel_distance_sq);
    
    return saturate(lerp(ALPHA_THRESHOLD_DEFAULT, 0.0f, distance_factor));
}

//= INCLUDES ===========================
#include "common_structs.hlsl"
#include "common_terrain.hlsl"
#include "common_vertex_processing.hlsl"
//======================================

#endif // SPARTAN_COMMON
