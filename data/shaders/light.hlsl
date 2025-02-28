/*
Copyright(c) 2016-2025 Panos Karabelas

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

float3 subsurface_scattering(Surface surface, Light light, AngularInfo angular_info)
{
    const float surface_thickness = 0.01f;

    // compute backface lighting
    float n_dot_l_backface = saturate(dot(surface.normal, light.to_pixel));
    float3 light_radiance  = light.color * light.intensity * light.attenuation * surface.occlusion * n_dot_l_backface;
    
    // determine sss
    float sss_strength = surface.subsurface_scattering * exp(-surface_thickness) * 7.0f;
    float3 sss_color   = surface.albedo * light_radiance * sss_strength;

    // fresnel effect using schlick's approximation to modulate final color
    float3 F              = F_Schlick(surface.F0, get_f90(surface), angular_info.n_dot_v);
    float3 diffuse_energy = compute_diffuse_energy(F, surface.metallic);
    
    // combine SSS color with fresnel effect
    return sss_color * F * diffuse_energy;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // create surface
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, true);

    // early exit cases
    bool early_exit_1 = pass_is_opaque()      && surface.is_transparent() && !surface.is_sky(); // shade sky pixels during the opaque pass (volumetric lighting)
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque();
    if (early_exit_1 || early_exit_2)
        return;

    // create light
    Light light;
    light.Build(surface);

    float4 shadow           = 1.0f;
    float3 light_diffuse    = 0.0f;
    float3 light_specular   = 0.0f;
    float3 volumetric_fog   = 0.0f;
    float3 light_subsurface = 0.0f;

    if (!surface.is_sky() && light.intensity > 0.0f)
    {
        // shadows
        {
            if (light.has_shadows())
            {
                // shadow maps
                shadow = Shadow_Map(surface, light);
            
                // screen space shadows - for opaque objects
                uint array_slice_index = light.get_array_index();
                if (light.has_shadows_screen_space() && pass_is_opaque() && array_slice_index != -1)
                {
                    shadow.a = min(shadow.a, tex_uav_sss[int3(thread_id.xy, array_slice_index)].x);
                }
            }

            // ensure that the shadow is as transparent as the material
            if (pass_is_transparent())
            {
                shadow.a = clamp(shadow.a, surface.alpha, 1.0f);
            }
        }

        // compute final radiance
        light.radiance *= shadow.rgb * shadow.a;

        // reflectance equation(s)
        {
            AngularInfo angular_info;
            angular_info.Build(light, surface);

            // specular
            if (surface.anisotropic > 0.0f)
            {
                light_specular += BRDF_Specular_Anisotropic(surface, angular_info);
            }
            else
            {
                light_specular += BRDF_Specular_Isotropic(surface, angular_info);
            }

            // specular clearcoat
            if (surface.clearcoat > 0.0f)
            {
                light_specular += BRDF_Specular_Clearcoat(surface, angular_info);
            }

            // sheen
            if (surface.sheen > 0.0f)
            {
                light_specular += BRDF_Specular_Sheen(surface, angular_info);
            }

            // subsurface scattering
            if (surface.subsurface_scattering > 0.0f)
            {
                light_subsurface += subsurface_scattering(surface, light, angular_info);
            }
        
            // diffuse
            light_diffuse += BRDF_Diffuse(surface, angular_info);

            // energy conservation - only non metals have diffuse
            light_diffuse *= surface.diffuse_energy * surface.alpha;
        }
    }
    
    // volumetric
    if (light.is_volumetric())
    {
        float max_mip    = pass_get_f3_value().z;
        float3 sky_color = tex_environment.SampleLevel(samplers[sampler_trilinear_clamp], float2(0.5, 0.5f), max_mip).rgb;
        volumetric_fog   = compute_volumetric_fog(surface, light, thread_id.xy) * sky_color;
    }

    // to avoid clearing the buffers with API calls (and introducing memory barriers), we clear them via not accumulating on the first light
    float accumulate       = light.index != 0;
    float shadow_value     = lerp(tex_uav3[thread_id.xy].r, 1.0f, 1.0f - accumulate);
    tex_uav[thread_id.xy]  = tex_uav[thread_id.xy]  * accumulate + float4(light_diffuse  * light.radiance + light_subsurface, 0.0f) * surface.alpha; /* diffuse    - clears to zero*/
    tex_uav2[thread_id.xy] = tex_uav2[thread_id.xy] * accumulate + float4(light_specular * light.radiance, 0.0f) * surface.alpha;                    /* specular   - clears to zero*/
    tex_uav3[thread_id.xy] = saturate(shadow_value - (1.0f - shadow.a));                                                                             /* shadow     - clears to one*/
    tex_uav4[thread_id.xy] = tex_uav4[thread_id.xy] * accumulate + float4(volumetric_fog, 1.0f);                                                     /* volumetric - clears to zero*/
}
