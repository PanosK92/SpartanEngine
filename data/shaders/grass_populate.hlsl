/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =================
#include "common.hlsl"
#include "common_culling.hlsl"
//============================

// conservative world-space bound used to cull an instance against the camera frustum and the occluder
// hi-z, covers the tallest scaled instance plus wind sway and the intra-cell scatter, generous enough
// that nothing on screen is ever wrongly rejected so visible density stays identical. both are scaled
// by the slot's largest instance, a stone chip a few centimetres across does not need a metre of slack
static const float grass_cull_half_height = 0.5f;
static const float grass_cull_radius      = 1.5f;

// the populate dispatch is sized from the same numbers on the cpu, so the expected instance count has
// to sit below the range or the tail of the distribution gets clipped by the atomic and blinks
#define GRASS_FILL_MARGIN 0.85f

// gpu scatter placement (ghost of tsushima style), slot 0 is grass and the higher slots are micro
// detail, pebbles and chips, the same ring machinery with a different mesh and a tighter reach
//
// one thread per cell in a 2d ring around the camera, the ring lives entirely inside grass_instances
// each accepted cell is atomically committed into the per-lod section of the buffer and the per-lod
// counter in grass_count is bumped, the args compute later turns that count into DrawIndexedIndirect
//
// determinism: the cell grid is snapped to world space using floor(camera / cell_size), the hash
// inside each cell is keyed off world-space integer coordinates, so the same blade lands at the same
// world position regardless of the camera path. only ring-boundary cells appear and disappear.
//
// push constant layout (PassBufferData.values, 16 floats total):
//   values[0] = (cell_size, ring_radius, lod_base_in_instances, max_instances_per_lod)
//   values[1] = (height_min, height_max, max_slope_cos, inner_radius)
//   values[2] = (map_origin_x, map_origin_z, map_inv_size_x, map_inv_size_z)
//   values[3] = (patch_size_m, patch_coverage, patch_edge, patch_scar)
// a patch size of zero spreads the slot evenly, which is what this pass did before patches existed,
// and a negative one inverts the field so a slot takes the ground the others left bare
// every float is taken, so the rest travels in the bits of draw_index:
//   bits 0-3   lod ring
//   bits 4-7   scatter slot
//   bits 8-11  prop mask channel, 0 grass, 1 trees, 2 rocks
//   bits 12-19 packed instance scale at the small end of the size range
//   bits 20-27 packed instance scale at the large end
//   bits 28-31 random lean away from the surface normal, in units of three degrees
// camera xz comes from buffer_frame
// terrain height is r32 local y bound to tex (t7), material_index bitcast is the entity y plus the
// layer's seating offset, a negative offset is what pushes an instance down into the ground
// biome_min arrives asfloat(is_transparent), negative disables the gate

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
// hard ceiling on how much the thread count may grow to refill the patches, a coverage slider near
// zero would otherwise ask for an unbounded dispatch
static const float grass_patch_max_boost = 4.0f;
// ceiling on the patch and frustum boosts combined, this is what bounds the dispatch, every thread
// past it would be work the atomic cap refuses to accept anyway
static const float grass_max_boost = 12.0f;
// how far above its own gate the biome mask has to climb before a slot runs at full density, a
// narrow band here is what keeps meadow cores thick while the mask edges still fade out
static const float grass_biome_gain = 0.25f;

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
    float  scar,
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
    float e     = max(edge * 0.5f, 1e-3f);
    float w     = smoothstep(t - e, t + e, u);
    if (invert)
    {
        w = 1.0f - w;
    }

    // bare scars. real ground inside a meadow is broken by paths, outcrops and dry spots, and
    // without them a patch interior reads as a printed texture no matter how good its edge is.
    // applied after the flip so an inverted slot gets broken up too, and so the average cost is the
    // same either way and the density compensation stays one formula
    if (scar > 0.0f)
    {
        float s = grass_gaussian_cdf(
            grass_fbm(world_xz * inv * 3.7f + 31.0f, grass_patch_seed + 1409u) / grass_patch_sigma
        );
        w *= 1.0f - scar * smoothstep(0.62f, 0.86f, s);
    }

    return w;
}

