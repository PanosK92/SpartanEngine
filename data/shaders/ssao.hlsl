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
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// constants
static const float g_ao_radius        = 1.5f;
static const float g_ao_intensity     = 1.0f;
static const uint  g_directions       = 3;
static const uint  g_steps            = 3;
// r2 (roberts 2018), low discrepancy 2d sequence with no short period beats
static const float g_r2_a1            = 0.7548776662466927f;
static const float g_r2_a2            = 0.5698402909980532f;
// short history, taa owns residual noise, prefer reject over ghosting
static const float g_max_accum_frames = 8.0f;
static const float g_depth_reject     = 0.01f;
static const float g_reproj_tol_px    = 0.75f;
static const float g_disoccl_rel      = 0.01f;
static const float g_motion_reject_px = 0.75f;
static const float g_vis_reject       = 0.04f;
static const float g_bent_reject      = 0.98f;
static const float g_wake_motion_px   = 1.5f;
static const float g_wake_delta_px    = 1.0f;

float2 temporal_r2(uint frame)
{
    float f = float(frame) + 0.5f;
    return float2(frac(f * g_r2_a1), frac(f * g_r2_a2));
}

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

// jimenez / xegtao rotfromto, maps local -z onto view
float3x3 rotation_from_to(float3 from, float3 to)
{
    from            = normalize(from);
    to              = normalize(to);
    float cos_theta = dot(from, to);

    if (cos_theta > 0.9999f)
    {
        return float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1);
    }

    if (cos_theta < -0.9999f)
    {
        return float3x3(1, 0, 0, 0, -1, 0, 0, 0, -1);
    }

    float3 axis     = normalize(cross(from, to));
    float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
    float omc       = 1.0f - cos_theta;
    float x         = axis.x;
    float y         = axis.y;
    float z         = axis.z;

    float3x3 mat;
    mat._11 = cos_theta + x * x * omc;
    mat._12 = x * y * omc - z * sin_theta;
    mat._13 = x * z * omc + y * sin_theta;
    mat._21 = y * x * omc + z * sin_theta;
    mat._22 = cos_theta + y * y * omc;
    mat._23 = y * z * omc - x * sin_theta;
    mat._31 = z * x * omc - y * sin_theta;
    mat._32 = z * y * omc + x * sin_theta;
    mat._33 = cos_theta + z * z * omc;
    return mat;
}

// returns cosine-weighted slice visibility and horizon angles for bent normals
float compute_slice_visibility(
    float horizon_cos0,
    float horizon_cos1,
    float cos_norm,
    float n,
    out float h0,
    out float h1
)
{
    h0 = -fast_acos(horizon_cos1);
    h1 = fast_acos(horizon_cos0);
    h0 = n + clamp(h0 - n, -PI_HALF, PI_HALF);
    h1 = n + clamp(h1 - n, -PI_HALF, PI_HALF);

    float iarc0 = (cos_norm + 2.0f * h0 * sin(n) - cos(2.0f * h0 - n)) * 0.25f;
    float iarc1 = (cos_norm + 2.0f * h1 * sin(n) - cos(2.0f * h1 - n)) * 0.25f;
    return iarc0 + iarc1;
}

