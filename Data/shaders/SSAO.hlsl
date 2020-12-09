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

static const uint ao_directions     = 1;
static const uint ao_steps          = 4;
static const float ao_radius        = 0.3f;
static const float ao_intensity     = 3.3f;
static const float ao_n_dot_v_bias  = 0.0f;

static const float ao_samples       = (float)(ao_directions * ao_steps);
static const float ao_radius2       = ao_radius * ao_radius;
static const float ao_negInvRadius2 = -1.0f / ao_radius2;
static const float2 noise_scale     = float2(g_resolution.x / 256.0f, g_resolution.y / 256.0f);

float falloff(float distance_squared)
{
    return distance_squared * ao_negInvRadius2 + 1.0f;
}

float compute_occlusion(float3 position, float3 normal, float3 sample_position)
{
    float3 v        = sample_position - position;
    float v_dot_v   = dot(v, v);
    float n_dot_v   = dot(normal, v) * rsqrt(v_dot_v);

    return saturate(n_dot_v - ao_n_dot_v_bias) * saturate(falloff(v_dot_v));
}

float horizon_based_ambient_occlusion(int2 pos)
{
    const float2 uv = (pos + 0.5f) / g_resolution;
    float3 position = get_position_view_space(pos);
    float3 normal   = get_normal_view_space(pos);
    
    // Compute length and rotation steps
    float step_length   = max((ao_radius * g_resolution.x * 0.5f) / position.z, (float)ao_steps);
    step_length         = step_length / (ao_steps + 1); // divide by ao_steps + 1 so that the farthest samples are not fully attenuated
    float step_angle    = PI2 / (float)ao_directions;

    // Offsets (noise over space and time)
    float noise_gradient_temporal   = interleaved_gradient_noise(uv * g_resolution);
    float offset_spatial            = noise_spatial_offset(uv * g_resolution);
    float offset_temporal           = noise_temporal_offset();
    float offset_rotation_temporal  = noise_temporal_direction();
    float ray_offset                = frac(offset_spatial + offset_temporal) + (random(uv) * 2.0 - 1.0) * 0.25;

    float occlusion = 0;
    
    [unroll]
    for (uint direction_index = 0; direction_index < ao_directions; direction_index++)
    {
        float rotation_angle        = (direction_index + noise_gradient_temporal + offset_rotation_temporal) * step_angle;
        float2 rotation_direction   = float2(cos(rotation_angle), sin(rotation_angle)) * g_texel_size;

        [unroll]
        for (uint step_index = 0; step_index < ao_steps; ++step_index)
        {
            float2 uv_offset        = max(step_length * (step_index + ray_offset), 1 + step_index) * rotation_direction;
            float2 sample_uv        = uv + uv_offset;
            float3 sample_position  = get_position_view_space(sample_uv);

            // Occlusion
            occlusion += compute_occlusion(position, normal, sample_position);
        }
    }

    return 1.0f - saturate(occlusion * ao_intensity / ao_samples);
}

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    tex_out_r[thread_id.xy] = horizon_based_ambient_occlusion(thread_id.xy);
}

// DEPRECATED

