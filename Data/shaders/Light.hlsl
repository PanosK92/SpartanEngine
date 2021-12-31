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

#define FOG_REGULAR 1
#define FOG_VOLUMETRIC 1

//= INCLUDES =========
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "fog.hlsl"
//============================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Create surface
    Surface surface;
    surface.Build(thread_id.xy, true, true, true);

    // Early exit cases
    bool early_exit_out_of_bounds = any(int2(thread_id.xy) >= g_resolution_rt.xy);
    bool early_exit_1             = !g_is_transparent_pass && surface.is_transparent() && !surface.is_sky(); // do shade sky pixels during the opaque pass (volumetric lighting)
    bool early_exit_2             = g_is_transparent_pass && surface.is_opaque();
    if (early_exit_out_of_bounds || early_exit_1 || early_exit_2)
        return;

    // Create light
    Light light;
    light.Build(surface);

    // Shadows
    float4 shadow = 1.0f;
    {
        // Shadow mapping
        if (light_has_shadows())
        {
            shadow = Shadow_Map(surface, light);
        }
        
        // Screen space shadows
        if (is_screen_space_shadows_enabled() && light_has_shadows_screen_space())
        {
            shadow.a = min(shadow.a, ScreenSpaceShadows(surface, light));
        }

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
    if (!surface.is_sky())
    {
        // Compute some vectors and dot products
        float3 l      = -light.to_pixel;
        float3 v      = -surface.camera_to_pixel;
        float3 h      = normalize(v + l);
        float l_dot_h = saturate(dot(l, h));
        float v_dot_h = saturate(dot(v, h));
        float n_dot_v = saturate(dot(surface.normal, v));
        float n_dot_h = saturate(dot(surface.normal, h));

        float3 diffuse_energy    = 1.0f;
        float3 reflective_energy = 1.0f;
        
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

        // Subsurface scattering from LIDL
        {
            //const float thickness_edge = 0.1f;
            //const float thickness_face = 1.0f;
            //const float distortion     = 0.65f;
            //const float ambient        = 1.0f - surface.alpha;
            //const float scale          = 1.0f;
            //const float power          = 0.8f;
            
            //float thickness = lerp(thickness_edge, thickness_face, n_dot_v);
            //float3 h        = normalize(l + surface.normal * distortion);
            //float v_dot_h   = pow(saturate(dot(v, -h)), power) * scale;
            //float intensity = (v_dot_h + ambient) * thickness;
            
            //light_diffuse += surface.albedo.rgb * light.color * intensity;
        }

        // Tone down diffuse such as that only non metals have it
        light_diffuse *= diffuse_energy;
    }

    // Diffuse and specular
    tex_out_rgb[thread_id.xy]  += saturate_16(light_diffuse * light.radiance + surface.gi);
    tex_out_rgb2[thread_id.xy] += saturate_16(light_specular * light.radiance);

    // Volumetric
    if (light_is_volumetric() && is_volumetric_fog_enabled())
    {
        light_volumetric           += VolumetricLighting(surface, light);
        tex_out_rgb3[thread_id.xy] += saturate_16(light_volumetric);
    }
}
