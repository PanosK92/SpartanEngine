/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===========
#include "Common.hlsl"
#include "Velocity.hlsl"
//======================

float3 reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 reinhard_inverse(float3 sdr, float k = 1.0)
{
    return k * sdr / (k - sdr);
}

// From "Temporal Reprojection Anti-Aliasing"
// https://github.com/playdeadgames/temporal
float3 clip_aabb(float3 aabb_min, float3 aabb_max, float3 p, float3 q, float box_size)
{
    float3 r = q - p;
    float3 rmax = (aabb_max - p.xyz) * box_size;
    float3 rmin = (aabb_min - p.xyz) * box_size;

    if (r.x > rmax.x + FLT_MIN)
        r *= (rmax.x / r.x);
    if (r.y > rmax.y + FLT_MIN)
        r *= (rmax.y / r.y);
    if (r.z > rmax.z + FLT_MIN)
        r *= (rmax.z / r.z);

    if (r.x < rmin.x - FLT_MIN)
        r *= (rmin.x / r.x);
    if (r.y < rmin.y - FLT_MIN)
        r *= (rmin.y / r.y);
    if (r.z < rmin.z - FLT_MIN)
        r *= (rmin.z / r.z);

    return p + r;
}

// Clip history to the neighbourhood of the current sample
float3 clip_history(uint2 thread_id, uint group_index, uint3 group_id, Texture2D tex_input, float3 color_history, float2 velocity)
{
    float3 ctl = tex_input[thread_id + uint2(-1, -1)].rgb;
    float3 ctc = tex_input[thread_id + uint2(0, -1)].rgb;
    float3 ctr = tex_input[thread_id + uint2(1, -1)].rgb;
    float3 cml = tex_input[thread_id + uint2(-1, 0)].rgb;
    float3 cmc = tex_input[thread_id].rgb;
    float3 cmr = tex_input[thread_id + uint2(1, 0)].rgb;
    float3 cbl = tex_input[thread_id + uint2(-1, 1)].rgb;
    float3 cbc = tex_input[thread_id + uint2(0, 1)].rgb;
    float3 cbr = tex_input[thread_id + uint2(1, 1)].rgb;

    float3 color_min = min(ctl, min(ctc, min(ctr, min(cml, min(cmc, min(cmr, min(cbl, min(cbc, cbr))))))));
    float3 color_max = max(ctl, max(ctc, max(ctr, max(cml, max(cmc, max(cmr, max(cbl, max(cbc, cbr))))))));
    float3 color_avg = (ctl + ctc + ctr + cml + cmc + cmr + cbl + cbc + cbr) / 9.0f;

    const float box_size = lerp(0.5f, 2.5f, smoothstep(0.02f, 0.0f, length(velocity)));
    
    return saturate_16(clip_aabb(color_min, color_max, clamp(color_avg, color_min, color_max), color_history, box_size));
}

float3 sample_history_catmull_rom(in float2 uv, in float2 texelSize, Texture2D tex_history)
{
    // Source: https://gist.github.com/TheRealMJP/c83b8c0f46b63f3a88a5986f4fa982b1
    // License: https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae

    // We're going to sample a a 4x4 grid of texels surrounding the target UV coordinate. We'll do this by rounding
    // down the sample location to get the exact center of our "starting" texel. The starting texel will be at
    // location [1, 1] in the grid, where [0, 0] is the top left corner.
    float2 samplePos = uv / texelSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

    // Compute the fractional offset from our starting texel to our original sample location, which we'll
    // feed into the Catmull-Rom spline function to get our filter weights.
    float2 f = samplePos - texPos1;

    // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
    // These equations are pre-expanded based on our knowledge of where the texels will be located,
    // which lets us avoid having to evaluate a piece-wise function.
    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
    // simultaneously evaluate the middle 2 samples from the 4x4 grid.
    float2 w12 = w1 + w2;
    float2 offset12 = w2 / (w1 + w2);

    // Compute the final UV coordinates we'll use for sampling the texture
    float2 texPos0 = texPos1 - 1.0f;
    float2 texPos3 = texPos1 + 2.0f;
    float2 texPos12 = texPos1 + offset12;

    texPos0 *= texelSize;
    texPos3 *= texelSize;
    texPos12 *= texelSize;

    float3 result = float3(0.0f, 0.0f, 0.0f);

    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos0.x, texPos0.y), 0.0f).xyz * w0.x * w0.y;
    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos12.x, texPos0.y), 0.0f).xyz * w12.x * w0.y;
    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos3.x, texPos0.y), 0.0f).xyz * w3.x * w0.y;

    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos0.x, texPos12.y), 0.0f).xyz * w0.x * w12.y;
    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos12.x, texPos12.y), 0.0f).xyz * w12.x * w12.y;
    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos3.x, texPos12.y), 0.0f).xyz * w3.x * w12.y;

    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos0.x, texPos3.y), 0.0f).xyz * w0.x * w3.y;
    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos12.x, texPos3.y), 0.0f).xyz * w12.x * w3.y;
    result += tex_history.SampleLevel(sampler_bilinear_clamp, float2(texPos3.x, texPos3.y), 0.0f).xyz * w3.x * w3.y;

    return max(result, 0.0f);
}

