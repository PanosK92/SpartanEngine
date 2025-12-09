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

// = INCLUDES ======
#include "brdf.hlsl"
//==================

// compute dominant specular direction for rough surfaces using reflection vector
// for smooth surfaces, use perfect reflection; for rough surfaces, reflection is already correct
float3 get_dominant_specular_direction(float3 normal, float3 view_dir, float roughness)
{
    // compute perfect reflection vector
    float3 reflection = reflect(-view_dir, normal);
    
    // for very rough surfaces, slightly bias toward normal to reduce fireflies
    // this is a heuristic to improve stability, not physically accurate
    const float roughness_threshold = 0.8f;
    if (roughness > roughness_threshold)
    {
        float blend = (roughness - roughness_threshold) / (1.0f - roughness_threshold);
        reflection = normalize(lerp(reflection, normal, blend * 0.1f));
    }
    
    return reflection;
}

// fresnel schlick with roughness modification for ibl
float3 fresnel_schlick_roughness(float cos_theta, float3 F0, float roughness)
{
    return F0 + (max(1.0f - roughness.xxx, F0) - F0) * pow(saturate(1.0f - cos_theta), 5.0f);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get resolution and build surface data
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // skip sky pixels (no ibl needed for sky itself)
    if (surface.is_sky())
        return;

    // compute fresnel and energy terms using original normal (not bent normal)
    const float3 view_dir            = normalize(-surface.camera_to_pixel);
    const float n_dot_v              = saturate(dot(surface.normal, view_dir));
    const float3 F                   = fresnel_schlick_roughness(n_dot_v, surface.F0, surface.roughness);
    const float2 envBRDF             = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy     = F * envBRDF.x + envBRDF.y;
    const float3 diffuse_energy      = compute_diffuse_energy(specular_energy, surface.metallic);

    // compute specular reflection using original normal (bent normal is for diffuse only)
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, view_dir, surface.roughness);
    float mip_count_environment        = pass_get_f3_value().x;
    float mip_level                    = lerp(0.0f, mip_count_environment - 1.0f, surface.roughness);
    
    // sample environment map for specular (using original normal) and diffuse (using bent normal)
    float3 specular_skysphere = tex3.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(dominant_specular_direction), mip_level).rgb;
    float3 diffuse_skysphere  = tex3.SampleLevel(samplers[sampler_trilinear_clamp], direction_sphere_uv(surface.bent_normal), mip_count_environment).rgb;
    float shadow_mask         = tex[thread_id.xy].r;

    // apply specular energy and shadow mask to specular ibl
    specular_skysphere *= specular_energy * shadow_mask;

    // apply shadow mask and occlusion to diffuse ibl (bent normal already accounts for ao direction)
    shadow_mask        = max(0.3f, shadow_mask);  // prevent complete darkness
    float3 diffuse_ibl = (diffuse_skysphere * shadow_mask) * surface.occlusion;

    // combine specular ibl (gi fallback can be added here if needed)
    float3 specular_ibl = specular_skysphere;
    
    // combine diffuse and specular ibl with proper energy terms
    float3 ibl = (diffuse_ibl * diffuse_energy * surface.albedo.rgb) + specular_ibl;

    // apply alpha for transparent surfaces
    ibl *= surface.alpha;

    // accumulate ibl contribution to output
    tex_uav[thread_id.xy] += validate_output(float4(ibl, 0.0f));
}
