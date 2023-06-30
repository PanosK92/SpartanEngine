/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "fog.hlsl"
//============================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    // Out of bounds check.
    if (any(int2(thread_id.xy) >= buffer_pass.resolution_rt.xy))
        return;

    // Create surface
    Surface surface;
    surface.Build(thread_id.xy, true, true, true);

    // Early exit cases
    bool early_exit_1 = is_opaque_pass() && surface.is_transparent() && !surface.is_sky(); // do shade sky pixels during the opaque pass (volumetric lighting)
    bool early_exit_2 = is_transparent_pass() && surface.is_opaque();
    if (early_exit_1 || early_exit_2)
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
        if (buffer_pass.is_transparent_pass)
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
        AngularInfo angular_info;
        angular_info.Build(light, surface);

        // Specular
        if (surface.anisotropic == 0.0f)
        {
            light_specular += BRDF_Specular_Isotropic(surface, angular_info);
        }
        else
        {
            light_specular += BRDF_Specular_Anisotropic(surface, angular_info);
        }

        // Specular clearcoat
        if (surface.clearcoat != 0.0f)
        {
            light_specular += BRDF_Specular_Clearcoat(surface, angular_info);
        }

        // Sheen
        if (surface.sheen != 0.0f)
        {
            light_specular += BRDF_Specular_Sheen(surface, angular_info);
        }
        
        // Diffuse
        light_diffuse += BRDF_Diffuse(surface, angular_info);

        // Tone down diffuse such as that only non metals have it
        light_diffuse *= surface.diffuse_energy;
    }

    float3 emissive = surface.emissive * surface.albedo;
    
     // Diffuse and specular
    tex_uav[thread_id.xy]  += float4(saturate_11(light_diffuse * light.radiance + surface.gi + emissive), 1.0f);
    tex_uav2[thread_id.xy] += float4(saturate_11(light_specular * light.radiance), 1.0f);

    // Volumetric
    if (light_is_volumetric() && is_volumetric_fog_enabled())
    {
        light_volumetric       += VolumetricLighting(surface, light);
        tex_uav3[thread_id.xy] += float4(saturate_11(light_volumetric), 1.0f);
    }
}