static const float3 sample_kernel[64] =
{
    float3(0.04977, -0.04471, 0.04996),
    float3(0.01457, 0.01653, 0.00224),
    float3(-0.04065, -0.01937, 0.03193),
    float3(0.01378, -0.09158, 0.04092),
    float3(0.05599, 0.05979, 0.05766),
    float3(0.09227, 0.04428, 0.01545),
    float3(-0.00204, -0.0544, 0.06674),
    float3(-0.00033, -0.00019, 0.00037),
    float3(0.05004, -0.04665, 0.02538),
    float3(0.03813, 0.0314, 0.03287),
    float3(-0.03188, 0.02046, 0.02251),
    float3(0.0557, -0.03697, 0.05449),
    float3(0.05737, -0.02254, 0.07554),
    float3(-0.01609, -0.00377, 0.05547),
    float3(-0.02503, -0.02483, 0.02495),
    float3(-0.03369, 0.02139, 0.0254),
    float3(-0.01753, 0.01439, 0.00535),
    float3(0.07336, 0.11205, 0.01101),
    float3(-0.04406, -0.09028, 0.08368),
    float3(-0.08328, -0.00168, 0.08499),
    float3(-0.01041, -0.03287, 0.01927),
    float3(0.00321, -0.00488, 0.00416),
    float3(-0.00738, -0.06583, 0.0674),
    float3(0.09414, -0.008, 0.14335),
    float3(0.07683, 0.12697, 0.107),
    float3(0.00039, 0.00045, 0.0003),
    float3(-0.10479, 0.06544, 0.10174),
    float3(-0.00445, -0.11964, 0.1619),
    float3(-0.07455, 0.03445, 0.22414),
    float3(-0.00276, 0.00308, 0.00292),
    float3(-0.10851, 0.14234, 0.16644),
    float3(0.04688, 0.10364, 0.05958),
    float3(0.13457, -0.02251, 0.13051),
    float3(-0.16449, -0.15564, 0.12454),
    float3(-0.18767, -0.20883, 0.05777),
    float3(-0.04372, 0.08693, 0.0748),
    float3(-0.00256, -0.002, 0.00407),
    float3(-0.0967, -0.18226, 0.29949),
    float3(-0.22577, 0.31606, 0.08916),
    float3(-0.02751, 0.28719, 0.31718),
    float3(0.20722, -0.27084, 0.11013),
    float3(0.0549, 0.10434, 0.32311),
    float3(-0.13086, 0.11929, 0.28022),
    float3(0.15404, -0.06537, 0.22984),
    float3(0.05294, -0.22787, 0.14848),
    float3(-0.18731, -0.04022, 0.01593),
    float3(0.14184, 0.04716, 0.13485),
    float3(-0.04427, 0.05562, 0.05586),
    float3(-0.02358, -0.08097, 0.21913),
    float3(-0.14215, 0.19807, 0.00519),
    float3(0.15865, 0.23046, 0.04372),
    float3(0.03004, 0.38183, 0.16383),
    float3(0.08301, -0.30966, 0.06741),
    float3(0.22695, -0.23535, 0.19367),
    float3(0.38129, 0.33204, 0.52949),
    float3(-0.55627, 0.29472, 0.3011),
    float3(0.42449, 0.00565, 0.11758),
    float3(0.3665, 0.00359, 0.0857),
    float3(0.32902, 0.0309, 0.1785),
    float3(-0.08294, 0.51285, 0.05656),
    float3(0.86736, -0.00273, 0.10014),
    float3(0.45574, -0.77201, 0.00384),
    float3(0.41729, -0.15485, 0.46251),
    float3(-0.44272, -0.67928, 0.1865)
};

float normal_oriented_hemisphere_ambient_occlusion(int2 pos)
{
    const float2 uv = (pos + 0.5f) / g_resolution;
    float3 position = get_position_view_space(pos);
    float3 normal   = get_normal_view_space(pos);
    float occlusion = 0.0f;
    
    // Use temporal interleaved gradient noise to rotate the random vector (free detail with TAA on)
    float3 random_vector    = unpack(normalize(tex_normal_noise.Sample(sampler_bilinear_wrap, uv * noise_scale).xyz));
    float ign               = interleaved_gradient_noise(uv * g_resolution);
    float rotation_angle    = max(ign * PI2, FLT_MIN);
    float3 rotation         = float3(cos(rotation_angle), sin(rotation_angle), 0.0f);
    random_vector           = float3(length(random_vector.xy) * normalize(rotation.xy), random_vector.z);
    
    [unroll]
    for (uint i = 0; i < ao_directions; i++)
    {
        // Compute offset
        float3 offset   = reflect(sample_kernel[i], random_vector);
        offset          *= ao_radius;                   // Scale by radius
        offset          *= sign(dot(offset, normal));   // Flip if behind normal
        
        // Compute sample pos
        float3 sample_pos   = position + offset;
        float2 sample_uv    = project_uv(sample_pos, g_projection);
        sample_pos          = get_position_view_space(sample_uv);

        // Occlusion
        occlusion += compute_occlusion(position, normal, sample_pos);
    }

    return 1.0f - saturate(occlusion * ao_intensity / float(ao_directions));
}
