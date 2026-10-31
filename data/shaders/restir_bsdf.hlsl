/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_BSDF
#define SPARTAN_RESTIR_BSDF
#include "restir_dielectric.hlsl"
#include "restir_surface.hlsl"

// A sampled event carries throughput independently of its continuous density:
// a delta event has nonzero throughput but no solid-angle PDF.
struct RestirSurfaceSample
{
    float3 direction;
    float3 weight;
    float pdf;
    bool transmission;
    bool delta;
    bool valid;
};

float3 restir_surface_evaluate(PathSurface surface, float3 view, float3 direction,
    out float pdf, float specular_blend, bool dielectric = false, float eta = 1.0f)
{
    if (!dielectric)
        return evaluate_reflection_brdf(surface.albedo, surface.roughness, surface.metallic,
            surface.hit_normal, surface.geometric_normal, view, direction, pdf, specular_blend);

    pdf = 0.0f;
    bool transmission = dot(surface.hit_normal, direction) < 0.0f;
    if (restir_reflection_hemisphere(surface.geometric_normal, view, direction) == transmission)
        return 0.0f;
    float3 tangent, bitangent;
    build_orthonormal_basis_fast(surface.hit_normal, tangent, bitangent);
    float3 wi = float3(dot(view, tangent), dot(view, bitangent), dot(view, surface.hit_normal));
    float3 wo = float3(dot(direction, tangent), dot(direction, bitangent), dot(direction, surface.hit_normal));
    // Volume absorption is applied along segments, not as an extra interface tint.
    return restir_dielectric_evaluate(wi, wo, surface.roughness * surface.roughness, eta, 1.0f, pdf);
}

RestirSurfaceSample restir_surface_sample(PathSurface surface, float3 view, float3 xi,
    float specular_blend, bool dielectric = false, float eta = 1.0f)
{
    RestirSurfaceSample result = (RestirSurfaceSample)0;
    if (!dielectric)
    {
        result.direction = sample_reflection_brdf(surface.albedo, surface.roughness, surface.metallic,
            surface.hit_normal, surface.geometric_normal, view, xi.xy, result.pdf, specular_blend);
        if (!restir_pdf_valid(result.pdf) || dot(result.direction, surface.hit_normal) <= 0.0f ||
            any(!isfinite(result.direction))) return result;
        float unused_pdf;
        result.weight = restir_surface_evaluate(surface, view, result.direction, unused_pdf, specular_blend) / result.pdf;
    }
    else
    {
        float3 tangent, bitangent;
        build_orthonormal_basis_fast(surface.hit_normal, tangent, bitangent);
        float3 wi = float3(dot(view, tangent), dot(view, bitangent), dot(view, surface.hit_normal));
        RestirDielectricSample local = restir_dielectric_sample(wi, surface.roughness * surface.roughness, eta, 1.0f, xi);
        if (!local.valid) return result;
        result.direction = tangent * local.direction.x + bitangent * local.direction.y + surface.hit_normal * local.direction.z;
        result.weight = local.weight;
        result.pdf = local.pdf;
        result.transmission = local.transmission;
        result.delta = local.delta;
        if (restir_reflection_hemisphere(surface.geometric_normal, view, result.direction) == result.transmission)
            return result;
    }
    // A geometrically invalid event is a zero draw, never a request to resample.
    result.valid = all(isfinite(result.weight)) && all(isfinite(result.direction));
    return result;
}

#endif
