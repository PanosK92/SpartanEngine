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

// svgf style a-trous filter (schied 2017) adapted for ray traced sun shadows, the filter radius
// scales with the estimated penumbra so contact shadows stay razor sharp while distant blockers
// receive a wide, continuous soft falloff, this is what reconstructs penumbra from a single ray
// per pixel without the brute force per pixel sampling
// inputs:
//   tex   rgba = (visibility, variance, blocker_distance) from the previous denoise level
// step_width comes from the pass push constant and doubles each a-trous level

static const int2 spatial_offsets[25] =
{
    int2(-2, -2), int2(-1, -2), int2(0, -2), int2(1, -2), int2(2, -2),
    int2(-2, -1), int2(-1, -1), int2(0, -1), int2(1, -1), int2(2, -1),
    int2(-2,  0), int2(-1,  0), int2(0,  0), int2(1,  0), int2(2,  0),
    int2(-2,  1), int2(-1,  1), int2(0,  1), int2(1,  1), int2(2,  1),
    int2(-2,  2), int2(-1,  2), int2(0,  2), int2(1,  2), int2(2,  2)
};

static const float spatial_kernel[25] =
{
     1.0f / 256.0f,  4.0f / 256.0f,  6.0f / 256.0f,  4.0f / 256.0f,  1.0f / 256.0f,
     4.0f / 256.0f, 16.0f / 256.0f, 24.0f / 256.0f, 16.0f / 256.0f,  4.0f / 256.0f,
     6.0f / 256.0f, 24.0f / 256.0f, 36.0f / 256.0f, 24.0f / 256.0f,  6.0f / 256.0f,
     4.0f / 256.0f, 16.0f / 256.0f, 24.0f / 256.0f, 16.0f / 256.0f,  4.0f / 256.0f,
     1.0f / 256.0f,  4.0f / 256.0f,  6.0f / 256.0f,  4.0f / 256.0f,  1.0f / 256.0f
};

float gaussian_filtered_variance(int2 pixel, uint2 resolution)
{
    float variance_sum = 0.0f;
    float weight_sum   = 0.0f;
    [unroll]
    for (uint i = 0; i < 25; i++)
    {
        int2 sp = clamp(pixel + spatial_offsets[i], int2(0, 0), int2(resolution) - 1);
        float w = spatial_kernel[i];
        variance_sum += tex.Load(int3(sp, 0)).g * w;
        weight_sum   += w;
    }
    return variance_sum / max(weight_sum, 1e-6f);
}

// penumbra estimate from the neighborhood maximum blocker distance, a lit edge pixel adopts the
// blocker distance of nearby shadowed pixels so the soft falloff blurs across the shadow boundary
float estimate_penumbra(int2 pixel, uint2 resolution)
{
    float max_hd = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; y++)
    {
        [unroll]
        for (int x = -1; x <= 1; x++)
        {
            int2 sp = clamp(pixel + int2(x, y), int2(0, 0), int2(resolution) - 1);
            max_hd  = max(max_hd, tex.Load(int3(sp, 0)).b);
        }
    }
    return saturate(max_hd / 25.0f);
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
        tex_uav[pixel] = float4(1.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float4 center_sample   = tex.Load(int3(pixel, 0));
    float  center_vis      = saturate(center_sample.r);
    float  center_variance = max(center_sample.g, 0.0f);
    float  center_depth    = linearize_depth(depth);
    float3 center_normal   = get_normal(uv);

    int step_width = max((int)pass_get_f3_value().x, 1);

    // penumbra drives the filter footprint, contact shadows cluster the taps tightly to stay crisp
    // while a distant blocker spreads them for a soft edge, a small floor keeps minimal denoising
    float penumbra      = estimate_penumbra(int2(pixel), resolution);
    float penumbra_step = lerp(0.35f, 1.0f, penumbra);
    float phi_normal    = lerp(64.0f, 16.0f, penumbra);

    float blurred_variance = gaussian_filtered_variance(int2(pixel), resolution);
    float vis_sigma        = max(sqrt(max(blurred_variance, 1e-6f)) * 4.0f, 0.05f);

    float depth_x = abs(linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(1.0f / resolution.x, 0.0f), 0).r) - center_depth);
    float depth_y = abs(linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(0.0f, 1.0f / resolution.y), 0).r) - center_depth);
    float depth_grad_max = max(max(depth_x, depth_y), 1e-3f);

    float filtered_vis      = center_vis * spatial_kernel[12];
    float filtered_variance = center_variance * spatial_kernel[12] * spatial_kernel[12];
    float filtered_hd       = center_sample.b * spatial_kernel[12];
    float total_weight      = spatial_kernel[12];

    [unroll]
    for (uint i = 0; i < 25; i++)
    {
        if (i == 12)
        {
            continue;
        }

        float2 step_f       = float2(spatial_offsets[i]) * (float)step_width * penumbra_step;
        int2   sample_pixel = int2(pixel) + int2(round(step_f));
        if (sample_pixel.x < 0 || sample_pixel.x >= (int)resolution.x || sample_pixel.y < 0 || sample_pixel.y >= (int)resolution.y)
        {
            continue;
        }

        float2 sample_uv        = (sample_pixel + 0.5f) / resolution;
        float  sample_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), sample_uv, 0).r;
        if (sample_depth_raw <= 0.0f)
        {
            continue;
        }

        float  sample_depth  = linearize_depth(sample_depth_raw);
        float3 sample_normal = get_normal(sample_uv);
        float4 sample_data   = tex.Load(int3(sample_pixel, 0));
        float  sample_vis    = saturate(sample_data.r);
        float  sample_var    = max(sample_data.g, 0.0f);

        float pixel_dist    = length(step_f);
        float depth_delta   = abs(sample_depth - center_depth) / max(depth_grad_max * pixel_dist, 1e-3f);
        float depth_weight  = exp(-depth_delta);
        float normal_weight = pow(saturate(dot(center_normal, sample_normal)), phi_normal);
        float vis_weight    = exp(-abs(sample_vis - center_vis) / vis_sigma);

        float weight = spatial_kernel[i] * depth_weight * normal_weight * vis_weight;
        if (weight <= 0.0f)
        {
            continue;
        }

        filtered_vis      += sample_vis * weight;
        filtered_variance += sample_var * weight * weight;
        filtered_hd       += sample_data.b * weight;
        total_weight      += weight;
    }

    if (total_weight > 0.0f)
    {
        filtered_vis      /= total_weight;
        filtered_variance /= max(total_weight * total_weight, 1e-6f);
        filtered_hd       /= total_weight;
    }
    else
    {
        filtered_vis      = center_vis;
        filtered_variance = center_variance;
        filtered_hd       = center_sample.b;
    }

    tex_uav[pixel] = validate_output(float4(saturate(filtered_vis), max(filtered_variance, 0.0f), max(filtered_hd, 0.0f), 0.0f));
}
