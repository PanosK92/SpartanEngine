/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_GRASS_PATCH
#define SPARTAN_GRASS_PATCH

// the patch field grass grows in, shared by the blade populate pass and the far field terrain tint so
// both agree on where every pocket of grass is, keyed off world xz only

// 32-bit integer hash, takes the cell's integer world coords and returns a uniform 32-bit value
// keyed off coordinates that do not move with the camera, so blade placement is stable
uint hash_u32(uint x, uint y, uint seed)
{
    uint h = (x * 73856093u) ^ (y * 19349663u) ^ (seed * 83492791u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

float hash_unit(uint h)
{
    return float(h) * (1.0f / 4294967296.0f);
}

// avalanche one hash into an independent one. deriving a second random by multiplying or xoring the
// first is not enough, a multiply only carries bits upward and an xor flips fixed ones, so the second
// value stays a linear function of the first and the pair lands on a rank 1 lattice, which draws the
// scatter as evenly spaced parallel rows the moment a cell holds more than a handful of instances
uint hash_mix(uint h)
{
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return h;
}

// bilinear value noise, used to warp lod ring distances into blobs
float grass_value_noise(float2 p, uint seed)
{
    int2 i = int2(floor(p));
    float2 f = frac(p);
    f = f * f * (3.0f - 2.0f * f);
    float a = hash_unit(hash_u32((uint)i.x,      (uint)i.y,      seed));
    float b = hash_unit(hash_u32((uint)i.x + 1u, (uint)i.y,      seed));
    float c = hash_unit(hash_u32((uint)i.x,      (uint)i.y + 1u, seed));
    float d = hash_unit(hash_u32((uint)i.x + 1u, (uint)i.y + 1u, seed));
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y) * 2.0f - 1.0f;
}

// grass does not carpet a hillside evenly. it takes ground in pockets, thick through the middle and
// frayed at the edge, because soil depth, moisture, shelter and grazing vary far faster than any of
// the terrain analysis channels can see. the biome mask decides where grass can live, everything
// below decides where it actually took hold
//
// one seed for every slot, not one per slot, so two slots authored at the same patch size interlock
// rather than drifting apart. that is what lets the pebbles invert the grass and land exactly on the
// bare ground between the tufts
static const uint  grass_patch_seed  = 0x5f3a91u;
// standard deviation of the four octave fbm below, the octave amplitudes are fixed so this is a
// constant, it only has to be close because the threshold feather absorbs the error
static const float grass_patch_sigma = 0.25f;
// how far above its own gate the biome mask has to climb before a slot runs at full density, a
// narrow band here is what keeps meadow cores thick while the mask edges still fade out. grass is
// thousands of individual blades, it only reads as a field at full density, so a wide ramp here
// left whole meadows sitting on a mid mask value half thinned
static const float grass_biome_gain = 0.06f;
// width of the pocket fringe in the uniform patch space, per unit of edge. the threshold alone sets
// the outline, this only decides how far either side of it the density ramps, and anything wide
// turns most of a pocket into fringe so its core never reaches full thickness
static const float grass_patch_fringe = 0.08f;
// the slope gate fades density over this many radians below the slope ceiling instead of across the
// whole band, a gentle hillside is still a meadow and has to be as thick as the flat ground
static const float grass_slope_fade = 0.07f;
// terrain_layer_max, the dominant layer index in the mask alpha can never exceed this
static const uint grass_ground_layer_max = 8u;

// four octaves, the fewest that still reads as an organic outline rather than a blob
float grass_fbm(float2 p, uint seed)
{
    return grass_value_noise(p,          seed)       * 0.5333f +
           grass_value_noise(p * 2.03f,  seed + 17u) * 0.2667f +
           grass_value_noise(p * 4.11f,  seed + 53u) * 0.1333f +
           grass_value_noise(p * 8.07f,  seed + 97u) * 0.0667f;
}

// the fbm is a sum of independent octaves so it lands close to a normal distribution, pushing it
// through the cdf gives a value uniform on 0 to 1. that is the only thing that makes the coverage
// number below mean the fraction of ground the patches actually take, which in turn is what lets the
// density compensation on the cpu be honest instead of a guess
float grass_gaussian_cdf(float x)
{
    return saturate(1.0f / (1.0f + exp(-1.702f * x)));
}

// fraction of this point's budget the patch structure keeps, 1 deep inside a pocket and 0 on bare
// ground, with a fringe in between. coverage means the same thing whether the slot is inverted or
// not, it is always the share of ground this slot ends up taking
float grass_patch_weight(
    float2 world_xz,
    float  patch_size,
    float  coverage,
    float  edge,
    bool   invert
)
{
    // a slot that asked for no patches, or one asked to cover everything, spreads evenly
    if (patch_size <= 0.0f || coverage >= 1.0f)
    {
        return 1.0f;
    }

    float inv = 1.0f / patch_size;

    // domain warp. thresholding a plain fbm gives rounded blobs with a smooth outline, pushing the
    // sample point around with a second low frequency field is what frays the boundary into
    // something that reads as grown rather than stamped
    float2 warp = float2(
        grass_value_noise(world_xz * inv * 0.61f, grass_patch_seed + 811u),
        grass_value_noise(world_xz * inv * 0.61f, grass_patch_seed + 977u)
    );
    float2 p = world_xz * inv + warp * 0.55f;

    float u = grass_gaussian_cdf(
        grass_fbm(p, grass_patch_seed) / grass_patch_sigma
    );

    // u is uniform, so thresholding at 1 - coverage passes exactly that fraction of the ground and
    // the feather either side costs nothing on average, it only turns the boundary into a fringe.
    // an inverted slot asks for the complement first and flips, which lands it on the same threshold
    // as the slot it mirrors, so the two tile the ground with no gap and no overlap
    float share = invert ? (1.0f - coverage) : coverage;
    float t     = 1.0f - share;
    float e     = max(edge * grass_patch_fringe, 1e-3f);
    float w     = smoothstep(t - e, t + e, u);
    if (invert)
    {
        w = 1.0f - w;
    }

    return w;
}

#endif
