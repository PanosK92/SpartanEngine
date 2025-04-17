/*
Copyright(c) 2016-2025 Panos Karabelas

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
static const float g_ao_radius      = 32.0f;
static const float g_ao_intensity   = 1.0f;
static const uint g_directions      = 4;
static const uint g_steps           = 4;
static const float ao_radius2       = g_ao_radius * g_ao_radius;
static const float ao_negInvRadius2 = -1.0f / ao_radius2;

float get_offset_non_temporal(uint2 screen_pos)
{
    int2 position = (int2)(screen_pos);
    return 0.25 * (float)((position.y - position.x) & 3);
}

float compute_horizon_occlusion(float3 origin_position, float3 origin_normal, float2 sample_uv, float2 resolution)
{
    float3 sample_position = get_position_view_space(sample_uv);
    float3 origin_to_sample = sample_position - origin_position;
    float distance2 = dot(origin_to_sample, origin_to_sample);
    
    // early out if sample is too far
    if (distance2 > ao_radius2)
        return 0.0f;

    // normalize direction and compute cosine term
    float3 direction = origin_to_sample * rsqrt(distance2);
    float cos_horizon = max(0.0f, dot(origin_normal, direction));
    
    // falloff based on distance
    float falloff = saturate(distance2 * ao_negInvRadius2 + 1.0f);
    
    // GTAO-inspired occlusion: cosine-weighted contribution
    return cos_horizon * falloff;
}

float compute_gtao(uint2 pos, float2 resolution_out)
{
    const float2 origin_uv = (pos + 0.5f) / resolution_out;
    const float3 origin_position = get_position_view_space(origin_uv);
    const float3 origin_normal = get_normal_view_space(origin_uv);

    // reuse your surface roughness if needed (optional)
    Surface surface;
    surface.Build(pos, resolution_out, true, false);
    const float roughness = surface.roughness; // Could modulate intensity if desired

    // GTAO parameters
    float ao_samples     = (float)(g_directions * g_steps);
    float ao_samples_rcp = 1.0f / ao_samples;
    float2 texel_size    = 1.0f / resolution_out;

    // temporal noise for stability
    const float noise_gradient_temporal = get_noise_interleaved_gradient(pos);
    const float offset_spatial = get_offset_non_temporal(pos);
    const float ray_offset = frac(offset_spatial + noise_gradient_temporal);

    float occlusion = 0.0f;
    [loop]
    for (uint direction_index = 0; direction_index < g_directions; direction_index++)
    {
        // distribute directions evenly across 2PI, with temporal noise
        float rotation_angle = (direction_index + ray_offset) * (PI2 / (float)g_directions);
        float2 direction = float2(cos(rotation_angle), sin(rotation_angle)) * texel_size;

        // horizon-based sampling along each direction
        [loop]
        for (uint step_index = 0; step_index < g_steps; step_index++)
        {
            // linear sampling along the direction
            float step_size = g_ao_radius * ((step_index + 1.0f) / (float)g_steps);
            float2 uv_offset = direction * step_size;
            float2 sample_uv = origin_uv + uv_offset;

            // ensure sample is within bounds
            if (sample_uv.x < 0.0f || sample_uv.x > 1.0f || sample_uv.y < 0.0f || sample_uv.y > 1.0f)
                continue;

            // compute horizon occlusion
            float sample_occlusion = compute_horizon_occlusion(origin_position, origin_normal, sample_uv, resolution_out);
            occlusion += sample_occlusion;
        }
    }

    // normalize and apply intensity
    float visibility = 1.0f - saturate(occlusion * ao_samples_rcp * g_ao_intensity);
    return visibility;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);

    // ensure thread is within bounds
    if (thread_id.x >= resolution_out.x || thread_id.y >= resolution_out.y)
        return;

    float visibility = compute_gtao(thread_id.xy, resolution_out);
    tex_uav[thread_id.xy] = visibility;
}
