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

//= includes =========
#include "common.hlsl"
//====================


/*------------------------------------------------------------------------------
    constants
------------------------------------------------------------------------------*/
// full frame 35mm sensor, focal length is derived from horizontal fov each frame
static const float SENSOR_WIDTH_MM  = 36.0f;
static const float SENSOR_HEIGHT_MM = 24.0f;

// blur parameters
static const float MAX_COC_RADIUS     = 24.0f; // maximum blur in pixels
static const float COC_CLAMP_FACTOR   = 0.85f; // prevent excessive blur
static const float IN_FOCUS_THRESHOLD = 0.4f;  // coc below this = sharp
static const int   SAMPLE_COUNT       = 32;    // bokeh quality
static const float GOLDEN_ANGLE       = 2.39996323f;
static const int   APERTURE_BLADES    = 6;     // hexagonal cinematic bokeh

// auto focus parameters
static const int   FOCUS_SAMPLES      = 32;    // depth samples for focus calculation
static const float FOCUS_REGION       = 0.10f; // screen fraction for focus area
static const float CENTER_WEIGHT_BIAS = 3.0f;  // prefer centre of screen for focus
static const float SUBJECT_TOLERANCE  = 2.0f;  // depths within this multiple of the nearest hit are the same subject
static const float FOCUS_ADAPT_SPEED  = 3.5f;  // cinematic focus pull, higher is snappier

// depth handling
static const float NEAR_SCALE         = 1.25f; // foreground blur emphasis
static const float FAR_SCALE          = 1.0f;  // background blur scale
static const float BG_LEAK_PREVENTION = 0.45f; // reduce background bleeding into foreground

// highlight preserving bokeh, keeps specular orbs alive in the blur
static const float HIGHLIGHT_THRESHOLD = 1.0f;
static const float HIGHLIGHT_GAIN      = 2.5f;

// derived constants
static const float COC_CLAMP_PIXELS = MAX_COC_RADIUS * COC_CLAMP_FACTOR;
static const float INV_SAMPLE_COUNT = 1.0f / (float)SAMPLE_COUNT;
static const float INV_SCATTER_NORM = 1.0f / (MAX_COC_RADIUS * 0.3f);
static const float BLADE_ANGLE      = PI2 / (float)APERTURE_BLADES;

/*------------------------------------------------------------------------------
    lens constants computed once per group then read by every thread
------------------------------------------------------------------------------*/
struct lens_t
{
    float focus_distance; // s, focus distance in meters
    float coc_factor;     // aperture_diameter * f * pixels_per_meter / abs(s - f)
    float aperture_fstop; // used to blend circular vs polygonal bokeh
};

groupshared lens_t gs_lens;

/*------------------------------------------------------------------------------
    fast signed coc using precomputed lens factor
    sign is negative for foreground (d < s) and positive for background (d > s)
------------------------------------------------------------------------------*/
float compute_coc_signed(float depth, lens_t lens)
{
    float d         = max(depth, 0.01f);
    float s_minus_d = lens.focus_distance - d;
    float coc_pix   = abs(s_minus_d) * lens.coc_factor / d;
    bool  is_near   = s_minus_d > 0.0f;
    float scale     = is_near ? NEAR_SCALE : FAR_SCALE;
    coc_pix         = min(coc_pix * scale, COC_CLAMP_PIXELS);
    return is_near ? -coc_pix : coc_pix;
}

/*------------------------------------------------------------------------------
    auto focus, locks onto the nearest subject near the screen centre

    a weighted average of the sampled depths is a poor estimator, a thin subject
    like a bottle or a pole only covers a few of the samples so the far background
    dominates the average and the subject itself ends up out of focus, which is
    also why zooming in used to fix it

    instead the sky is discarded, the nearest remaining hit is taken as the
    subject, and everything within SUBJECT_TOLERANCE of it is averaged, a lone
    sample is not trusted on its own so it falls back to the next nearest
    cluster, this is what a camera does when it focuses on what is in front of it
------------------------------------------------------------------------------*/
float focus_sample_weight(int i)
{
    float t = (float)i / (float)(FOCUS_SAMPLES - 1);
    return exp(-t * CENTER_WEIGHT_BIAS);
}