static const int2 kOffsets3x3[9] =
{
    int2(-1, -1),
    int2(0, -1),
    int2(1, -1),
    int2(-1, 0),
    int2(0, 0),
    int2(1, 0),
    int2(-1, 1),
    int2(0, 1),
    int2(1, 1),
};

float distance_squared(float2 a_to_b, float2 offset)
{
    float2 v = a_to_b ;
    return dot(v, v);
}

float get_sample_weight(float distance_squared)
{
    const float std_dev = 0.18f;
    const float dawa = 1.0f / (2.0f * std_dev * std_dev);
    return exp(distance_squared - dawa);
}

float3 get_input_sample(Texture2D tex_input, const uint2 pos_out)
{
    if (!is_taa_upsampling_enabled())
        return tex_input[pos_out].rgb;

    const float2 uv = (pos_out + 0.5f) / g_resolution_rt;
    const float2 jitter_offset_pixels = g_taa_jitter_offset * g_resolution_render;
    const float2 pos_input = uv * g_resolution_render;
    const float2 pos_input_center = floor(pos_input) + 0.5f ;
    const float2 pos_out_center = pos_out + 0.5f;
    const float2 in_to_out = pos_out_center - pos_input_center;

    // Compute sample weights
    float weights[9];
    float weight_sum = 0.0f;
    float weight_normaliser = 1.0f;
    [unroll]
    for (uint i = 0; i < 9; i++)
    {
        weights[i] = get_sample_weight(distance_squared(pos_input_center, (float2)kOffsets3x3[i]));
        weight_sum += weights[i];
    }
   weight_normaliser /= weight_sum;
    
    // Fetch color samples
    float3 color = 0.0f;
    [unroll]
    for (uint j = 0; j < 9; j++)
    {
        float weigth = weights[j] * weight_normaliser;
        color += tex_input[pos_input + (float2)kOffsets3x3[j]].rgb * weigth;
    }

    return color;
}

float4 temporal_antialiasing(uint2 pos_out, uint group_index, uint3 group_id, Texture2D tex_history, Texture2D tex_input)
{
    const float2 uv         = (pos_out + 0.5f) / g_resolution_rt;
    const uint2 pos_input   = is_taa_upsampling_enabled() ? (uv * g_resolution_render) : pos_out;

    // Get reprojected uv
    float2 velocity         = get_velocity_closest_3x3(uv);
    float2 uv_reprojected   = uv - velocity;

    // If re-projected UV is out of screen, converge to current color immediately
    if (!is_saturated(uv_reprojected))
        return float4(get_input_sample(tex_input, pos_out), 1.0f);

    // Get input color
    float3 color_input = get_input_sample(tex_input, pos_out);
    
    // Get history color (removes a lot of the blurring that you get under motion)
    float3 color_history = sample_history_catmull_rom(uv_reprojected, g_texel_size, tex_history);

    // Clip history to the neighbourhood of the current sample
    color_history = clip_history(pos_input, group_index, group_id, tex_input, color_history, velocity);

    // Compute blend factor
    float blend_factor = 1.0f / 16.0f;
    {
        // Decrease blend factor when contrast is high
        float luminance_history   = luminance(color_history);
        float luminance_current   = luminance(color_input);
        float unbiased_difference = abs(luminance_current - luminance_history) / ((max(luminance_current, luminance_history) + 0.5f));
        blend_factor *= 1.0 - unbiased_difference;
    }

    // Resolve
    float3 color_resolved = 0.0f;
    {
        // Tonemap
        color_history = reinhard(color_history);
        color_input = reinhard(color_input);

        // Lerp/blend
        color_resolved = lerp(color_history, color_input, blend_factor);

        // Inverse tonemap
        color_resolved = reinhard_inverse(color_resolved);
    }

    return float4(color_resolved, 1.0f);
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex, uint3 group_id : SV_GroupID)
{
    if (any(int2(thread_id.xy) >= g_resolution_rt.xy))
        return; // out of bounds
    
    tex_out_rgba[thread_id.xy] = temporal_antialiasing(thread_id.xy, group_index, group_id, tex, tex2);
}
