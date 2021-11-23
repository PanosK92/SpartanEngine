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

static const uint g_ao_directions           = 2;
static const uint g_ao_steps                = 2;
static const float g_ao_radius              = 0.5f;
static const float g_ao_intensity_occlusion = 3.5f;
static const float g_ao_intensity_gi        = 10.0f;
static const float g_ao_occlusion_bias      = 0.0f;

static const float ao_samples       = (float)(g_ao_directions * g_ao_steps);
static const float ao_samples_rcp   = 1.0f / ao_samples;
static const float ao_radius2       = g_ao_radius * g_ao_radius;
static const float ao_negInvRadius2 = -1.0f / ao_radius2;

float compute_falloff(float distance_squared)
{
    return saturate(distance_squared * ao_negInvRadius2 + 1.0f);
}

float compute_occlusion(float3 origin_position, float3 origin_normal, float3 sample_position)
{
    float3 origin_to_sample = sample_position - origin_position;
    float distance_squared  = dot(origin_to_sample, origin_to_sample);
    float n_dot_v           = dot(origin_normal, origin_to_sample) * rsqrt(distance_squared);
    float falloff           = compute_falloff(distance_squared);

    return saturate(n_dot_v - g_ao_occlusion_bias) * falloff;
}

float4 ground_truth_ambient_occlusion(uint2 pos)
{
    const float2 origin_uv       = (pos + 0.5f) / g_resolution_rt;
    const float3 origin_position = get_position_view_space(pos);
    const float3 origin_normal   = get_normal_view_space(pos);
    
    // Compute step in pixels
    const float pixel_offset = max((g_ao_radius * g_resolution_rt.x * 0.5f) / origin_position.z, (float)g_ao_steps);
    const float step_offset  = pixel_offset / (g_ao_steps + 1); // divide by steps + 1 so that the farthest samples are not fully attenuated

    // Comute rotation step
    const float step_direction = PI2 / (float)g_ao_directions;

    // Offsets (noise over space and time)
    const float noise_gradient_temporal  = get_noise_interleaved_gradient(pos);
    const float offset_spatial           = get_offset_non_temporal(pos);
    const float offset_temporal          = get_offset();
    const float offset_rotation_temporal = get_direction();
    const float ray_offset               = frac(offset_spatial + offset_temporal) + (get_random(origin_uv) * 2.0 - 1.0) * 0.25;

    // Compute occlusion
    float4 light = 0;
    [unroll]
    for (uint direction_index = 0; direction_index < g_ao_directions; direction_index++)
    {
        const float rotation_angle      = (direction_index + noise_gradient_temporal + offset_rotation_temporal) * step_direction;
        const float2 rotation_direction = float2(cos(rotation_angle), sin(rotation_angle)) * g_texel_size;

        [unroll]
        for (uint step_index = 0; step_index < g_ao_steps; ++step_index)
        {
            const float2 uv_offset       = round(max(step_offset * (step_index + ray_offset), 1 + step_index)) * rotation_direction;
            const uint2 sample_pos       = (origin_uv + uv_offset) * g_resolution_rt;
            const float3 sample_position = get_position_view_space(sample_pos);
            const float transport        = compute_occlusion(origin_position, origin_normal, sample_position) * screen_fade(origin_uv);

            light.a += transport;
#if GI
            float3 diffuse = tex_light_diffuse[sample_pos].rgb;
            float3 albedo  = tex_albedo[sample_pos].rgb;
            light.rgb      += diffuse * albedo * transport;
#endif
        }
    }

    light.a   = 1.0f - saturate(light.a * g_ao_intensity_occlusion * ao_samples_rcp);
    light.rgb = saturate(light.rgb * g_ao_intensity_gi * ao_samples_rcp) * light.a;

    return light;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check
    if (any(int2(thread_id.xy) >= g_resolution_rt.xy))
        return;

#if GI
    tex_out_rgba[thread_id.xy] = ground_truth_ambient_occlusion(thread_id.xy);
#else
    tex_out_r[thread_id.xy] = ground_truth_ambient_occlusion(thread_id.xy).a;
#endif
}
