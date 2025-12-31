/*
Copyright(c) 2015-2026 Panos Karabelas

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

// Subsurface scattering with wrapped diffuse and thickness estimation
float3 subsurface_scattering(Surface surface, Light light, AngularInfo angular_info)
{
    // material-dependent scattering parameters
    const float wrap_factor        = 0.5f;  // wrapped lighting factor (0 = no wrap, 1 = full wrap)
    const float sss_exponent       = 3.0f;  // translucency falloff sharpness
    const float thickness_exponent = 1.5f;  // edge thickness falloff
    const float sss_scale          = 0.8f;  // overall scattering strength multiplier
    const float min_scatter        = 0.05f; // minimum ambient scattering
    
    // compute key vectors (use geometric normal for sss)
    float3 L = normalize(-light.to_pixel);
    float3 V = normalize(-surface.camera_to_pixel);
    float3 N = surface.normal;
    
    // Wrapped diffuse: allows light to wrap around surface, simulating subsurface penetration
    float n_dot_l_wrapped = saturate((dot(N, L) + wrap_factor) / (1.0f + wrap_factor));
    float wrapped_diffuse = n_dot_l_wrapped * n_dot_l_wrapped; // square for smoother falloff
    
    // Back-scattering translucency: light passing through from behind using distorted normal
    const float distortion = 0.4f;
    float3 N_distorted     = normalize(N + L * distortion);
    float back_scatter     = saturate(dot(V, -N_distorted));
    back_scatter           = pow(back_scatter, sss_exponent);
    
    // Combine forward (wrapped diffuse) and backward (translucency) scattering
    float sss_term = lerp(back_scatter, wrapped_diffuse, saturate(dot(N, L) * 0.5f + 0.5f));
    sss_term = max(sss_term, min_scatter); // ensure minimum scattering
    
    // Thickness modulation: stronger scattering at thin edges (view-dependent)
    float n_dot_v = saturate(dot(N, V));
    float view_thickness = pow(1.0f - n_dot_v, thickness_exponent);
    
    // Light-dependent: backlit areas show more scattering
    float n_dot_l = saturate(dot(N, L));
    float light_thickness = pow(1.0f - n_dot_l, 1.0f);
    
    // combine thickness terms
    float thickness_modulation = saturate(view_thickness + light_thickness * 0.5f);
    
    // compute light contribution with proper radiance
    float3 light_radiance = light.radiance;
    
    // apply material strength and scale
    float sss_strength = surface.subsurface_scattering * sss_scale;
    
    // Color tinting: preserve material color for subsurface scattering
    float3 sss_color = surface.albedo;
    
    // combine all terms
    return light_radiance * sss_term * thickness_modulation * sss_strength * sss_color;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get resolution and build surface data
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, true);

    // early exit for mismatched pass/surface types
    bool early_exit_1 = pass_is_opaque() && surface.is_transparent() && !surface.is_sky();
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque();
    if (early_exit_1 || early_exit_2)
        return;

    // initialize output accumulators
    float3 out_diffuse    = 0.0f;
    float3 out_specular   = 0.0f;
    float3 out_volumetric = 0.0f;

    // pre-compute common terms (alpha and occlusion)
    float3 light_precomputed = surface.alpha * surface.occlusion;
    
    // loop over all lights and accumulate contributions
    uint light_count = pass_get_f3_value().x;
    for (uint i = 0; i < light_count; i++)
    {
        Light light;
        light.Build(i, surface);

        // per-light accumulators
        float  L_shadow        = 1.0f;
        float3 L_specular_sum  = 0.0f;
        float3 L_diffuse_term  = 0.0f;
        float3 L_subsurface    = 0.0f;
        float3 L_volumetric    = 0.0f;

        if (!surface.is_sky())
        {
            // compute shadow term
            if (light.has_shadows())
            {
                L_shadow = compute_shadow(surface, light);

                // combine with screen-space shadows if available
                if (light.has_shadows_screen_space() && surface.is_opaque())
                {
                    L_shadow = min(L_shadow, tex_uav_sss[int3(thread_id.xy, light.screen_space_shadows_slice_index)].x);
                }

                // apply shadow to light radiance
                light.radiance *= L_shadow;
            }

            // build angular information for brdf calculations
            AngularInfo angular_info;
            angular_info.Build(light, surface);

            // compute specular brdf lobes
            {
                // main specular lobe (anisotropic or isotropic)
                if (surface.anisotropic > 0.0f)
                {
                    L_specular_sum += BRDF_Specular_Anisotropic(surface, angular_info);
                }
                else
                {
                    L_specular_sum += BRDF_Specular_Isotropic(surface, angular_info);
                }
                
                // clearcoat layer (secondary specular)
                if (surface.clearcoat > 0.0f)
                {
                    L_specular_sum += BRDF_Specular_Clearcoat(surface, angular_info);
                }
                
                // sheen layer (cloth-like materials)
                if (surface.sheen > 0.0f)
                {
                    L_specular_sum += BRDF_Specular_Sheen(surface, angular_info);
                }
                
                // subsurface scattering (translucent materials)
                if (surface.subsurface_scattering > 0.0f)
                {
                    L_subsurface += subsurface_scattering(surface, light, angular_info);
                }
            }
            
            // compute diffuse brdf term
            L_diffuse_term += BRDF_Diffuse(surface, angular_info);
        }

        // compute volumetric fog contribution
        if (light.is_volumetric())
        {
            L_volumetric += compute_volumetric_fog(surface, light, thread_id.xy);
        }
        
        // combine per-light terms with radiance and precomputed factors
        float3 write_diffuse    = L_diffuse_term * light.radiance * light_precomputed * surface.diffuse_energy + L_subsurface;
        float3 write_specular   = L_specular_sum * light.radiance * light_precomputed;
        float  write_shadow     = L_shadow;
        float3 write_volumetric = L_volumetric;

        // accumulate into output buffers
        out_diffuse    += write_diffuse;
        out_specular   += write_specular;
        out_volumetric += write_volumetric;
    }

    // write results to output buffers
    tex_uav[thread_id.xy]  = validate_output(float4(out_diffuse,    1.0f));
    tex_uav2[thread_id.xy] = validate_output(float4(out_specular,   1.0f));
    tex_uav3[thread_id.xy] = validate_output(float4(out_volumetric, 1.0f));
}
