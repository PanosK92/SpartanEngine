/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_COMMON_RADIANCE_CACHE
#define SPARTAN_COMMON_RADIANCE_CACHE

// world space radiance cache, a spatial hash grid in the spirit of sharc (nvidia 2024), each cell
// holds the cosine weighted radiance arriving over the hemisphere of one normal axis, so a lambert
// surface in it reflects albedo * cell, cells grow with camera distance to keep a steady screen
// density, reflection hits read it for their diffuse bounce and write one bounce ray back into it,
// cells that read other cells converge toward multiple bounces over frames
// an entry is 4 uints, key checksum (0 is empty), last frame touched, radiance rg f16, radiance b f16 | sample count f16
// the buffer is zeroed at creation, keys are claimed with a compare exchange, the radiance itself is
// written without atomics, a lost race only drops one sample of a moving average

RWStructuredBuffer<uint> radiance_cache : register(u70);

static const uint  radiance_cache_capacity  = 1u << 19;
static const uint  radiance_cache_probes    = 8;
static const uint  radiance_cache_max_age   = 300;   // frames a cell survives without being written
static const float radiance_cache_cell_near = 0.25f; // metres, cell size up to 12.5 m from the camera
static const float radiance_cache_window    = 32.0f; // moving average length in samples
static const float radiance_cache_trusted   = 8.0f;  // samples before a cell fully replaces the fallback

uint radiance_cache_hash(uint x)
{
    // pcg
    uint state = x * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

struct RadianceCacheKey
{
    uint slot;
    uint checksum;
};

// jitter is a per pixel offset in cell units, it dithers the cell boundaries the denoiser then resolves
RadianceCacheKey radiance_cache_key(float3 position, float3 normal, float3 jitter)
{
    float distance = length(position - get_camera_position());
    uint  level    = (uint)clamp(floor(log2(max(distance * 0.02f / radiance_cache_cell_near, 1.0f))), 0.0f, 15.0f);
    float size     = radiance_cache_cell_near * exp2((float)level);

    // tangential jitter only, pushing a sample off its surface would read the cell of the air in front
    jitter        -= normal * dot(jitter, normal);
    int3 cell      = int3(floor(position / size + jitter));
    float3 a       = abs(normal);
    uint axis      = a.x > a.y && a.x > a.z ? 0u : (a.y > a.z ? 1u : 2u);
    uint side      = normal[axis] < 0.0f ? 1u : 0u;
    uint tag       = level | ((axis * 2u + side) << 4u);

    uint h = radiance_cache_hash(asuint(cell.x) + radiance_cache_hash(asuint(cell.y) + radiance_cache_hash(asuint(cell.z) + radiance_cache_hash(tag))));
    RadianceCacheKey key;
    key.slot     = h & (radiance_cache_capacity - 1u);
    key.checksum = radiance_cache_hash(h ^ 0x9e3779b9u ^ (asuint(cell.x) * 73856093u) ^ (asuint(cell.y) * 19349663u) ^ (asuint(cell.z) * 83492791u)) | 1u;
    return key;
}

bool radiance_cache_fresh(uint base)
{
    return (buffer_frame.frame - radiance_cache[base + 1u]) <= radiance_cache_max_age;
}

// confidence ramps from 0 for an empty cell to 1 once it holds radiance_cache_trusted samples
float3 radiance_cache_lookup(float3 position, float3 normal, float3 jitter, out float confidence)
{
    confidence           = 0.0f;
    RadianceCacheKey key = radiance_cache_key(position, normal, jitter);
    [loop]
    for (uint i = 0; i < radiance_cache_probes; i++)
    {
        uint base   = ((key.slot + i) & (radiance_cache_capacity - 1u)) * 4u;
        uint stored = radiance_cache[base];
        if (stored == 0u)
            break;
        if (stored == key.checksum)
        {
            if (!radiance_cache_fresh(base))
                break;
            uint rg    = radiance_cache[base + 2u];
            uint bn    = radiance_cache[base + 3u];
            confidence = saturate(f16tof32(bn >> 16u) / radiance_cache_trusted);
            return float3(f16tof32(rg), f16tof32(rg >> 16u), f16tof32(bn));
        }
    }
    return 0.0f;
}

void radiance_cache_update(float3 position, float3 normal, float3 jitter, float3 radiance)
{
    if (any(isnan(radiance)) || any(isinf(radiance)))
        return;

    RadianceCacheKey key = radiance_cache_key(position, normal, jitter);
    uint frame           = buffer_frame.frame;
    [loop]
    for (uint i = 0; i < radiance_cache_probes; i++)
    {
        uint base = ((key.slot + i) & (radiance_cache_capacity - 1u)) * 4u;
        uint previous;
        InterlockedCompareExchange(radiance_cache[base], 0u, key.checksum, previous);
        bool claimed = previous == 0u;
        if (!claimed && previous != key.checksum)
        {
            // a cell nobody wrote for a while is evicted, the camera left it behind
            if (radiance_cache_fresh(base))
                continue;
            uint evicted;
            InterlockedCompareExchange(radiance_cache[base], previous, key.checksum, evicted);
            if (evicted != previous)
                continue;
            claimed = true;
        }

        float  count = 0.0f;
        float3 value = 0.0f;
        if (!claimed && radiance_cache_fresh(base))
        {
            uint rg = radiance_cache[base + 2u];
            uint bn = radiance_cache[base + 3u];
            value   = float3(f16tof32(rg), f16tof32(rg >> 16u), f16tof32(bn));
            count   = f16tof32(bn >> 16u);
        }

        // a converged cell rejects a single sample far brighter than itself, the sun seen past an edge or a tiny emitter
        float value_luminance = luminance(value);
        if (count >= radiance_cache_trusted)
        {
            float limit = max(value_luminance * 16.0f, 1e-3f);
            float l     = luminance(radiance);
            radiance   *= l > limit ? limit / l : 1.0f;
        }

        count = min(count + 1.0f, radiance_cache_window);
        value = min(lerp(value, radiance, 1.0f / count), FLT_MAX_16U);

        radiance_cache[base + 1u] = frame;
        radiance_cache[base + 2u] = f32tof16(value.r) | (f32tof16(value.g) << 16u);
        radiance_cache[base + 3u] = f32tof16(value.b) | (f32tof16(count) << 16u);
        return;
    }
}

#endif