// what fraction of the eligible ground survives the patch field on average. the threshold keeps
// coverage exactly, and the scar field independently removes the mean of its own smoothstep, which
// for the 0.62 to 0.86 band above is 0.26
float grass_patch_coverage_expected(float patch_size, float coverage, float scar)
{
    if (patch_size <= 0.0f || coverage >= 1.0f)
    {
        return 1.0f;
    }

    return coverage * (1.0f - 0.26f * scar);
}

// pulling grass into pockets empties most of the ring, so the same budget spread over what is left
// is what turns a thin even carpet into a thick patchy one. this is the whole reason clustering buys
// density rather than costing it
float grass_patch_density_boost(float patch_size, float coverage, float scar)
{
    float expected = grass_patch_coverage_expected(patch_size, coverage, scar);

    return min(1.0f / max(expected, 0.08f), grass_patch_max_boost);
}

// the ring is a full circle, the camera sees a wedge of it, and the cull below runs before the atomic
// allocates, so everything behind the viewer spent budget that was then thrown away and the counter
// never came close to its cap. handing the wedge what the rest of the circle cannot use is what makes
// the cover thick, and it is the largest single lever on density there is
//
// the visible share is deliberately over estimated so the boost lands under the truth, overshooting
// saturates the atomic and the cover then stops dead partway across the view
float grass_frustum_density_boost()
{
    float wedge = clamp(buffer_frame.camera_fov, 0.35f, PI2);

    // the wedge widens toward the whole circle as the camera pitches down, and the near ring is in
    // view whichever way it points, so the visible share never drops below a fifth
    float visible = clamp((wedge * 1.6f) / PI2, 0.18f, 1.0f);

    return 1.0f / visible;
}

// 0 at the terrain's world minimum, 1 at its maximum, the range test lives in this space
float2 terrain_world_to_normalized(float2 world_xz)
{
    float2 origin   = buffer_pass.values[2].xy;
    float2 inv_size = buffer_pass.values[2].zw;
    return (world_xz - origin) * inv_size;
}

// the terrain grid stores one sample per texel, sample 0 at the world minimum and sample n-1 at the
// maximum, so it spans n-1 intervals while the texture spans n. a fetch puts those samples on the
// texel centres, which means the normalized position has to be rebased onto them
//
// without this every tap reads half a texel toward -x and -z, and a texel is the terrain grid spacing,
// 25 m by default, so the blade takes its height from a completely different part of the slope
float2 terrain_normalized_to_uv(float2 normalized, float2 texture_size)
{
    return (normalized * (texture_size - 1.0f) + 0.5f) / texture_size;
}

// world y of the heightfield under this xz, reconstructed the way the terrain mesh triangulates
// its cells. a bilinear tap reads the twist of the quad instead of the triangle plane, and on a
// twenty five metre cell that misses the rendered surface by enough to leave every chip floating
float sample_terrain_height(float2 world_xz, float2 height_size, out float valid)
{
    float2 normalized = terrain_world_to_normalized(world_xz);

    if (any(normalized < 0.0f) || any(normalized > 1.0f))
    {
        valid = 0.0f;
        return 0.0f;
    }

    valid = 1.0f;

    float2 texels = max(height_size - 1.0f, 1.0f);
    float2 grid   = normalized * texels;
    float2 base   = min(floor(grid), texels - 1.0f);
    float2 f      = saturate(grid - base);

    float2 rcp_size = 1.0f / max(height_size, 1.0f);
    float h00 = tex.SampleLevel(samplers[sampler_point_clamp], (base + float2(0.5f, 0.5f)) * rcp_size, 0).r;
    float h10 = tex.SampleLevel(samplers[sampler_point_clamp], (base + float2(1.5f, 0.5f)) * rcp_size, 0).r;
    float h01 = tex.SampleLevel(samplers[sampler_point_clamp], (base + float2(0.5f, 1.5f)) * rcp_size, 0).r;
    float h11 = tex.SampleLevel(samplers[sampler_point_clamp], (base + float2(1.5f, 1.5f)) * rcp_size, 0).r;

    return (f.x + f.y <= 1.0f)
        ? h00 + f.x * (h10 - h00) + f.y * (h01 - h00)
        : h11 + (1.0f - f.x) * (h01 - h11) + (1.0f - f.y) * (h10 - h11);
}

