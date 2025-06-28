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
static const float refraction_strength = 0.02f;
static const float default_ior         = 1.333f;

float3 fresnel_schlick_roughness(float cos_theta, float3 F0, float roughness)
{
    // schlick's approximation: F = F0 + (1 - F0) * (1 - cos_theta)^5
    float3 F = F0 + (1.0f - F0) * pow(1.0f - cos_theta, 5.0f);
    
    // adjust for roughness: interpolate between F0 and max(1 - roughness, F0)
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0) * pow(1.0f - cos_theta, 5.0f);
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
    
    // compute view dir and fresnel
    float3 view_dir = -surface.camera_to_pixel;
    float n_dot_v   = saturate(dot(surface.normal, view_dir));
    float3 fresnel  = fresnel_schlick_roughness(n_dot_v, surface.F0, surface.roughness);
    
    // get background color (refraction), always needed for transparency
    float2 uv         = (thread_id.xy + 0.5f) / resolution_out;
    float3 background = tex2[thread_id.xy].rgb;
    
    if (surface.is_transparent())
    {
        float ior          = surface.ior > 0.0f ? surface.ior : default_ior;
        float3 refract_dir = refract(-view_dir, surface.normal, 1.0f / ior);
        float2 uv_offset   = refract_dir.xy * refraction_strength * (1.0f - surface.roughness);
        float2 refract_uv  = clamp(uv + uv_offset, 0.0f, 1.0f);
        background         = tex2.SampleLevel(samplers[sampler_bilinear_clamp], refract_uv, 0.0f).rgb;
    }
    
    // get reflection (SSR)
    float3 reflection = tex[thread_id.xy].rgb;
    
    // fresnel mix between background (refraction) and reflection
    float3 surface_color = lerp(background, reflection, fresnel);
    
    // blend surface color over background using surface.alpha
    // but remember: background is already 'behind' the surface
    float3 final_color = lerp(background, surface_color, surface.alpha);
    
    // write to output
    tex_uav[thread_id.xy] += float4(final_color, surface.alpha);
}
