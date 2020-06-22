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

static const uint g_dof_temporal_directions = 16;
static const uint g_dof_sample_count        = 22;
static const float g_dof_size               = 15.0f;

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

// Returns the focus distance by computing the average depth in a cross pattern neighborhood
float get_focus_distnace()
{
    float2 uv   = 0.5f; // center
    float dx    = g_dof_size * g_texel_size.x;
    float dy    = g_dof_size * g_texel_size.y;
	
    float tl = get_linear_depth(uv + float2(-dx, -dy));
    float tr = get_linear_depth(uv + float2(+dx, -dy));
    float bl = get_linear_depth(uv + float2(-dx, +dy));
    float br = get_linear_depth(uv + float2(+dx, +dy));
    float ce = get_linear_depth(uv);
	
    return (tl + tr + bl + br + ce) * 0.2f;
}

float circle_of_confusion(float depth, float focus_distance, float focus_range)
{
    float coc = (depth - focus_distance) / focus_range;
    return abs(clamp(coc, -1.0f, 1.0f));
}

float2 rotate_direction(float2 Dir, float2 CosSin)
{
    return float2(Dir.x * CosSin.x - Dir.y * CosSin.y,
              Dir.x * CosSin.y + Dir.y * CosSin.x);
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    // Autofocus
    float focus_distance = get_focus_distnace();
    
    // Find circle of confusion
    float depth         = get_linear_depth(input.uv);
    float focus_range   = g_camera_aperture * 100.0f;
    float coc           = circle_of_confusion(depth, focus_distance, focus_range);

    // Temporal noise and rotation
    float noise_gradient_temporal   = interleaved_gradient_noise(input.uv * g_resolution);
    float offset_rotation_temporal  = noise_temporal_direction();
    
    // Temporal offset
    float temporal_scale    = 10.0f;
    float temporal_offset   = noise_gradient_temporal * (temporal_scale / g_dof_size) * any(g_taa_jitter_offset);

    // Temporal rotation
    float rotation_angle        = (g_frame + noise_gradient_temporal + offset_rotation_temporal) * g_dof_rotation_step * any(g_taa_jitter_offset);
    float2 rotation_direction   = normalize(float2(cos(rotation_angle), sin(rotation_angle)));
    
    // Sample color
    float4 color = 0.0f;
    [unroll]
    for (uint i = 0; i < g_dof_sample_count; i++)
    {
        float2 direction            = g_dof_samples[i];
        float2 temporal_direction   = rotate_direction(direction, rotation_direction);
        float2 jitter               = temporal_direction - temporal_offset * temporal_direction;
        jitter                      *= g_texel_size * g_dof_size;
      
        color += tex.Sample(sampler_point_clamp, input.uv + jitter);
    }
    color /= (float)g_dof_sample_count;

    // Lerp
    color = lerp(tex.Sample(sampler_point_clamp, input.uv), color, coc);
    return saturate_16(color);
}
