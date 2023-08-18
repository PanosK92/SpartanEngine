/*
Copyright(c) 2016-2023 Panos Karabelas

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

static const uint g_dof_sample_count  = 22;
static const float g_dof_bokeh_radius = 5.5f;

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

// Returns the average depth in a cross pattern neighborhood
float get_focal_depth()
{
    const float2 uv         = float2(0.5f, 0.5f); // center
    const float2 texel_size = float2(1.0f / buffer_frame.resolution_render.x, 1.0f / buffer_frame.resolution_render.y);
    const float radius      = 10.0f;
    const float4 o          = texel_size.xyxy * float2(-radius, radius).xxyy;

    float s1 = get_linear_depth(uv + o.xy);
    float s2 = get_linear_depth(uv + o.zy);
    float s3 = get_linear_depth(uv + o.xw);
    float s4 = get_linear_depth(uv + o.zw);
    float s5 = get_linear_depth(uv);

    return (s1 + s2 + s3 + s4 + s5) * 0.2f;
}

float circle_of_confusion(float2 uv, float focus_distance)
{
    float depth           = get_linear_depth(uv);
    float camera_aperture = pass_get_f3_value().x;
    float focus_range     = camera_aperture * 20.0f;
    float coc             = ((depth - focus_distance) / (focus_range + FLT_MIN)) * g_dof_bokeh_radius;

    return coc;
}

#if DOWNSAMPLE_CIRCLE_OF_CONFUSION
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / pass_get_resolution_out();

    // Coc
    const float4 o          = get_rt_texel_size().xyxy * float2(-0.5, 0.5).xxyy;
    const float focal_depth = get_focal_depth();
    float coc1              = circle_of_confusion(uv + o.xy, focal_depth);
    float coc2              = circle_of_confusion(uv + o.zy, focal_depth);
    float coc3              = circle_of_confusion(uv + o.xw, focal_depth);
    float coc4              = circle_of_confusion(uv + o.zw, focal_depth);
    float coc_min           = min4(coc1, coc2, coc3, coc4);
    float coc_max           = max4(coc1, coc2, coc3, coc4);
    float coc               = coc_max >= -coc_min ? coc_max : coc_min;

    // Color
    float3 color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;

    tex_uav[thread_id.xy] = float4(color, coc);
}
#endif

#if BOKEH
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / pass_get_resolution_out();

    // Sample color
    float3 color = 0.0f;
    float weight = 0.0f;
    [unroll]
    for (uint i = 0; i < g_dof_sample_count; i++)
    {
        float2 radius = g_dof_samples[i] * g_dof_bokeh_radius;
        float4 s = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + radius * get_rt_texel_size(), 0);

        // If the sample's CoC is at least as large as the kernel radius, use it.
        if (abs(s.a) >= length(radius))
        {
            color  += s.rgb;
            weight += 1.0f;
        }
    }
    color /= weight;

    tex_uav[thread_id.xy] = float4(color, tex[thread_id.xy].a);
}
#endif

#if TENT
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / pass_get_resolution_out();
    const float4 o  = get_rt_texel_size().xyxy * float2(-0.5, 0.5).xxyy;

    float3 s1 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + o.xy, 0).rgb;
    float3 s2 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + o.zy, 0).rgb;
    float3 s3 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + o.xw, 0).rgb;
    float3 s4 = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + o.zw, 0).rgb;

    float coc = tex[thread_id.xy].a;

    tex_uav[thread_id.xy] = float4((s1 + s2 + s3 + s4) * 0.25f, coc);
}
#endif

#if UPSCALE_BLEND
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    const float2 uv = (thread_id.xy + 0.5f) / pass_get_resolution_out();

    // Get dof and coc
    float4 bokeh = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0);
    float3 dof   = bokeh.rgb;
    float coc    = bokeh.a;

    // prevent blurry background from bleeding onto sharp foreground
    float focal_depth = get_focal_depth();
    if (get_linear_depth(uv) > focal_depth)
    {
       //coc = 0.0f;
    }

    // Compute final color
    float4 base = tex[thread_id.xy];
    float blend = smoothstep(0.0f, 1.0f, abs(coc));
    tex_uav[thread_id.xy] = lerp(base, float4(dof, base.a), blend);
}
#endif
