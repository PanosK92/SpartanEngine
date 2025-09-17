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

//= INCLUDES =================
#include "common.hlsl"
#include "brdf.hlsl"
#include "shadow_mapping.hlsl"
#include "fog.hlsl"
//============================

float3 subsurface_scattering(Surface surface, Light light, AngularInfo angular_info)
{
    const float distortion         = 0.3f;
    const float sss_exponent       = 4.0f;
    const float thickness_exponent = 2.0f;
    const float ambient            = 0.1f;
    const float sss_strength       = surface.subsurface_scattering * 0.5f;

    // compute key vectors
    float3 L = normalize(-light.to_pixel);          // to light
    float3 V = normalize(-surface.camera_to_pixel); // to camera
    float3 N = surface.normal;                      // surface normal
    
    // distorted half-vector for better translucency
    float3 H = normalize(L + N * distortion);
    float translucency = pow(saturate(dot(V, -H)), sss_exponent);
    
    // combined scattering term
    float sss_term = (translucency + ambient);
    
    // modulation: stronger near edges
    float dot_N_V    = saturate(dot(N, V));
    float modulation = pow(1.0f - dot_N_V, thickness_exponent);
    
    // light contribution
    float3 light_color = light.color * light.intensity * light.attenuation;
    
    // combine
    return light_color * sss_term * modulation * sss_strength * surface.albedo;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // build surface
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, true);

    // early exits
    bool early_exit_1 = pass_is_opaque()      && surface.is_transparent() && !surface.is_sky();
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque();
    if (early_exit_1 || early_exit_2)
        return;
    
    float3 out_diffuse    = 0.0f;
    float3 out_specular   = 0.0f;
    float  out_shadow     = 1.0f;
    float3 out_volumetric = 0.0f;

    // we pre-compute the part that we can
    float3 light_precomputed = surface.alpha * surface.occlusion;
    
    // loop lights and accumulate
    uint light_count = pass_get_f3_value().x;
    for (uint i = 0; i < light_count; i++)
    {
        Light light;
        light.Build(i, surface);

        float  L_shadow        = 1.0f;
        float3 L_specular_sum  = 0.0f;
        float3 L_diffuse_term  = 0.0f;
        float3 L_subsurface    = 0.0f;
        float3 L_volumetric    = 0.0f;

        if (!surface.is_sky())
        {
            // shadows
            if (light.has_shadows())
            {
                L_shadow = compute_shadow(surface, light);

                if (light.has_shadows_screen_space())
                {
                    L_shadow = min(L_shadow, tex_uav_sss[int3(thread_id.xy, light.screen_space_shadows_slice_index)].x);
                }

                light.radiance *= L_shadow;
            }

            // reflectance terms
            AngularInfo angular_info;
            angular_info.Build(light, surface);

            // specular lobes
            {
                if (surface.anisotropic > 0.0f)
                {
                    L_specular_sum += BRDF_Specular_Anisotropic(surface, angular_info);
                }
                else
                {
                    L_specular_sum += BRDF_Specular_Isotropic(surface, angular_info);
                }
                
                if (surface.clearcoat > 0.0f)
                {
                    L_specular_sum += BRDF_Specular_Clearcoat(surface, angular_info);
                }
                
                if (surface.sheen > 0.0f)
                {
                    L_specular_sum += BRDF_Specular_Sheen(surface, angular_info);
                }
                
                // subsurface
                if (surface.subsurface_scattering > 0.0f)
                {
                    L_subsurface += subsurface_scattering(surface, light, angular_info);
                }
            }
            
            // diffuse term before radiance
            L_diffuse_term += BRDF_Diffuse(surface, angular_info);
        }

        // volumetric
        if (light.is_volumetric())
        {
            L_volumetric += compute_volumetric_fog(surface, light, thread_id.xy);
        }
        
        // convert per light terms to what we actually store in the UAVs
        float3 write_diffuse    = L_diffuse_term * light.radiance * light_precomputed * surface.diffuse_energy + L_subsurface;
        float3 write_specular   = L_specular_sum * light.radiance * light_precomputed;
        float  write_shadow     = L_shadow;
        float3 write_volumetric = L_volumetric;

        // accumulate
        out_diffuse    += write_diffuse;
        out_specular   += write_specular;
        out_shadow     *= write_shadow;
        out_volumetric += write_volumetric;
    }

    tex_uav[thread_id.xy]  = float4(out_diffuse,    0.0f);
    tex_uav2[thread_id.xy] = float4(out_specular,   0.0f);
    tex_uav3[thread_id.xy] = out_shadow;
    tex_uav4[thread_id.xy] = float4(out_volumetric, 1.0f);
}
