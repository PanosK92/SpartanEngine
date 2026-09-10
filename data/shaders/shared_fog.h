// Shared grid and transport math. Distances are metres, extinction is inverse metres.
#ifndef SPARTAN_SHARED_FOG
#define SPARTAN_SHARED_FOG
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace spartan::fog
{
using std::clamp;
using std::max;
using std::min;
using std::exp;
using std::log;
using std::pow;
#define FOG_INLINE inline
#else
#define FOG_INLINE
#endif

static const unsigned int fog_width = 256;
static const unsigned int fog_height = 144;
static const unsigned int fog_depth = 128;
static const float fog_near_slices = 96.0f;
// sky visibility is low frequency, trace it on a coarser grid
static const unsigned int fog_sky_visibility_divisor_xy = 4;
static const unsigned int fog_sky_visibility_divisor_z = 2;
static const float fog_detail_far = 64.0f;
static const float fog_far = 32000.0f;

FOG_INLINE float fog_slice_to_distance(float u)
{
    float slice = clamp(u, 0.0f, 1.0f) * float(fog_depth);
    if (slice <= fog_near_slices)
        return 2.0f * (pow(33.0f, slice / fog_near_slices) - 1.0f);
    return fog_detail_far * pow(fog_far / fog_detail_far,
        (slice - fog_near_slices) / (float(fog_depth) - fog_near_slices));
}

FOG_INLINE float fog_distance_to_slice(float distance)
{
    distance = clamp(distance, 0.0f, fog_far);
    float slice = distance <= fog_detail_far
        ? fog_near_slices * log(distance * 0.5f + 1.0f) / log(33.0f)
        : fog_near_slices + (float(fog_depth) - fog_near_slices)
            * log(distance / fog_detail_far) / log(fog_far / fog_detail_far);
    return slice / float(fog_depth);
}

// Exact constant-medium integral, with a cancellation-free limit in clear air.
FOG_INLINE float fog_segment_weight(float extinction, float distance)
{
    float tau = max(extinction, 0.0f) * max(distance, 0.0f);
    return tau < 0.001f
        ? max(distance, 0.0f) * (1.0f - tau * 0.5f + tau * tau / 6.0f)
        : (1.0f - exp(-tau)) / max(extinction, 1e-20f);
}

// Mean contributing distance under Beer attenuation. Long cells must evaluate
// water lighting near its entry, where photons survive, rather than in darkness
// at the geometric midpoint hundreds of metres below the surface.
FOG_INLINE float fog_segment_centroid(float extinction, float distance)
{
    float tau = max(extinction, 0.0f) * max(distance, 0.0f);
    if (tau < 0.1f)
        return max(distance, 0.0f) * (0.5f - tau / 12.0f + tau * tau * tau / 720.0f);
    return 1.0f / extinction - distance * exp(-tau) / (1.0f - exp(-tau));
}

FOG_INLINE float fog_reproject_height_distance(float distance, float ray_y, float tap_y)
{
    return tap_y * ray_y > 0.0f && tap_y * tap_y > 1e-8f ? distance * ray_y / tap_y : distance;
}

// Ratio of constant-source scattering integrals after rescaling the optical
// path. Evaluate in optical depth to avoid 0/0 at a clear horizon column.
FOG_INLINE float fog_rescale_scattering(float transmittance, float scale)
{
    float tau = -log(clamp(transmittance, 1e-20f, 1.0f));
    return fog_segment_weight(tau, max(scale, 0.0f)) / fog_segment_weight(tau, 1.0f);
}

// A cell crossed by the water surface contains two ordered media, not a blend.
// The partial integral is used by full-resolution composition at the interface.
FOG_INLINE float fog_layered_weight(float air_extinction, float water_extinction,
    float air_scattering, float water_scattering, float length, float water_fraction,
    bool water_first, float partial_length)
{
    float first_length = length * (water_first ? water_fraction : 1.0f - water_fraction);
    float d0 = min(max(partial_length, 0.0f), first_length);
    float d1 = max(min(partial_length, length) - first_length, 0.0f);
    float e0 = water_first ? water_extinction : air_extinction;
    float e1 = water_first ? air_extinction : water_extinction;
    float s0 = water_first ? water_scattering : air_scattering;
    float s1 = water_first ? air_scattering : water_scattering;
    return s0 * fog_segment_weight(e0, d0) + exp(-e0 * d0) * s1 * fog_segment_weight(e1, d1);
}

FOG_INLINE float fog_layered_transmittance(float air_extinction, float water_extinction,
    float length, float water_fraction, bool water_first, float partial_length)
{
    float first_length = length * (water_first ? water_fraction : 1.0f - water_fraction);
    float d0 = min(max(partial_length, 0.0f), first_length);
    float d1 = max(min(partial_length, length) - first_length, 0.0f);
    return exp(-(water_first ? water_extinction : air_extinction) * d0
               -(water_first ? air_extinction : water_extinction) * d1);
}

#ifdef __cplusplus
}
#endif
#undef FOG_INLINE
#endif
