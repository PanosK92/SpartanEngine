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

static const float3 luminance_weights = float3(0.299f, 0.587f, 0.114f);

static const int2 spatial_offsets[9] =
{
    int2(-1, -1), int2(0, -1), int2(1, -1),
    int2(-1,  0), int2(0,  0), int2(1,  0),
    int2(-1,  1), int2(0,  1), int2(1,  1)
};

static const float spatial_kernel[9] =
{
    1.0f, 2.0f, 1.0f,
    2.0f, 4.0f, 2.0f,
    1.0f, 2.0f, 1.0f
};

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
    float  center_history     = saturate(center_sample.a);
    // confidence is the high f16 of tex4.w, see reservoir packing layout
    uint   center_age_conf    = asuint(tex_reservoir_prev4[pixel].w);
    float  reservoir_confidence = saturate(f16tof32(center_age_conf >> 16u));
    float  center_confidence  = saturate(center_history * 0.6f + reservoir_confidence * 0.4f);
    float  center_luma        = dot(center_color, luminance_weights);
    float  center_depth       = linearize_depth(depth);
    float3 center_normal      = get_normal(uv);
    float  low_light_factor   = saturate(1.0f - center_luma / 0.2f);

    int step_width      = max((int)pass_get_f3_value().x, 1);
    float depth_phi     = max(center_depth * lerp(0.03f, 0.012f, center_confidence), 0.004f);
    float luma_phi      = max((center_luma + 0.08f) * lerp(4.5f, 1.4f, center_confidence), 0.08f);
    float normal_power  = lerp(16.0f, 48.0f, center_confidence);
    // tighten luma tolerance in dark regions so contact shadows don't get smeared
    luma_phi           *= lerp(0.75f, 1.0f, 1.0f - low_light_factor);

    float4 filtered_color = center_sample * spatial_kernel[4];
    float  total_weight   = spatial_kernel[4];

    for (uint i = 0; i < 9; i++)
    {
        if (i == 4)
            continue;

        int2 sample_pixel = int2(pixel) + spatial_offsets[i] * step_width;
        if (sample_pixel.x < 0 || sample_pixel.x >= (int)resolution.x || sample_pixel.y < 0 || sample_pixel.y >= (int)resolution.y)
            continue;

        float2 sample_uv   = (sample_pixel + 0.5f) / resolution;
        float sample_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), sample_uv, 0).r;
        if (sample_depth_raw <= 0.0f)
            continue;

        float  sample_depth   = linearize_depth(sample_depth_raw);
        float3 sample_normal  = get_normal(sample_uv);
        float4 sample_history = tex.Load(int3(sample_pixel, 0));
        uint   sample_age_conf = asuint(tex_reservoir_prev4[sample_pixel].w);
        float  sample_reservoir_confidence = saturate(f16tof32(sample_age_conf >> 16u));
        float3 sample_color   = max(sample_history.rgb, 0.0f);
        float  sample_luma    = dot(sample_color, luminance_weights);

        float spatial_weight = spatial_kernel[i];
        float depth_weight   = exp(-abs(sample_depth - center_depth) / depth_phi);
        float normal_weight  = pow(saturate(dot(center_normal, sample_normal)), normal_power);
        float luma_weight    = exp(-abs(sample_luma - center_luma) / luma_phi);
        float history_confidence = max(saturate(sample_history.a), sample_reservoir_confidence);
        float history_weight = lerp(0.45f, 1.0f, history_confidence);

        float weight = spatial_weight * depth_weight * normal_weight * luma_weight * history_weight;
        if (weight <= 0.0f)
            continue;

        filtered_color += sample_history * weight;
        total_weight   += weight;
    }

    float4 filtered_sample = total_weight > 0.0f ? filtered_color / total_weight : center_sample;
    float filter_strength  = lerp(1.0f, 0.35f, center_confidence);
    // gentler filtering in dark regions so shadow detail survives
    filter_strength        = saturate(filter_strength - low_light_factor * 0.2f);
    float4 output_sample   = lerp(center_sample, filtered_sample, filter_strength);
    output_sample.a        = saturate(lerp(center_history, filtered_sample.a, filter_strength * 0.75f));

    tex_uav[pixel] = validate_output(output_sample);
}
