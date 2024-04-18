/*
Copyright(c) 2016-2024 Panos Karabelas

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
static const uint g_ao_directions = 3;
static const uint g_ao_steps      = 2;
static const float g_ao_radius    = 3.0f;
static const float g_ao_intensity = 4.0f;

// derived constants
static const float ao_samples        = (float)(g_ao_directions * g_ao_steps);
static const float ao_samples_rcp    = 1.0f / ao_samples;
static const float ao_radius2        = g_ao_radius * g_ao_radius;
static const float ao_negInvRadius2  = -1.0f / ao_radius2;
static const float ao_step_direction = PI2 / (float) g_ao_directions;

/*------------------------------------------------------------------------------
                              SOME GTAO FUNCTIONS
   https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
------------------------------------------------------------------------------*/

float get_offset_non_temporal(uint2 screen_pos)
{
    int2 position = (int2)(screen_pos);
    return 0.25 * (float)((position.y - position.x) & 3);
}
static const float offsets[]   = { 0.0f, 0.5f, 0.25f, 0.75f };
static const float rotations[] = { 0.1666f, 0.8333, 0.5f, 0.6666, 0.3333, 0.0f }; // 60.0f, 300.0f, 180.0f, 240.0f, 120.0f, 0.0f devived by 360.0f

/*------------------------------------------------------------------------------
                              SSGI
------------------------------------------------------------------------------*/

float compute_occlusion(float3 origin_position, float3 origin_normal, uint2 sample_position)
{
    float3 origin_to_sample = get_position_view_space(sample_position) - origin_position;
    float distance2         = dot(origin_to_sample, origin_to_sample);
    float occlusion         = dot(origin_normal, origin_to_sample) * rsqrt(distance2);
    float falloff           = saturate(distance2 * ao_negInvRadius2 + 1.0f);

    return occlusion * falloff;
}

void compute_ssgi(uint2 pos, float2 resolution_out, inout float visibility, inout float3 diffuse_bounce)
{
    const float2 origin_uv       = (pos + 0.5f) / resolution_out;
    const float3 origin_position = get_position_view_space(origin_uv);
    const float3 origin_normal   = get_normal_view_space(origin_uv);

    // compute step in pixels
    const float pixel_offset = max((g_ao_radius * resolution_out.x * 0.5f) / origin_position.z, (float)g_ao_steps);
    const float step_offset  = pixel_offset / float(g_ao_steps + 1.0f); // divide by steps + 1 so that the farthest samples are not fully attenuated

    // offsets (noise over space and time)
    const float noise_gradient_temporal  = get_noise_interleaved_gradient(pos, true, true);
    const float offset_spatial           = get_offset_non_temporal(pos);
    const float offset_temporal          = offsets[buffer_frame.frame % 4];
    const float offset_rotation_temporal = rotations[buffer_frame.frame % 6];
    const float ray_offset               = frac(offset_spatial + offset_temporal) + (get_random(origin_uv) * 2.0 - 1.0) * 0.25;

    // compute light/occlusion
    float2 texel_size = 1.0f / resolution_out;
    float occlusion   = 0.0f;
    [unroll]
    for (uint direction_index = 0; direction_index < g_ao_directions; direction_index++)
    {
        float rotation_angle      = (direction_index + noise_gradient_temporal + offset_rotation_temporal) * ao_step_direction;
        float2 rotation_direction = float2(cos(rotation_angle), sin(rotation_angle)) * texel_size;

        [unroll]
        for (uint step_index = 0; step_index < g_ao_steps; step_index++)
        {
            float2 uv_offset       = round(max(step_offset * (step_index + ray_offset), 1 + step_index)) * rotation_direction;
            uint2 sample_pos       = (origin_uv + uv_offset) * buffer_frame.resolution_render;
            float sample_occlusion = compute_occlusion(origin_position, origin_normal, sample_pos);

            occlusion      += sample_occlusion;
            diffuse_bounce += tex_light_diffuse[sample_pos].rgb * tex_albedo[sample_pos].rgb * max(0.01f, sample_occlusion); // max(0.01f, x) to avoid black pixels
        }
    }

    visibility      = 1.0f - saturate(occlusion * ao_samples_rcp * g_ao_intensity);
    diffuse_bounce *= ao_samples_rcp * g_ao_intensity * 3.0f; // 3.0f - this is what looks right
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    // ssgi
    float visibility      = 0.0f;
    float3 diffuse_bounce = 0.0f;
    compute_ssgi(thread_id.xy, resolution_out, visibility, diffuse_bounce);

    // out
    tex_uav[thread_id.xy] = float4(diffuse_bounce, visibility);
}
