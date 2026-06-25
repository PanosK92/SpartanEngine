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

// svgf style temporal accumulation for stochastic ray traced sun shadows (schied 2017), the
// sun ray is jittered across the solar disk every frame so this pass reprojects the previous
// accumulation by surface motion and blends it with the noisy current frame, the variance
// written into g drives the spatial pass kernel, the blocker distance in b is carried forward
// for penumbra aware filtering
// bindings:
//   tex      raw noisy shadow (this frame), r = visibility, g = blocker distance
//   tex2     accumulated shadow history (previous frame), r = visibility, b = blocker distance
//   tex3     moments history (vis_m1, vis_m2, sample_count)
//   tex4     previous frame depth
//   tex_uav  out, r = accumulated visibility, g = variance, b = blocker distance
//   tex_uav2 out, (vis_m1, vis_m2, sample_count, unused)

float2 reproject_to_previous_frame(float2 current_uv)
{
    float2 velocity_ndc = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv, 0).xy;
    float2 velocity_uv  = velocity_ndc * float2(0.5f, -0.5f);
    return current_uv - velocity_uv;
}

void compute_local_statistics(uint2 pixel, uint2 resolution, out float mean, out float sigma)
{
    float second_moment = 0.0f;
    mean                 = 0.0f;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            int2 sp           = clamp(int2(pixel) + int2(x, y), int2(0, 0), int2(resolution) - 1);
            float sample_value = tex.Load(int3(sp, 0)).r;
            mean          += sample_value;
            second_moment += sample_value * sample_value;
        }
    }
    mean /= 9.0f;
    float variance = max(second_moment / 9.0f - mean * mean, 0.0f);
    sigma = sqrt(variance);
}

// depth based disocclusion gate, the shadow signal has no previous normal buffer so the previous
// linear depth at the reprojected uv is compared against the current linear depth, a surface that
// survives reprojection keeps its history, a disoccluded one resets to the current noisy frame
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
        tex_uav[pixel]  = float4(1.0f, 0.0f, 0.0f, 0.0f);
        tex_uav2[pixel] = float4(1.0f, 1.0f, 0.0f, 0.0f);
        return;
    }

    float4 current_sample   = tex.Load(int3(pixel, 0));
    float  current_vis      = saturate(current_sample.r);
    float  current_hit_dist = current_sample.g;
    float  current_linear_z = linearize_depth(depth);

    float2 prev_uv         = reproject_to_previous_frame(uv);
    float  confidence      = 0.0f;
    float  history_weight  = 0.0f;
    float  history_vis     = current_vis;
    float  history_hd      = current_hit_dist;
    float3 history_moments = float3(current_vis, current_vis * current_vis, 0.0f);

    bool history_ok = is_history_valid(prev_uv, current_linear_z, confidence);
    if (history_ok)
    {
        float4 hist = tex2.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), prev_uv, 0);
        history_vis = saturate(hist.r);
        history_hd  = hist.b;

        int2 prev_pixel = clamp(int2(prev_uv * resolution), int2(0, 0), int2(resolution) - 1);
        history_moments = tex3.Load(int3(prev_pixel, 0)).xyz;

        // schied 2017 alpha schedule, the 1/(n+1) ramp lowers the new sample weight as frames
        // accumulate, the floor keeps a held still view averaging down residual disk noise
        float n_eff     = history_moments.z;
        float min_alpha = saturate(1.0f / max(n_eff + 1.0f, 1.0f));
        float ema_alpha = max(min_alpha, 0.05f);
        history_weight  = saturate(min((1.0f - ema_alpha) * confidence, 0.97f));
    }

    // local neighborhood stats stabilize the variance estimate and feed the history clamp
    float mean_vis, sigma_vis;
    compute_local_statistics(pixel, resolution, mean_vis, sigma_vis);

    // clamp the reprojected history into the current neighborhood band, the key anti ghosting
    // tool for a high frequency binary signal, a moving shadow edge cannot smear across frames
    if (history_ok)
    {
        float band       = max(sigma_vis * 3.0f, 0.05f);
        float clamp_low  = mean_vis - band;
        float clamp_high = mean_vis + band;
        history_vis      = clamp(history_vis, clamp_low, clamp_high);
    }

    float ema_alpha = 1.0f - history_weight;
    float new_m1    = lerp(history_moments.x, current_vis, ema_alpha);
    float new_m2    = lerp(history_moments.y, current_vis * current_vis, ema_alpha);
    float new_n_eff = min(history_moments.z + 1.0f, 256.0f);

    float temporal_var      = max(new_m2 - new_m1 * new_m1, 0.0f);
    float variance_estimate = max(temporal_var, sigma_vis * sigma_vis * 0.25f);

    float output_vis = lerp(current_vis, history_vis, history_weight);

    // carry the blocker distance forward, keep the historical penumbra memory where the current
    // sample found no blocker so shadow edges retain their soft falloff under accumulation
    float output_hd;
    if (current_hit_dist > 0.0f)
    {
        output_hd = lerp(current_hit_dist, history_hd, history_weight);
    }
    else
    {
        output_hd = history_hd * history_weight;
    }

    tex_uav[pixel]  = validate_output(float4(saturate(output_vis), max(variance_estimate, 0.0f), max(output_hd, 0.0f), 0.0f));
    tex_uav2[pixel] = float4(new_m1, new_m2, new_n_eff, 0.0f);
}
