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

//= INCLUDES ===================
#include "common.hlsl"
//==============================

// svgf style a-trous filter (schied et al. 2017 spatiotemporal variance guided filter)
// inputs:
//   tex   rgba = (color, variance) from the temporal accumulation pass
//   tex3  rgba = (luma_M1, luma_M2, n_eff, _) moments texture
// the per pixel variance estimate in tex.a drives the luma weight phi as phi_luma = sqrt(var),
// instead of the previous hand tuned global luma_phi, so noisy regions are filtered more
// aggressively while clean regions preserve high frequency detail
// step_width comes from the pass push constant and doubles each à-trous level (1, 2, 4)

static const float3 luminance_weights = float3(0.299f, 0.587f, 0.114f);

// 3x3 gaussian weights matched to the a-trous wavelet kernel
static const int2 spatial_offsets[9] =
{
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1)
};

static const float spatial_kernel[9] =
{
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
    2.0f / 16.0f, 4.0f / 16.0f, 2.0f / 16.0f,
    1.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f
};

// pre blurred variance, schied 2017 §4.2 applies a small gaussian blur over the per pixel
// variance estimate before driving the a-trous luma weight, this prevents single pixel
// fireflies from over-tightening the filter on their immediate neighborhood
float gaussian_filtered_variance(int2 pixel, uint2 resolution)
{
    float variance_sum = 0.0f;
    float weight_sum   = 0.0f;
    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        int2 sp = clamp(pixel + spatial_offsets[i], int2(0, 0), int2(resolution) - 1);
        float w = spatial_kernel[i];
        variance_sum += tex.Load(int3(sp, 0)).a * w;
        weight_sum   += w;
    }
    return variance_sum / max(weight_sum, 1e-6f);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);
    uint2 resolution = uint2(resolution_x, resolution_y);

    if (pixel.x >= resolution.x || pixel.y >= resolution.y)
        return;

    float2 uv = (pixel + 0.5f) / resolution;
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        tex_uav[pixel] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float4 center_sample      = tex.Load(int3(pixel, 0));
    float3 center_color       = max(center_sample.rgb, 0.0f);
    float  center_variance    = max(center_sample.a, 0.0f);
    float  center_luma        = dot(center_color, luminance_weights);
    float  center_depth       = linearize_depth(depth);
    float3 center_normal      = get_normal(uv);

    int    step_width = max((int)pass_get_f3_value().x, 1);

    // svgf phi parameters, schied 2017 fig 4 plus lin 2022 follow-up tuning, the previous
    // tier 2 tightening to (128 / 0.5) was preserving micro detail at the cost of leaving
    // visible firefly clumps in the output because the denoiser refused to smooth across
    // any feature boundary the gates barely registered, the reverted (64 / 1.0) pair gives
    // back the broader smoothing kernel that production svgf relies on, the upstream
    // spatial reuse and reservoir firefly cap already keep the contact shadow signal
    // (a-trous variance estimation pulls the per pixel sigma down on stable surfaces so
    // the kernel still respects edges, it just trusts continuous geometry more)
    //   phi_luma  = 4.0 (multiplied by sqrt(blurred variance) per pixel)
    //   phi_depth = 1.0 (relative depth tolerance, scales with step_width and depth gradient)
    //   phi_normal = 64 (cosine power, ~14 deg discrimination)
    const float phi_luma   = 4.0f;
    const float phi_depth  = 1.0f;
    const float phi_normal = 64.0f;

    float blurred_variance = gaussian_filtered_variance(int2(pixel), resolution);
    float luma_sigma       = sqrt(max(blurred_variance, 1e-6f)) * phi_luma;
    luma_sigma             = max(luma_sigma, 0.01f);

    // per pixel screen space depth gradient, schied 2017 §4.5 estimates df/dz with finite
    // differences inside the kernel so the depth weight scales with the surface slope
    float depth_x = abs(linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(1.0f / resolution.x, 0.0f), 0).r) - center_depth);
    float depth_y = abs(linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(0.0f, 1.0f / resolution.y), 0).r) - center_depth);
    float depth_grad_max = max(max(depth_x, depth_y), 1e-3f);

    float4 filtered_color    = center_sample * spatial_kernel[4];
    float  filtered_variance = center_variance * spatial_kernel[4] * spatial_kernel[4];
    float  total_weight      = spatial_kernel[4];
    float  total_var_weight  = spatial_kernel[4] * spatial_kernel[4];

    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        if (i == 4)
            continue;

        int2 sample_pixel = int2(pixel) + spatial_offsets[i] * step_width;
        if (sample_pixel.x < 0 || sample_pixel.x >= (int)resolution.x || sample_pixel.y < 0 || sample_pixel.y >= (int)resolution.y)
            continue;

        float2 sample_uv       = (sample_pixel + 0.5f) / resolution;
        float  sample_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), sample_uv, 0).r;
        if (sample_depth_raw <= 0.0f)
            continue;

        float  sample_depth   = linearize_depth(sample_depth_raw);
        float3 sample_normal  = get_normal(sample_uv);
        float4 sample_data    = tex.Load(int3(sample_pixel, 0));
        float3 sample_color   = max(sample_data.rgb, 0.0f);
        float  sample_var     = max(sample_data.a, 0.0f);
        float  sample_luma    = dot(sample_color, luminance_weights);

        // distance in pixels along the gradient direction so the depth tolerance scales with
        // the kernel reach (schied 2017 eq. 9), step_width drives the a-trous spread
        float pixel_dist  = length(float2(spatial_offsets[i] * step_width));
        float depth_delta = abs(sample_depth - center_depth) / max(phi_depth * depth_grad_max * pixel_dist, 1e-3f);
        float depth_weight = exp(-depth_delta);

        float normal_weight = pow(saturate(dot(center_normal, sample_normal)), phi_normal);

        float luma_delta  = abs(sample_luma - center_luma) / luma_sigma;
        float luma_weight = exp(-luma_delta);

        float spatial_w = spatial_kernel[i];
        float weight    = spatial_w * depth_weight * normal_weight * luma_weight;
        if (weight <= 0.0f)
            continue;

        filtered_color    += sample_data * weight;
        total_weight      += weight;
        // variance combines as sum(w_i^2 * var_i) / (sum(w_i))^2 (schied 2017 eq. 10)
        filtered_variance += sample_var * weight * weight;
        total_var_weight  += weight * weight;
    }

    if (total_weight > 0.0f)
    {
        filtered_color    /= total_weight;
        filtered_variance /= max(total_weight * total_weight, 1e-6f);
    }
    else
    {
        filtered_color    = center_sample;
        filtered_variance = center_variance;
    }

    // store color in rgb, variance in alpha so the next a-trous level has a fresh variance
    // estimate to drive its luma weight
    tex_uav[pixel] = validate_output(float4(filtered_color.rgb, max(filtered_variance, 0.0f)));
}
