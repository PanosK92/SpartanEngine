/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ================
#include "Common.hlsl"
#include "BRDF.hlsl"
#include "ShadowMapping.hlsl"
#include "VolumetricFog.hlsl"
#include "Fog.hlsl"
//===========================

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // Create surface
    Surface surface;
    bool use_albedo = false; // we write out pure light color (just a choice)
    surface.Build(thread_id.xy, use_albedo);

    // If this is a transparent pass, ignore all opaque pixels, and vice versa.
    if ((g_is_transparent_pass && surface.is_opaque()) || (!g_is_transparent_pass && surface.is_transparent()))
        return;

    // Create light
    Light light;
    light.Build(surface);

    // Shadows
    float4 shadow = 1.0f;
    {
        // Shadow mapping
        #if SHADOWS
        {
            shadow = Shadow_Map(surface, light);
        }
        #endif
        
        // Screen space shadows
        #if SHADOWS_SCREEN_SPACE
        {
            shadow.a = min(shadow.a, ScreenSpaceShadows(surface, light));
        }
        #endif

        // Ensure that the shadow is as transparent as the material
        if (g_is_transparent_pass)
        {
            shadow.a = clamp(shadow.a, surface.alpha, 1.0f);
        }
    }

    // Compute final radiance
    light.radiance *= shadow.rgb * shadow.a;
    
    float3 light_diffuse    = 0.0f;
    float3 light_specular   = 0.0f;
    float3 light_volumetric = 0.0f;

    // Reflectance equation
    [branch]
    if (any(light.radiance) && !surface.is_sky())
    {
        // Compute some vectors and dot products
        float3 l        = -light.direction;
        float3 v        = -surface.camera_to_pixel;
        float3 h        = normalize(v + l);
        float l_dot_h   = saturate(dot(l, h));
        float v_dot_h   = saturate(dot(v, h));
        float n_dot_v   = saturate(dot(surface.normal, v));
        float n_dot_h   = saturate(dot(surface.normal, h));

        float3 diffuse_energy       = 1.0f;
        float3 reflective_energy    = 1.0f;
        
        // Specular
        if (surface.anisotropic == 0.0f)
        {
            light_specular += BRDF_Specular_Isotropic(surface, n_dot_v, light.n_dot_l, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }
        else
        {
            light_specular += BRDF_Specular_Anisotropic(surface, v, l, h, n_dot_v, light.n_dot_l, n_dot_h, l_dot_h, diffuse_energy, reflective_energy);
        }

        // Specular clearcoat
        if (surface.clearcoat != 0.0f)
        {
            light_specular += BRDF_Specular_Clearcoat(surface, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }

        // Sheen;
        if (surface.sheen != 0.0f)
        {
            light_specular += BRDF_Specular_Sheen(surface, n_dot_v, light.n_dot_l, n_dot_h, diffuse_energy, reflective_energy);
        }
        
        // Diffuse
        light_diffuse += BRDF_Diffuse(surface, n_dot_v, light.n_dot_l, v_dot_h);

        /* Light - Subsurface scattering fast approximation - Will activate soon
        if (g_is_transprent_pass)
        {
            const float thickness_edge  = 0.1f;
            const float thickness_face  = 1.0f;
            const float distortion      = 0.65f;
            const float ambient         = 1.0f - surface.albedo.a;
            const float scale           = 1.0f;
            const float power           = 0.8f;
            
            float thickness = lerp(thickness_edge, thickness_face, n_dot_v);
            float3 h        = normalize(l + surface.normal * distortion);
            float v_dot_h   = pow(saturate(dot(v, -h)), power) * scale;
            float intensity = (v_dot_h + ambient) * thickness;
            
            light_diffuse += surface.albedo.rgb * light.color * intensity;
        }
        */

        // Tone down diffuse such as that only non metals have it
        light_diffuse *= diffuse_energy;
    }

    // Volumetric lighting
    #if VOLUMETRIC
    {
        light_volumetric += VolumetricLighting(surface, light) * light.color * light.intensity * get_fog_factor(surface);
    }
    #endif
    
    tex_out_rgb[thread_id.xy]   += saturate_16(light_diffuse * light.radiance + surface.emissive);
    tex_out_rgb2[thread_id.xy]  += saturate_16(light_specular * light.radiance);
    tex_out_rgb3[thread_id.xy]  += saturate_16(light_volumetric);
}
