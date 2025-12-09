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

//= INCLUDES =========
#include "common.hlsl"
//====================

/*------------------------------------------------------------------------------
    FOG - ATMOSPHERIC
    Exponential atmosphere model with height-based falloff
------------------------------------------------------------------------------*/
float get_fog_atmospheric(const float camera_to_pixel_length, const float pixel_height_world)
{
    float camera_height = buffer_frame.camera_position.y;
    float density       = pass_get_f3_value().y * 0.0001f;
    
    // scale height: lower = denser near ground, higher = more uniform
    float scale_height = 50.0f;
    float b            = 1.0f / scale_height;
    float delta_height = pixel_height_world - camera_height;
    float dist         = camera_to_pixel_length;
    
    // early exit for very close objects
    if (dist < 0.1f)
        return 0.0f;
    
    // compute ray direction component in height direction
    float rd_y = (abs(dist) > 1e-6f) ? delta_height / dist : 0.0f;

    float tau = 0.0f;
    if (abs(rd_y) < 1e-5f)
    {
        // horizontal ray approximation
        float base_density = density * exp(-camera_height * b);
        tau = base_density * dist;
    }
    else
    {
        // analytical integral for optical depth (numerically stable)
        float base_density = density * exp(-camera_height * b);
        float exponent     = -dist * rd_y * b;
        float exp_term     = 1.0f - exp(exponent);
        tau = base_density * exp_term / (b * rd_y);
    }

    // Beer's law: fog factor = in-scatter (1 - transmittance)
    float transmittance = exp(-tau);
    float fog_factor    = 1.0f - transmittance;
    
    // smooth falloff curve
    fog_factor = pow(fog_factor, 0.8f);
    
    return saturate(fog_factor);
}

/*------------------------------------------------------------------------------
    FOG - VOLUMETRIC - VISIBILITY
    Check if position is visible from light (not in shadow)
------------------------------------------------------------------------------*/
float visible(float3 position, Light light, uint2 pixel_pos)
{
    // directional light is everywhere, assume visible
    bool is_visible = light.is_directional();
    float2 projected_uv  = 0.0f;
    float3 projected_pos = 0.0f;

    // early exit for directional lights (no shadow map lookup needed)
    if (is_visible)
        return 1.0f;

    // slice index for point lights (cube map face)
    float dot_result = dot(light.forward, light.to_pixel);
    uint slice_index = light.is_point() * step(0.0f, -dot_result);

    // projection and shadow map comparison
    if (light.is_point())
    {
        // project
        projected_pos = mul(float4(position, 1.0f), light.transform[slice_index]).xyz;
        float3 ndc    = project_onto_paraboloid(projected_pos, light.near, light.far);
        projected_uv  = ndc_to_uv(ndc.xy);

        // compare
        float3 sample_coords = float3(projected_uv, slice_index);
        float shadow_depth   = light.sample_depth(sample_coords);
        is_visible           = ndc.z > shadow_depth;
    }
    else
    {
        // project
        projected_pos = world_to_ndc(position, light.transform[slice_index]);
        projected_uv  = ndc_to_uv(projected_pos);

        // compare
        if (is_valid_uv(projected_uv))
        {
            float3 sample_coords = float3(projected_uv.x, projected_uv.y, slice_index);
            float shadow_depth   = light.sample_depth(sample_coords);
            is_visible           = projected_pos.z > shadow_depth;
        }
        else
        {
            is_visible = false; // outside shadow map bounds
        }
    }

    return is_visible ? 1.0f : 0.0f;
}

/*------------------------------------------------------------------------------
    FOG - VOLUMETRIC
    Transmittance, adaptive sampling, phase function
------------------------------------------------------------------------------*/
float henyey_greenstein_phase(float cos_theta, float g)
{
    // g: -1=backscatter, 0=isotropic, 1=forward scatter
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cos_theta;
    return (1.0f - g2) / (4.0f * PI * pow(max(denom, 0.0001f), 1.5f));
}

float3 compute_volumetric_fog(Surface surface, Light light, uint2 pixel_pos)
{
    // parameters
    const float  fog_density     = pass_get_f3_value().y * 0.03f;
    const float  total_distance  = surface.camera_to_pixel_length;
    const float3 ray_origin      = buffer_frame.camera_position;
    const float3 ray_direction   = normalize(surface.camera_to_pixel);
    
    // early exit for very close objects
    if (total_distance < 0.1f)
        return 0.0f;
    
    // adaptive step count: closer = more samples, distant = fewer
    const float  max_distance    = 1000.0f;
    const uint   min_steps       = 16;
    const uint   max_steps       = 64;
    const float  distance_factor = saturate(total_distance / max_distance);
    const uint   step_count      = (uint)lerp((float)min_steps, (float)max_steps, distance_factor);
    const float  step_length     = total_distance / (float)step_count;
    const float3 ray_step        = ray_direction * step_length;
    
    // noise offset for dithering
    float noise_offset = noise_interleaved_gradient(pixel_pos) * step_length * 0.5f;
    float3 ray_pos     = ray_origin + ray_direction * noise_offset;
    
    // phase function: forward scattering
    const float phase_g = 0.7f;
    
    // light direction: directional=constant, point/spot=midpoint approximation
    float3 ray_midpoint = ray_origin + ray_direction * (total_distance * 0.5f);
    float3 light_dir = light.is_directional() ? normalize(-light.forward) : normalize(light.position - ray_midpoint);
    float cos_theta  = dot(ray_direction, light_dir);
    float phase      = henyey_greenstein_phase(cos_theta, phase_g);
    
    // accumulators
    float3 inscatter = 0.0f;
    float  transmittance = 1.0f;
    
    if (surface.is_sky())
    {
        // sky: simplified fog (no raymarching)
        float sky_fog = fog_density * phase;
        return sky_fog * light.intensity * light.color;
    }
    else
    {
        // raymarch through volume
        const float min_transmittance = 0.01f; // early exit
        
        for (uint i = 0; i < step_count; i++)
        {
            // early exit if transmittance too low
            if (transmittance < min_transmittance)
                break;
            
            // sample density at current position
            float visibility = visible(ray_pos, light, pixel_pos);
            float attenuation = light.compute_attenuation_volumetric(ray_pos);
            
            // compute local density contribution
            float local_density = fog_density * visibility * attenuation;
            
            // transmittance through step (Beer's law)
            float step_tau = local_density * step_length;
            float step_transmittance = exp(-step_tau);
            
            // accumulate in-scatter: density * phase * transmittance * step_length
            float3 step_inscatter = local_density * phase * transmittance * step_length;
            inscatter += step_inscatter;
            
            // update transmittance
            transmittance *= step_transmittance;
            
            // advance ray
            ray_pos += ray_step;
        }
    }
    
    // multiply by light properties
    return inscatter * light.intensity * light.color;
}
