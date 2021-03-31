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

//= INCLUDES =========
#include "Common.hlsl"
//====================

static const uint g_dof_sample_count        = 22;
static const float g_dof_bokeh_radius       = 8.0f;

// From https://github.com/Unity-Technologies/PostProcessing/
// blob/v2/PostProcessing/Shaders/Builtins/DiskKernels.hlsl
static const float2 g_dof_samples[22] =
{
    float2(0, 0),
    float2(0.53333336, 0),
    float2(0.3325279, 0.4169768),
    float2(-0.11867785, 0.5199616),
    float2(-0.48051673, 0.2314047),
    float2(-0.48051673, -0.23140468),
    float2(-0.11867763, -0.51996166),
    float2(0.33252785, -0.4169769),
    float2(1, 0),
    float2(0.90096885, 0.43388376),
    float2(0.6234898, 0.7818315),
    float2(0.22252098, 0.9749279),
    float2(-0.22252095, 0.9749279),
    float2(-0.62349, 0.7818314),
    float2(-0.90096885, 0.43388382),
    float2(-1, 0),
    float2(-0.90096885, -0.43388376),
    float2(-0.6234896, -0.7818316),
    float2(-0.22252055, -0.974928),
    float2(0.2225215, -0.9749278),
    float2(0.6234897, -0.7818316),
    float2(0.90096885, -0.43388376),
};

// Returns the focal depth by computing the average depth in a cross pattern neighborhood
float get_focal_depth(float2 texel_size)
{
    uint2 center    = 0.5f / texel_size;
    float dx        = g_dof_bokeh_radius * texel_size.x;
    float dy        = g_dof_bokeh_radius * texel_size.y;

    float tl = get_linear_depth(center + uint2(-1, -1));
    float tr = get_linear_depth(center + uint2(1, -1));
    float bl = get_linear_depth(center + uint2(-1, 1));
    float br = get_linear_depth(center + uint2(+1, 1));
    float ce = get_linear_depth(center);

    return min(min(min(min(tl, tr), bl), br), ce);
}

float circle_of_confusion(float2 uv, float focal_depth)
{
    float depth         = get_linear_depth(uv);
    float focus_range   = g_camera_aperture;
    float coc           = abs(depth - focal_depth) / (focus_range + FLT_MIN);
    return saturate(coc);
}

#if DOWNSAMPLE_CIRCLE_OF_CONFUSION
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    // Color
    float3 color = tex.SampleLevel(sampler_bilinear_clamp, uv, 0).rgb;

    // Coc
    float focal_depth   = get_focal_depth(g_texel_size * 0.5f); // Autofocus
    float dx            = g_texel_size.x * 0.5f;
    float dy            = g_texel_size.y * 0.5f;
    float2 uv_tl        = uv + float2(-dx, -dy);
    float2 uv_tr        = uv + float2(dx, -dy);
    float2 uv_bl        = uv + float2(-dx, dy);
    float2 uv_br        = uv + float2(dx, dy);
    float coc_tl        = circle_of_confusion(uv_tl, focal_depth);
    float coc_tr        = circle_of_confusion(uv_tr, focal_depth);
    float coc_bl        = circle_of_confusion(uv_bl, focal_depth);
    float coc_br        = circle_of_confusion(uv_br, focal_depth);
    float coc_min       = min(min(min(coc_tl, coc_tr), coc_bl), coc_br);
    float coc_max       = max(max(max(coc_tl, coc_tr), coc_bl), coc_br);
    float coc           = coc_max >= -coc_min ? coc_max : coc_min;

    tex_out_rgba[thread_id.xy] = float4(color, coc);
}
#endif

#if BOKEH
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;
    
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    // Sample color
    float4 color = 0.0f;
    [unroll]
    for (uint i = 0; i < g_dof_sample_count; i++)
    {
        float2 sample_uv = uv + g_dof_samples[i] * g_texel_size * g_dof_bokeh_radius;
        color += tex.SampleLevel(sampler_bilinear_clamp, sample_uv, 0);
    }
    color /= (float)g_dof_sample_count;

    tex_out_rgba[thread_id.xy] = saturate_16(color);
}
#endif

#if TENT
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    const float dx  = g_texel_size.x * 0.5f;
    const float dy  = g_texel_size.y * 0.5f;

    float4 tl = tex.SampleLevel(sampler_bilinear_clamp, uv + float2(-dx, -dy), 0);
    float4 tr = tex.SampleLevel(sampler_bilinear_clamp, uv + float2(dx, -dy), 0);
    float4 bl = tex.SampleLevel(sampler_bilinear_clamp, uv + float2(-dx, dy), 0);
    float4 br = tex.SampleLevel(sampler_bilinear_clamp, uv + float2(dx, dy), 0);

    tex_out_rgba[thread_id.xy] = (tl + tr + bl + br) * 0.25f;
}
#endif

#if UPSCALE_BLEND
[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    // Get dof and coc
    float4 bokeh    = tex2.SampleLevel(sampler_bilinear_clamp, uv, 0);
    float3 dof      = bokeh.rgb;
    float coc       = bokeh.a;

    // prevent blurry background from bleeding onto sharp foreground
    float focal_depth = get_focal_depth(g_texel_size);
    if (get_linear_depth(uv) > focal_depth)
    {
        //coc = 0.0f;
    }

    // Compute final color
    float4 base = tex[thread_id.xy];
    tex_out_rgba[thread_id.xy] = lerp(base, float4(dof, base.a), coc);
}
#endif
