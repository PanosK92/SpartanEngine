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

float3 get_dominant_specular_direction(float3 normal, float3 reflection, float roughness)
{
    const float smoothness = 1.0f - roughness;
    const float alpha      = smoothness * (sqrt(smoothness) + roughness);
    
    return lerp(normal, reflection, alpha);
}

float3 sample_environment(float2 uv, float mip_level, float mip_max)
{
    return tex3.SampleLevel(samplers[sampler_trilinear_clamp], uv, mip_level).rgb;
}

float get_blend_weight(float value, float smoothness)
{
    return saturate((value + smoothness) / (smoothness * 2.0f));
}

float3 combine_specular_sources(float3 specular_gi, float3 specular_sky)
{
    const float smoothness = 0.5f; // aka blend region size
    
    // get weights for each source
    float gi_weight  = get_blend_weight(luminance(specular_gi), smoothness);

    // blend
    float3 result = specular_sky;                               // start with sky as base
    result        = lerp(result, specular_gi, gi_weight);       // blend in gi
    
    return result;
}

float3 fresnel_schlick_roughness(float cos_theta, float3 F0, float roughness)
{
    return F0 + (max(1.0 - roughness.xxx, F0) - F0) * pow(saturate(1.0 - cos_theta), 5.0);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // create surface
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    if (surface.is_sky())  // we don't want to do ibl on the sky itself
        return;

    // diffuse and specular energy
    const float n_dot_v          = saturate(dot(-surface.camera_to_pixel, surface.normal));
    const float3 F               = fresnel_schlick_roughness(n_dot_v, surface.F0, surface.roughness);
    const float2 envBRDF         = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;
    const float3 diffuse_energy  = compute_diffuse_energy(specular_energy, surface.metallic);

    // sample all the textures
    const float3 reflection            = reflect(surface.camera_to_pixel, surface.bent_normal);
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.bent_normal, reflection, surface.roughness);
    float mip_count_environment        = pass_get_f3_value().x;
    float mip_level                    = lerp(0, mip_count_environment - 1, surface.roughness);
    float3 specular_skysphere          = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level, mip_count_environment);
    float3 diffuse_skysphere           = sample_environment(direction_sphere_uv(surface.bent_normal), mip_count_environment, mip_count_environment);
    float shadow_mask                  = tex[thread_id.xy].r;
    float3 diffuse_gi                  = tex_uav2[thread_id.xy].rgb;
    float3 specular_gi                 = tex_uav3[thread_id.xy].rgb;

    // modulate specular light source with the outcoming energy
    specular_gi        *= F;
    specular_skysphere *= specular_energy * shadow_mask;

    // combine the diffuse light
    shadow_mask        = max(0.5f, shadow_mask); // GI is not as good, so never go full dark
    float3 diffuse_ibl = diffuse_skysphere * surface.occlusion * shadow_mask + diffuse_gi;

    // combine all the specular light, fallback order: gi -> skysphere
    float3 specular_ibl = combine_specular_sources(specular_gi, specular_skysphere);
    
    // combine the diffuse and specular light
    float3 ibl = (diffuse_ibl * diffuse_energy * surface.albedo.rgb) + specular_ibl;

    // tone down for transparent surfaces
    ibl *= surface.alpha;

    tex_uav[thread_id.xy] += float4(ibl, 0.0f);
}
