/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// cortao (circular occluder ray traced ambient occlusion), see cortao/LICENSE
// cortao view space is right handed (camera looks down -z), spartan view space looks down +z, so z flips at the callbacks
#define CORTAO_HLSL                    1
#define CORTAO_PERSPECTIVE_PROJECTION  1
#define CORTAO_PACKED_NORMAL_BITS      10

// the shader preprocessor inlines each file once, so the shared base has to sit outside the variant branches
#include "cortao/CORTAOBase.h"

// constants
static const float g_ao_radius                  = 1.5f;  // world space, metres
static const float g_ao_screen_radius_ratio     = 0.2f;  // max sampling radius as a fraction of the screen height
static const float g_ao_mip_bias                = -4.0f;
static const float g_ao_max_slope_compensation  = 4.0f;
static const float g_ao_backface_threshold      = 0.5f;
static const float g_ao_backface_boost          = 0.5f;

// the packed buffer is r32 float, depth sits in the float bits with the normal in the low mantissa bits
float4 cortao_projection_terms()
{
    matrix projection = get_projection();
    return float4(projection[0][0], projection[1][1], projection[2][0], projection[2][1]);
}

uint cortao_frame_seed()
{
    uint state = buffer_frame.frame * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint cortao_packed_resolution()
{
    uint2 resolution = uint2(get_render_resolution_active());
    return resolution.x | (resolution.y << 16u);
}

float3 cortao_load_view_normal(int2 pixel)
{
    float3 normal = get_normal_view_space(uint2(pixel));
    return float3(normal.x, normal.y, -normal.z);
}

#if defined(SSAO_PREPASS)

#include "cortao/CORTAOPrepass.h"

cortao_float cortao_loadDepth(cortao_int2 pixelCoords)
{
    return get_depth(uint2(pixelCoords));
}

cortao_float3 cortao_loadNormal(cortao_int2 pixelCoords)
{
    return cortao_load_view_normal(pixelCoords);
}

void cortao_writePackedData(cortao_int2 pixelCoords, cortao_int mipLevel, cortao_uint data)
{
    tex_uav_mips[mipLevel][pixelCoords] = asfloat(data);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // reverse z, d = p22 + p32 / z, so 1 / z = d / p32 - p22 / p32
    matrix projection = get_projection();

    CORTAOPrepassParameters params;
    params.packedResolution = cortao_packed_resolution();
    params.depthScale       = 1.0f / projection[3][2];
    params.depthOffset      = -projection[2][2] / projection[3][2];
    params.randomSeed       = cortao_frame_seed();
    params.mipCount         = pass_uint(pass_ssao::mip_count);
    params._padding0        = 0;
    params._padding1        = 0;
    params._padding2        = 0;

    cortao_prepass(thread_id.xy, params);
}

#else

#define CORTAO_SAMPLE_COUNT             8
#define CORTAO_RAY_COUNT                16
#define CORTAO_OCCLUDER_LOOKUP_TABLE    1
#define CORTAO_BENT_NORMAL_LOOKUP_TABLE 1

#include "cortao/CORTAOMainPass.h"

// short history, taa owns residual noise, prefer reject over ghosting
static const float g_max_accum_frames = 8.0f;
static const float g_depth_reject     = 0.01f;
static const float g_reproj_tol_px    = 0.75f;
static const float g_disoccl_rel      = 0.01f;
static const float g_motion_reject_px = 0.75f;
static const float g_vis_reject       = 0.35f; // cortao traces 16 rays a frame, a single frame is that noisy
static const float g_bent_reject      = 0.5f;
static const float g_wake_motion_px   = 1.5f;
static const float g_wake_delta_px    = 1.0f;

static float  g_cortao_occlusion = 1.0f;
static float3 g_cortao_bent      = float3(0.0f, 0.0f, 0.0f);
static bool   g_cortao_written   = false;

float2 octahedral_encode_ssao(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0f)
    {
        float2 sign_not_zero = float2(
            n.x >= 0.0f ? 1.0f : -1.0f,
            n.y >= 0.0f ? 1.0f : -1.0f
        );
        n.xy = (1.0f - abs(n.yx)) * sign_not_zero;
    }
    return n.xy;
}

