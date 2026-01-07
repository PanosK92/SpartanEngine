/*
Copyright(c) 2015-2026 Panos Karabelas

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

// shadow mapping constants
static const uint  g_shadow_sample_count            = 4;
static const float g_shadow_filter_size             = 2.5f;
static const float g_shadow_cascade_blend_threshold = 0.8f;
static const uint  g_penumbra_sample_count          = 8;
static const float g_penumbra_filter_size           = 128.0f;
static const float g_shadow_sample_reciprocal       = 1.0f / (float)g_shadow_sample_count;
static const float g_minimum_penumbra_size          = 0.5f;
static const float g_maximum_penumbra_size          = 64.0f;
static const float g_contact_hardening_factor       = 0.5f;
static const float g_base_bias_texels               = 0.2f;
static const float g_slope_bias_texels              = 1.5f;

// pre-computed vogel disk samples
static const float2 g_vogel_samples_shadow[g_shadow_sample_count] =
{
    float2(0.353553f, 0.000000f),
    float2(-0.451544f, 0.413652f),
    float2(0.069116f, -0.787542f),
    float2(0.569143f, 0.742345f)
};

static const float2 g_vogel_samples_penumbra[g_penumbra_sample_count] =
{
    float2(0.250000f, 0.000000f),
    float2(-0.319290f, 0.292496f),
    float2(0.048872f, -0.556877f),
    float2(0.402445f, 0.524917f),
    float2(-0.738535f, -0.130636f),
    float2(0.699604f, -0.445032f),
    float2(-0.234003f, 0.870484f),
    float2(-0.446273f, -0.859268f)
};

// rotate 2d vector by angle
float2 rotate_2d(float2 v, float angle)
{
    float sine, cosine;
    sincos(angle, sine, cosine);
    return float2(
        v.x * cosine - v.y * sine,
        v.x * sine + v.y * cosine
    );
}

// get vogel disk sample from lookup table
float2 vogel_disk_sample(uint sample_index, uint sample_count, float angle)
{
    float2 sample;
    
    // lookup pre-computed sample
    if (sample_count == g_shadow_sample_count)
    {
        sample = g_vogel_samples_shadow[sample_index];
    }
    else if (sample_count == g_penumbra_sample_count)
    {
        sample = g_vogel_samples_penumbra[sample_index];
    }
    else
    {
        // fallback calculation
        const float golden_angle = 2.399963f;
        float radius             = sqrt(sample_index + 0.5f) / sqrt(sample_count);
        float theta              = sample_index * golden_angle + angle;
        float sine, cosine;
        sincos(theta, sine, cosine);
        return float2(cosine, sine) * radius;
    }
    
    // apply rotation
    return rotate_2d(sample, angle);
}

// estimate penumbra size using pcss
float compute_penumbra(Light light, float rotation_angle, float3 sample_coords, float receiver_depth, float light_distance)
{
    float penumbra                  = g_minimum_penumbra_size;
    float blocker_depth_sum         = 0.0f;
    uint  blocker_count             = 0;
    uint  cascade_index             = (uint)sample_coords.z;
    float2 texel_size_cascade_local = light.atlas_texel_size[cascade_index] / light.atlas_scale[cascade_index];
    float search_radius             = g_penumbra_filter_size * texel_size_cascade_local.x;

    // adaptive blocker search
    for(uint i = 0; i < g_penumbra_sample_count; i++)
    {
        float2 offset = vogel_disk_sample(i, g_penumbra_sample_count, rotation_angle) * search_radius;
        float depth   = light.sample_depth(sample_coords + float3(offset, 0.0f));

        if(depth > receiver_depth + 0.0001f)
        {
            blocker_depth_sum += depth;
            blocker_count++;
        }
    }

    // compute penumbra size
    if (blocker_count > 0)
    {
        float blocker_depth_avg = blocker_depth_sum / float(blocker_count);
        float depth_difference  = receiver_depth - blocker_depth_avg;
        
        if (depth_difference > 0.0f && blocker_depth_avg > 0.0f)
        {
            float light_size_factor = saturate(light_distance * 0.1f);
            penumbra                = (depth_difference / blocker_depth_avg) * light_size_factor * search_radius;
            
            // contact hardening
            float contact_factor = saturate(depth_difference * 1000.0f);
            penumbra             = lerp(penumbra * g_contact_hardening_factor, penumbra, contact_factor);
        }
    }
    
    return clamp(penumbra, g_minimum_penumbra_size, g_maximum_penumbra_size);
}

// compute shadow factor using vogel disk sampling
float vogel_depth(Light light, Surface surface, float3 sample_coords, float receiver_depth, float filter_size_multiplier = 1.0f)
{
    float shadow_factor = 0.0f;
    
    // temporal jitter
    float temporal_offset = noise_interleaved_gradient(surface.pos);
    float temporal_angle  = temporal_offset * PI2;
    
    // estimate penumbra
    float light_distance  = light.is_directional() ? 1000.0f : length(surface.position - light.position);
    float penumbra        = compute_penumbra(light, temporal_angle, sample_coords, receiver_depth, light_distance);

    // setup sampling
    uint cascade_index              = (uint)sample_coords.z;
    float2 texel_size_cascade_local = light.atlas_texel_size[cascade_index] / light.atlas_scale[cascade_index];
    float valid_sample_count        = 0.0f;
    
    // sample shadow map
    for (uint i = 0; i < g_shadow_sample_count; i++)
    {
        float2 filter_size = texel_size_cascade_local * g_shadow_filter_size * filter_size_multiplier * penumbra;
        float2 offset      = vogel_disk_sample(i, g_shadow_sample_count, temporal_angle) * filter_size;
        float2 sample_uv   = sample_coords.xy + offset;

        // check bounds and fade
        float2 uv_clamped = clamp(sample_uv, 0.0f, 1.0f);
        float2 fade_dist  = min(sample_uv, 1.0f - sample_uv) * 10.0f;
        float fade_factor = saturate(min(fade_dist.x, fade_dist.y));
        float is_valid    = step(0.0f, sample_uv.x) * step(sample_uv.x, 1.0f) * step(0.0f, sample_uv.y) * step(sample_uv.y, 1.0f);

        // depth comparison
        float depth_sample = light.compare_depth(float3(uv_clamped, sample_coords.z), receiver_depth);
        depth_sample       = lerp(1.0f, depth_sample, fade_factor * is_valid + (1.0f - is_valid));
        
        shadow_factor      += depth_sample;
        valid_sample_count += is_valid;
    }

    float sample_count = max(valid_sample_count, 1.0f);
    return shadow_factor / sample_count;
}

// compute depth bias using slope-scaled technique in world space
float3 compute_normal_offset(Surface surface, Light light, uint cascade_index)
{
    // calculate world-space texel size from projection matrix
    float world_frustum_width = 2.0f / length(light.transform[cascade_index][0].xyz);
    float texel_size_world    = world_frustum_width * light.atlas_texel_size[cascade_index].x;

    // compute slope
    float3 light_dir = light.is_directional() ? normalize(-light.forward.xyz) : normalize(surface.position - light.position);
    float n_dot_l    = dot(surface.normal, light_dir);
    float slope      = sqrt(saturate(1.0f - n_dot_l * n_dot_l));

    // calculate final offset
    float offset_amount = (g_base_bias_texels + (slope * g_slope_bias_texels)) * texel_size_world;
    
    return surface.normal * offset_amount;
}

// main shadow computation function
float compute_shadow(Surface surface, Light light)
{
    float3 to_light = light.position - surface.position;
    float dist_sq   = dot(to_light, to_light);

    // quadratic falloff for point/spot lights
    float fade = 1.0f;
    if (light.is_point() || light.is_spot())
    {
        fade = saturate(1.0f - dist_sq / (light.far * light.far));
        fade = fade * fade; 
        if (fade <= 0.0f)
            return 1.0f; 
    }

    float shadow = 1.0f;

    // point light (cubemap)
    if (light.is_point())
    {
        float3 position_world = surface.position + compute_normal_offset(surface, light, 0);
        float3 light_to_pixel = position_world - light.position;
        float3 abs_dir        = abs(light_to_pixel);

        uint face_index = (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) ? (light_to_pixel.x > 0.0f ? 0u : 1u) :
                          (abs_dir.y >= abs_dir.z)                           ? (light_to_pixel.y > 0.0f ? 2u : 3u) :
                                                                               (light_to_pixel.z > 0.0f ? 4u : 5u);

        float4 clip_pos = mul(float4(position_world, 1.0f), light.transform[face_index]);
        float3 ndc      = clip_pos.xyz / clip_pos.w;
        float2 uv       = ndc_to_uv(ndc.xy);
        shadow          = vogel_depth(light, surface, float3(uv, (float)face_index), ndc.z);

        shadow = lerp(1.0f, shadow, fade);
    }
    // directional or spot light (cascaded shadow maps)
    else 
    {
        const uint near_cascade = 0;
        
        // sample near cascade
        float3 position_world = surface.position + compute_normal_offset(surface, light, near_cascade);
        float4 clip_pos_near  = mul(float4(position_world, 1.0f), light.transform[near_cascade]);
        float3 ndc_near       = clip_pos_near.xyz / clip_pos_near.w;
        float2 uv_near        = ndc_to_uv(ndc_near.xy);
        
        float2 ndc_abs_near = abs(ndc_near.xy);
        bool near_valid     = max(ndc_abs_near.x, ndc_abs_near.y) <= 1.0f;
        
        float shadow_near = 1.0f;
        if (near_valid)
        {
            shadow_near = vogel_depth(light, surface, float3(uv_near, near_cascade), ndc_near.z, 1.0f);
        }
        shadow = shadow_near;

        // directional cascade blending
        if (light.is_directional())
        {
            const uint far_cascade = 1;
            
            // sample far cascade with re-calculated offset
            float3 position_world_far = surface.position + compute_normal_offset(surface, light, far_cascade);
            float4 clip_pos_far       = mul(float4(position_world_far, 1.0f), light.transform[far_cascade]);
            float3 ndc_far            = clip_pos_far.xyz / clip_pos_far.w;
            float2 uv_far             = ndc_to_uv(ndc_far.xy);
            
            float2 ndc_abs_far = abs(ndc_far.xy);
            bool far_valid     = max(ndc_abs_far.x, ndc_abs_far.y) <= 1.0f;
            
            if (near_valid && far_valid)
            {
                float shadow_far = vogel_depth(light, surface, float3(uv_far, far_cascade), ndc_far.z, 1.0f);

                // smooth blend based on distance from center
                float2 ndc_dist_from_center = abs(ndc_near.xy);
                float max_dist              = max(ndc_dist_from_center.x, ndc_dist_from_center.y);
                float blend_start           = 0.8f;
                float blend_end             = 0.95f;
                float blend_factor          = smoothstep(blend_start, blend_end, max_dist);
                
                shadow = lerp(shadow_near, shadow_far, blend_factor);
            }
            else if (!near_valid && far_valid)
            {
                shadow = vogel_depth(light, surface, float3(uv_far, far_cascade), ndc_far.z, 1.0f);
            }
        }

        if (light.is_spot())
            shadow = lerp(1.0f, shadow, fade);
    }

    return shadow;
}
