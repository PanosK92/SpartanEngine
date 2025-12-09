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

// refraction and transparency constants
static const float refraction_strength = 0.8f;  // strength of refraction distortion
static const float ior_water           = 1.333f; // index of refraction for water (air to water)
static const float ior_air             = 1.0f;   // index of refraction for air

// apply water absorption based on depth (beer-lambert law)
// deeper water absorbs more red/green light, making it appear more blue
float3 apply_water_absorption(float3 color, float depth)
{
    // absorption coefficients for rgb channels (approximate, in 1/meters)
    // red and green are absorbed more than blue, creating the blue tint
    float3 absorption = float3(0.1f, 0.05f, 0.02f);
    return color * exp(-absorption * depth);
}

// compute refracted direction using snell's law
// returns zero vector if total internal reflection occurs
float3 compute_refracted_dir(float3 incident_dir, float3 normal)
{
    float ior_ratio   = ior_air / ior_water; // from air to water
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
    
    // compute refraction for water surfaces
    if (surface.is_water())
    {
        float2 uv               = (thread_id.xy + 0.5f) / resolution_out;
        float depth_transparent = linearize_depth(surface.depth);
        float3 view_dir         = normalize(surface.camera_to_pixel);
        
        // compute refracted direction using snell's law
        float3 refracted_dir = compute_refracted_dir(view_dir, surface.normal);
        
        // check for total internal reflection (fallback to background)
        if (dot(refracted_dir, refracted_dir) < 0.001f)
        {
            refraction = background;
        }
        else
        {
            // project refracted direction to screen space for uv offset
            // distance-based scaling makes refraction stronger for closer objects
            float inv_dist       = saturate(1.0f / (surface.camera_to_pixel_length + FLT_MIN));
            float3 refracted_view = world_to_view(refracted_dir, false);
            float2 uv_offset     = refracted_view.xy * refraction_strength * inv_dist;

            // compute refracted uv coordinates with bounds clamping
            float2 refracted_uv = clamp(uv + uv_offset, 0.0f, 1.0f);
            float3 refracted    = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).rgb;
            float depth_opaque  = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).r);
            
            // only use refracted color if there's geometry behind the water surface
            // fade at screen edges to avoid artifacts
            float use_refraction = float(depth_opaque > depth_transparent) * screen_fade(refracted_uv);
            refraction          = lerp(background, refracted, use_refraction);
            
            // apply water absorption based on depth (beer-lambert law)
            float water_depth = max(depth_opaque - depth_transparent, 0.0f);
            refraction        = apply_water_absorption(refraction, water_depth);
        }
    }

    // compute specular reflection using fresnel and brdf lookup
    // use geometric normal for physically correct reflection
    float3 view_dir_normalized = normalize(surface.camera_to_pixel);
    float n_dot_v              = saturate(dot(surface.normal, -view_dir_normalized));
    float3 reflection          = tex[thread_id.xy].rgb;
    float2 brdf                = tex3.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).rg;
    
    // combine reflection and refraction using fresnel-weighted brdf
    // brdf.x = fresnel-dependent term, brdf.y = fresnel-independent term
    float3 specular_reflection = reflection * (surface.F0 * brdf.x + brdf.y);
    float3 surface_color       = specular_reflection + refraction;
    
    // accumulate result to output buffer
    tex_uav[thread_id.xy] += float4(surface_color, 0.0f);
}
