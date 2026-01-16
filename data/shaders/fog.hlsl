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

// Atmospheric fog using exponential height-based falloff model
float get_fog_atmospheric(const float camera_to_pixel_length, const float pixel_height_world)
{
    float camera_height = buffer_frame.camera_position.y;
    float density       = pass_get_f3_value().y * 0.0001f;
    float scale_height  = 50.0f; // Lower = denser near ground, higher = more uniform
    float b             = 1.0f / scale_height;
    float delta_height  = pixel_height_world - camera_height;
    float dist          = camera_to_pixel_length;
    
    if (dist < 0.1f)
        return 0.0f;
    
    float rd_y = (abs(dist) > 1e-6f) ? delta_height / dist : 0.0f;
    float tau = 0.0f;
    
    if (abs(rd_y) < 1e-5f)
    {
        // Horizontal ray approximation
        float base_density = density * exp(-camera_height * b);
        tau = base_density * dist;
    }
    else
    {
        // Analytical integral for optical depth (numerically stable)
        float base_density = density * exp(-camera_height * b);
        float exponent     = -dist * rd_y * b;
        float exp_term     = 1.0f - exp(exponent);
        tau = base_density * exp_term / (b * rd_y);
    }

    // Beer's law: fog factor = in-scatter (1 - transmittance)
    float transmittance = exp(-tau);
    float fog_factor    = 1.0f - transmittance;
    fog_factor = pow(fog_factor, 0.8f); // Smooth falloff curve
    
    return saturate(fog_factor);
}

// Check if position is visible from light (not in shadow), returns 1.0 if lit, 0.0 if shadowed
float visible(float3 position, Light light, uint2 pixel_pos)
{
    bool is_visible = false;
    float dot_result = dot(light.forward, light.to_pixel);
    uint slice_index = light.is_point() * step(0.0f, -dot_result);

    if (light.is_point())
    {
        // Point light: use cube map shadow atlas
        float3 light_to_pixel = position - light.position;
        float3 abs_dir = abs(light_to_pixel);
        
        // Determine which cube map face to sample based on dominant axis
        uint face_index = (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) ? (light_to_pixel.x > 0.0f ? 0u : 1u) :
                          (abs_dir.y >= abs_dir.z)                           ? (light_to_pixel.y > 0.0f ? 2u : 3u) :
                                                                               (light_to_pixel.z > 0.0f ? 4u : 5u);
        
        // Transform to shadow space and compute shadow
        float4 clip_pos = mul(float4(position, 1.0f), light.transform[face_index]);
        float3 ndc = clip_pos.xyz / clip_pos.w;
        float2 projected_uv = ndc_to_uv(ndc.xy);
        float3 sample_coords = float3(projected_uv, (float)face_index);
        float shadow_depth = light.sample_depth(sample_coords);
        is_visible = ndc.z > shadow_depth;
    }
    else if (light.is_directional())
    {
        // Directional lights use cascaded shadow maps - check both near and far cascades
        const uint near_cascade = 0;
        const uint far_cascade = 1;
        
        // Check near cascade
        float3 projected_pos_near = world_to_ndc(position, light.transform[near_cascade]);
        float2 projected_uv_near = ndc_to_uv(projected_pos_near);
        float shadow_near = 1.0f; // Default to lit if out of bounds
        
        if (is_valid_uv(projected_uv_near))
        {
            float3 sample_coords_near = float3(projected_uv_near.x, projected_uv_near.y, near_cascade);
            float shadow_depth_near = light.sample_depth(sample_coords_near);
            shadow_near = (projected_pos_near.z > shadow_depth_near) ? 1.0f : 0.0f;
        }
        
        // Check far cascade
        float3 projected_pos_far = world_to_ndc(position, light.transform[far_cascade]);
        float2 projected_uv_far = ndc_to_uv(projected_pos_far);
        float shadow_far = 1.0f; // Default to lit if out of bounds
        
        if (is_valid_uv(projected_uv_far))
        {
            float3 sample_coords_far = float3(projected_uv_far.x, projected_uv_far.y, far_cascade);
            float shadow_depth_far = light.sample_depth(sample_coords_far);
            shadow_far = (projected_pos_far.z > shadow_depth_far) ? 1.0f : 0.0f;
        }
        
        // Blend between cascades based on distance from cascade edge (same as shadow_mapping.hlsl)
        float edge_dist = max(abs(projected_pos_near.x), abs(projected_pos_near.y));
        float blend_factor = smoothstep(0.7f, 1.0f, edge_dist);
        float shadow = lerp(shadow_near, shadow_far, blend_factor);
        
        is_visible = shadow > 0.5f;
    }
    else
    {
        // Spot light: use single cascade
        float3 projected_pos = world_to_ndc(position, light.transform[slice_index]);
        float2 projected_uv  = ndc_to_uv(projected_pos);
        if (is_valid_uv(projected_uv))
        {
            float3 sample_coords = float3(projected_uv.x, projected_uv.y, slice_index);
            float shadow_depth   = light.sample_depth(sample_coords);
            is_visible           = projected_pos.z > shadow_depth;
        }
        else
        {
            is_visible = true; // Out of bounds, assume lit
        }
    }

    return is_visible ? 1.0f : 0.0f;
}

// Henyey-Greenstein phase function for volumetric scattering (g: -1=backscatter, 0=isotropic, 1=forward scatter)
float henyey_greenstein_phase(float cos_theta, float g)
{
    cos_theta = clamp(cos_theta, -1.0f, 1.0f);
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cos_theta;
    float phase = (1.0f - g2) / (4.0f * PI * pow(max(denom, 0.0001f), 1.5f));
    // Heavily normalize phase function to prevent unrealistic bright halo around sun
    return min(phase * 0.001f, 0.01f);
}

