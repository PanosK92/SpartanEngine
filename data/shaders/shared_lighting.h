// Scene RGB is linear Rec.709 photometric light divided by 683 for storage.
// This is a consistent RGB scale, not a spectral conversion to electrical watts.
#ifndef SPARTAN_SHARED_LIGHTING
#define SPARTAN_SHARED_LIGHTING

#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace spartan::lighting
{
using std::clamp;
using std::cos;
using std::max;
#define LIGHTING_INLINE inline
#else
#define LIGHTING_INLINE
#endif

static const float lighting_luminous_efficacy = 683.0f;
static const float lighting_emissive_nits_from_albedo = 100000.0f;
static const float lighting_emissive_nits_texture = 10000.0f;

LIGHTING_INLINE float lighting_spot_half_angle(float angle)
{
    // Keep the cone nonzero and its shadow projection below a 180 degree FOV.
    return clamp(angle, 0.01f, 1.57079632679f - 0.001f);
}

LIGHTING_INLINE float lighting_spot_solid_angle(float angle)
{
    angle = lighting_spot_half_angle(angle);
    float cos_outer = cos(angle);
    float cos_inner = cos(angle * 0.9f);
    // Integral of the shader's squared linear falloff in cos(theta): the
    // inner cone has weight one, and the penumbra has average weight 1/3.
    return 6.28318530718f * ((1.0f - cos_inner) + (cos_inner - cos_outer) / 3.0f);
}

LIGHTING_INLINE float lighting_spot_attenuation(float cos_theta, float angle)
{
    angle = lighting_spot_half_angle(angle);
    float cos_outer = cos(angle);
    float cos_inner = cos(angle * 0.9f);
    float falloff = clamp((cos_theta - cos_outer) / max(cos_inner - cos_outer, 0.000001f), 0.0f, 1.0f);
    return falloff * falloff;
}

#ifdef __cplusplus
}
#endif
#undef LIGHTING_INLINE
#endif
