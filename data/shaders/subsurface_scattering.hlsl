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

float subsurface_diffuse_response(float n_dot_l, float view_dot_minus_light, bool thin_foliage)
{
    float reflection = subsurface_wrapped_diffuse(n_dot_l);
    if (!thin_foliage)
        return reflection;

    // Thin sheets spend most of their scattering budget on the opposite side.
    // A broad, bounded forward lobe strengthens sunlight seen through leaves;
    // neither the view direction nor a fake grazing-angle thickness creates energy.
    float forward = saturate(view_dot_minus_light);
    forward *= forward;
    forward *= forward;
    float phase = 0.65f + 0.35f * forward;
    float transmission = subsurface_wrapped_diffuse(-n_dot_l) * phase;
    return 0.30f * reflection + 0.70f * transmission;
}

#endif
