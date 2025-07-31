//= INCLUDES =========
#include "common.hlsl"
//====================

// constants
static const float MAX_BLUR_RADIUS            = 15.0f;       // max blur extent in pixels
static const float BASE_FOCAL_LENGTH          = 50.0;        // base lens focal length in mm
static const float SENSOR_HEIGHT              = 24.0;        // sensor size in mm (full-frame)
static const float EDGE_DETECTION_THRESHOLD   = 0.01f;       // sensitivity for edge detection
static const uint  AVERAGE_DEPTH_SAMPLE_COUNT = 10;          // samples for avg focus depth
static const float AVERAGE_DEPTH_RADIUS       = 0.5f;        // radius for avg depth sampling
static const float GOLDEN_ANGLE               = 2.39996323f; // angle for spiral sampling
static const int   SAMPLE_COUNT               = 16;          // blur samples (perf/quality balance)
static const float BACKGROUND_CLAMP_FACTOR    = 2.0f;        // background CoC clamp multiplier
static const float COC_SCALE                  = 1.0f;        // overall CoC strength scalar

float compute_coc(float2 uv, float2 texel_size, float2 resolution, float focus_distance, float aperture, float depth, bool compute_edge)
{
    float edge_factor = 0.0f;
    if (compute_edge)
    {
        float depth_diff = 0;
        for (int i = -1; i <= 1; i++)
        {
            for (int j = -1; j <= 1; j++)
            {
                float2 offset         = float2(i, j) * texel_size;
                float neighbor_depth  = get_linear_depth(uv + offset);
                depth_diff           += abs(depth - neighbor_depth);
            }
        }
        edge_factor = saturate(depth_diff / EDGE_DETECTION_THRESHOLD);
    }
    
    // adjust focal length based on focus distance
    float focal_length = BASE_FOCAL_LENGTH * (1.0 + saturate(focus_distance / 100.0));
    
    float f  = focal_length * 0.001; // convert to meters
    float s1 = focus_distance;
    float s2 = depth;
    
    // calculate the diameter of the circle of confusion
    float coc_diameter = abs(aperture * (s2 - s1) * f) / (s2 * (s1 - f));
    
    // convert the diameter to pixels
    float coc_pixels = coc_diameter * (resolution.y / SENSOR_HEIGHT);
    
    // adjust coc based on focus distance to enhance the effect
    float distance_factor  = saturate(focus_distance / 50.0);
    coc_pixels            *= lerp(2.0, 0.5, distance_factor);
    
    // enhance coc for edges
    coc_pixels *= (1.0 + edge_factor);
    
    return saturate(coc_pixels / 20.0) * COC_SCALE; // normalize CoC
}

float3 depth_aware_bokeh_blur(float2 uv, float center_coc, float center_depth, float focus_distance, float aperture, float2 texel_size, float2 resolution)
{
    if (center_coc < 0.01f) // early out for in-focus pixels to save performance
    {
        return tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f).rgb;
    }

    float3 color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0.0f).rgb;
    float total = 1.0f;

    float ang = 0.0f;
    for (int i = 1; i < SAMPLE_COUNT; ++i)
    {
        ang += GOLDEN_ANGLE;
        
        // sample radius quadratic for better distribution
        float r          = sqrt((float)i / (float)(SAMPLE_COUNT - 1)) * MAX_BLUR_RADIUS;
        float2 offset    = float2(cos(ang), sin(ang)) * r * texel_size;
        float2 sample_uv = uv + offset;

        float3 sample_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], sample_uv, 0.0f).rgb;
        float sample_depth  = get_linear_depth(sample_uv);
        float sample_coc    = compute_coc(sample_uv, texel_size, resolution, focus_distance, aperture, sample_depth, false); // skip edge for perf

        float sample_size = sample_coc * MAX_BLUR_RADIUS;
        // clamp background CoC to prevent over-blending into foreground
        if (sample_depth > center_depth)
        {
            sample_size = min(sample_size, center_coc * MAX_BLUR_RADIUS * BACKGROUND_CLAMP_FACTOR);
        }

        // soft contribution: does this sample's disk cover the center pixel?
        float dist = r; // since r is in pixels
        float m    = smoothstep(dist - 0.5f, dist + 0.5f, sample_size);

        color += lerp(color / total, sample_color, m);
        total += 1.0f;
    }

    return color / total;
}

float get_average_depth_circle(float2 center, float2 resolution_out)
{
    float2 texel_size = 1.0f / resolution_out;
    float angle_step  = PI2 / (float)AVERAGE_DEPTH_SAMPLE_COUNT;

    float average_depth = 0.0f;
    for (int i = 0; i < AVERAGE_DEPTH_SAMPLE_COUNT; i++)
    {
        float angle           = i * angle_step;
        float2 sample_offset  = float2(cos(angle), sin(angle)) * AVERAGE_DEPTH_RADIUS * texel_size;
        float2 sample_uv      = center + sample_offset;
        average_depth        += get_linear_depth(sample_uv);
    }

    return average_depth / (float)AVERAGE_DEPTH_SAMPLE_COUNT;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    const float2 uv         = (thread_id.xy + 0.5f) / resolution;
    const float2 texel_size = 1.0 / resolution;
    
    // get focal depth from camera
    float focal_depth = get_average_depth_circle(float2(0.5, 0.5f), resolution);
    float aperture    = pass_get_f3_value().x;

    // compute center values
    float center_depth = get_linear_depth(uv);
    float center_coc   = compute_coc(uv, texel_size, resolution, focal_depth, aperture, center_depth, true); // full edge for center

    // do the actual blurring with depth awareness
    float3 blurred_color  = depth_aware_bokeh_blur(uv, center_coc, center_depth, focal_depth, aperture, texel_size, resolution);
    float4 original_color = tex[thread_id.xy];
    float3 final_color    = lerp(original_color.rgb, blurred_color, center_coc);

    tex_uav[thread_id.xy] = float4(final_color, original_color.a);
}