// two forward taps for the surface slope, reuses the centre height so a surviving blade pays three terrain
// samples total (one centre in sample_terrain_height plus these two) instead of five
float3 sample_terrain_normal(
    float2 world_xz,
    float2 height_size,
    float y_c
)
{
    float2 uv = terrain_normalized_to_uv(terrain_world_to_normalized(world_xz), height_size);

    float2 texel_uv = 1.0f / max(height_size, 1.0f);

    // world distance covered by one texel step, the grid spans n-1 intervals not n
    float2 inv_size      = buffer_pass.values[2].zw;
    float2 world_per_tap = 1.0f / (max(inv_size, 1e-8f) * max(height_size - 1.0f, 1.0f));

    float y_r = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(texel_uv.x, 0.0f), 0).r;
    float y_u = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0.0f, texel_uv.y), 0).r;

    float dy_dx = (y_r - y_c) / world_per_tap.x;
    float dy_dz = (y_u - y_c) / world_per_tap.y;

    return normalize(float3(-dy_dx, 1.0f, -dy_dz));
}

// pack a position-yaw-scale-normal tuple into the 16-byte GrassInstance layout
// world position is kept at full float32 precision, this is the only thing that lets the
// populate shader's sub-meter random scatter survive without snapping onto a half-float
// world-space lattice at typical play distances
GrassInstance build_grass_instance(float3 position, float3 normal, float yaw_01, float scale_01)
{
    GrassInstance gi;
    gi.pos_x = position.x;
    gi.pos_y = position.y;
    gi.pos_z = position.z;

    // octahedral pack the surface up vector, the raster vs feeds this back into
    // compose_instance_transform which expects the same unorm8 xy with z reconstructed
    float3 n_abs = abs(normal);
    float  sum   = n_abs.x + n_abs.y + n_abs.z;
    float2 oct   = normal.xy / max(sum, 1e-6f);
    if (normal.z < 0.0f)
    {
        float ox = oct.x;
        oct.x = (1.0f - abs(oct.y)) * (ox >= 0.0f ? 1.0f : -1.0f);
        oct.y = (1.0f - abs(ox))    * (oct.y >= 0.0f ? 1.0f : -1.0f);
    }
    uint ox = (uint)round((oct.x * 0.5f + 0.5f) * 255.0f);
    uint oy = (uint)round((oct.y * 0.5f + 0.5f) * 255.0f);
    uint normal_oct = (ox << 8) | oy;

    uint yaw_p   = (uint)round(saturate(yaw_01)   * 255.0f);
    uint scale_p = (uint)round(saturate(scale_01) * 255.0f);

    gi.normal_yaw_scale = (normal_oct << 16) | (yaw_p << 8) | scale_p;
    return gi;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    // unpack the push constant payload
    float cell_size             = buffer_pass.values[0].x;
    float ring_radius           = buffer_pass.values[0].y;
    uint  lod_base              = (uint)buffer_pass.values[0].z;
    uint  max_instances_per_lod = (uint)buffer_pass.values[0].w;

    float height_min    = buffer_pass.values[1].x;
    float height_max    = buffer_pass.values[1].y;
    float max_slope_cos = buffer_pass.values[1].z;
    float inner_radius  = buffer_pass.values[1].w;

    // a negative size is the slot asking for the complement of the patch field, the magnitude is
    // still the pocket scale and has to match the slot it is inverting or the two will not interlock
    float patch_size_signed = buffer_pass.values[3].x;
    float patch_size        = abs(patch_size_signed);
    bool  patch_invert      = patch_size_signed < 0.0f;
    float patch_coverage    = saturate(buffer_pass.values[3].y);
    float patch_edge        = saturate(buffer_pass.values[3].z);
    float patch_scar        = saturate(buffer_pass.values[3].w);

    uint  packed_index  = buffer_pass.draw_index;
    uint  lod_index     = packed_index & 0xFu;
    uint  slot_index    = (packed_index >> 4)  & 0xFu;
    uint  mask_channel  = (packed_index >> 8)  & 0xFu;
    float scale_01_min  = float((packed_index >> 12) & 0xFFu) / 255.0f;
    float scale_01_max  = float((packed_index >> 20) & 0xFFu) / 255.0f;
    float tilt_deg      = float((packed_index >> 28) & 0xFu) * (45.0f / 15.0f);

    // the counters and the indirect args are laid out slot major, matches renderer_gpu_scatter_arg_index
    uint  count_index   = slot_index * 3u + lod_index;

    // the cull sphere has to grow with the instance, compose_instance_transform maps the packed scale
    // logarithmically over 0.01 to 100 and the raster will unpack it exactly the same way
    float largest_scale = exp2(lerp(-6.643856f, 6.643856f, scale_01_max));
    float cull_radius   = grass_cull_radius * largest_scale;
    float cull_height   = grass_cull_half_height * largest_scale;

    float2 camera_xz_anchor = get_camera_position().xz;

    // stratified scatter: the world is divided into a grid of cells, each cell holds blades_per_cell
    // blades, and each blade is placed at a uniformly random position INSIDE the cell, independently of
    // every other blade. each cell is a stratum, blades inside a stratum are pure random scatter so
    // there is no per-blade lattice, blades between strata add up to a globally uniform density so
    // there is no per-cell lattice either, this is the cheapest stable approximation of a poisson disk
    // distribution that produces even coverage at any view angle. blade_index lives in dispatch_thread_id.z
    // so cells_per_axis stays a simple 2d grid sized to the ring
    uint cells_per_axis = (uint)(
        2.0f * ceil(ring_radius / cell_size)
    ) + 2u;
    float ring_area      = PI * (
        ring_radius * ring_radius -
        inner_radius * inner_radius
    );
    float cells_in_ring  = ring_area / max(cell_size * cell_size, 1e-6f);

    // the patches reject most of the ring, so the cell has to be handed proportionally more threads
    // or the budget is simply lost and clustering costs density instead of buying it. the cpu sizes
    // dispatch z from the identical expression
    float patch_boost = grass_patch_density_boost(patch_size, patch_coverage, patch_scar);

    // the same argument applies to the part of the circle the camera cannot see, and the two multiply
    float total_boost = min(patch_boost * grass_frustum_density_boost(), grass_max_boost);

    // the budget divided by the cells is rarely a whole number, flooring it threw the remainder away
    // and turned the fill slider into a staircase, 1 instance per cell then 2 with nothing in between.
    // ceil the count and carry the fraction as a keep probability instead, so density is continuous
    // and two rings with different cell sizes can be tuned to the same instances per square metre
    float per_cell        = float(max_instances_per_lod) * GRASS_FILL_MARGIN * total_boost /
                            max(cells_in_ring, 1.0f);
    uint  blades_per_cell = max(1u, (uint)ceil(per_cell));
    float cell_keep       = saturate(per_cell / float(blades_per_cell));
    if (dispatch_thread_id.x >= cells_per_axis ||
        dispatch_thread_id.y >= cells_per_axis ||
        dispatch_thread_id.z >= blades_per_cell)
        return;

    // signed cell index in the camera-relative grid, centered on the snapped camera position
    int half_cells = (int)(cells_per_axis / 2u);
    int cell_x_s   = (int)dispatch_thread_id.x - half_cells;
    int cell_z_s   = (int)dispatch_thread_id.y - half_cells;

    // integer world coordinates of the cell, stable as the camera moves
    int world_cell_x = (int)floor(camera_xz_anchor.x / cell_size) + cell_x_s;
    int world_cell_z = (int)floor(camera_xz_anchor.y / cell_size) + cell_z_s;

    // hash combines the cell coordinates, the blade index inside the cell, and the lod seed,
    // each (cell, blade) pair gets an independent stable hash, no two blades collide and there is
    // no per-cell pattern because blade_index decorrelates blades that share a cell
    // the slot is part of the seed, otherwise every slot would stack its instances on the exact same
    // points and a pebble would be born inside every blade of grass
    uint blade_index = dispatch_thread_id.z;
    uint h0  = hash_u32(
        (uint)world_cell_x,
        (uint)world_cell_z,
        lod_index * 2654435761u + blade_index * 0x9e3779b9u + slot_index * 0x85ebca6bu
    );
    // each roll is a fresh avalanche of the one before it, so the streams are independent rather than
    // linear images of each other, which is what keeps the in cell positions a scatter and not a grid
    uint h1 = hash_mix(h0 ^ 0x9e3779b9u);
    uint h2 = hash_mix(h1 ^ 0x85ebca6bu);
    uint h3 = hash_mix(h2 ^ 0xc2b2ae35u);

    float jx = hash_unit(h0);
    float jz = hash_unit(h1);
    float ys = hash_unit(h2);
    float sc = hash_unit(h3);

    // uniform random position inside the cell, the cell is the stratum, every blade in a cell rolls
    // its own jx/jz so the cell interior is filled with pure random scatter rather than a single
    // jittered grid point, which is what kills the doll-hair look
    float2 cell_origin_xz = float2(world_cell_x, world_cell_z) * cell_size;
    float2 world_xz       = cell_origin_xz + float2(jx, jz) * cell_size;

    // wide overlapping fades, then warp the radius with low frequency noise so the lod
    // seam is a blob instead of a stamped circle
    float inner_transition = max(
        cell_size * 10.0f,
        inner_radius * 0.5f
    );
    float outer_transition = max(
        cell_size * 10.0f,
        ring_radius * 0.42f
    );
    float inner_start = max(
        0.0f,
        inner_radius - inner_transition
    );
    float outer_start = max(
        inner_radius,
        ring_radius - outer_transition
    );

    float2 to_camera = world_xz - camera_xz_anchor;
    float  dist2     = dot(to_camera, to_camera);
    float  reject_outer = ring_radius + outer_transition * 0.15f;
    if (
        dist2 < inner_start * inner_start ||
        dist2 > reject_outer * reject_outer
    )
    {
        return;
    }

    float distance_to_camera = sqrt(dist2);
    float warp_n = grass_value_noise(world_xz * (1.0f / 34.0f), 91u) * 0.7f
                 + grass_value_noise(world_xz * (1.0f / 13.0f), 53u) * 0.3f;
    float warp_amp = max(inner_transition, outer_transition) * 0.55f;
    float distance_warped = distance_to_camera + warp_n * warp_amp;

    float fade_in = inner_radius > 0.0f ?
        smoothstep(
            inner_start,
            inner_radius,
            distance_warped
        ) :
        1.0f;
    float fade_out = 1.0f - smoothstep(
        outer_start,
        ring_radius,
        distance_warped
    );
    // the patch field, evaluated before any texture tap so the threads it rejects are the cheapest
    // ones in the pass, which is most of what pays for the boosted thread count above. it is keyed
    // off world space only, never off the lod or the camera, so a pocket keeps the same outline
    // across every ring and the lod seams stay invisible
    float patch = grass_patch_weight(
        world_xz,
        patch_size,
        patch_coverage,
        patch_edge,
        patch_scar,
        patch_invert
    );

    float lod_weight = fade_in * fade_out * cell_keep * patch;
    float lod_random = hash_unit(hash_mix(h0 ^ 0xa511e9b3u));
    if (lod_random > lod_weight)
    {
        return;
    }

    // height reject, single centre tap, also yields world_y for the visibility cull below
    uint height_w;
    uint height_h;
    tex.GetDimensions(height_w, height_h);
    float2 height_size = float2(height_w, height_h);

    float valid;
    float local_y = sample_terrain_height(world_xz, height_size, valid);
    float seat_y  = asfloat(buffer_pass.material_index);
    float world_y = local_y + seat_y;
    if (valid < 0.5f || world_y < height_min || world_y > height_max)
    {
        return;
    }

    // visibility cull, frustum + occluder hi-z on a conservative blade sphere, only blades the camera
    // can actually see survive so on-screen density is unchanged while off-screen and occluded blades
    // are dropped before the expensive slope taps, vertex shading and the atomic allocate
    float3 blade_center = float3(world_xz.x, world_y + cull_height, world_xz.y);
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);
    if (!sphere_in_side_planes(blade_center, cull_radius, plane_l, plane_r, plane_b, plane_t))
        return;

    // hi-z arrives on tex2, derive the max mip from the texture so no extra push constant is needed
    // the sphere test also rejects blades behind the camera (the side planes alone do not)
    uint hiz_w, hiz_h, hiz_mips;
    tex2.GetDimensions(0, hiz_w, hiz_h, hiz_mips);
    if (!sphere_hiz_visible(tex2, blade_center, cull_radius, float(hiz_mips - 1u)))
        return;

    // slope reject, two forward taps reusing the centre height, paid only by visible blades
    float3 surface_normal = sample_terrain_normal(
        world_xz,
        height_size,
        local_y
    );
    if (surface_normal.y < max_slope_cos)
    {
        return;
    }

    // biome mask, same uv as the heightfield, the slot picks the channel it is gated on
    // is_transparent carries biome_min as float bits, negative disables the gate
    float biome_min = asfloat(buffer_pass.is_transparent);
    if (biome_min >= 0.0f)
    {
        // the mask is baked at its own resolution, so it needs its own texel centre rebase
        uint mask_w;
        uint mask_h;
        tex3.GetDimensions(mask_w, mask_h);

        float2 mask_uv = terrain_normalized_to_uv(
            terrain_world_to_normalized(world_xz),
            float2(mask_w, mask_h)
        );
        float4 mask     = tex3.SampleLevel(samplers[sampler_bilinear_clamp], mask_uv, 0);
        float grass_w   = mask_channel == 0u ? mask.r : (mask_channel == 1u ? mask.g : mask.b);
        float slope_fit = saturate((surface_normal.y - max_slope_cos) / max(1.0f - max_slope_cos, 1e-4f));
        if (grass_w < biome_min)
        {
            return;
        }

        // the mask decides where grass can live, the patch field above decides where it did, so the
        // mask is remapped to saturate just past its own gate instead of being used as the density
        // directly. a meadow core reading 0.6 used to throw away four blades in ten for nothing
        float ground = saturate((grass_w - biome_min) / grass_biome_gain) * slope_fit;
        float biome_roll = hash_unit(hash_mix(h0 ^ 0x27d4eb2du));
        if (biome_roll > ground)
        {
            return;
        }
    }

    // one roll inside the authored size range, the ends arrive already packed the way the raster
    // unpacks them so no size ever leaves the range the layer asked for
    float scale_01 = lerp(scale_01_min, scale_01_max, sc);

    // a pocket that keeps full height right up to its edge reads as mown, so the fringe tapers back
    // toward the small end of the range. the packed scale is logarithmic, so this interpolates the
    // exponent and the blades shrink geometrically, which is how a tuft actually thins out
    scale_01 = lerp(scale_01_min, scale_01, saturate(patch * 1.4f));

    // a stone that has come to rest is not flat on the ground. the lean goes on after the slope gate so
    // it cannot smuggle an instance onto a cliff, and the mesh sits low enough that the raised edge
    // stays seated. this is most of what separates a field of gravel from a field of stickers
    if (tilt_deg > 0.0f)
    {
        float2 lean = float2(
            hash_unit(hash_mix(h0 ^ 0x1b56c4e9u)),
            hash_unit(hash_mix(h0 ^ 0x632be5abu))
        ) * 2.0f - 1.0f;

        surface_normal = normalize(
            surface_normal + float3(lean.x, 0.0f, lean.y) * tan(radians(tilt_deg))
        );
    }

    // atomic-allocate a slot inside this lod's section, bail out cleanly once full
    // blades_per_cell was sized from cells_in_ring with floor() so the upper bound on writes is
    // floor(cap / cells_in_ring) * cells_in_ring <= cap, the clamp here is just a defensive guard
    uint local_slot;
    InterlockedAdd(grass_count[count_index], 1u, local_slot);
    if (local_slot >= max_instances_per_lod)
        return;

    uint global_slot = lod_base + local_slot;
    grass_instances[global_slot] = build_grass_instance(
        float3(world_xz.x, world_y, world_xz.y),
        surface_normal,
        ys,
        scale_01
    );
}