float3 octahedral_decode_ssao(float2 e)
{
    float3 n = float3(e.xy, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
    {
        float2 sign_not_zero = float2(
            n.x >= 0.0f ? 1.0f : -1.0f,
            n.y >= 0.0f ? 1.0f : -1.0f
        );
        n.xy = (1.0f - abs(n.yx)) * sign_not_zero;
    }
    return normalize(n);
}

// tex3 = packed depth and normal mips, tex4 = bent normal table, tex3d = occluder table
cortao_uint cortao_samplePackedData(cortao_float2 texCoords, cortao_float mipmap)
{
    // cortao expects a border of 0 (no occluder), the packed target is also larger than the active render area
    if (any(texCoords < 0.0f) || any(texCoords >= 1.0f))
    {
        return 0u;
    }

    return asuint(tex3.SampleLevel(samplers[sampler_point_clamp], texCoords * get_render_uv_scale(), mipmap).r);
}

cortao_half3 cortao_loadNormal(cortao_int2 pixelCoords)
{
    return cortao_load_view_normal(pixelCoords);
}

// r16 unorm stand in for the r16 uint table, the bits come back exactly
cortao_uint cortao_sampleOccluderLookupTable(cortao_float3 texCoords)
{
    return uint(tex3d.SampleLevel(samplers[sampler_point_clamp], texCoords, 0.0f).r * 65535.0f + 0.5f);
}

// 256x256 r8g8 unorm holding the raw snorm bytes, one texel per ray mask
cortao_half3 cortao_loadBentNormalLookupTable(cortao_uint rayBits)
{
    uint2 bytes  = uint2(tex4.Load(int3(rayBits & 0xFFu, rayBits >> 8u, 0)).rg * 255.0f + 0.5f);
    int2 values  = int2(bytes) - int2(bytes >= 128u) * 256;
    float2 xy    = max(float2(values) / 127.0f, -1.0f);
    return float3(xy, sqrt(max(0.0f, 1.0f - dot(xy, xy))));
}

void cortao_writeResult(cortao_int2 pixelCoords, cortao_half occlusion, cortao_half3 bentNormal, cortao_half3x3 tangentSpaceTransform, cortao_uint rayBits)
{
    g_cortao_occlusion = occlusion;
    g_cortao_bent      = bentNormal;
    g_cortao_written   = true;
}

CORTAOMainPassParameters cortao_main_pass_parameters()
{
    // spartan projection is left handed, x' = p00 x / z + p20, y' = p11 y / z + p21, uv.y points down
    float4 projection  = cortao_projection_terms();
    float2 resolution  = get_render_resolution_active();
    float inverse_x    = 1.0f / projection.x;
    float inverse_y    = 1.0f / projection.y;
    float apparent_max = inverse_y * g_ao_screen_radius_ratio * 2.0f;

    CORTAOMainPassParameters params;
    params.packedResolution                   = cortao_packed_resolution();
    params.randomSeed                         = cortao_frame_seed();
    params.normalBias                         = inverse_x / resolution.x * sqrt(2.0f);
    params.radius                             = g_ao_radius;
    params.unprojectParams                    = float4(2.0f * inverse_x, -2.0f * inverse_y, (-1.0f - projection.z) * inverse_x, (1.0f - projection.w) * inverse_y);
    params.projectionParams                   = float4(0.5f * projection.x, -0.5f * projection.y, -0.5f - 0.5f * projection.z, -0.5f + 0.5f * projection.w);
    params.maxApparentCircleRadiusCoefficient = apparent_max / sqrt(apparent_max * apparent_max + 1.0f);
    params.mipBias                            = log2(projection.y * resolution.y * 0.5f) + g_ao_mip_bias;
    params.inverseMaxSlopeCompensation        = 1.0f / g_ao_max_slope_compensation;
    params.backfaceThreshold                  = g_ao_backface_threshold;
    params.backfaceBoost                      = g_ao_backface_boost;
    params._padding0                          = 0;
    params._padding1                          = 0;
    params._padding2                          = 0;
    return params;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(thread_id.xy >= uint2(resolution_out)))
    {
        return;
    }

    uint2 pos              = thread_id.xy;
    const float2 origin_uv = (pos + 0.5f) / resolution_out;
    const float depth_raw  = get_depth(pos);

    cortao_mainPass(pos, cortao_main_pass_parameters());

    if (depth_raw < 1e-4f || !g_cortao_written)
    {
        float3 sky_bent = float3(0.0f, 1.0f, 0.0f);
        tex_uav[thread_id.xy]  = float4(sky_bent, 1.0f);
        tex_uav2[thread_id.xy] = float4(octahedral_encode_ssao(sky_bent), 1.0f, 1.0f);
        return;
    }

    float visibility = saturate(g_cortao_occlusion);

    // back to spartan view space, then world, fall back to the shading normal when every ray is blocked
    float3 bent_view   = float3(g_cortao_bent.x, g_cortao_bent.y, -g_cortao_bent.z);
    float3 bent_normal = get_normal(pos);
    if (dot(bent_view, bent_view) > 1e-8f && !any(isnan(bent_view)))
    {
        bent_normal = normalize(view_to_world(normalize(bent_view), false));
    }

    // temporal, fail closed on any ghosting risk, taa clears the noise
    float3 out_bent = bent_normal;
    float  out_vis  = visibility;
    float  accum    = 1.0f;

    float3 world_pos   = get_position(origin_uv);
    // velocity is a screen uv delta, origin_uv and the history live in the scaled render subrect
    float2 uv_scale      = get_render_uv_scale();
    float2 resolution_px = get_render_resolution_active();
    float2 velocity_uv   = get_velocity_uv(pos) * float2(0.5f, -0.5f);
    float2 uv_prev       = origin_uv - velocity_uv * uv_scale;
    float  motion_px     = length(velocity_uv * resolution_px);

    // plus of radius 2, same wake reject without a 5x5 velocity fetch
    static const int2 wake_taps[8] =
    {
        int2( 1,  0), int2(-1,  0), int2(0,  1), int2( 0, -1),
        int2( 2,  0), int2(-2,  0), int2(0,  2), int2( 0, -2)
    };
    float neighbor_motion_px = 0.0f;
    [unroll]
    for (int t = 0; t < 8; t++)
    {
        int2 p = int2(pos) + wake_taps[t];
        if (any(p < 0) || any(p >= int2(resolution_px)))
        {
            continue;
        }
        float2 v = get_velocity_uv(uint2(p)) * float2(0.5f, -0.5f);
        neighbor_motion_px = max(neighbor_motion_px, length(v * resolution_px));
    }
    bool in_wake = neighbor_motion_px > g_wake_motion_px
        && (neighbor_motion_px - motion_px) > g_wake_delta_px;

    float2 inset         = 2.0f / resolution_out;
    bool   history_valid = !pass_bool(pass_ssao::reset_history)
        && !in_wake
        && motion_px < g_motion_reject_px
        && all(uv_prev > inset)
        && all(uv_prev < uv_scale - inset);

    if (history_valid)
    {
        float4 hist = tex.SampleLevel(samplers[sampler_point_clamp], uv_prev, 0);

        float4 prev_clip = mul(float4(world_pos, 1.0f), get_view_projection_previous());
        float  prev_w    = max(prev_clip.w, 1e-9f);
        float3 prev_ndc  = prev_clip.xyz / prev_w;
        float2 expected_uv = screen_uv_to_render_uv(prev_ndc.xy * float2(0.5f, -0.5f) + 0.5f);
        float  reproj_dist = length((uv_prev - expected_uv) * resolution_out);

        float prev_depth_raw    = tex2.SampleLevel(samplers[sampler_point_clamp], uv_prev, 0).r;
        float expected_prev_lin = linearize_depth(prev_ndc.z);
        float actual_prev_lin   = linearize_depth(prev_depth_raw);
        float depth_rel = abs(actual_prev_lin - expected_prev_lin)
            / max(expected_prev_lin, 1e-3f);

        // any screen space depth change at this uv, occluder arrived or left
        float lin_curr    = get_linear_depth(origin_uv);
        float lin_prev_ss = linearize_depth(
            tex2.SampleLevel(samplers[sampler_point_clamp], origin_uv, 0).r
        );
        float ss_rel = abs(lin_curr - lin_prev_ss) / max(max(lin_curr, lin_prev_ss), 1e-3f);

        float3 hist_bent = octahedral_decode_ssao(hist.xy);
        float  bent_ok   = saturate(dot(hist_bent, bent_normal));
        float  vis_delta = abs(hist.z - visibility);

        history_valid = prev_clip.w > 0.0f
            && prev_depth_raw > 0.0f
            && reproj_dist < g_reproj_tol_px
            && depth_rel < g_depth_reject
            && ss_rel < g_disoccl_rel
            && bent_ok > g_bent_reject
            && vis_delta < g_vis_reject
            && hist.w >= 1.0f
            && !any(isnan(hist))
            && !any(isnan(hist_bent));

        if (history_valid)
        {
            accum   = min(hist.w + 1.0f, g_max_accum_frames);
            // floor toward current so accepted history cannot trail
            float w = max(rcp(accum), 0.35f);
            out_vis  = lerp(hist.z, visibility, w);
            out_bent = normalize(lerp(hist_bent, bent_normal, w));
        }
    }

    tex_uav[thread_id.xy]  = float4(out_bent, out_vis);
    tex_uav2[thread_id.xy] = float4(octahedral_encode_ssao(out_bent), out_vis, accum);
}

#endif