// Compute volumetric fog using raymarching with phase function and shadow mapping
float3 compute_volumetric_fog(Surface surface, Light light, uint2 pixel_pos)
{
    const float  fog_density     = pass_get_f3_value().y * 0.03f;
    const float  total_distance  = surface.camera_to_pixel_length;
    const float3 ray_origin      = buffer_frame.camera_position;
    const float3 ray_direction   = normalize(surface.camera_to_pixel);
    
    if (total_distance < 0.1f)
        return 0.0f;
    
    // Adaptive step count: closer = more samples, distant = fewer (use smoothstep to avoid quantization bands)
    const float  max_distance    = 1000.0f;
    const uint   min_steps       = 16;
    const uint   max_steps       = 64;
    const float  distance_factor = saturate(total_distance / max_distance);
    const float  smooth_factor   = smoothstep(0.0f, 1.0f, distance_factor);
    const float  step_count_float = lerp((float)min_steps, (float)max_steps, smooth_factor);
    const uint   step_count      = (uint)step_count_float;
    const float  step_length     = total_distance / step_count_float;
    const float3 ray_step        = ray_direction * step_length;
    
    // Temporal dithering for ray start offset (reduces banding)
    float temporal_noise = noise_interleaved_gradient(pixel_pos, true);
    float noise_offset = (temporal_noise * 2.0f - 1.0f) * step_length * 0.5f;
    float3 ray_pos     = ray_origin + ray_direction * noise_offset;
    
    const float phase_g = 0.7f; // Phase function: forward scattering
    const bool is_directional = light.is_directional();
    float3 light_dir_directional = is_directional ? normalize(-light.forward) : 0.0f;
    float3 inscatter = 0.0f;
    float  transmittance = 1.0f;
    
    if (surface.is_sky())
    {
        // Sky: raymarch through entire volume for proper volumetric fog (don't use simplified approximation)
        const float min_transmittance = 0.01f;
        float step_noise_seed = frac(temporal_noise * 7.13f);
        
        for (uint i = 0; i < step_count; i++)
        {
            if (transmittance < min_transmittance)
                break;
            
            // Compute light direction per step
            float3 light_dir;
            if (is_directional)
            {
                light_dir = light_dir_directional;
            }
            else
            {
                float3 to_light = light.position - ray_pos;
                float dist_to_light = length(to_light);
                light_dir = (dist_to_light > 1e-6f) ? to_light / dist_to_light : float3(0.0f, 1.0f, 0.0f);
            }
            
            // Compute phase function per step
            float cos_theta = dot(ray_direction, light_dir);
            float phase = henyey_greenstein_phase(cos_theta, phase_g);
            
            // Sample density at current position
            float visibility = visible(ray_pos, light, pixel_pos);
            float attenuation = light.compute_attenuation_volumetric(ray_pos);
            float local_density = fog_density * visibility * attenuation;
            
            // Per-step dithering
            float step_jitter = (frac(step_noise_seed + (float)i * 0.618f) * 2.0f - 1.0f) * 0.1f;
            float jittered_step_length = step_length * (1.0f + step_jitter);
            
            // Transmittance through step (Beer's law)
            float step_tau = local_density * jittered_step_length;
            float step_transmittance = exp(-step_tau);
            
            // Accumulate in-scatter
            float3 step_inscatter = local_density * phase * transmittance * jittered_step_length;
            inscatter += step_inscatter;
            transmittance *= step_transmittance;
            ray_pos += ray_step;
        }
    }
    else
    {
        // Raymarch through volume
        const float min_transmittance = 0.01f; // Early exit
        float step_noise_seed = frac(temporal_noise * 7.13f);
        
        for (uint i = 0; i < step_count; i++)
        {
            if (transmittance < min_transmittance)
                break;
            
            // Compute light direction per step for point/spot lights (fixes incorrect approximation)
            float3 light_dir;
            if (is_directional)
            {
                light_dir = light_dir_directional;
            }
            else
            {
                // Compute actual light direction from current ray position
                float3 to_light = light.position - ray_pos;
                float dist_to_light = length(to_light);
                light_dir = (dist_to_light > 1e-6f) ? to_light / dist_to_light : float3(0.0f, 1.0f, 0.0f);
            }
            
            // Compute phase function per step (important for point/spot lights)
            float cos_theta = dot(ray_direction, light_dir);
            float phase = henyey_greenstein_phase(cos_theta, phase_g);
            
            // Sample density at current position
            float visibility = visible(ray_pos, light, pixel_pos);
            float attenuation = light.compute_attenuation_volumetric(ray_pos);
            float local_density = fog_density * visibility * attenuation;
            
            // Per-step dithering to reduce banding (subtle jitter on step length)
            float step_jitter = (frac(step_noise_seed + (float)i * 0.618f) * 2.0f - 1.0f) * 0.1f;
            float jittered_step_length = step_length * (1.0f + step_jitter);
            
            // Transmittance through step (Beer's law)
            float step_tau = local_density * jittered_step_length;
            float step_transmittance = exp(-step_tau);
            
            // Accumulate in-scatter: density * phase * transmittance * step_length
            float3 step_inscatter = local_density * phase * transmittance * jittered_step_length;
            inscatter += step_inscatter;
            transmittance *= step_transmittance;
            ray_pos += ray_step;
        }
    }
    
    // Multiply by light properties (inscatter is already in correct units: density * phase * transmittance * distance)
    float3 result = inscatter * light.intensity * light.color;
    return min(result, 100.0f); // Clamp to prevent extreme values that cause artifacts
}
