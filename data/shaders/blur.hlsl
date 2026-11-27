/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

float compute_gaussian_weight(int sample_distance, const float sigma2)
{
    float g = 1.0f / sqrt(PI2 * sigma2);
    return (g * exp(-(sample_distance * sample_distance) / (2.0f * sigma2)));
}

// A linear fetch at the weighted centroid integrates two adjacent Gaussian taps.
static const uint max_pairs = 64u;
groupshared float2 gs_pairs[max_pairs];

float3 gaussian_blur(const uint2 pos, float2 resolution_in, float2 resolution_out, const float2 uv, const float radius, const float sigma2, const float2 direction)
{
    const float2 texel_size      = 1.0f / resolution_in;
    const float2 direction_texel = direction * texel_size;

    #if PASS_BLUR_GAUSSIAN_BILATERAL
    const float center_depth   = get_linear_depth(pos);
    const float3 center_normal = get_normal(pos);
    #endif

    float3 color  = 0.0f;
    float weights = 0.0f;
#if !PASS_BLUR_GAUSSIAN_BILATERAL
    const uint pairs = uint(max(1, int(ceil(radius)))) + 1u;
    if (radius > 0.0f && pairs <= max_pairs)
    {
        float2 source_size = direction.y != 0.0f ? resolution_out : resolution_in;
        for (uint pair = 0u; pair < pairs; ++pair)
        {
            float2 tap = gs_pairs[pair];
            float2 pixel = clamp(float2(pos) + direction * tap.x, 0.0f, resolution_in - 1.0f);
            color += tex.SampleLevel(samplers[sampler_bilinear_clamp], (pixel + 0.5f) / source_size, 0).rgb * tap.y;
            weights += tap.y;
        }
        return color / weights;
    }
#endif
    for (int i = -radius; i < radius; i++)
    {
        float2 sample_uv = uv + (i * direction_texel);

        float depth_awareness = 1.0f;
        #if PASS_BLUR_GAUSSIAN_BILATERAL
        float sample_depth     = get_linear_depth(sample_uv);
        float3 sample_normal   = get_normal(sample_uv);
        float awareness_depth  = saturate(0.1f - abs(center_depth - sample_depth));
        float awareness_normal = saturate(dot(center_normal, sample_normal)) + FLT_MIN; // FLT_MIN prevents NaN
        depth_awareness        = awareness_normal * awareness_depth;
        #endif

        float weight  = compute_gaussian_weight(i, sigma2) * depth_awareness;
        // during the vertical pass, the input texture is secondary scratch texture which belongs to the blur pass
        // it's at least as big as the original input texture (to be blurred), so we have to adapt the sample uv
        // clamp to the populated region, not the larger scratch texture's edge
        float2 sample_pos = clamp(floor(sample_uv * resolution_in), 0.0f, resolution_in - 1.0f);
        sample_uv  = lerp(sample_uv, (sample_pos + 0.5f) / resolution_out, direction.y != 0.0f);
        color     += tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).rgb * weight;
        weights   += weight;
    }

    return color / (weights + FLT_MIN);
}

#if PASS_BLUR_DOWNSAMPLE
// antialiased downsample for frosted glass backdrops: four bilinear taps average an entire 4x4 block for a 4x shrink,
// and a five-tap kawase tent pre-filters a 2x step so the blurred backdrop never shimmers or shows blocks
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(thread_id.xy >= uint2(resolution_out)))
        return;

    float2 resolution_in;
    tex.GetDimensions(resolution_in.x, resolution_in.y);

    const float2 uv    = (float2(thread_id.xy) + 0.5f) / resolution_out;
    const float2 texel = 1.0f / resolution_in;
    const float  scale = resolution_in.x / resolution_out.x;

    float3 corners = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(-texel.x, -texel.y), 0).rgb
                   + tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2( texel.x, -texel.y), 0).rgb
                   + tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(-texel.x,  texel.y), 0).rgb
                   + tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2( texel.x,  texel.y), 0).rgb;

    float3 color;
    [branch]
    if (scale > 2.5f)
    {
        color = corners * 0.25f;
    }
    else
    {
        float3 center = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
        color = center * 0.5f + corners * 0.125f;
    }

    tex_uav[thread_id.xy] = float4(color, 1.0f);
}
#elif PASS_BLUR_COMPOSITE
// copies a blurred low resolution backdrop into a rectangle of the target, the frosted glass behind overlay panels
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    const float2 rect_min = float2(pass_float(pass_blur::rect_x0), pass_float(pass_blur::rect_y0));
    const float2 rect_max = float2(pass_float(pass_blur::rect_x1), pass_float(pass_blur::rect_y1));

    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);

    const uint2 pos = uint2(rect_min) + thread_id.xy;
    if (any(float2(pos) >= rect_max) || any(float2(pos) >= resolution_out))
        return;

    const float2 uv       = (pos + 0.5f) / resolution_out;
    const float3 backdrop = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    tex_uav[pos]          = float4(backdrop, tex_uav[pos].a);
}
#else
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint lane : SV_GroupIndex)
{
    const float radius     = pass_float(pass_blur::radius);
    const float sigma      = radius / 3.0f;
    const float2 direction = pass_bool(pass_blur::vertical) ? float2(0.0f, 1.0f) : float2(1.0f, 0.0f);

    float2 resolution_in;
    tex.GetDimensions(resolution_in.x, resolution_in.y);
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);

    if (direction.y == 1.0f)
    {
        float2 temp    = resolution_in;
        resolution_in  = resolution_out;
        resolution_out = temp;
    }
#if !PASS_BLUR_GAUSSIAN_BILATERAL
    const int  extent = max(1, int(ceil(radius)));
    const uint pairs  = uint(extent) + 1u;
    if (radius > 0.0f && pairs <= max_pairs)
    {
        if (lane < pairs)
        {
            int offset = -extent + int(lane) * 2;
            float w0   = exp(-float(offset * offset) / (2.0f * sigma * sigma));
            float w1   = (offset + 1 <= extent) ? exp(-float((offset + 1) * (offset + 1)) / (2.0f * sigma * sigma)) : 0.0f;
            gs_pairs[lane] = float2(float(offset) + w1 / (w0 + w1), w0 + w1);
        }
        GroupMemoryBarrierWithGroupSync();
    }
#endif
    // dispatch dimensions are rounded up to whole thread groups
    if (any(thread_id.xy >= resolution_in))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / resolution_in;
    
    float4 color          = tex_uav[thread_id.xy];
    color.rgb             = gaussian_blur(thread_id.xy, resolution_in, resolution_out, uv, radius, sigma * sigma, direction);
    tex_uav[thread_id.xy] = color;
}
#endif
