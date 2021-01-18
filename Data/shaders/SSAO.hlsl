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

static const uint ao_directions     = 1;
static const uint ao_steps          = 4;
static const float ao_radius        = 0.3f;
static const float ao_intensity     = 3.3f;
static const float ao_n_dot_v_bias  = 0.0f;

static const float ao_samples       = (float)(ao_directions * ao_steps);
static const float ao_radius2       = ao_radius * ao_radius;
static const float ao_negInvRadius2 = -1.0f / ao_radius2;

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
    float noise_gradient_temporal   = get_noise_interleaved_gradient(pos);
    float offset_spatial            = get_offset_non_temporal(pos);
    float offset_temporal           = get_offset();
    float offset_rotation_temporal  = get_direction();
    float ray_offset                = frac(offset_spatial + offset_temporal) + (get_random(uv) * 2.0 - 1.0) * 0.25;

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

float normal_oriented_hemisphere_ambient_occlusion(int2 pos)
{
    const float2 uv = (pos + 0.5f) / g_resolution;
    float3 position = get_position_view_space(pos);
    float3 normal   = get_normal_view_space(pos);
    float occlusion = 0.0f;
    
    // Use temporal interleaved gradient noise to rotate the random vector (free detail with TAA on)
    float3 random_vector    = unpack(get_noise_normal(pos));
    float ign               = get_noise_interleaved_gradient(pos);
    float rotation_angle    = max(ign * PI2, FLT_MIN);
    float3 rotation         = float3(cos(rotation_angle), sin(rotation_angle), 0.0f);
    random_vector           = float3(length(random_vector.xy) * normalize(rotation.xy), random_vector.z);
    
    [unroll]
    for (uint i = 0; i < ao_directions; i++)
    {
        // Compute offset
        float3 offset   = reflect(hemisphere_samples[i], random_vector);
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
