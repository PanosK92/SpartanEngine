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

// = INCLUDES ========
#include "common.hlsl"
//====================

// note: golden ratio exists in the universe to maximize surface coverage, hence the use of it here

static const uint   g_shadow_sample_count            = 4;
static const float  g_shadow_filter_size             = 1.0f; // get's x4 for directional
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
        float2 offset = vogel_disk_sample(i, g_penumbra_sample_count, rotation_angle) * light.atlas_texel_size[0] * g_penumbra_filter_size;
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

float vogel_depth(Light light, Surface surface, float3 sample_coords, float receiver_depth, float filter_size_multipler = 1.0f)
{
    float shadow_factor   = 0.0f;
    float temporal_offset = noise_interleaved_gradient(surface.pos);
    float temporal_angle  = temporal_offset * PI2;
    float penumbra        = compute_penumbra(light, temporal_angle, sample_coords, receiver_depth);

    for (uint i = 0; i < g_shadow_sample_count; i++)
    {
        float2 filter_size = light.atlas_texel_size[0] * g_shadow_filter_size * filter_size_multipler * penumbra;
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

float3 compute_normal_offset(Surface surface, Light light, uint cascade_index)
{
    // base bias per cascade: near/far
    float base_bias = (cascade_index == 0) ? 400.0f : 1000.0f;
    // slope factor
    float slope_scale = saturate(1.0f - light.n_dot_l);
    slope_scale = slope_scale * slope_scale; // stronger at grazing angles

    // texel scale in world units
    float texel_size = light.atlas_texel_size[cascade_index].x; // assuming square texels

    float3 normal_offset = surface.normal * base_bias * slope_scale * texel_size;

    // clamp to avoid over-biasing
    normal_offset = clamp(normal_offset, -0.5f * base_bias, 0.5f * base_bias);

    return normal_offset;
}

float compute_shadow(Surface surface, Light light)
{
    float3 to_light = light.position - surface.position;
    float dist_sq   = dot(to_light, to_light);

    // range-based fade for point and spot lights
    float fade = 1.0f;
    if (light.is_point() || light.is_spot())
    {
        fade = saturate(1.0f - dist_sq / (light.far * light.far));
        fade = fade * fade; // quadratic falloff
        if (fade <= 0.0f)
            return 1.0f; // fully lit
    }

    float shadow = 1.0f;

    if (light.is_point())
    {
        float3 position_world = surface.position + compute_normal_offset(surface, light, 0);
        float3 light_to_pixel = position_world - light.position;
        float3 abs_dir        = abs(light_to_pixel);

        // determine cube face
        uint face_index = (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) ? (light_to_pixel.x > 0 ? 0u : 1u) :
                          (abs_dir.y >= abs_dir.z)                           ? (light_to_pixel.y > 0 ? 2u : 3u) :
                                                                                (light_to_pixel.z > 0 ? 4u : 5u);

        float4 clip_pos = mul(float4(position_world, 1.0f), light.transform[face_index]);
        float3 ndc      = clip_pos.xyz / clip_pos.w;
        float2 uv       = ndc_to_uv(ndc.xy);
        shadow          = vogel_depth(light, surface, float3(uv, (float)face_index), ndc.z);

        // apply range fade
        shadow = lerp(1.0f, shadow, fade);
    }
    else // directional / spot
    {
        const uint near_cascade = 0;
        float3 position_world   = surface.position + compute_normal_offset(surface, light, near_cascade);
        float4 clip_pos_near    = mul(float4(position_world, 1.0f), light.transform[near_cascade]);
        float3 ndc_near         = clip_pos_near.xyz / clip_pos_near.w;
        float2 uv_near          = ndc_to_uv(ndc_near.xy);
        float  shadow_near      = vogel_depth(light, surface, float3(uv_near, near_cascade), ndc_near.z, 4.0f);
        shadow                  = shadow_near;

        if (light.is_directional())
        {
            const uint far_cascade = 1;
            float3 position_world_far = surface.position + compute_normal_offset(surface, light, far_cascade);
            float4 clip_pos_far       = mul(float4(position_world_far, 1.0f), light.transform[far_cascade]);
            float3 ndc_far            = clip_pos_far.xyz / clip_pos_far.w;
            float2 uv_far             = ndc_to_uv(ndc_far.xy);
            float  shadow_far         = vogel_depth(light, surface, float3(uv_far, far_cascade), ndc_far.z, 4.0f);

            float edge_dist    = max(abs(ndc_near.x), abs(ndc_near.y));
            float blend_factor = smoothstep(0.9f, 1.0f, edge_dist);
            shadow             = lerp(shadow_near, shadow_far, blend_factor);
        }

        // apply range fade for spot light
        if (light.is_spot())
            shadow = lerp(1.0f, shadow, fade);
    }

    return shadow;
}

