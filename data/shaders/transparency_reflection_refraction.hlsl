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

//= includes =========
#include "common.hlsl"
//====================

// constants
static const float refraction_strength = 0.8f;
static const float ior_water           = 1.333f; // index of refraction for water
static const float ior_air             = 1.0f;   // air

// emulates the fact that deep water gets more blue because of absorption
float3 apply_water_absorption(float3 color, float depth)
{
    float3 absorption = float3(0.1f, 0.05f, 0.02f); // absorption coefficients for rgb (approximate, in 1/meters)
    return color * exp(-absorption * depth);
}

// compute refracted direction using snell's law
float3 compute_refracted_dir(float3 incident_dir, float3 normal)
{
    float ior_ratio   = ior_air / ior_water; // from air to water
    float cos_theta_i = -dot(incident_dir, normal);
    float sin_theta_i = sqrt(max(0.0f, 1.0f - cos_theta_i * cos_theta_i));
    float sin_theta_t = ior_ratio * sin_theta_i;
    
    // skip if total internal reflection
    if (sin_theta_t >= 1.0f)
        return float3(0.0f, 0.0f, 0.0f);
    
    float cos_theta_t = sqrt(max(0.0f, 1.0f - sin_theta_t * sin_theta_t));
    return normalize(ior_ratio * incident_dir + (ior_ratio * cos_theta_i - cos_theta_t) * normal);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get resolution
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    
    // create surface
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);
    
    // skip sky
    if (surface.is_sky())
        return;
    
    // background/transparency/refraction
    float3 background = tex2[thread_id.xy].rgb;
    float3 refraction = background;
    if (surface.is_water())
    {
        // compute common values
        float2 uv               = (thread_id.xy + 0.5f) / resolution_out;
        float depth_transparent = linearize_depth(surface.depth);
        float3 view_dir         = normalize(surface.camera_to_pixel);
        
        // refraction with snell's law
        float3 refracted_dir = compute_refracted_dir(view_dir, surface.normal);
        float inv_dist       = saturate(1.0f / (surface.camera_to_pixel_length + FLT_MIN));
        float2 uv_offset     = world_to_view(refracted_dir, false).xy * refraction_strength * inv_dist;

        // clamp UVs and compute edge fade factor
        float2 refracted_uv = clamp(uv + uv_offset, 0.0f, 1.0f);
        float3 refracted    = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).rgb;
        float depth_opaque  = linearize_depth(tex4.SampleLevel(samplers[sampler_bilinear_clamp], refracted_uv, 0.0f).r);
        refraction          = lerp(background, refracted, float(depth_opaque > depth_transparent) * screen_fade(refracted_uv));
        
        // absorption
        float water_depth = max(depth_opaque - depth_transparent, 0.0f);
        refraction        = apply_water_absorption(refraction, water_depth);
    }

    // add reflections and refraction
    float n_dot_v          = saturate(dot(surface.normal, normalize(surface.camera_to_pixel)));
    float3 reflection      = tex[thread_id.xy].rgb;
    float2 brdf            = tex3.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).rg;
    float3 surface_color   = reflection * (surface.F0 * brdf.x + brdf.y) + refraction;
    tex_uav[thread_id.xy] += float4(surface_color, 0.0f);
}
