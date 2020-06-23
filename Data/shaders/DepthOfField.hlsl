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

static const uint g_dof_temporal_directions = 6;
static const uint g_dof_sample_count        = 22;
static const float g_dof_bokeh_radius       = 8.0f;

static const float g_dof_rotation_step = PI2 / (float) g_dof_temporal_directions;

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
float get_focal_depth()
{
    float2 uv   = 0.5f; // center
    float dx    = g_dof_bokeh_radius * g_texel_size.x;
    float dy    = g_dof_bokeh_radius * g_texel_size.y;
	
    float tl = get_linear_depth(uv + float2(-dx, -dy));
    float tr = get_linear_depth(uv + float2(+dx, -dy));
    float bl = get_linear_depth(uv + float2(-dx, +dy));
    float br = get_linear_depth(uv + float2(+dx, +dy));
    float ce = get_linear_depth(uv);
	
    return (tl + tr + bl + br + ce) * 0.2f;
}

float circle_of_confusion(float2 uv, float focal_depth)
{
    float camera_aperture   = g_camera_aperture / 1000.0f;  // convert to meters
    float camera_range      = g_camera_far * 0.25f;         // use 1/4 of the clipping range, how many cameras can go that far to begin with ?
    
    float depth         = get_linear_depth(uv);
    float focus_range   = camera_aperture * g_camera_far;
    float coc           = (depth - focal_depth) / (focus_range + FLT_MIN);
    return saturate(abs(coc));
}

float2 rotate_vector(float2 v, float2 direction)
{
    return float2(v.x * direction.x - v.y * direction.y, v.x * direction.y + v.y * direction.x);
}

#if DOWNSAMPLE_CIRCLE_OF_CONFUSION
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    // UVs
    // g_texel_size refers to the current render target, which is half the size of the input texture, so we multiply by 2
    float dx        = g_texel_size.x * 2.0f;
    float dy        = g_texel_size.y * 2.0f;
    float2 uv_tl    = input.uv + float2(-dx, -dy);
    float2 uv_tr    = input.uv + float2(dx, -dy);
    float2 uv_bl    = input.uv + float2(-dx, dy);
    float2 uv_br    = input.uv + float2(dx, dy);

    // Color samples
    float3 color_tl = tex.Sample(sampler_bilinear_clamp, uv_tl).rgb;
    float3 color_tr = tex.Sample(sampler_bilinear_clamp, uv_tr).rgb;
    float3 color_bl = tex.Sample(sampler_bilinear_clamp, uv_bl).rgb;
    float3 color_br = tex.Sample(sampler_bilinear_clamp, uv_br).rgb;

    // Coc samples
    float focal_depth = get_focal_depth(); // Autofocus
    float coc_tl = circle_of_confusion(uv_tl, focal_depth);
    float coc_tr = circle_of_confusion(uv_tr, focal_depth);
    float coc_bl = circle_of_confusion(uv_bl, focal_depth);
    float coc_br = circle_of_confusion(uv_br, focal_depth);

    // Compute final circle of confusion    
    float coc_min   = min(min(min(coc_tl, coc_tr), coc_bl), coc_br);
    float coc_max   = max(max(max(coc_tl, coc_tr), coc_bl), coc_br);
    float coc       = coc_max >= -coc_min ? coc_max : coc_min;

    return float4((color_tl + color_tr + color_bl + color_br) * 0.25f, coc);
}
#endif

#if BOKEH
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    // Sample color
    float4 color = 0.0f;
    [unroll]
    for (uint i = 0; i < g_dof_sample_count; i++)
    {
        float2 sample_uv = input.uv + g_dof_samples[i] * g_texel_size * g_dof_bokeh_radius;
        color += tex.Sample(sampler_bilinear_clamp, sample_uv);
    }
    color /= (float)g_dof_sample_count;

    return saturate_16(color);
}
#endif

#if TENT
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float dx = g_texel_size.x * 0.5f;
    float dy = g_texel_size.y * 0.5f;
    
    float4 tl = tex.Sample(sampler_bilinear_clamp, input.uv + float2(-dx, -dy));
    float4 tr = tex.Sample(sampler_bilinear_clamp, input.uv + float2(dx, -dy));
    float4 bl = tex.Sample(sampler_bilinear_clamp, input.uv + float2(-dx, dy));
    float4 br = tex.Sample(sampler_bilinear_clamp, input.uv + float2(dx, dy));
    
    return (tl + tr + bl + br) * 0.25f;
}
#endif

#if UPSCALE_BLEND
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    // Upsample bokeh
    // g_texel_size refers to the current render target, which is twice the size of the input texture, so we multiply by 0.5
    float dx            = g_texel_size.x * 0.5f;
    float dy            = g_texel_size.y * 0.5f;  
    float4 tl           = tex2.Sample(sampler_bilinear_clamp, input.uv + float2(-dx, -dy));
    float4 tr           = tex2.Sample(sampler_bilinear_clamp, input.uv + float2(dx, -dy));
    float4 bl           = tex2.Sample(sampler_bilinear_clamp, input.uv + float2(-dx, dy));
    float4 br           = tex2.Sample(sampler_bilinear_clamp, input.uv + float2(dx, dy));
    float4 bokeh        = (tl + tr + bl + br) * 0.25f;

    // Get dof and coc
    float3 dof  = bokeh.rgb;
    float coc   = bokeh.a;

    // Compute final color
    float4 base     = tex.Sample(sampler_point_clamp, input.uv);
    float4 color    = lerp(base, float4(dof, base.a), coc);

    return color;
}
#endif
