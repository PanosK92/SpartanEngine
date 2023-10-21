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

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "fog.hlsl"
//============================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    // create surface
    Surface surface;
    surface.Build(thread_id.xy, true, true, true);

    // early exit cases
    bool early_exit_1 = pass_is_opaque() && surface.is_transparent() && !surface.is_sky(); // do shade sky pixels during the opaque pass (volumetric lighting)
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque();
    if (early_exit_1 || early_exit_2)
        return;

    // create light
    Light light;
    light.Build(surface);

    // shadows
    float4 shadow = 1.0f;
    {
        // shadow mapping
        if (light_has_shadows())
        {
            shadow = Shadow_Map(surface, light);
        }
        
        // screen space shadows
        int array_slice_index = pass_get_f3_value2().x;
        if (light_has_shadows() && is_screen_space_shadows_enabled() && array_slice_index != -1)
        {
            shadow.a = min(shadow.a, tex_sss[int3(thread_id.xy, array_slice_index)].x);
        }

        // ensure that the shadow is as transparent as the material
        if (pass_is_transparent())
        {
            shadow.a = clamp(shadow.a, surface.alpha, 1.0f);
        }
    }

    // compute final radiance
    light.radiance *= shadow.rgb * shadow.a;
    
    float3 light_diffuse  = 0.0f;
    float3 light_specular = 0.0f;

    // reflectance equation
    if (!surface.is_sky())
    {
        AngularInfo angular_info;
        angular_info.Build(light, surface);

        // specular
        if (surface.anisotropic == 0.0f)
        {
            light_specular += BRDF_Specular_Isotropic(surface, angular_info);
        }
        else
        {
            light_specular += BRDF_Specular_Anisotropic(surface, angular_info);
        }

        // specular clearcoat
        if (surface.clearcoat != 0.0f)
        {
            light_specular += BRDF_Specular_Clearcoat(surface, angular_info);
        }

        // sheen
        if (surface.sheen != 0.0f)
        {
            light_specular += BRDF_Specular_Sheen(surface, angular_info);
        }
        
        // diffuse
        light_diffuse += BRDF_Diffuse(surface, angular_info);

        // tone down diffuse such as that only non metals have it
        light_diffuse *= surface.diffuse_energy;
    }

    float3 emissive = surface.emissive * surface.albedo;

     // diffuse and specular
    tex_uav[thread_id.xy]  += float4(saturate_11(light_diffuse * light.radiance + emissive), 1.0f);
    tex_uav2[thread_id.xy] += float4(saturate_11(light_specular * light.radiance), 1.0f);

    // volumetric
    //if (light_is_volumetric() && is_fog_volumetric_enabled())
    //{
    //    float3 light_fog = VolumetricLighting(surface, light);
    //    tex_uav3[thread_id.xy] += float4(saturate_11(light_fog), 1.0f);
    //}
}
