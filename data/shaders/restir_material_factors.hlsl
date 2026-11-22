/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_MATERIAL_FACTORS
#define SPARTAN_RESTIR_MATERIAL_FACTORS

// Correlated-Smith GGX integral fit (Ray Tracing Gems, chapter 32, equation 4),
// also used by the reference's approxSpecularIntegralGGX. Alpha is the actual
// GGX width, independent of the application's material roughness convention.
float3 restir_integrated_specular(float3 f0, float alpha, float no_v)
{
    float c = abs(no_v);
    float c2 = c * c;
    float c3 = c2 * c;
    float a3 = alpha * alpha * alpha;
    float bias_numerator = (0.995367f - 1.38839f * c) + alpha * (-0.24751f + 1.97442f * c);
    float bias_denominator = (1.0f + 2.68132f * c + 52.366f * c3) +
        alpha * (16.0932f - 3.98452f * c + 59.3013f * c3) +
        a3 * (-5.18731f + 255.259f * c + 2544.07f * c3);
    float scale_numerator = (-0.0564526f + 3.82901f * c) + alpha * (16.91f - 11.0303f * c);
    float scale_denominator = (1.0f + 4.11118f * c2 - 1.37886f * c3) +
        alpha * (19.3254f - 28.9947f * c2 + 16.9514f * c3) +
        a3 * (0.545386f + 96.0994f * c2 - 79.4492f * c3);
    float bias = (bias_numerator / bias_denominator) * saturate(dot(f0, 1.0f / 3.0f) * 50.0f);
    return f0 * max(scale_numerator / scale_denominator, 0.0f) + max(bias, 0.0f);
}

// Reference FetchDenoiserData: split non-delta radiance by material albedos,
// not by the sampled path direction. Keep zero diffuse albedo exactly zero.
void restir_nrd_material_factors(float3 albedo, float metallic, float alpha, float no_v,
    out float3 diffuse, out float3 specular)
{
    diffuse = albedo * (1.0f - metallic);
    specular = max(0.01f, restir_integrated_specular(lerp(0.04f, albedo, metallic), alpha, no_v));
}

// The color on a glass interface describes absorption. Its camera signal consists
// of reflection and transmission, so sending any of it to diffuse REBLUR erases lens detail.
bool restir_nrd_glass(float2 uv)
{
    MaterialParameters mat = material_parameters[get_material_index(uv)];
    return mat.color.a < 1.0f && mat.alpha_blend <= 0.5f && !mat.is_alpha_tested();
}

void restir_nrd_material_factors_full(float2 uv, float3 albedo, float metallic, float raw_roughness, float no_v,
    out float3 diffuse, out float3 specular, out float nrd_roughness)
{
    float clamped_roughness = max(raw_roughness, 0.04f);
    float alpha = ggx_alpha_from_roughness(clamped_roughness);
    nrd_roughness = sqrt(alpha);
    if (restir_nrd_glass(uv))
    {
        // Route glass/water transmission through NRD diffuse (smoothly denoises rough interiors and
        // underwater seabed) while keeping mirror reflection in NRD specular at a tight lobe.
        diffuse = 1.0f;
        specular = 1.0f;
        float dielectric_alpha = clamped_roughness * clamped_roughness;
        nrd_roughness = dielectric_alpha < 0.0064f ? 0.04f : sqrt(dielectric_alpha);
        return;
    }

    diffuse = albedo * (1.0f - metallic);
    float3 base_specular = restir_integrated_specular(lerp(0.04f, albedo, metallic), alpha, no_v);
    MaterialParameters mat = material_parameters[get_material_index(uv)];
    float clearcoat = saturate(mat.clearcoat);
    if (clearcoat > 0.0f)
    {
        float coat_alpha = max(mat.clearcoat_roughness, 0.004f);
        float3 coat_specular = restir_integrated_specular(0.04f, coat_alpha, no_v) * clearcoat;
        float coat_f = saturate(coat_specular.x);
        diffuse *= (1.0f - coat_f);
        float3 total_specular = base_specular * (1.0f - coat_f) + coat_specular;
        // White clearcoat over deeply saturated metal (e.g. red car paint) carries achromatic
        // reflections on top of low green/blue base albedo. Floor the demodulator per-channel so
        // dividing white clearcoat radiance does not explode into extreme cyan fireflies in NRD.
        float max_spec = max(total_specular.r, max(total_specular.g, total_specular.b));
        total_specular = max(total_specular, max_spec * (0.5f * clearcoat));
        float coat_weight = get_rt_reflection_coat(clearcoat, metallic);
        // Match hybrid reflection NRD sharpness on smooth clearcoat surfaces.
        float coat_nrd_r = min(sqrt(coat_alpha), sqrt(ggx_alpha_from_roughness(mat.clearcoat_roughness)));
        nrd_roughness = lerp(nrd_roughness, coat_nrd_r, coat_weight);
        specular = max(0.01f, total_specular);
    }
    else
    {
        specular = max(0.01f, base_specular);
    }
}

float3 restir_primary_specular_fraction(float3 normal, float3 view, float3 albedo, float roughness, float metallic)
{
    float3 diffuse, specular;
    restir_nrd_material_factors(albedo, metallic, ggx_alpha_from_roughness(roughness),
        saturate(dot(normal, view)), diffuse, specular);
    return specular / (diffuse + specular);
}

#endif
