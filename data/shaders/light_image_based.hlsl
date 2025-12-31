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

// = INCLUDES ======
#include "brdf.hlsl"
//==================

// approx of multi-bounce (inter-reflection) for ao
float3 gtao_multi_bounce(float visibility, float3 albedo)
{
    // Jimenez et al. 2016 approximation
    float3 a = 2.0404f * albedo - 0.3324f;
    float3 b = -4.7951f * albedo + 0.6417f;
    float3 c = 2.7552f * albedo + 0.6903f;
    
    return max(visibility, ((visibility * a + b) * visibility + c) * visibility);
}

float3 get_dominant_specular_direction(float3 normal, float3 view_dir, float roughness)
{
    // perfect reflection direction
    float3 reflection = reflect(-view_dir, normal);
    
    // bias toward normal for rough surfaces to reduce artifacts
    const float roughness_threshold = 0.8f;
    if (roughness > roughness_threshold)
    {
        float blend = (roughness - roughness_threshold) / (1.0f - roughness_threshold);
        reflection  = normalize(lerp(reflection, normal, blend * 0.1f));
    }
    
    return reflection;
}

float3 fresnel_schlick_roughness(float cos_theta, float3 F0, float roughness)
{
    // roughness modified fresnel
    return F0 + (max(1.0f - roughness.xxx, F0) - F0) * pow(saturate(1.0f - cos_theta), 5.0f);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // surface setup
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // sky check
    if (surface.is_sky())
        return;

    // view and energy calculations
    const float3 view_dir        = normalize(-surface.camera_to_pixel);
    const float n_dot_v          = saturate(dot(surface.normal, view_dir));
    const float3 F               = fresnel_schlick_roughness(n_dot_v, surface.F0, surface.roughness);
    const float2 envBRDF         = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;
    const float3 diffuse_energy  = compute_diffuse_energy(specular_energy, surface.metallic);

    // specular reflection setup
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, view_dir, surface.roughness);
    float mip_count_environment        = pass_get_f3_value().x;
    float mip_level                    = lerp(0.0f, mip_count_environment - 1.0f, surface.roughness);
    
    // specular occlusion (prevent reflection leakage using bent normal)
    float bent_reflection_factor       = saturate(dot(surface.bent_normal, dominant_specular_direction));
    float specular_occlusion           = surface.occlusion * saturate(bent_reflection_factor + (1.0f - surface.roughness));

    // environment sampling
    float3 specular_skysphere          = tex3.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(dominant_specular_direction), mip_level).rgb;
    float3 diffuse_skysphere           = tex3.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(surface.bent_normal), mip_count_environment).rgb;
    
    // apply energy and occlusion
    // multi-bounce is applied to diffuse ONLY, specular relies on simple occlusion
    float3 diffuse_occlusion           = gtao_multi_bounce(surface.occlusion, surface.albedo.rgb);
    float3 diffuse_ibl                 = diffuse_skysphere * diffuse_occlusion * diffuse_energy * surface.albedo.rgb;
    float3 specular_ibl                = specular_skysphere * specular_energy * specular_occlusion;

    // combine ibl
    float3 ibl  = diffuse_ibl + specular_ibl;
    ibl        *= surface.alpha;

    // output
    tex_uav[thread_id.xy] += validate_output(float4(ibl, 0.0f));
}
