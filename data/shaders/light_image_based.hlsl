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

float compute_blend_factor(float alpha)
{
    const float blend_threshold = 0.001f;
    const float blend_speed     = 0.005f;
    
    return saturate((alpha - blend_threshold) / blend_speed);
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

    float mip_count_environment = pass_get_f3_value().x;

    // diffuse and specular energy
    float3 specular_energy    = 0.0f;
    float3 diffuse_energy     = 0.0f;
    {
        const float n_dot_v  = saturate(dot(-surface.camera_to_pixel, surface.normal));
        const float3 F       = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
        const float2 envBRDF = tex_lut_ibl.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
        specular_energy      = F * envBRDF.x + envBRDF.y;
        diffuse_energy       = compute_diffuse_energy(specular_energy, surface.metallic);
    }
    
    // sample all the textures
    const float3 reflection            = reflect(surface.camera_to_pixel, surface.normal);
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, reflection, surface.roughness);
    float mip_level                    = lerp(0, mip_count_environment - 1, surface.roughness);
    float3 specular_skysphere          = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level);
    float3 diffuse_skysphere           = sample_environment(direction_sphere_uv(surface.normal), mip_count_environment);
    float4 specular_ssr                = tex_ssr.SampleLevel(samplers[sampler_trilinear_clamp], surface.uv, 0);
    float3 diffuse_gi                  = tex_light_diffuse_gi[thread_id.xy].rgb;
    float3 specular_gi                 = tex_light_specular_gi[thread_id.xy].rgb;
    float shadow_mask                  = max(tex[thread_id.xy].r, 0.2f);

    // combine the diffuse light
    float3 diffuse_ibl = diffuse_skysphere * shadow_mask + diffuse_gi;

    // combine all the specular light, fallback order: ssr -> gi -> skysphere
    float3 specular_ssr_gi = lerp(specular_gi, specular_ssr.rgb, compute_blend_factor(specular_ssr.a));
    float3 specular_ibl    = lerp(specular_skysphere * shadow_mask, specular_ssr_gi, compute_blend_factor(luminance(specular_ssr_gi)));
    
    // combine the diffuse and specular light
    float3 ibl             = (diffuse_ibl * diffuse_energy * surface.albedo.rgb) + (specular_ibl * specular_energy);
    ibl                   *= surface.occlusion;

    tex_uav[thread_id.xy] += float4(saturate_16(ibl), 0.0f);
}

