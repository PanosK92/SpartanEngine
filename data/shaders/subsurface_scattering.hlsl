#ifndef SPARTAN_SUBSURFACE_SCATTERING
#define SPARTAN_SUBSURFACE_SCATTERING

// A local diffuse approximation, not a volumetric thickness simulation. The wrap
// integrates to one over incident directions: the (1 + wrap)^2 denominator keeps
// broadening the lobe from manufacturing light. The caller blends this with the
// ordinary diffuse lobe using the material's scattering fraction.
float subsurface_wrapped_diffuse(float n_dot_l)
{
    const float wrap = 0.35f;
    return max(n_dot_l + wrap, 0.0f) / ((1.0f + wrap) * (1.0f + wrap) * 3.14159265359f);
}

float subsurface_diffuse_response(float n_dot_l)
{
    return subsurface_wrapped_diffuse(n_dot_l);
}

// Burley 2015 normalized diffusion, the fit to brute force scattering that skin, wax and marble
// use. d is the shape distance per channel in meters, the mean distance light travels is 2.5 d.
// This is the radial profile times r, the 1 / r of the area measure cancels against the sampling.
float3 subsurface_burley_profile(float r, float3 d)
{
    float3 s      = 1.0f / max(d, 1e-7f);
    float3 exp_13 = exp(-r * s / 3.0f);
    return s * (exp_13 + exp_13 * exp_13 * exp_13) / (8.0f * 3.14159265359f);
}

// fraction of the light entering at a point that leaves within r of it
float3 subsurface_burley_cdf(float r, float3 d)
{
    float3 exp_13 = exp(-r / (3.0f * max(d, 1e-7f)));
    return 1.0f - 0.25f * exp_13 * exp_13 * exp_13 - 0.75f * exp_13;
}

// analytic inverse of the cdf above (golubev 2018), u in [0, 1) maps to a radius, rcp_pdf is one
// over the density of that radius in the same r weighted form as subsurface_burley_profile
void subsurface_burley_sample(float u, float d, out float r, out float rcp_pdf)
{
    u = 1.0f - u;

    float g = 1.0f + (4.0f * u) * (2.0f * u + sqrt(1.0f + (4.0f * u) * u));
    float n = pow(g, -1.0f / 3.0f);
    float p = (g * n) * n;
    float c = 1.0f + p + n;
    float x = 3.0f * log(c / (4.0f * u));

    // exp(-x / 3) is 4u / c, so the sum of both exponentials needs no further exp
    float rcp_exp = (c * c * c) / ((4.0f * u) * (c * c + (4.0f * u) * (4.0f * u)));

    r       = x * d;
    rcp_pdf = (8.0f * 3.14159265359f * d) * rcp_exp;
}

// the same profile integrated through a slab, how much light arriving on the far side of a
// solid t meters thick comes out of this one
float3 subsurface_burley_transmittance(float t, float3 d)
{
    float3 exp_13 = exp(-t / (3.0f * max(d, 1e-7f)));
    return 0.25f * (exp_13 * exp_13 * exp_13 + 3.0f * exp_13);
}

// Thin leaves are a reflectance and a transmittance of about the same size, a sunlit leaf keeps
// its whole lambert lobe and light arriving on the reverse side comes out of the viewed side.
// Most of it leaves diffusely, the rest keeps going roughly the way it came in, which is the glow
// of a canopy seen against the sun. The phase is normalised to one over the sphere so the peak
// redistributes the transmitted light instead of adding to it.
float foliage_transmission_response(float n_dot_l, float view_dot_minus_light)
{
    // the double sided flip and leaf normal maps make the terminator noisy, a small wrap hides it
    const float wrap  = 0.2f;
    float reverse     = max(-n_dot_l + wrap, 0.0f) / ((1.0f + wrap) * (1.0f + wrap) * 3.14159265359f);

    const float g     = 0.55f;
    float denominator = 1.0f + g * g - 2.0f * g * saturate(view_dot_minus_light);
    float phase       = (1.0f - g * g) / (denominator * sqrt(denominator));

    return reverse * (0.6f + 0.4f * phase);
}

// Transmitted light crosses the chlorophyll a second time, so red and blue are absorbed again and
// what makes it through is a deeper, yellower green than what the surface reflects. Returned as a
// multiplier on albedo because the composition applies albedo once to all diffuse light.
float3 foliage_transmission_tint(float3 albedo)
{
    float peak    = max(max(albedo.r, albedo.g), albedo.b) + 1e-4f;
    float3 chroma = saturate(albedo / peak);
    return chroma * chroma * float3(0.85f, 1.0f, 0.8f);
}

#endif
