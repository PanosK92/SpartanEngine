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

// = INCLUDES ========================
#include "brdf.hlsl"
#include "spherical_harmonics.hlsl"
//====================================

float3 sample_restir_gi_bilateral(
    float2 uv,
    float depth_linear,
    float3 normal)
{
    uint width;
    uint height;
    tex4.GetDimensions(width, height);

    float2 size       = float2(width, height);
    float2 size_inv   = 1.0f / size;
    float2 position   = uv * size - 0.5f;
    int2 base         = int2(floor(position));
    float2 fraction   = frac(position);
    int2 offsets[4]   =
    {
        int2(0, 0),
        int2(1, 0),
        int2(0, 1),
        int2(1, 1)
    };
    float weights[4] =
    {
        (1.0f - fraction.x) * (1.0f - fraction.y),
        fraction.x * (1.0f - fraction.y),
        (1.0f - fraction.x) * fraction.y,
        fraction.x * fraction.y
    };

    float3 result      = 0.0f;
    float weight_total = 0.0f;
    int2 pixel_max     = int2(width, height) - 1;

    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        int2 pixel       = clamp(base + offsets[i], int2(0, 0), pixel_max);
        float2 sample_uv = (float2(pixel) + 0.5f) * size_inv;
        float depth      = tex_depth.SampleLevel(
            samplers[sampler_point_clamp],
            sample_uv,
            0.0f
        ).r;
        if (depth <= 0.0f)
        {
            continue;
        }

        float depth_difference =
            abs(linearize_depth(depth) - depth_linear) /
            max(depth_linear, 1e-3f);
        float depth_weight = exp(-depth_difference * 64.0f);
        float normal_weight = pow(
            saturate(dot(normal, get_normal(sample_uv))),
            16.0f
        );
        float weight = weights[i] * depth_weight * normal_weight;

        result       += tex4.Load(int3(pixel, 0)).rgb * weight;
        weight_total += weight;
    }

    if (weight_total > 1e-5f)
    {
        return result / weight_total;
    }

    return tex4.SampleLevel(
        samplers[sampler_point_clamp],
        uv,
        0.0f
    ).rgb;
}

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
    // frostbite/epic dominant direction - biases toward normal as roughness increases
    // this better represents where most specular energy comes from for rough surfaces
    float3 reflection = reflect(-view_dir, normal);
    float smoothness  = 1.0f - roughness;
    float factor      = smoothness * (sqrt(smoothness) + roughness);
    return normalize(lerp(normal, reflection, factor));
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

    // stain already multiplied into the frame, do not add rubber ibl on top
    if (surface.is_skid_mark())
        return;

    // view and energy calculations
    const float3 view_dir = normalize(-surface.camera_to_pixel);
    const float n_dot_v   = saturate(dot(surface.normal, view_dir));
    
    // split-sum approximation: use F0 (not full fresnel) as the LUT already encodes fresnel response
    const float2 envBRDF         = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = surface.F0 * envBRDF.x + envBRDF.y;
    
    // diffuse energy uses roughness-modified fresnel for proper energy conservation
    const float3 F              = fresnel_schlick_roughness(n_dot_v, surface.F0, surface.roughness);
    const float3 diffuse_energy = compute_diffuse_energy(F, surface.metallic);

    // specular reflection setup
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, view_dir, surface.roughness);
    float mip_count_environment        = pass_get_f3_value().x;
    float mip_level                    = surface.roughness * surface.roughness * (mip_count_environment - 1.0f);
    
    // specular occlusion stack, three terms in sequence

    // water has analytic normals and an unoccluded sky view, the ao and bent normal terms run on placeholder data and wrongly crush its reflection, so the open ocean keeps only the horizon fade
    float specular_occlusion = 1.0f;
    if (!surface.is_water())
    {
        // lagarde 2014 cone aperture vs ao, derives the cone aperture from roughness
        specular_occlusion = saturate(pow(n_dot_v + surface.occlusion, exp2(-16.0f * surface.roughness - 1.0f)) - 1.0f + surface.occlusion);

        // bent normal cone overlap, jimenez 2016, weighted by smoothness^2 so it only bites near mirrors
        float bent_dot               = saturate(dot(surface.bent_normal, dominant_specular_direction));
        float smoothness             = 1.0f - surface.roughness;
        float bent_reflection_factor = lerp(1.0f, bent_dot, smoothness * smoothness);
        specular_occlusion          *= bent_reflection_factor;
    }

    // horizon fade, lagarde 2014, stops sampling the panorama below the surface plane
    float horizon       = saturate(1.0f + dot(dominant_specular_direction, surface.normal));
    specular_occlusion *= horizon * horizon;

    // environment sampling, specular stays on the prefiltered skysphere
    float3 specular_skysphere = tex3.SampleLevel(
        samplers[sampler_trilinear_clamp],
        direction_sphere_uv(dominant_specular_direction),
        mip_level
    ).rgb;

    // diffuse directional gtao on sh, bent normal is the visibility cone axis, ao narrows
    // higher bands (soft integration over the open cone), multi-bounce is applied as a
    // relative boost so occlusion is not squared on top of the cone
    float3 sky_sh[9];
    sh_load_l2(tex5, sky_sh);
    const float ibl_visibility_floor = 0.1f;
    float  ibl_visibility = max(surface.occlusion, ibl_visibility_floor);
    float3 diffuse_skysphere = sh_irradiance_l2(
        surface.bent_normal,
        sky_sh,
        ibl_visibility
    );
    float3 multi_bounce = gtao_multi_bounce(ibl_visibility, surface.albedo.rgb);
    float3 bounce_boost = multi_bounce / ibl_visibility;
    float3 diffuse_ibl  = diffuse_skysphere * bounce_boost * diffuse_energy * surface.albedo.rgb;
    float3 specular_ibl = specular_skysphere * specular_energy * specular_occlusion;

    // transparents have no diffuse lobe, transmission is composited in reflections_apply, a sky
    // lambert layer on top reads as an opaque milky sheet, the specular sky reflection stays
    if (surface.is_water() || surface.is_transparent())
    {
        diffuse_ibl = 0.0f;
    }

    // restir replaces diffuse ibl so its visibility controls both dark and lit regions
    // restir_pt_nrd_pack marks any texel past nrd's denoising range as invalid and zeroes its
    // radiance, so the replacement has to fade back to the sky term over that last stretch or
    // distant surfaces lose all ambient and read as black
    if (pass_get_f3_value().y > 0.5f &&
        !surface.is_water() &&
        !surface.is_transparent())
    {
        float view_z          = abs(get_position_view_space(surface.uv).z);
        float denoising_range = max(buffer_frame.camera_far * 0.99f, 1.0f);
        float fade_start      = denoising_range * 0.9f;
        float coverage        = 1.0f - saturate((view_z - fade_start) / max(denoising_range - fade_start, 1e-3f));

        if (coverage > 0.0f)
        {
            float3 restir_gi = sample_restir_gi_bilateral(
                surface.uv,
                linearize_depth(surface.depth),
                surface.normal
            );
            float3 restir_ibl =
                restir_gi *
                restir_gi_demodulator(surface.albedo.rgb) *
                pass_get_f3_value().z;

            diffuse_ibl = lerp(diffuse_ibl, restir_ibl, coverage);
        }
    }

    // ray traced reflections replace specular ibl
    if (is_ray_traced_reflections_enabled())
    {
        specular_ibl *= 0.0f;
    }

    // transparents take full ibl, fresnel inside the split sum already governs the reflection split
    float3 ibl  = diffuse_ibl + specular_ibl;
    ibl        *= surface.is_transparent() ? 1.0f : surface.alpha;

    // output
    tex_uav[thread_id.xy] += validate_output(float4(ibl, 0.0f));
}