float compute_focus_distance(float2 resolution)
{
    const float2 center = float2(0.5f, 0.5f);

    // anything at or past the far plane is sky or an empty background, it must never
    // pull focus, otherwise a subject against the sky is always defocused
    const float sky_depth = max(buffer_frame.camera_far * 0.99f, 1.0f);

    float depths[FOCUS_SAMPLES];
    float nearest  = sky_depth;
    float any_sum  = 0.0f;
    float any_wsum = 0.0f;

    [unroll]
    for (int i = 0; i < FOCUS_SAMPLES; i++)
    {
        float t      = (float)i / (float)(FOCUS_SAMPLES - 1);
        float angle  = i * GOLDEN_ANGLE;
        float radius = sqrt(t) * FOCUS_REGION;

        float sin_a, cos_a;
        sincos(angle, sin_a, cos_a);
        float2 uv = center + float2(cos_a, sin_a) * radius;

        float depth = get_linear_depth(uv * get_render_uv_scale());
        depths[i]   = depth;

        if (depth < sky_depth)
        {
            float w   = focus_sample_weight(i);
            nearest   = min(nearest, depth);
            any_sum  += depth * w;
            any_wsum += w;
        }
    }

    // the whole focus region is sky, focus far so nothing gets blurred
    if (any_wsum <= FLT_MIN)
        return sky_depth;

    // the nearest hit and everything close behind it are one subject, a single isolated
    // sample is treated as noise instead so a stray thin edge cannot grab focus
    float near_limit = nearest * SUBJECT_TOLERANCE;
    float near_sum   = 0.0f;
    float near_wsum  = 0.0f;
    int   near_count = 0;
    float next_near  = sky_depth;

    [unroll]
    for (int j = 0; j < FOCUS_SAMPLES; j++)
    {
        if (depths[j] >= sky_depth)
            continue;

        if (depths[j] <= near_limit)
        {
            float w     = focus_sample_weight(j);
            near_sum   += depths[j] * w;
            near_wsum  += w;
            near_count += 1;
        }
        else
        {
            next_near = min(next_near, depths[j]);
        }
    }

    if (near_count >= 2)
        return near_sum / near_wsum;

    // the nearest hit stood alone, fall back to the cluster behind it
    float second_limit = next_near * SUBJECT_TOLERANCE;
    float second_sum   = 0.0f;
    float second_wsum  = 0.0f;

    [unroll]
    for (int k = 0; k < FOCUS_SAMPLES; k++)
    {
        if (depths[k] >= sky_depth || depths[k] <= near_limit || depths[k] > second_limit)
            continue;

        float w      = focus_sample_weight(k);
        second_sum  += depths[k] * w;
        second_wsum += w;
    }

    if (second_wsum > FLT_MIN)
        return second_sum / second_wsum;

    // a single non sky sample in the whole region, trust it
    return any_sum / any_wsum;
}

/*------------------------------------------------------------------------------
    smooth the raw focus target toward a cinematic focus pull
------------------------------------------------------------------------------*/
float smooth_focus_distance(float target_distance)
{
    float prev = tex2.Load(int3(0, 0, 0)).r;
    if (isnan(prev) || prev <= 0.0f)
        return target_distance;

    // pull nearer a touch faster, matches how real lenses and eyes settle
    float speed = target_distance < prev ? FOCUS_ADAPT_SPEED * 1.6f : FOCUS_ADAPT_SPEED;
    float alpha = 1.0f - exp(-speed * buffer_frame.delta_time);
    return lerp(prev, target_distance, alpha);
}

/*------------------------------------------------------------------------------
    map a unit disk sample onto a polygonal aperture
    wide open stays round, stopping down reveals blade shape
------------------------------------------------------------------------------*/
float2 aperture_sample_offset(float t, float angle, float aperture_fstop)
{
    float r = sqrt(t);

    float sector = fmod(angle, BLADE_ANGLE) - BLADE_ANGLE * 0.5f;
    float poly_r = cos(BLADE_ANGLE * 0.5f) / max(cos(sector), 1e-3f);

    // f/1.4 ~ circular, f/8+ ~ clear hexagon
    float poly_amount = saturate((aperture_fstop - 1.4f) / 6.5f);
    r *= lerp(1.0f, poly_r, poly_amount * 0.9f);

    float sin_a, cos_a;
    sincos(angle, sin_a, cos_a);
    return float2(cos_a, sin_a) * r;
}

