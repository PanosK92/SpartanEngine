/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_RESTIR_DIELECTRIC
#define SPARTAN_RESTIR_DIELECTRIC
// Included after the reservoir's shared GGX/VNDF primitives. Do not include
// the reservoir here: it also includes the path walker and surface BSDF adapter.

float restir_dielectric_ndf(float3 h, float alpha)
{
    float a2 = alpha * alpha;
    // Keep sin^2(theta) from the transverse components: 1 - h.z^2 loses
    // narrow-lobe variation once h.z rounds to one in FP32.
    float denominator = dot(h.xy, h.xy) + a2 * h.z * h.z;
    return a2 / (PI * denominator * denominator);
}

// Local-frame dielectric transport. Directions point away from the interface;
// wi.z > 0, eta = incident IOR / transmitted IOR. The caller owns medium state,
// geometric-hemisphere validation and the conversion to world coordinates.
// Uses the radiance convention of Falcor's SpecularMicrofacetBSDF: transmission
// carries eta^2 in f*cos, never in its solid-angle sampling PDF.
float restir_dielectric_fresnel(float eta, float cos_i, out float cos_t)
{
    // Equal media have no interface, including at grazing angles where
    // 1 - cos_i^2 can round to one in FP32 and mimic total internal reflection.
    if (eta == 1.0f)
    {
        cos_t = cos_i;
        return 0.0f;
    }
    float sin_t_sq = eta * eta * max(0.0f, 1.0f - cos_i * cos_i);
    cos_t = sqrt(max(0.0f, 1.0f - sin_t_sq));
    if (sin_t_sq >= 1.0f)
        return 1.0f;
    float rs = (eta * cos_i - cos_t) / (eta * cos_i + cos_t);
    float rp = (cos_i - eta * cos_t) / (cos_i + eta * cos_t);
    return saturate(0.5f * (rs * rs + rp * rp));
}

// Non-delta BSDF*cos and its Fresnel-mixture solid-angle PDF. Null samples
// retain zero density; rejecting them must not resample the visible normal.
float3 restir_dielectric_evaluate(float3 wi, float3 wo, float alpha, float eta,
    float3 transmission_tint, out float pdf)
{
    pdf = 0.0f;
    if (wi.z <= 0.0f || wo.z == 0.0f || alpha <= 0.0f || eta <= 0.0f || eta == 1.0f)
        return 0.0f;
    bool reflection = wo.z > 0.0f;
    float3 h = wo + wi * (reflection ? 1.0f : eta);
    float h_sq = dot(h, h);
    if (!(h_sq > 0.0f))
        return 0.0f;
    h *= rsqrt(h_sq);
    if (h.z < 0.0f) h = -h;
    float wi_h = dot(wi, h);
    float wo_h = dot(wo, h);
    if (wi_h <= 0.0f || (reflection ? wo_h <= 0.0f : wo_h >= 0.0f))
        return 0.0f;

    float a2 = alpha * alpha;
    float d = restir_dielectric_ndf(h, alpha);
    float cos_o = abs(wo.z);
    float root_i = sqrt(a2 + (1.0f - a2) * wi.z * wi.z);
    float root_o = sqrt(a2 + (1.0f - a2) * cos_o * cos_o);
    float g = 2.0f * wi.z * cos_o / (cos_o * root_i + wi.z * root_o);
    float visible_pdf = d * 2.0f * wi_h / (wi.z + root_i);
    float cos_t;
    float fresnel = restir_dielectric_fresnel(eta, wi_h, cos_t);
    if (reflection)
    {
        // For reflection wi.h == wo.h. Cancel this factor analytically;
        // reconstructing h from near-opposite FP32 directions loses the equality.
        pdf = fresnel * d / (2.0f * (wi.z + root_i));
        return fresnel * d * g / (4.0f * wi.z);
    }
    float denominator = wo_h + eta * wi_h;
    float denominator_sq = denominator * denominator;
    if (!(denominator_sq > 0.0f))
        return 0.0f;
    pdf = visible_pdf * (1.0f - fresnel) * abs(wo_h) / denominator_sq;
    return transmission_tint * ((1.0f - fresnel) * d * g * eta * eta *
        abs(wi_h * wo_h) / (wi.z * denominator_sq));
}

struct RestirDielectricSample
{
    float3 direction;
    float3 weight; // f*cos / pdf, or discrete throughput for a delta event
    float pdf;    // zero for a delta event, which has no solid-angle density
    bool transmission;
    bool delta;
    bool valid;
};

RestirDielectricSample restir_dielectric_sample(float3 wi, float alpha, float eta,
    float3 transmission_tint, float3 xi)
{
    RestirDielectricSample result = (RestirDielectricSample)0;
    if (wi.z <= 0.0f || eta <= 0.0f || alpha < 0.0f)
        return result;
    result.delta = alpha == 0.0f || eta == 1.0f;
    float3 h = result.delta ? float3(0.0f, 0.0f, 1.0f) : sample_ggx_vndf(wi, xi.xy, alpha);
    float wi_h = dot(wi, h);
    float cos_t;
    float fresnel = restir_dielectric_fresnel(eta, wi_h, cos_t);
    result.transmission = xi.z >= fresnel;
    result.direction = result.transmission ? (eta * wi_h - cos_t) * h - eta * wi : 2.0f * wi_h * h - wi;
    if (result.direction.z == 0.0f || ((result.direction.z < 0.0f) != result.transmission))
        return result;
    if (result.delta)
    {
        result.weight = result.transmission ? transmission_tint * eta * eta : 1.0f;
    }
    else
    {
        // Keep the sampled h for density evaluation. Reconstructing it from
        // rounded directions is ill-conditioned for narrow grazing reflections.
        float a2 = alpha * alpha;
        float cos_o = abs(result.direction.z);
        float root_i = sqrt(a2 + (1.0f - a2) * wi.z * wi.z);
        float root_o = sqrt(a2 + (1.0f - a2) * cos_o * cos_o);
        float g_over_g1 = cos_o * (wi.z + root_i) / (cos_o * root_i + wi.z * root_o);
        float d = restir_dielectric_ndf(h, alpha);
        if (result.transmission)
        {
            float wo_h = dot(result.direction, h);
            float denominator = wo_h + eta * wi_h;
            if (denominator == 0.0f)
                return result;
            result.pdf = (1.0f - fresnel) * d * 2.0f * wi_h * abs(wo_h) /
                ((wi.z + root_i) * denominator * denominator);
            result.weight = transmission_tint * (eta * eta * g_over_g1);
        }
        else
        {
            result.pdf = fresnel * d / (2.0f * (wi.z + root_i));
            result.weight = g_over_g1;
        }
        if (!(result.pdf > 0.0f) || !isfinite(result.pdf))
            return result;
    }
    result.valid = all(isfinite(result.direction)) && all(isfinite(result.weight));
    return result;
}

#endif
