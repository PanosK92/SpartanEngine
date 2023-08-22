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
#include "common.hlsl"
//====================

// constants
static const uint g_ao_directions      = 2;
static const uint g_ao_steps           = 2;
static const float g_ao_radius         = 1.7f;
static const float g_ao_intensity      = 4.0f;

// derived constants
static const float ao_samples        = (float)(g_ao_directions * g_ao_steps);
static const float ao_samples_rcp    = 1.0f / ao_samples;
static const float ao_radius2        = g_ao_radius * g_ao_radius;
static const float ao_negInvRadius2  = -1.0f / ao_radius2;
static const float ao_step_direction = PI2 / (float) g_ao_directions;

float compute_occlusion(float3 origin_position, float3 origin_normal, uint2 sample_pos)
{
    float3 sample_position  = get_position_view_space(sample_pos);
    float3 origin_to_sample = sample_position - origin_position;
    float distance_squared  = dot(origin_to_sample, origin_to_sample);
    float n_dot_s           = dot(origin_normal, origin_to_sample) * rsqrt(distance_squared);
    float falloff           = saturate(distance_squared * ao_negInvRadius2 + 1.0f);

    // create a bent normal in the direction of the sample
    float3 bent_normal = normalize(origin_normal + origin_to_sample * ao_radius2 * ao_negInvRadius2);

    // check the difference between the bent normal and the original normal
    float difference = dot(bent_normal, origin_normal);
    float occlusion  = (difference < n_dot_s) ? 1.0f : 0.0f;

    return occlusion * falloff;
}

// screen space temporal occlusion and diffuse illumination
void compute_ssgi(uint2 pos, inout float occlusion, inout float3 diffuse_bounce)
{
    const float2 origin_uv       = (pos + 0.5f) / pass_get_resolution_out();
    const float3 origin_position = get_position_view_space(pos);
    const float3 origin_normal   = get_normal_view_space(pos);

    // compute step in pixels
    const float pixel_offset = max((g_ao_radius * pass_get_resolution_out().x * 0.5f) / origin_position.z, (float) g_ao_steps);
    const float step_offset  = pixel_offset / float(g_ao_steps + 1.0f); // divide by steps + 1 so that the farthest samples are not fully attenuated

    // offsets (noise over space and time)
    const float noise_gradient_temporal  = get_noise_interleaved_gradient(pos);
    const float offset_spatial           = get_offset_non_temporal(pos);
    const float offset_temporal          = get_offset();
    const float offset_rotation_temporal = get_direction();
    const float ray_offset               = frac(offset_spatial + offset_temporal) + (get_random(origin_uv) * 2.0 - 1.0) * 0.25;

    // compute light/occlusion
    [unroll]
    for (uint direction_index = 0; direction_index < g_ao_directions; direction_index++)
    {
        float rotation_angle      = (direction_index + noise_gradient_temporal + offset_rotation_temporal) * ao_step_direction;
        float2 rotation_direction = float2(cos(rotation_angle), sin(rotation_angle)) * get_rt_texel_size();

        [unroll]
        for (uint step_index = 0; step_index < g_ao_steps; ++step_index)
        {
            float2 uv_offset      = round(max(step_offset * (step_index + ray_offset), 1 + step_index)) * rotation_direction;
            uint2 sample_pos      = (origin_uv + uv_offset) * pass_get_resolution_out();
            float sample_occlsion = compute_occlusion(origin_position, origin_normal, sample_pos);

            occlusion      += sample_occlsion;
            diffuse_bounce += tex_light_diffuse[sample_pos].rgb * tex_albedo[sample_pos].rgb * sample_occlsion;
        }
    }

    occlusion      = 1.0f - saturate(occlusion * ao_samples_rcp * g_ao_intensity);
    diffuse_bounce *= g_ao_intensity;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    float occlusion       = 0.0f;
    float3 diffuse_bounce = 0.0f;
    compute_ssgi(thread_id.xy, occlusion, diffuse_bounce);

    tex_uav[thread_id.xy] = float4(diffuse_bounce, occlusion);
}