// jimenez 2016 eqs 20-21, t0 along slice omega, t1 along local +z
float2 compute_slice_bent_t(float h0, float h1, float n)
{
    float t0 = (
        6.0f * sin(h0 - n) - sin(3.0f * h0 - n)
        + 6.0f * sin(h1 - n) - sin(3.0f * h1 - n)
        + 16.0f * sin(n)
        - 3.0f * (sin(h0 + n) + sin(h1 + n))
    ) / 12.0f;
    float t1 = (
        -cos(3.0f * h0 - n) - cos(3.0f * h1 - n)
        + 8.0f * cos(n)
        - 3.0f * (cos(h0 + n) + cos(h1 + n))
    ) / 12.0f;
    return float2(t0, t1);
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

    uint2 pos                          = thread_id.xy;
    const float2 origin_uv             = (pos + 0.5f) / resolution_out;
    const float3 origin_position       = get_position_view_space(origin_uv);
    const float3 origin_normal         = get_normal_view_space(origin_uv);
    const float3 geo_normal_world      = view_to_world(origin_normal, false);

    // r2 temporal offsets, spatial ign stays non-temporal so frames do not beat
    const float2 r2                    = temporal_r2(buffer_frame.frame);
    const float noise_gradient_spatial = noise_interleaved_gradient(pos, false);
    const float offset_spatial         = 0.25 * (float)((pos.y - pos.x) & 3);
    const float ray_offset             = frac(offset_spatial + r2.x)
        + (hash(origin_uv) * 2.0f - 1.0f) * 0.25f;
    const float2 texel_size            = 1.0f / resolution_out;
    const float3 view_vec              = normalize(-origin_position);

    const float falloff_range = 0.6f * g_ao_radius;
    const float falloff_from  = g_ao_radius - falloff_range;
    float falloff_mul         = -1.0f / falloff_range;
    float falloff_add         = falloff_from / falloff_range + 1.0f;
    float3 pos_right          = get_position_view_space(origin_uv + float2(texel_size.x, 0));
    float pixel_dir_rb_viewspace_size_at_center_z = length(pos_right - origin_position);
    float screenspace_radius  = g_ao_radius / pixel_dir_rb_viewspace_size_at_center_z;
    const float pixel_too_close_threshold = 1.3f;
    const float min_s         = pixel_too_close_threshold / screenspace_radius;
    const float noise_slice   = noise_gradient_spatial + r2.y;
    const float noise_sample  = ray_offset;

    // same matrix for every slice, local -z maps onto view (toward camera)
    const float3x3 local_to_view = rotation_from_to(float3(0.0f, 0.0f, -1.0f), view_vec);

    float visibility   = 0.0f;
    float3 bent_normal = 0.0f;

    [unroll]
    for (uint slice = 0; slice < g_directions; slice++)
    {
        float slice_k                    = (float(slice) + noise_slice) / float(g_directions);
        float phi                        = slice_k * PI;
        float cos_phi                    = cos(phi);
        float sin_phi                    = sin(phi);
        float2 omega                     = float2(cos_phi, -sin_phi) * screenspace_radius;
        const float3 direction_vec       = float3(cos_phi, sin_phi, 0.0f);
        const float3 ortho_direction_vec = direction_vec - dot(direction_vec, view_vec) * view_vec;
        const float3 axis_vec            = normalize(cross(ortho_direction_vec, view_vec));

        float3 projected_normal_vec       = origin_normal - axis_vec * dot(origin_normal, axis_vec);
        float projected_normal_vec_length = length(projected_normal_vec);
        float cos_norm = saturate(
            dot(projected_normal_vec, view_vec) / max(projected_normal_vec_length, 1e-6f)
        );
        float sign_norm = sign(dot(ortho_direction_vec, projected_normal_vec));
        float n         = sign_norm * fast_acos(cos_norm);

        float low_horizon_cos0 = cos(n + PI_HALF);
        float low_horizon_cos1 = cos(n - PI_HALF);
        float horizon_cos0     = low_horizon_cos0;
        float horizon_cos1     = low_horizon_cos1;

        [unroll]
        for (uint step = 0; step < g_steps; step++)
        {
            float step_base_noise = float(slice + step * g_steps) * 0.6180339887498948482f;
            float step_noise      = frac(noise_sample + step_base_noise);
            float s               = (step + step_noise) / float(g_steps);
            s                    += min_s;

            float2 sample_offset       = round(s * omega) * texel_size;
            float2 sample_screen_pos0  = origin_uv + sample_offset;
            float3 sample_pos0         = get_position_view_space(sample_screen_pos0);
            float3 sample_delta0       = sample_pos0 - origin_position;
            float sample_dist0         = length(sample_delta0);
            float3 sample_horizon_vec0 = sample_delta0 / max(sample_dist0, 1e-6f);

            float2 sample_screen_pos1  = origin_uv - sample_offset;
            float3 sample_pos1         = get_position_view_space(sample_screen_pos1);
            float3 sample_delta1       = sample_pos1 - origin_position;
            float sample_dist1         = length(sample_delta1);
            float3 sample_horizon_vec1 = sample_delta1 / max(sample_dist1, 1e-6f);

            float weight0 = saturate(sample_dist0 * falloff_mul + falloff_add);
            float weight1 = saturate(sample_dist1 * falloff_mul + falloff_add);

            float shc0 = dot(sample_horizon_vec0, view_vec);
            float shc1 = dot(sample_horizon_vec1, view_vec);
            shc0       = lerp(low_horizon_cos0, shc0, weight0);
            shc1       = lerp(low_horizon_cos1, shc1, weight1);

            horizon_cos0 = max(horizon_cos0, shc0);
            horizon_cos1 = max(horizon_cos1, shc1);
        }

        // analytic gtao visibility and bent normal share the same horizons
        float h0, h1;
        float slice_vis = compute_slice_visibility(
            horizon_cos0,
            horizon_cos1,
            cos_norm,
            n,
            h0,
            h1
        );

        // paper / xegtao, weight by projected normal length so grazing slices fade out
        visibility += slice_vis * projected_normal_vec_length;

        // algorithm 2, local -t1 for handedness, then rotate -z onto view, weight by proj length
        float2 t = compute_slice_bent_t(h0, h1, n);
        float3 local_bent = float3(direction_vec.x * t.x, direction_vec.y * t.x, -t.y);
        bent_normal += mul(local_to_view, local_bent) * projected_normal_vec_length;
    }

    visibility /= float(g_directions);
    visibility  = pow(saturate(visibility), g_ao_intensity);
    // match xegtao floor, a fully occluded visible pixel is still lit a little
    visibility  = max(visibility, 0.03f);

    // world space bent normal, fall back to geometric normal when the integral collapses
    float3 bent_view = bent_normal;
    float bent_len_sq = dot(bent_view, bent_view);
    if (bent_len_sq > 1e-8f && !any(isnan(bent_view)))
    {
        bent_normal = normalize(view_to_world(normalize(bent_view), false));
    }
    else
    {
        bent_normal = geo_normal_world;
    }

    if (isnan(visibility))
    {
        visibility = 1.0f;
    }

    // temporal, fail closed on any ghosting risk, taa clears the noise
    float3 out_bent = bent_normal;
    float  out_vis  = visibility;
    float  accum    = 1.0f;

    float3 world_pos   = get_position(origin_uv);
    float2 velocity_uv = get_velocity_uv(pos) * float2(0.5f, -0.5f);
    float2 uv_prev     = origin_uv - velocity_uv;
    float  motion_px   = length(velocity_uv * resolution_out);

    // wake, static pixel next to a fast mover (road behind car) must go noisy
    float neighbor_motion_px = 0.0f;
    [unroll]
    for (int my = -2; my <= 2; my++)
    {
        [unroll]
        for (int mx = -2; mx <= 2; mx++)
        {
            if (mx == 0 && my == 0)
            {
                continue;
            }
            int2 p = int2(pos) + int2(mx, my);
            if (any(p < 0) || any(p >= int2(resolution_out)))
            {
                continue;
            }
            float2 v = get_velocity_uv(uint2(p)) * float2(0.5f, -0.5f);
            neighbor_motion_px = max(neighbor_motion_px, length(v * resolution_out));
        }
    }
    bool in_wake = neighbor_motion_px > g_wake_motion_px
        && (neighbor_motion_px - motion_px) > g_wake_delta_px;

    float2 inset         = 2.0f / resolution_out;
    bool   history_valid = pass_get_f3_value().x < 0.5f
        && !in_wake
        && motion_px < g_motion_reject_px
        && all(uv_prev > inset)
        && all(uv_prev < 1.0f - inset);

    if (history_valid)
    {
        float4 hist = tex.SampleLevel(samplers[sampler_point_clamp], uv_prev, 0);

        float4 prev_clip = mul(float4(world_pos, 1.0f), get_view_projection_previous());
        float  prev_w    = max(prev_clip.w, 1e-9f);
        float3 prev_ndc  = prev_clip.xyz / prev_w;
        float2 expected_uv = prev_ndc.xy * float2(0.5f, -0.5f) + 0.5f;
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
