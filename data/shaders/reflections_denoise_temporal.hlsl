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

//= INCLUDES =========
#include "common.hlsl"
//====================

// svgf style temporal accumulation for stochastic ray traced reflections (schied 2017), the
// reflection ray is jittered across the ggx lobe every frame so this pass reprojects the
// previous accumulation by surface motion and blends it with the noisy current frame, the
// variance written into alpha drives the spatial pass kernel
// bindings:
//   tex      raw noisy reflection (this frame)
//   tex2     accumulated reflection history (previous frame)
//   tex3     moments history (luma_m1, luma_m2, sample_count)
//   tex4     previous frame depth
//   tex_uav  out, rgb accumulated color, a per pixel luminance variance estimate
//   tex_uav2 out, (luma_m1, luma_m2, sample_count, unused)

static const float3 luminance_weights = float3(0.299f, 0.587f, 0.114f);

float2 reproject_to_previous_frame(float2 current_uv)
{
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv, 0).xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    return current_uv - velocity_uv;
}

float3 sample_input(int2 pixel, uint2 resolution)
{
    uint2 clamped_pixel = (uint2)clamp(pixel, int2(0, 0), int2(resolution) - 1);
    return max(tex.Load(int3(clamped_pixel, 0)).rgb, 0.0f);
}

void compute_local_statistics(uint2 pixel, uint2 resolution, out float3 mean, out float3 sigma)
{
    float3 second_moment = 0.0f;
    mean                 = 0.0f;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 sp             = clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(resolution) - 1);
            float3 sample_color = sample_input(sp, resolution);
            mean          += sample_color;
            second_moment += sample_color * sample_color;
        }
    }
    mean /= 9.0f;
    float3 variance = max(second_moment / 9.0f - mean * mean, 0.0f);
    sigma = sqrt(variance);
}

// depth based disocclusion gate, the reflection signal has no previous normal buffer so the
// previous linear depth at the reprojected uv is compared against the current linear depth, a
// surface that survives reprojection keeps its history, a disoccluded one resets to the current
// noisy frame and lets the spatial pass bootstrap from the neighborhood
bool is_history_valid(float2 prev_uv, float current_linear_z, out float confidence)
{
    confidence = 0.0f;
    if (!is_valid_uv(prev_uv))
    {
        return false;
    }

    float prev_depth_raw = tex4.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).r;
    if (prev_depth_raw <= 0.0f)
    {
        return false;
    }

    float prev_linear_z = linearize_depth(prev_depth_raw);
    float z_delta       = abs(prev_linear_z - current_linear_z) / max(current_linear_z, 1e-3f);
    if (z_delta > 0.1f)
    {
        return false;
    }

    confidence = saturate(1.0f - z_delta / 0.1f);
    return true;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);
    uint2 resolution = uint2(resolution_x, resolution_y);

    if (pixel.x >= resolution.x || pixel.y >= resolution.y)
    {
        return;
    }

    float2 uv    = (pixel + 0.5f) / resolution;
    float  depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[pixel]  = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_uav2[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 current_color    = sample_input(int2(pixel), resolution);
    float  current_linear_z = linearize_depth(depth);
    float  current_luma     = dot(current_color, luminance_weights);
    float  source_roughness = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    uint   source_material_index = uint(tex_normal.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).a);
    MaterialParameters source_mat = material_parameters[source_material_index];
    source_roughness = lerp(source_roughness, source_mat.clearcoat_roughness, saturate(source_mat.clearcoat));
    float  source_alpha     = ggx_alpha_from_roughness(source_roughness);
    float  rough_reflection = smoothstep(0.03f, 0.45f, source_alpha);

    float2 prev_uv         = reproject_to_previous_frame(uv);
    float  confidence      = 0.0f;
    float  history_weight  = 0.0f;
    float3 history_color   = current_color;
    float3 history_moments = float3(current_luma, current_luma * current_luma, 0.0f);

    bool history_ok = is_history_valid(prev_uv, current_linear_z, confidence);
    if (history_ok)
    {
        history_color = max(tex2.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), prev_uv, 0).rgb, 0.0f);

        int2 prev_pixel = clamp(int2(prev_uv * resolution), int2(0, 0), int2(resolution) - 1);
        history_moments = tex3.Load(int3(prev_pixel, 0)).xyz;

        // schied 2017 alpha schedule, the 1/(n+1) ramp lowers the new sample weight as frames
        // accumulate, the floor keeps a held still view averaging down residual lobe noise
        float n_eff     = history_moments.z;
        float min_alpha = saturate(1.0f / max(n_eff + 1.0f, 1.0f));
        float ema_floor = lerp(0.03f, 0.01f, rough_reflection);
        float ema_alpha = max(min_alpha, ema_floor);
        float max_history = lerp(0.97f, 0.99f, rough_reflection);
        history_weight  = saturate(min((1.0f - ema_alpha) * confidence, max_history));
    }

    // local neighborhood stats stabilize the variance estimate and feed the firefly clamp
    float3 mean_color, sigma_color;
    compute_local_statistics(pixel, resolution, mean_color, sigma_color);
    float spatial_sigma_luma = dot(sigma_color, luminance_weights);

    float raw_mean_luma = dot(mean_color, luminance_weights);
    float raw_band      = lerp(10.0f, 2.0f, rough_reflection);
    float raw_high      = raw_mean_luma + raw_band * max(spatial_sigma_luma, 0.05f);
    if (rough_reflection > 0.0f && current_luma > raw_high && current_luma > 1e-3f)
    {
        current_color *= raw_high / current_luma;
        current_luma   = raw_high;
    }

    // firefly soft clamp before the moment ema, a bright lobe sample is pulled back toward the
    // converged temporal mean so it cannot slowly accumulate and smear across frames
    if (history_ok)
    {
        float history_sigma = sqrt(max(history_moments.y - history_moments.x * history_moments.x, 0.0f));
        float band_sigma    = max(history_sigma, spatial_sigma_luma);
        float band_widen    = lerp(12.0f, 6.0f, saturate(history_moments.z / 4.0f));
        float clamp_high    = history_moments.x + band_widen * max(band_sigma, 0.05f);
        if (current_luma > clamp_high && current_luma > 1e-3f)
        {
            current_color *= clamp_high / current_luma;
            current_luma   = clamp_high;
        }
    }

    float ema_alpha = 1.0f - history_weight;
    float new_m1    = lerp(history_moments.x, current_luma, ema_alpha);
    float new_m2    = lerp(history_moments.y, current_luma * current_luma, ema_alpha);
    float new_n_eff = min(history_moments.z + 1.0f, 256.0f);

    float temporal_var      = max(new_m2 - new_m1 * new_m1, 0.0f);
    float variance_estimate = max(temporal_var, spatial_sigma_luma * spatial_sigma_luma * 0.25f);

    float3 output_color = lerp(current_color, history_color, history_weight);

    tex_uav[pixel]  = validate_output(float4(output_color, max(variance_estimate, 0.0f)));
    tex_uav2[pixel] = float4(new_m1, new_m2, new_n_eff, 0.0f);
}
