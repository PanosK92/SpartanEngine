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

float3 restir_primary_specular_fraction(float3 normal, float3 view, float3 albedo, float roughness, float metallic)
{
    float3 diffuse, specular;
    restir_nrd_material_factors(albedo, metallic, ggx_alpha_from_roughness(roughness),
        saturate(dot(normal, view)), diffuse, specular);
    return specular / (diffuse + specular);
}

#endif
