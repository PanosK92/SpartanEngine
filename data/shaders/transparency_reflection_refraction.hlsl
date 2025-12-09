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
#include "brdf.hlsl"
//====================

// refraction and transparency constants
static const float refraction_strength  = 0.8f;   // strength of refraction distortion
static const float ior_water            = 1.333f; // index of refraction for water (air to water)
static const float ior_glass            = 1.5f;   // index of refraction for glass
static const float ior_air              = 1.0f;   // index of refraction for air
static const float chromatic_aberration = 0.02f;  // chromatic aberration strength
static const float absorption_scale     = 0.1f;   // water absorption scaling factor

// Compute Fresnel for dielectrics using Schlick approximation (F0 from IOR: ((n1-n2)/(n1+n2))^2)
float3 compute_dielectric_fresnel(float cos_theta, float ior_outer, float ior_inner)
{
    // compute F0 for dielectric (normal incidence)
    float f0_val = pow((ior_outer - ior_inner) / (ior_outer + ior_inner), 2.0f);
    float3 F0 = float3(f0_val, f0_val, f0_val);
    
    // schlick fresnel approximation
    return F_Schlick(F0, get_f90(), cos_theta);
}

// Apply water absorption using Beer-Lambert law (deeper water absorbs red/green, appears blue)
float3 apply_water_absorption(float3 color, float depth)
{
    // wavelength-dependent absorption coefficients (approximate, in 1/meters)
    // red and green are absorbed more than blue, creating the blue tint
    float3 absorption = float3(0.15f, 0.08f, 0.03f) * absorption_scale;
    
    // apply beer-lambert law for absorption
    float3 absorbed = color * exp(-absorption * depth);
    
    // add subtle scattering approximation (blue tint increases with depth)
    float scatter_factor = saturate(depth * 0.5f);
    float3 scatter_tint = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.7f, 0.85f, 1.0f), scatter_factor);
    
    return absorbed * scatter_tint;
}

