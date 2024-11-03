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

float3 sample_environment(float2 uv, float mip_level, float mip_max)
{
    // sample at texture center for the lowest mip level to avoid seams (two giant pixels with different color)
    if (mip_level >= mip_max - 1)
    {
        uv = float2(0.5, 0.5);
    }
    
    return tex_environment.SampleLevel(samplers[sampler_trilinear_clamp], uv, mip_level).rgb;
}

float get_blend_weight(float value, float threshold, float smoothness)
{
    // create a smooth transition around the threshold
    // smoothness controls the size of the blend region
    return saturate((value - (threshold - smoothness)) / (smoothness * 2.0f));
}

float3 combine_specular_sources(float4 specular_ssr, float3 specular_gi, float3 specular_sky)
{
    // smooth blending parameters
    const float threshold  = 0.01f;
    const float smoothness = 0.2f; // blend region size
    
    // get smooth weights for each source
    float ssr_weight = get_blend_weight(specular_ssr.a, threshold, smoothness);
    float gi_weight  = get_blend_weight(luminance(specular_gi), threshold, smoothness);

    // start with sky as base
    float3 result = specular_sky;
    // blend GI on top
    result = lerp(result, specular_gi, gi_weight);
    // blend ssr on top
    result = lerp(result, specular_ssr.rgb, ssr_weight);
    
    return result;
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

    // diffuse and specular energy
    const float n_dot_v          = saturate(dot(-surface.camera_to_pixel, surface.normal));
    const float3 F               = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
    const float2 envBRDF         = tex_lut_ibl.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;
    const float3 diffuse_energy  = compute_diffuse_energy(specular_energy, surface.metallic);
    
    // sample all the textures
    const float3 reflection            = reflect(surface.camera_to_pixel, surface.normal);
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, reflection, surface.roughness);
    float mip_count_environment        = pass_get_f3_value().x;
    float mip_level                    = lerp(0, mip_count_environment - 1, surface.roughness);
    float3 specular_skysphere          = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level, mip_count_environment);
    float3 diffuse_skysphere           = sample_environment(direction_sphere_uv(surface.normal), mip_count_environment, mip_count_environment);
    float4 specular_ssr                = tex_ssr[thread_id.xy].rgba * (float)surface.is_opaque();                     // only compute for opaques
    float3 diffuse_gi                  = tex_light_diffuse_gi[thread_id.xy].rgb  * 2.0f * (float)surface.is_opaque(); // only computed for opaques
    float3 specular_gi                 = tex_light_specular_gi[thread_id.xy].rgb * 2.0f * (float)surface.is_opaque(); // only computed for opaques
    float shadow_mask                  = tex[thread_id.xy].r;

    // combine the diffuse light
    float3 diffuse_ibl = diffuse_skysphere * shadow_mask + diffuse_gi;

    // combine all the specular light, fallback order: ssr -> gi -> skysphere
    float3 specular_ibl = combine_specular_sources(specular_ssr, specular_gi, specular_skysphere * shadow_mask);
    
    // combine the diffuse and specular light
    float3 ibl             = (diffuse_ibl * diffuse_energy * surface.albedo.rgb) + (specular_ibl * specular_energy);
    ibl                   *= surface.occlusion;

    tex_uav[thread_id.xy] += float4(ibl, 0.0f);
}
