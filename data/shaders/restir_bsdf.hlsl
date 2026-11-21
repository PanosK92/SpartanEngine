/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_BSDF
#define SPARTAN_RESTIR_BSDF
#include "restir_dielectric.hlsl"
#include "brdf.hlsl"
#include "restir_surface.hlsl"

// A sampled event carries throughput independently of its continuous density:
// a delta event has nonzero throughput but no solid-angle PDF.
struct RestirSurfaceSample
{
    float3 direction;
    float3 weight;
    float pdf;
    float joint_pdf; // Direction and selected component; pdf remains marginal for MIS/footprints.
    uint component;
    bool transmission;
    bool delta;
    bool valid;
};

// The BSDF reports zero solid-angle density for a delta event. The reference
// path state uses a finite sentinel instead for MIS and reconnection eligibility;
// throughput still comes from sample.weight, never from dividing by this value.
float restir_path_sample_pdf(RestirSurfaceSample sample)
{
    return sample.delta ? 1e20f : sample.pdf;
}

float restir_path_sample_joint_pdf(RestirSurfaceSample sample)
{
    // ReSTIRPT::ProcessScatterData keeps lastScatterPdf at one for a delta
    // event. Its discrete probability is already included in sample.weight;
    // the large footprint/MIS sentinel is not a solid-angle density.
    return sample.delta ? 1.0f : sample.joint_pdf;
}

// Match StandardBSDF's EnableDeltaBSDF roughness classification before both
// evaluation and sampling. The low-level GGX functions accept alpha directly.
float restir_dielectric_alpha(float roughness)
{
    float alpha = roughness * roughness;
    return alpha < 0.0064f ? 0.0f : alpha;
}

// Evaluate the engine's paint, coat and cloth lobes on the traced direction.
// The base cosine/GGX mixture remains the proposal and its PDF stays unchanged.
float3 restir_layered_brdf(PathSurface hit, float3 view, float3 direction, uint component)
{
    MaterialParameters m = material_parameters[hit.material_index];
    Surface s = (Surface)0;
    s.normal = hit.hit_normal;
    s.camera_to_pixel = -view;
    s.albedo = hit.albedo;
    s.roughness = hit.roughness;
    s.metallic = hit.metallic;
    s.F0 = lerp(0.04f, hit.albedo, hit.metallic);
    s.diffuse_energy = 1.0f;
    s.anisotropic = m.anisotropic;
    s.anisotropic_rotation = m.anisotropic_rotation;
    s.clearcoat = m.clearcoat;
    s.clearcoat_roughness = max(m.clearcoat_roughness, 0.001f);
    s.coat_tint = m.coat_tint.rgb;
    s.coat_tint_strength = m.coat_tint.a;
    s.flake_weight = saturate(m.flake_strength);
    s.flake_color = paint_flake_color(hit.albedo);
    s.sheen = m.sheen;
    s.sheen_color = m.sheen_color.rgb;
    s.sheen_roughness = m.sheen_color.w;
    Light light = (Light)0;
    light.to_pixel = -direction;
    AngularInfo angles;
    angles.Build(light, s);
    float edge = pow5(1.0f - angles.n_dot_v);
    float pearl = saturate(m.pearl_strength * edge);
    s.albedo = lerp(s.albedo, m.pearl_color.rgb, pearl) * lerp(1.0f, s.coat_tint, s.coat_tint_strength * lerp(0.35f, 1.0f, edge));
    s.F0 = lerp(s.F0, m.pearl_color.rgb, pearl * 0.35f);
    paint_iridescence(m.iridescence, m.iridescence_thickness, s.clearcoat, angles.n_dot_v, s.F0, s.flake_color);
    float3 specular;
    if (s.anisotropic > 0.0f)
        specular = BRDF_Specular_Anisotropic(s, angles);
    else
        specular = BRDF_Specular_Isotropic(s, angles);
    if (s.clearcoat > 0.0f)
    {
        specular *= 1.0f - F_Schlick(0.04f, 1.0f, angles.v_dot_h) * s.clearcoat;
        specular += BRDF_Specular_Clearcoat(s, angles);
    }
    if (s.flake_weight > 0.0f)
        specular += BRDF_Specular_Flakes(s, angles, light);
    if (s.sheen > 0.0f)
        specular += BRDF_Specular_Sheen(s, angles);
    float3 diffuse = BRDF_Diffuse(s, angles) * s.diffuse_energy;
    return ((component == 2u ? 0.0f : diffuse) + (component == 1u ? 0.0f : specular)) * angles.n_dot_l;
}

