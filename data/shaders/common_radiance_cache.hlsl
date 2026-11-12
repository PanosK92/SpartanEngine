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
// a cell also holds the light its surface receives straight from the lights, shadows included, and how much of the
// hemisphere its bounce rays found open to the sky, a reflection lobe wider than the cell cannot resolve more than that,
// so its hits read all of their lighting from the cell and trace nothing
// an entry is 6 uints, key checksum (0 is empty), last frame touched, bounced rg f16, bounced b f16 | sample count 6 bits |
// sky openness 10 bits, direct rg f16, direct b f16
// the buffer is zeroed at creation, keys are claimed with a compare exchange, the radiance itself is
// written without atomics, a lost race only drops one sample of a moving average

// capacity * stride has to fit the buffer renderer_resources.cpp creates, 2^21 uints
RWStructuredBuffer<uint> radiance_cache : register(u70);

static const uint  radiance_cache_capacity  = 1u << 18;
static const uint  radiance_cache_stride    = 6;
static const uint  radiance_cache_probes    = 8;
static const uint  radiance_cache_max_age   = 300;   // frames a cell survives without being written
static const uint  radiance_cache_idle_age  = 32;    // frames unwritten before a cell may give its slot to a new one in a full table
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

// cells double in size with every doubling of the camera distance past 12.5 m
uint radiance_cache_level(float camera_distance)
{
    return (uint)clamp(floor(log2(max(camera_distance * 0.02f / radiance_cache_cell_near, 1.0f))), 0.0f, 15.0f);
}

// edge length in metres of the cell a world position falls in
float radiance_cache_cell_size(float3 position)
{
    return radiance_cache_cell_near * exp2((float)radiance_cache_level(length(position - get_camera_position())));
}

// jitter is a per pixel offset in cell units, it dithers the cell boundaries the denoiser then resolves
RadianceCacheKey radiance_cache_key(float3 position, float3 normal, float3 jitter)
{
    float distance = length(position - get_camera_position());
    uint  level    = radiance_cache_level(distance);
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

// returns the bounced light, direct is what the lights deliver, both as the radiance a lambert surface multiplies its albedo with
// confidence ramps from 0 for an empty cell to 1 once it holds radiance_cache_trusted samples
float3 radiance_cache_lookup(float3 position, float3 normal, float3 jitter, out float confidence, out float sky_openness, out float3 direct)
{
    confidence           = 0.0f;
    sky_openness         = 1.0f;
    direct               = 0.0f;
    RadianceCacheKey key = radiance_cache_key(position, normal, jitter);
    [loop]
    for (uint i = 0; i < radiance_cache_probes; i++)
    {
        uint base   = ((key.slot + i) & (radiance_cache_capacity - 1u)) * radiance_cache_stride;
        uint stored = radiance_cache[base];
        if (stored == 0u)
            break;
        if (stored == key.checksum)
        {
            if (!radiance_cache_fresh(base))
                break;
            uint rg    = radiance_cache[base + 2u];
            uint bn    = radiance_cache[base + 3u];
            confidence   = saturate(float((bn >> 16u) & 63u) / radiance_cache_trusted);
            sky_openness = float(bn >> 22u) / 1023.0f;
            uint d_rg    = radiance_cache[base + 4u];
            direct       = float3(f16tof32(d_rg), f16tof32(d_rg >> 16u), f16tof32(radiance_cache[base + 5u]));
            return float3(f16tof32(rg), f16tof32(rg >> 16u), f16tof32(bn));
        }
    }
    return 0.0f;
}

// folds one sample into the entry at base, an entry that was just claimed starts from nothing
void radiance_cache_write(uint base, bool claimed, float3 radiance, float sky_openness, float3 direct)
{
    float  count = 0.0f;
    float3 value = 0.0f;
    float3 lit   = 0.0f;
    float  open  = 0.0f;
    if (!claimed && radiance_cache_fresh(base))
    {
        uint rg = radiance_cache[base + 2u];
        uint bn = radiance_cache[base + 3u];
        value   = float3(f16tof32(rg), f16tof32(rg >> 16u), f16tof32(bn));
        count   = float((bn >> 16u) & 63u);
        open    = float(bn >> 22u) / 1023.0f;
        uint d  = radiance_cache[base + 4u];
        lit     = float3(f16tof32(d), f16tof32(d >> 16u), f16tof32(radiance_cache[base + 5u]));
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
    open  = saturate(lerp(open, sky_openness, 1.0f / count));
    lit   = min(lerp(lit, direct, 1.0f / count), FLT_MAX_16U);

    radiance_cache[base + 1u] = buffer_frame.frame;
    radiance_cache[base + 2u] = f32tof16(value.r) | (f32tof16(value.g) << 16u);
    radiance_cache[base + 3u] = f32tof16(value.b) | (uint(count) << 16u) | (uint(open * 1023.0f + 0.5f) << 22u);
    radiance_cache[base + 4u] = f32tof16(lit.r) | (f32tof16(lit.g) << 16u);
    radiance_cache[base + 5u] = f32tof16(lit.b);
}

// sky_openness is 1 when the bounce ray that gathered the radiance left the scene, 0 when it hit something
void radiance_cache_update(float3 position, float3 normal, float3 jitter, float3 radiance, float sky_openness, float3 direct)
{
    if (any(isnan(radiance)) || any(isinf(radiance)) || any(isnan(direct)) || any(isinf(direct)))
        return;

    RadianceCacheKey key = radiance_cache_key(position, normal, jitter);
    uint frame           = buffer_frame.frame;
    uint idle_base       = 0u;
    uint idle_key        = 0u;
    uint idle_age        = radiance_cache_idle_age;
    [loop]
    for (uint i = 0; i < radiance_cache_probes; i++)
    {
        uint base = ((key.slot + i) & (radiance_cache_capacity - 1u)) * radiance_cache_stride;
        uint previous;
        InterlockedCompareExchange(radiance_cache[base], 0u, key.checksum, previous);
        bool claimed = previous == 0u;
        if (!claimed && previous != key.checksum)
        {
            // a cell nobody wrote for a while is evicted, the camera left it behind, among the ones still
            // alive the one left alone the longest is remembered
            uint age = frame - radiance_cache[base + 1u];
            if (age <= radiance_cache_max_age)
            {
                if (age > idle_age)
                {
                    idle_age  = age;
                    idle_base = base;
                    idle_key  = previous;
                }
                continue;
            }
            uint evicted;
            InterlockedCompareExchange(radiance_cache[base], previous, key.checksum, evicted);
            if (evicted != previous)
                continue;
            claimed = true;
        }

        radiance_cache_write(base, claimed, radiance, sky_openness, direct);
        return;
    }

    // every probe holds a live cell, the camera moved on faster than cells age out, what is in view is written
    // every few frames, so the cell that has gone unwritten the longest makes room and a new view does not
    // wait on the one before it
    if (idle_key != 0u)
    {
        uint evicted;
        InterlockedCompareExchange(radiance_cache[idle_base], idle_key, key.checksum, evicted);
        if (evicted == idle_key)
        {
            radiance_cache_write(idle_base, true, radiance, sky_openness, direct);
        }
    }
}

#endif
