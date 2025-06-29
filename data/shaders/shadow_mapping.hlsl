/*
Copyright(c) 2015-2025 Panos Karabelas

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

#include "common.hlsl"

// note: golden ratio exists in the universe to maximize surface coverage, hence the use of it here

static const uint   g_shadow_sample_count            = 4;
static const float  g_shadow_filter_size             = 4.0f;
static const float  g_shadow_cascade_blend_threshold = 0.8f;
static const uint   g_penumbra_sample_count          = 8;
static const float  g_penumbra_filter_size           = 128.0f;
static const float  g_shadow_sample_reciprocal       = 1.0f / (float)g_shadow_sample_count;

float2 vogel_disk_sample(uint sample_index, uint sample_count, float angle)
{
    const float golden_angle = 2.399963f; // radians
    float radius             = sqrt(sample_index + 0.5f) / sqrt(sample_count);
    float theta              = sample_index * golden_angle + angle;
    float sine, cosine;
    sincos(theta, sine, cosine);
    
    return float2(cosine, sine) * radius;
}

float compute_penumbra(Light light, float rotation_angle, float3 sample_coords, float receiver_depth)
{
    float penumbra          = 1.0f;
    float blocker_depth_sum = 0.0f;
    uint  blocker_count     = 0;

    for(uint i = 0; i < g_penumbra_sample_count; i++)
    {
        float2 offset = vogel_disk_sample(i, g_penumbra_sample_count, rotation_angle) * light.texel_size * g_penumbra_filter_size;
        float depth   = light.sample_depth(sample_coords + float3(offset, 0.0f));

        if(depth > receiver_depth)
        {
            blocker_depth_sum += depth;
            blocker_count++;
        }
    }

    if (blocker_count > 0)
    {
        float blocker_depth_avg = blocker_depth_sum / blocker_count;
        
        // calculate depth difference
        float depth_difference = abs(receiver_depth - blocker_depth_avg);
        
        // use depth difference to scale penumbra
        penumbra = depth_difference / (blocker_depth_avg + FLT_MIN);
    }
    
    return clamp(penumbra * 16.0f, 1.0f, 1024.0f);
}

float vogel_depth(Light light, Surface surface, float3 sample_coords, float receiver_depth)
{
    float shadow_factor   = 0.0f;
    float temporal_offset = noise_interleaved_gradient(surface.pos);
    float temporal_angle  = temporal_offset * PI2;
    float penumbra        = compute_penumbra(light, temporal_angle, sample_coords, receiver_depth);

    for (uint i = 0; i < g_shadow_sample_count; i++)
    {
        float2 filter_size = light.texel_size * g_shadow_filter_size * penumbra;
        float2 offset      = vogel_disk_sample(i, g_shadow_sample_count, temporal_angle) * filter_size;
        float2 sample_uv   = sample_coords.xy + offset;

        // compute validity (1.0 = in-bounds, 0.0 = out-of-bounds)
        float is_valid = step(0.0f, sample_uv.x) * step(sample_uv.x, 1.0f) * 
                         step(0.0f, sample_uv.y) * step(sample_uv.y, 1.0f);

        // scale depth comparison by validity
        float depth_sample = light.compare_depth(float3(sample_uv, sample_coords.z), receiver_depth);
        shadow_factor += depth_sample + (1.0f - depth_sample) * (1.0f - is_valid);
    }

    return shadow_factor * g_shadow_sample_reciprocal;
}

float compute_shadow(Surface surface, Light light)
{
    float shadow = 1.0f;

    // process only if the pixel is within the light's effective range
    if (light.distance_to_pixel <= light.far)
    {
        // compute world position with normal offset bias to reduce shadow acne
        float3 normal_offset_bias = surface.normal * (1.0f - saturate(light.n_dot_l)) * 0.1f;
        float3 position_world     = surface.position + normal_offset_bias;

        if (light.is_point())
        {
            // compute paraboloid coordinates and depth
            uint slice_index     = dot(light.forward, light.to_pixel) < 0.0f; // 0 = front, 1 = back
            float3 position_view = mul(float4(position_world, 1.0f), light.transform[slice_index]).xyz;
            float3 ndc           = project_onto_paraboloid(position_view, light.near, light.far);
            
            // sample shadow map
            float3 sample_coords = float3(ndc_to_uv(ndc.xy), slice_index);
            shadow               = vogel_depth(light, surface, sample_coords, ndc.z);
        }
        else // directional, spot
        {
            // near cascade computation
            const uint near_cascade = 0;
            float3 near_ndc        = world_to_ndc(position_world, light.transform[near_cascade]);
            float2 near_uv         = ndc_to_uv(near_ndc);
            float3 near_sample     = float3(near_uv, near_cascade);
            float  near_depth      = near_ndc.z;
        
            shadow = vogel_depth(light, surface, near_sample, near_depth);

            // for directional lights, blend with the far cascade
            if (light.is_directional())
            {
                float blend_input = max(abs(near_ndc.x), abs(near_ndc.y));
                float cascade_blend_factor = smoothstep(g_shadow_cascade_blend_threshold, 1.0f, blend_input);
                if (cascade_blend_factor > 0.0f)
                {
                    const uint far_cascade = 1;
                    float3 far_ndc         = world_to_ndc(position_world + normal_offset_bias, light.transform[far_cascade]);
                    float2 far_uv          = ndc_to_uv(far_ndc);
                    float  far_depth       = far_ndc.z;
                    float  far_shadow      = vogel_depth(light, surface, float3(far_uv, far_cascade), far_depth);
        
                    shadow = lerp(shadow, far_shadow, cascade_blend_factor);
                }
            }
        }
    }

    return shadow;
}