float3 restir_surface_evaluate(PathSurface surface, float3 view, float3 direction,
    out float pdf, float specular_blend, bool dielectric = false, float eta = 1.0f, float3 transmission_tint = 1.0f, uint component = 0u)
{
    if (!dielectric)
    {
        float3 base = evaluate_reflection_brdf(surface.albedo, surface.roughness, surface.metallic,
            surface.hit_normal, surface.geometric_normal, view, direction, pdf, specular_blend, component);
        if (surface.layered && pdf > 0.0f)
            return restir_layered_brdf(surface, view, direction, component);
        return base;
    }

    pdf = 0.0f;
    bool transmission = dot(surface.hit_normal, direction) < 0.0f;
    if (restir_reflection_hemisphere(surface.geometric_normal, view, direction) == transmission)
        return 0.0f;
    float3 tangent, bitangent;
    build_orthonormal_basis_fast(surface.hit_normal, tangent, bitangent);
    float3 wi = float3(dot(view, tangent), dot(view, bitangent), dot(view, surface.hit_normal));
    float3 wo = float3(dot(direction, tangent), dot(direction, bitangent), dot(direction, surface.hit_normal));
    // Volume absorption is applied along segments, not as an extra interface tint.
    float3 f = restir_dielectric_evaluate(wi, wo, restir_dielectric_alpha(surface.roughness), eta, transmission_tint, pdf, surface.thin_walled);
    // The dielectric adapter has no diffuse component. Keep the marginal PDF
    // for MIS, but never turn a retained diffuse lobe into a specular interface.
    return component == 1u ? 0.0f : f;
}

RestirSurfaceSample restir_surface_sample(PathSurface surface, float3 view, float3 xi,
    float specular_blend, bool dielectric = false, float eta = 1.0f, float3 transmission_tint = 1.0f)
{
    RestirSurfaceSample result = (RestirSurfaceSample)0;
    if (!dielectric)
    {
        float p = saturate(compute_spec_probability(surface.albedo, surface.roughness, surface.metallic,
            dot(surface.hit_normal, view)) * specular_blend);
        result.component = xi.x < 1.0f - p ? 1u : 2u;
        result.direction = sample_reflection_brdf(surface.albedo, surface.roughness, surface.metallic,
            surface.hit_normal, surface.geometric_normal, view, xi.xy, result.pdf, specular_blend);
        if (!restir_pdf_valid(result.pdf) || dot(result.direction, surface.hit_normal) <= 0.0f ||
            any(!isfinite(result.direction))) return result;
        float unused_pdf;
        result.joint_pdf = restir_component_pdf(surface.albedo, surface.roughness, surface.metallic,
            surface.hit_normal, view, result.direction, result.component, specular_blend);
        if (!restir_pdf_valid(result.joint_pdf)) return result;
        result.weight = restir_surface_evaluate(surface, view, result.direction, unused_pdf,
            specular_blend, false, 1.0f, 1.0f, result.component) / result.joint_pdf;
    }
    else
    {
        float3 tangent, bitangent;
        build_orthonormal_basis_fast(surface.hit_normal, tangent, bitangent);
        float3 wi = float3(dot(view, tangent), dot(view, bitangent), dot(view, surface.hit_normal));
        RestirDielectricSample local = restir_dielectric_sample(wi, restir_dielectric_alpha(surface.roughness), eta, transmission_tint, xi, surface.thin_walled);
        if (!local.valid) return result;
        result.direction = tangent * local.direction.x + bitangent * local.direction.y + surface.hit_normal * local.direction.z;
        result.weight = local.weight;
        result.pdf = local.pdf;
        result.joint_pdf = local.pdf;
        result.component = 2u;
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
