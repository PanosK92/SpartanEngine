/*
Copyright(c) 2016-2024 Panos Karabelas

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

// = INCLUDES ======
#include "brdf.hlsl"
//==================

float3 get_dominant_specular_direction(float3 normal, float3 reflection, float roughness)
{
    const float smoothness = 1.0f - roughness;
    const float alpha      = smoothness * (sqrt(smoothness) + roughness);
    
    return lerp(normal, reflection, alpha);
}

float3 sample_environment(float2 uv, float mip_level)
{
    return tex_environment.SampleLevel(samplers[sampler_trilinear_clamp], uv, mip_level).rgb;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // create surface
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // if this is an opaque pass, ignore all transparent pixels
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // if this is an transparent pass, ignore all opaque pixels
    bool early_exit_3 = surface.is_sky();                             // we don't want to do ibl on the sky itself
    if (early_exit_1 || early_exit_2 || early_exit_3)
        return;

    float3 ibl_diffuse  = 0.0f;
    float3 ibl_specular = 0.0f;
    
    // specular energy
    const float n_dot_v          = saturate(dot(-surface.camera_to_pixel, surface.normal));
    const float3 F               = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
    const float2 envBRDF         = tex_lut_ibl.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;

    // diffuse
    float mip_count_environment = pass_get_f3_value().x;
    float3 diffuse_energy       = compute_diffuse_energy(specular_energy, surface.metallic); // used to town down diffuse such as that only non metals have it
    ibl_diffuse                 = sample_environment(direction_sphere_uv(surface.normal), mip_count_environment);

    // specular
    {
        const float3 reflection            = reflect(surface.camera_to_pixel, surface.normal);
        float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, reflection, surface.roughness);
        float mip_level                    = lerp(0, mip_count_environment - 1, surface.roughness);
        ibl_specular                       = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level) * specular_energy;
        
        // apply shadow mas
        float shadow_mask  = tex[thread_id.xy].r;
        shadow_mask        = max(shadow_mask, 0.35f);
        ibl_specular      *= shadow_mask;
    }
    
    // ssr
    float3 ssr_color = 0.0f;
    float ssr_alpha  = 0.0f;
    {
        float4 ssr_sample           = tex_ssr.SampleLevel(samplers[sampler_trilinear_clamp], surface.uv, 0) * float(is_ssr_enabled());
        const float blend_threshold = 0.0001f;
        const float blend_speed     = 0.01f;
        ssr_alpha                   = saturate((ssr_sample.a - blend_threshold) / blend_speed);
        ssr_color                   = ssr_sample.rgb;
    }

    // global illumination
    float3 gi_specular = 0.0f;
    {
        ibl_diffuse += tex_light_diffuse_gi[thread_id.xy].rgb;
        gi_specular  = tex_light_specular_gi[thread_id.xy].rgb;
    }

    // if ssr has no value, fallback to the specular gi, and then add that to the ibl_specular
    ibl_specular += lerp(gi_specular,  ssr_color, ssr_alpha);
    
    // combine
    float3 ibl  = (ibl_diffuse * surface.albedo.rgb * diffuse_energy) + (ibl_specular * specular_energy);
    ibl        *= surface.occlusion;

    tex_uav[thread_id.xy] += float4(saturate_16(ibl), 0.0f);
}