// Compute refracted direction using Snell's law (returns zero if total internal reflection)
float3 compute_refracted_dir(float3 incident_dir, float3 normal, float ior_outer, float ior_inner)
{
    float ior_ratio   = ior_outer / ior_inner;
    float cos_theta_i = -dot(incident_dir, normal);
    float sin_theta_i = sqrt(max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    float sin_theta_t = ior_ratio * sin_theta_i;
    
    // check for total internal reflection (tir)
    // when sin_theta_t >= 1.0, all light is reflected, none refracted
    if (sin_theta_t >= 1.0f)
        return float3(0.0f, 0.0f, 0.0f);
    
    // compute refracted direction using snell's law formula
    float cos_theta_t = sqrt(max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    return normalize(ior_ratio * incident_dir + (ior_ratio * cos_theta_i - cos_theta_t) * normal);
}

// Compute refraction with chromatic aberration (different wavelengths refract at different angles)
void compute_refraction_with_chromatic_aberration(
    float2 uv, float3 refracted_dir, float camera_to_pixel_length,
    out float3 refracted_color, out float2 base_uv_offset, out float2 refracted_uv)
{
    // compute base refraction offset
    float inv_dist        = saturate(1.0f / (camera_to_pixel_length + FLT_MIN));
    float3 refracted_view = world_to_view(refracted_dir, false);
    base_uv_offset        = refracted_view.xy * refraction_strength * inv_dist;
    
    // Chromatic aberration: sample with different offsets for RGB channels
    float2 offset_r = base_uv_offset * (1.0f + chromatic_aberration);
    float2 offset_g = base_uv_offset;
    float2 offset_b = base_uv_offset * (1.0f - chromatic_aberration);
    
    // sample refracted color with chromatic aberration
    float2 uv_r = clamp(uv + offset_r, 0.0f, 1.0f);
    float2 uv_g = clamp(uv + offset_g, 0.0f, 1.0f);
    float2 uv_b = clamp(uv + offset_b, 0.0f, 1.0f);
    
    float3 refracted_r = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv_r, 0.0f).rgb;
    float3 refracted_g = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv_g, 0.0f).rgb;
    float3 refracted_b = tex2.SampleLevel(samplers[sampler_bilinear_clamp], uv_b, 0.0f).rgb;
    
    // combine RGB channels with chromatic separation
    refracted_color = float3(refracted_r.r, refracted_g.g, refracted_b.b);
    
    // Use base offset for depth checking
    refracted_uv = clamp(uv + base_uv_offset, 0.0f, 1.0f);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get output resolution and build surface data
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);
    
    // skip sky pixels (no transparency/refraction needed)
    if (surface.is_sky())
        return;
    
    // get background color (what's behind transparent surfaces)
    float3 background = tex2[thread_id.xy].rgb;
    float3 refraction = background;
    
    // determine material IOR (water vs glass vs other transparent materials)
    float ior_material = surface.is_water() ? ior_water : ior_glass;
    
    // compute view direction and angle
    float3 view_dir_normalized = normalize(surface.camera_to_pixel);
    float n_dot_v = saturate(dot(surface.normal, -view_dir_normalized));
    
    // Compute Fresnel for dielectrics: F = reflection amount, (1-F) = refraction amount
    float3 F = compute_dielectric_fresnel(n_dot_v, ior_air, ior_material);
    
    // compute refraction for transparent surfaces
    if (surface.is_water() || surface.is_transparent())
    {
        float2 uv = (thread_id.xy + 0.5f) / resolution_out;
        float depth_transparent = linearize_depth(surface.depth);
        
        // compute refracted direction using snell's law
        float3 refracted_dir = compute_refracted_dir(view_dir_normalized, surface.normal, ior_air, ior_material);
        
            // Check for total internal reflection (fallback to background)
            if (dot(refracted_dir, refracted_dir) < 0.001f)
            {
                // Total internal reflection: all light reflects, no refraction
            refraction = background;
            F = float3(1.0f, 1.0f, 1.0f); // force full reflection
        }
        else
        {
            // compute refraction with chromatic aberration
            float3 refracted;
            float2 base_uv_offset;
            float2 refracted_uv;
            compute_refraction_with_chromatic_aberration(
                uv, refracted_dir, surface.camera_to_pixel_length,
                refracted, base_uv_offset, refracted_uv
            );
            
            // check depth to ensure we're refracting through valid geometry
            float depth_opaque = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).r);
            
            // Only use refracted color if geometry exists behind surface, fade at edges to avoid artifacts
            float use_refraction = float(depth_opaque > depth_transparent) * screen_fade(refracted_uv);
            refraction = lerp(background, refracted, use_refraction);
            
            // Apply water absorption for water surfaces (Beer-Lambert law)
            if (surface.is_water())
            {
                float water_depth = max(depth_opaque - depth_transparent, 0.0f);
                refraction = apply_water_absorption(refraction, water_depth);
            }
        }
    }
    
    // Compute specular reflection using Fresnel and BRDF lookup (geometric normal for correctness)
    float3 reflection = tex[thread_id.xy].rgb;
    float2 brdf = tex3.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).rg;
    
    // Compute F0 for dielectrics (needed for BRDF lookup): F0 = ((n1-n2)/(n1+n2))^2
    float f0_dielectric = pow((ior_air - ior_material) / (ior_air + ior_material), 2.0f);
    float3 F0_dielectric = float3(f0_dielectric, f0_dielectric, f0_dielectric);
    
    // Compute specular reflection: brdf.x = fresnel-dependent, brdf.y = fresnel-independent
    // Formula: specular = reflection * (F0 * brdf.x + brdf.y), using dielectric F0
    float3 specular_reflection = reflection * (F0_dielectric * brdf.x + brdf.y);
    
    // Blend reflection and refraction using Fresnel: F = reflection amount, (1-F) = refraction amount
    // BRDF already includes Fresnel, so weight refraction by (1-F) for energy conservation
    float3 kT = float3(1.0f, 1.0f, 1.0f) - F; // transmission (refraction) coefficient
    
    // Combine reflection and refraction with energy conservation
    float3 surface_color = specular_reflection + refraction * kT;
    
    // accumulate result to output buffer
    tex_uav[thread_id.xy] += float4(surface_color, 0.0f);
}