/*------------------------------------------------------------------------------
    main bokeh blur with gather sampling
------------------------------------------------------------------------------*/
float3 bokeh_gather(float2 uv, float center_coc, float center_depth, lens_t lens, float2 texel_size, float2 resolution)
{
    float blur_radius = abs(center_coc);

    float3 color_sum  = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
    float  weight_sum = 1.0f;

    bool center_is_fg = center_coc < 0.0f;

    // randomize starting angle per pixel for temporal stability with taa
    float angle = noise_interleaved_gradient(uv * resolution, true) * PI2;

    [loop]
    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        angle += GOLDEN_ANGLE;

        float t = (float)(i + 1) * INV_SAMPLE_COUNT;
        float2 disk = aperture_sample_offset(t, angle, lens.aperture_fstop);
        float r = length(disk) * blur_radius;

        float2 offset    = disk * blur_radius * texel_size;
        float2 sample_uv = uv + offset;

        if (!is_valid_uv(sample_uv))
            continue;

        float3 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0).rgb;
        float  sample_depth = get_linear_depth(sample_uv * get_render_uv_scale());
        float  sample_coc   = compute_coc_signed(sample_depth, lens);
        float  abs_sample_coc = abs(sample_coc);

        float effective_coc = max(abs_sample_coc, blur_radius);
        float coverage      = saturate(1.0f - r / max(effective_coc, FLT_MIN));
        float depth_weight  = (sample_depth > center_depth && center_is_fg) ? BG_LEAK_PREVENTION : 1.0f;
        float scatter       = lerp(0.35f, 1.0f, saturate(abs_sample_coc * INV_SCATTER_NORM));

        // keep bright specular energy so out of focus lights form soft orbs
        float lum       = luminance(sample_color);
        float highlight = 1.0f + HIGHLIGHT_GAIN * saturate((lum - HIGHLIGHT_THRESHOLD) * 0.25f);

        float w = coverage * depth_weight * scatter * highlight;

        color_sum  += sample_color * w;
        weight_sum += w;
    }

    return color_sum / max(weight_sum, FLT_MIN);
}

/*------------------------------------------------------------------------------
    compute shader entry point
------------------------------------------------------------------------------*/
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex, uint3 group_id : SV_GroupID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);

    // one thread per group computes the lens constants and shares them with the rest
    if (group_index == 0)
    {
        float aperture_fstop = max(pass_get_f3_value().x, 0.5f);

        // physical focal length from horizontal fov and full frame sensor width
        float fov_h    = max(buffer_frame.camera_fov, 0.01f);
        float f        = (SENSOR_WIDTH_MM * 0.001f) * 0.5f / max(tan(fov_h * 0.5f), 1e-4f);
        float aperture_diameter = f / aperture_fstop;
        float sensor_m          = SENSOR_HEIGHT_MM * 0.001f;
        float pixels_per_meter  = resolution.y / sensor_m;

        float target_focus   = compute_focus_distance(resolution);
        float focus_distance = smooth_focus_distance(target_focus);
        float s              = max(focus_distance, f + 0.01f);

        gs_lens.focus_distance = s;
        gs_lens.coc_factor     = (aperture_diameter * f * pixels_per_meter) / (abs(s - f) + FLT_MIN);
        gs_lens.aperture_fstop = aperture_fstop;

        // only the first group on the owning view persists the smoothed focus
        if (all(group_id.xy == 0) && pass_get_f3_value().y > 0.5f)
        {
            tex_uav2[uint2(0, 0)] = float4(s, s, s, 1.0f);
        }
    }
    GroupMemoryBarrierWithGroupSync();

    if (any(thread_id.xy >= uint2(resolution)))
        return;

    lens_t lens        = gs_lens;
    float2 uv          = (thread_id.xy + 0.5f) / resolution;
    float  depth       = get_linear_depth(uv * get_render_uv_scale());
    float  coc         = compute_coc_signed(depth, lens);
    float  blur_radius = abs(coc);

    // in focus passthrough
    if (blur_radius < IN_FOCUS_THRESHOLD)
    {
        tex_uav[thread_id.xy] = tex[thread_id.xy];
        return;
    }

    float2 texel_size = 1.0f / resolution;
    float3 blurred    = bokeh_gather(uv, coc, depth, lens, texel_size, resolution);

    float4 original = tex[thread_id.xy];
    // soft transition out of the sharp plane, then full cinematic blur
    float  blend  = smoothstep(IN_FOCUS_THRESHOLD, IN_FOCUS_THRESHOLD + 1.75f, blur_radius);
    float3 result = lerp(original.rgb, blurred, blend);

    tex_uav[thread_id.xy] = float4(result, original.a);
}
