/*
Copyright(c) 2016-2020 Panos Karabelas

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
    float2 uv   = 0.5f; // center
    float dx    = g_dof_bokeh_radius * texel_size.x;
    float dy    = g_dof_bokeh_radius * texel_size.y;

    // get_linear_depth() loads the texel using g_resolution
    // which refers to the render target size, not the depth texture.
    // we avoid this issue by using a sampler and simply pass the UVs
    float tl = get_linear_depth(uv + float2(-dx, -dy));
    float tr = get_linear_depth(uv + float2(+dx, -dy));
    float bl = get_linear_depth(uv + float2(-dx, +dy));
    float br = get_linear_depth(uv + float2(+dx, +dy));
    float ce = get_linear_depth(uv);
	
    return (tl + tr + bl + br + ce) * 0.2f;
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
    
    // g_texel_size refers to the current render target, which is half the size of the input texture.
    // so the texel size for the input texture is actually twice as big, however because we want
    // to do a "tent" filter, we use it as is, which is basically half the texel size.
    const float2 texel_size = g_texel_size;
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    // UVs
    float dx        = texel_size.x;
    float dy        = texel_size.y;
    float2 uv_tl    = uv + float2(-dx, -dy);
    float2 uv_tr    = uv + float2(dx, -dy);
    float2 uv_bl    = uv + float2(-dx, dy);
    float2 uv_br    = uv + float2(dx, dy);

    // Color samples
    float3 color_tl = tex.SampleLevel(sampler_bilinear_clamp, uv_tl, 0).rgb;
    float3 color_tr = tex.SampleLevel(sampler_bilinear_clamp, uv_tr, 0).rgb;
    float3 color_bl = tex.SampleLevel(sampler_bilinear_clamp, uv_bl, 0).rgb;
    float3 color_br = tex.SampleLevel(sampler_bilinear_clamp, uv_br, 0).rgb;

    // Coc samples
    float focal_depth = get_focal_depth(texel_size * 2.0f); // Autofocus
    float coc_tl = circle_of_confusion(uv_tl, focal_depth);
    float coc_tr = circle_of_confusion(uv_tr, focal_depth);
    float coc_bl = circle_of_confusion(uv_bl, focal_depth);
    float coc_br = circle_of_confusion(uv_br, focal_depth);
    
    float3 color    = (color_tl + color_tr + color_bl + color_br) * 0.25f;
    float coc       = (coc_tl + coc_tr + coc_bl + coc_br) * 0.25f;
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
    
    // g_texel_size refers to the current render target, which is twice the size of the input texture, so we multiply by 0.5
    const float2 texel_size = g_texel_size.x * 0.5f;
    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    // Upsample bokeh
    float dx            = texel_size.x;
    float dy            = texel_size.y;  
    float4 tl           = tex2.SampleLevel(sampler_bilinear_clamp, uv + float2(-dx, -dy), 0);
    float4 tr           = tex2.SampleLevel(sampler_bilinear_clamp, uv + float2(dx, -dy), 0);
    float4 bl           = tex2.SampleLevel(sampler_bilinear_clamp, uv + float2(-dx, dy), 0);
    float4 br           = tex2.SampleLevel(sampler_bilinear_clamp, uv + float2(dx, dy), 0);
    float4 bokeh        = (tl + tr + bl + br) * 0.25f;

    // Get dof and coc
    float3 dof  = bokeh.rgb;
    float coc   = bokeh.a;

    // prevent blurry background from bleeding onto sharp foreground
    float depth         = get_linear_depth(uv);
    float focal_depth  = get_focal_depth(texel_size);
    float center_coc    = circle_of_confusion(uv, focal_depth);
    if (get_linear_depth(uv) > focal_depth) 
    {
        coc = clamp(coc, 0.0, center_coc * 2.0f);
    }
    
    // Compute final color
    float4 base = tex[thread_id.xy];
    tex_out_rgba[thread_id.xy] = lerp(base, float4(dof, base.a), coc);
}
#endif
