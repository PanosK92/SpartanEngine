// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
// Shared by the surface shader and CPU scatter. Keep the habitat boundary identical.
#ifndef TERRAIN_HABITAT_SHARED
#define TERRAIN_HABITAT_SHARED
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
namespace spartan::terrain_habitat_shared
{
using uint = unsigned int;
#define HABITAT_INLINE inline
#define HABITAT_FLOOR std::floor
#define HABITAT_CLAMP(v) std::clamp(v, 0.0f, 1.0f)
#else
#define HABITAT_INLINE
#define HABITAT_FLOOR floor
#define HABITAT_CLAMP(v) saturate(v)
#endif

HABITAT_INLINE uint habitat_hash(uint x)
{
    x = (x ^ (x >> 16)) * 0x7feb352du;
    x = (x ^ (x >> 15)) * 0x846ca68bu;
    return x ^ (x >> 16);
}
HABITAT_INLINE float habitat_smooth(float x)
{
    x = HABITAT_CLAMP(x);
    return x * x * (3.0f - 2.0f * x);
}
HABITAT_INLINE float habitat_cell(int x, int z, uint seed)
{
    return float(habitat_hash(uint(x) * 73856093u ^ uint(z) * 19349663u ^ seed) & 0xffffu) / 65535.0f;
}
HABITAT_INLINE float habitat_noise(float x, float z, uint seed)
{
    int ix = int(HABITAT_FLOOR(x)), iz = int(HABITAT_FLOOR(z));
    float u = habitat_smooth(x - float(ix)), v = habitat_smooth(z - float(iz));
    float a = habitat_cell(ix, iz, seed) * (1.0f-u) + habitat_cell(ix+1, iz, seed) * u;
    float b = habitat_cell(ix, iz+1, seed) * (1.0f-u) + habitat_cell(ix+1, iz+1, seed) * u;
    return a * (1.0f-v) + b * v;
}

// Exposure and sediment shift the same continuous patch field, rather than
// layering unrelated noise masks over the material and each species.
HABITAT_INLINE float habitat_woodland(float x, float z, float above_sea, float slope_degrees, float insolation, float deposition)
{
    float patch = habitat_noise(x / 430.0f, z / 430.0f, 71431u) * 0.78f
                + habitat_noise(x / 95.0f, z / 95.0f, 11939u) * 0.22f;
    patch += (0.5f - insolation) * 0.16f + (deposition - 0.5f) * 0.12f;
    return habitat_smooth((patch - 0.38f) / 0.27f)
         * habitat_smooth((above_sea - 3.0f) / 12.0f)
         * (1.0f - habitat_smooth((slope_degrees - 28.0f) / 22.0f));
}
HABITAT_INLINE float habitat_grove(float x, float z, float above_sea, float slope_degrees, float woodland)
{
    return (1.0f - woodland) * habitat_smooth((habitat_noise(x / 230.0f, z / 230.0f, 937u) - 0.32f) / 0.30f)
         * habitat_smooth((above_sea - 3.0f) / 8.0f)
         * (1.0f - habitat_smooth((above_sea - 180.0f) / 100.0f))
         * (1.0f - habitat_smooth((slope_degrees - 15.0f) / 15.0f));
}
#ifdef __cplusplus
}
#endif
#undef HABITAT_INLINE
#undef HABITAT_FLOOR
#undef HABITAT_CLAMP
#endif
