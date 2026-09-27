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
