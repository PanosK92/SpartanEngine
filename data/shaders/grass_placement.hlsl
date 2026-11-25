/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_GRASS_PLACEMENT
#define SPARTAN_GRASS_PLACEMENT

//= INCLUDES =================
#include "common.hlsl"
#include "common_culling.hlsl"
#include "grass_distribution.hlsl"
#include "grass_patch.hlsl"
#include "terrain_habitat.h"
//============================

// gpu scatter placement, every test that decides whether a (cell, blade) candidate becomes an instance,
// kept apart from the populate compute that commits the survivors into grass_instances

// Per-slot: blade height at unit scale, output-pixel budget.
// Last element protects the live car and its recent tire-pressure field.
StructuredBuffer<float4> grass_lod_parameters : register(t62);

// Exact projected road triangles, indexed by a world-space grid. The coarse
// biome mask stays intact outside roads, even when a road is narrower than one texel.
StructuredBuffer<float4> terrain_road_exclusions : register(t63);
bool on_road(float2 position)
{
    const float4 mapping = terrain_road_exclusions[0];
    const uint2 size = uint2(terrain_road_exclusions[1].xy);
    if (any(size == 0u)) return false;
    const float2 uv = (position - mapping.xy) * mapping.zw;
    if (any(uv < 0.0f) || any(uv > 1.0f)) return false;
    const uint2 cell = min(uint2(uv * size), size - 1u);
    const uint2 range = uint2(terrain_road_exclusions[2u + cell.y * size.x + cell.x].xy);
    for (uint i = 0; i < range.y; ++i)
    {
        const float4 ab = terrain_road_exclusions[range.x + i * 2u];
        const float2 c = terrain_road_exclusions[range.x + i * 2u + 1u].xy;
        const float e0 = (ab.z - ab.x) * (position.y - ab.y) - (ab.w - ab.y) * (position.x - ab.x);
        const float e1 = (c.x - ab.z) * (position.y - ab.w) - (c.y - ab.w) * (position.x - ab.z);
        const float e2 = (ab.x - c.x) * (position.y - c.y) - (ab.y - c.y) * (position.x - c.x);
        if ((e0 >= -1e-4f && e1 >= -1e-4f && e2 >= -1e-4f) || (e0 <= 1e-4f && e1 <= 1e-4f && e2 <= 1e-4f)) return true;
    }
    return false;
}

// conservative world-space bound used to cull an instance against the camera frustum and the occluder
// hi-z, covers the tallest scaled instance plus wind sway and the intra-cell scatter, generous enough
// that nothing on screen is ever wrongly rejected so visible density stays identical. both are scaled
// by the slot's largest instance, a stone chip a few centimetres across does not need a metre of slack
static const float grass_cull_half_height = 0.5f;
static const float grass_cull_radius      = 1.5f;

// the populate dispatch is sized from the same numbers on the cpu, so the expected instance count has
// to sit below the range or the tail of the distribution gets clipped by the atomic and blinks
#define GRASS_FILL_MARGIN 0.85f

// squares without overflowing, a band that never ends
static const float grass_band_unbounded = 1e18f;
// candidates of one cell a blade ring thread walks, has to match gpu_scatter_blades_per_thread
static const uint  grass_blades_per_thread = 4u;

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
// push constant layout (PassBufferData.values, 20 floats total):
//   values[0] = (cell_size, ring_radius, lod_base_in_instances, max_instances_per_lod)
//   values[1] = (height_min, height_max, max_slope_cos, inner_radius)
//   values[2] = (map_origin_x, map_origin_z, map_inv_size_x, map_inv_size_z)
//   values[3] = (patch_size_m, patch_coverage, patch_edge, ground_mask bits)
//   values[4] = (min_slope_cos, slope_bias, height_fade, optional grass root radius)
// a patch size of zero spreads the slot evenly, which is what this pass did before patches existed,
// and a negative one inverts the field so a slot takes the ground the others left bare
// the first four float4s are taken, so the rest travels in the bits of draw_index:
//   bits 0-1   lod ring
//   bits 2-3   card band, the block of cells a card ring thread walks is 2^band cells wide
//   bits 4-7   scatter slot
//   bits 8-11  prop mask channel, 0 grass, 1 trees, 2 rocks
//   bits 12-19 packed instance scale at the small end of the size range
//   bits 20-27 packed instance scale at the large end
//   bits 28-31 random lean away from the surface normal, in units of three degrees
// camera xz comes from buffer_frame
// terrain height is r32 local y bound to tex (t7), material_index bitcast is the entity y plus the
// layer's seating offset, a negative offset is what pushes an instance down into the ground
// biome_min arrives asfloat(is_transparent), negative disables the gate
// the prop mask is rgb weights plus the dominant surface layer index in alpha, the index needs a
// point tap because a bilinear one between two layers averages to a value that names a third

// hard ceiling on how much the thread count may grow to refill the patches, a coverage slider near
// zero would otherwise ask for an unbounded dispatch
static const float grass_patch_max_boost = 4.0f;
// ceiling on the patch and frustum boosts combined, this is what bounds the dispatch, every thread
// past it would be work the atomic cap refuses to accept anyway
static const float grass_max_boost = 10.0f;

// what fraction of the eligible ground survives the patch field on average, the threshold keeps
// coverage exactly and the fringe is symmetric about it so it costs nothing on average
float grass_patch_coverage_expected(float patch_size, float coverage)
{
    if (patch_size <= 0.0f || coverage >= 1.0f)
    {
        return 1.0f;
    }

    return coverage;
}

// pulling grass into pockets empties most of the ring, so the same budget spread over what is left
// is what turns a thin even carpet into a thick patchy one. this is the whole reason clustering buys
// density rather than costing it
float grass_patch_density_boost(float patch_size, float coverage)
{
    float expected = grass_patch_coverage_expected(patch_size, coverage);

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
float2 terrain_world_to_normalized(float2 world_xz, float4 mapping)
{
    return (world_xz - mapping.xy) * mapping.zw;
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
float sample_terrain_height(float2 world_xz, float2 height_size, float4 mapping, out float valid)
{
    float2 normalized = terrain_world_to_normalized(world_xz, mapping);

    if (any(normalized < 0.0f) || any(normalized > 1.0f))
    {
        valid = 0.0f;
        return 0.0f;
    }

    valid = 1.0f;

    float detail = 0.0f;
    if (sample_terrain_detail(world_xz, detail))
    {
        return detail - buffer_frame.terrain_height_y;
    }

    float2 texels = max(height_size - 1.0f, 1.0f);
    float2 grid   = normalized * texels;
    float2 base   = min(floor(grid), texels - 1.0f);
    float2 f      = saturate(grid - base);

    float2 rcp_size = 1.0f / max(height_size, 1.0f);
    // Both terrain triangles share the diagonal. Fetch only the third corner
    // of the triangle containing the blade, keeping the same plane arithmetic.
    bool lower = f.x + f.y <= 1.0f;
    float corner = tex.SampleLevel(samplers[sampler_point_clamp], (base + (lower ? 0.5f : 1.5f)) * rcp_size, 0).r;
    float h10 = tex.SampleLevel(samplers[sampler_point_clamp], (base + float2(1.5f, 0.5f)) * rcp_size, 0).r;
    float h01 = tex.SampleLevel(samplers[sampler_point_clamp], (base + float2(0.5f, 1.5f)) * rcp_size, 0).r;

    return lower
        ? corner + f.x * (h10 - corner) + f.y * (h01 - corner)
        : corner + (1.0f - f.x) * (h01 - corner) + (1.0f - f.y) * (h10 - corner);
}

// two forward taps for the surface slope, reuses the centre height so a surviving blade pays three terrain
// samples total (one centre in sample_terrain_height plus these two) instead of five
float3 sample_terrain_normal(
    float2 world_xz,
    float2 height_size,
    float4 mapping,
    float y_c
)
{
    float detail_c = 0.0f;
    float detail_r = 0.0f;
    float detail_f = 0.0f;
    if (sample_terrain_detail(world_xz, detail_c) && sample_terrain_detail(world_xz + float2(1.0f, 0.0f), detail_r) && sample_terrain_detail(world_xz + float2(0.0f, 1.0f), detail_f))
    {
        return normalize(float3(detail_c - detail_r, 1.0f, detail_c - detail_f));
    }

    float2 uv = terrain_normalized_to_uv(terrain_world_to_normalized(world_xz, mapping), height_size);

    float2 texel_uv = 1.0f / max(height_size, 1.0f);

    // world distance covered by one texel step, the grid spans n-1 intervals not n
    float2 inv_size      = mapping.zw;
    float2 world_per_tap = 1.0f / (max(inv_size, 1e-8f) * max(height_size - 1.0f, 1.0f));

    float y_r = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(texel_uv.x, 0.0f), 0).r;
    float y_u = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0.0f, texel_uv.y), 0).r;

    float dy_dx = (y_r - y_c) / world_per_tap.x;
    float dy_dz = (y_u - y_c) / world_per_tap.y;

    return normalize(float3(-dy_dx, 1.0f, -dy_dz));
}

// everything one slot's lod ring tells placement, read from the populate push constants
struct GrassPlacementParams
{
    float  cell_size;
    float  ring_radius;
    uint   lod_base;
    uint   max_instances_per_lod;
    float  height_min;
    float  height_max;
    float  max_slope_cos;
    float  inner_radius;
    float4 terrain_mapping;
    float  patch_size;
    float  patch_coverage;
    float  patch_edge;
    uint   ground_mask;
    float  min_slope_cos;
    float  slope_bias;
    float  height_fade;
    float  root_radius;
    uint   packed_index; // the draw_index bits, see the layout above
    float  seat_y;
    float  biome_min;
};

GrassPlacementParams grass_placement_params_from_pass()
{
    GrassPlacementParams params;
    params.cell_size             = pass_float(pass_grass_populate::cell_size);
    params.ring_radius           = pass_float(pass_grass_populate::ring_radius);
    params.lod_base              = pass_uint(pass_grass_populate::lod_base);
    params.max_instances_per_lod = pass_uint(pass_grass_populate::max_instances_per_lod);
    params.height_min            = pass_float(pass_grass_populate::height_min);
    params.height_max            = pass_float(pass_grass_populate::height_max);
    params.max_slope_cos         = pass_float(pass_grass_populate::max_slope_cos);
    params.inner_radius          = pass_float(pass_grass_populate::inner_radius);
    params.terrain_mapping       = pass_float4(pass_grass_populate::terrain_mapping);
    params.patch_size            = pass_float(pass_grass_populate::patch_size);
    params.patch_coverage        = pass_float(pass_grass_populate::patch_coverage);
    params.patch_edge            = pass_float(pass_grass_populate::patch_edge);
    params.ground_mask           = pass_uint(pass_grass_populate::ground_mask);
    params.min_slope_cos         = pass_float(pass_grass_populate::min_slope_cos);
    params.slope_bias            = pass_float(pass_grass_populate::slope_bias);
    params.height_fade           = pass_float(pass_grass_populate::height_fade);
    params.root_radius           = pass_float(pass_grass_populate::root_radius);
    params.packed_index          = buffer_pass.draw_index;
    params.seat_y                = asfloat(buffer_pass.material_index);
    params.biome_min             = asfloat(buffer_pass.is_transparent);
    return params;
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

// a (cell, blade) candidate that passed every placement and visibility test
struct GrassCandidate
{
    float3 position;
    float3 normal;
    float  yaw_01;
    float  scale_01;
    float  cull_radius;
    uint   hash;
};

// one (cell, blade) candidate, band_min and band_max hand it to exactly one card band by distance
// pure, it reads the slot constants and the terrain and writes nothing, so it can run in any stage
bool grass_place_candidate(GrassPlacementParams params, uint2 cell_id, uint blade_index, float band_min, float band_max, out GrassCandidate candidate)
{
    candidate = (GrassCandidate)0;

    float cell_size             = params.cell_size;
    float ring_radius           = params.ring_radius;
    uint  lod_base              = params.lod_base;
    uint  max_instances_per_lod = params.max_instances_per_lod;

    float height_min    = params.height_min;
    float height_max    = params.height_max;
    float max_slope_cos = params.max_slope_cos;
    float inner_radius  = params.inner_radius;

    // a negative size is the slot asking for the complement of the patch field, the magnitude is
    // still the pocket scale and has to match the slot it is inverting or the two will not interlock
    float patch_size_signed = params.patch_size;
    float patch_size        = abs(patch_size_signed);
    bool  patch_invert      = patch_size_signed < 0.0f;
    float patch_coverage    = saturate(params.patch_coverage);
    float patch_edge        = saturate(params.patch_edge);
    uint  ground_mask       = params.ground_mask;

    // the same shaping the cpu placer applies, a slope floor, a bias toward one end of the slope band
    // and a density ramp above the height floor
    float min_slope_cos = params.min_slope_cos;
    float slope_bias    = params.slope_bias;
    float height_fade   = params.height_fade;

    uint  packed_index  = params.packed_index;
    uint  lod_index     = packed_index & 0x3u;
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
    // Grass bends rotate about its planted root, preserving radial length.
    // Zero retains the general scatter bound for stones and custom materials.
    float root_radius = params.root_radius;
    float cull_radius = (root_radius > 0.0f ? root_radius : grass_cull_radius) * largest_scale;
    float cull_height = root_radius > 0.0f ? 0.0f : grass_cull_half_height * largest_scale;

    float2 camera_xz_anchor = get_camera_position().xz;

    // Each grass blade gets a separate, jittered subcell. This spreads the budget
    // over the ground instead of wasting nearby blades on random coincidences.
    // blade_index lives in dispatch_thread_id.z; the enclosing cells stay snapped.
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
    float patch_boost = grass_patch_density_boost(patch_size, patch_coverage);

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

    if (cell_id.x >= cells_per_axis ||
        cell_id.y >= cells_per_axis ||
        blade_index >= blades_per_cell)
        return false;

    // signed cell index in the camera-relative grid, centered on the snapped camera position
    int half_cells = (int)(cells_per_axis / 2u);
    int cell_x_s   = (int)cell_id.x - half_cells;
    int cell_z_s   = (int)cell_id.y - half_cells;

    // integer world coordinates of the cell, stable as the camera moves
    int world_cell_x = (int)floor(camera_xz_anchor.x / cell_size) + cell_x_s;
    int world_cell_z = (int)floor(camera_xz_anchor.y / cell_size) + cell_z_s;

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
    float reject_outer = ring_radius + outer_transition * 0.15f;

    // Cell-level ring & band distance reject BEFORE hashing, stratification, or heightmap fetches.
    // Rejects the corners and inner hole of the rectangular dispatch window in ~4 ALU instructions.
    float2 cell_min      = float2(world_cell_x, world_cell_z) * cell_size;
    float2 cell_max      = cell_min + cell_size;
    float2 cell_nearest  = clamp(camera_xz_anchor, cell_min, cell_max) - camera_xz_anchor;
    float  cell_min_d2   = dot(cell_nearest, cell_nearest);
    float  outer_bound   = min(reject_outer, band_max);
    if (cell_min_d2 > outer_bound * outer_bound)
        return false;
    float inner_bound = max(inner_start, band_min);
    if (inner_bound > 0.0f)
    {
        float2 cell_farthest = max(abs(cell_min - camera_xz_anchor), abs(cell_max - camera_xz_anchor));
        if (dot(cell_farthest, cell_farthest) < inner_bound * inner_bound)
            return false;
    }

    // distance thinning of the blade rings: evaluate before frustum plane work so thinned threads exit immediately
    float thin_start    = grass_lod_parameters[5u + count_index].z;
    float thin_exponent = grass_lod_parameters[5u + count_index].w;
    if (thin_start > 0.0f)
    {
        if (float(blade_index) >= grass_distance_keep(cell_min_d2, thin_start, thin_exponent) * float(blades_per_cell))
            return false;
    }

    // Conservative cell-level AABB test against the left/right frustum planes before hashing or heightmap fetch.
    // The vertical span covers the full authored [height_min, height_max] band plus blade cull height/radius.
    {
        float4 cell_plane_l, cell_plane_r, cell_plane_b, cell_plane_t;
        get_frustum_side_planes(cell_plane_l, cell_plane_r, cell_plane_b, cell_plane_t);
        float cell_y_min = height_min - cull_radius;
        float cell_y_max = height_max + cull_height + cull_radius;
        float2 cell_x_min = cell_min - cull_radius;
        float2 cell_x_max = cell_max + cull_radius;

        float3 p_l = float3(
            cell_plane_l.x >= 0.0f ? cell_x_max.x : cell_x_min.x,
            cell_plane_l.y >= 0.0f ? cell_y_max   : cell_y_min,
            cell_plane_l.z >= 0.0f ? cell_x_max.y : cell_x_min.y
        );
        if (dot(cell_plane_l.xyz, p_l) + cell_plane_l.w < 0.0f)
            return false;

        float3 p_r = float3(
            cell_plane_r.x >= 0.0f ? cell_x_max.x : cell_x_min.x,
            cell_plane_r.y >= 0.0f ? cell_y_max   : cell_y_min,
            cell_plane_r.z >= 0.0f ? cell_x_max.y : cell_x_min.y
        );
        if (dot(cell_plane_r.xyz, p_r) + cell_plane_r.w < 0.0f)
            return false;
    }

    // hash combines the cell coordinates, the blade index inside the cell, and the lod seed,
    // each (cell, blade) pair gets stable random dimensions, yaw and position jitter
    // the slot is part of the seed, otherwise every slot would stack its instances on the exact same
    // points and a pebble would be born inside every blade of grass
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

    float2 cell_position = float2(jx, jz);
    if (slot_index == 0u)
    {
        uint seed = hash_u32((uint)world_cell_x, (uint)world_cell_z, lod_index * 2654435761u);
        // Use the maximum FOV boost for the subdivision, not the current count.
        // The chase camera varies FOV with speed; it must not move existing roots.
        float maximum_boost = min(patch_boost / 0.18f, grass_max_boost);
        uint capacity = max(1u, (uint)ceil(float(max_instances_per_lod) * GRASS_FILL_MARGIN * maximum_boost / max(cells_in_ring, 1.0f)));
        cell_position = grass_stratified_position(blade_index, capacity, cell_position, seed);
    }
    float2 cell_origin_xz = cell_min;
    float2 world_xz       = cell_origin_xz + cell_position * cell_size;

    float2 to_camera = world_xz - camera_xz_anchor;
    float  dist2     = dot(to_camera, to_camera);
    if (
        dist2 < inner_start * inner_start ||
        dist2 > reject_outer * reject_outer ||
        dist2 < band_min * band_min ||
        dist2 >= band_max * band_max
    )
    {
        return false;
    }

    // height reject, single centre tap, also yields world_y for the visibility cull below
    uint height_w;
    uint height_h;
    tex.GetDimensions(height_w, height_h);
    float2 height_size = float2(height_w, height_h);

    float valid;
    float local_y = sample_terrain_height(world_xz, height_size, params.terrain_mapping, valid);
    float seat_y  = params.seat_y;
    float world_y = local_y + seat_y;
    if (valid < 0.5f || world_y < height_min || world_y > height_max)
    {
        return false;
    }

    // the exact share, from the distance to the root itself, height included.
    // the last blades in line are the first to go, they shrink on the way out and grow on the way in
    float thin_grow = 1.0f;
    if (thin_start > 0.0f)
    {
        float  camera_dy = world_y - get_camera_position().y;
        float  keep      = grass_distance_keep(dist2 + camera_dy * camera_dy, thin_start, thin_exponent);
        float  rank      = (float(blade_index) + 0.5f) / float(blades_per_cell);
        if (rank >= keep)
            return false;

        thin_grow = saturate((keep - rank) / (keep * grass_thin_exit_share));
    }

    // visibility cull, frustum + occluder hi-z on a conservative blade sphere, only blades the camera
    // can actually see survive so on-screen density is unchanged while off-screen and occluded blades
    // are dropped before the expensive slope taps, vertex shading and the atomic allocate
    float3 blade_center = float3(world_xz.x, world_y + cull_height, world_xz.y);
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);
    if (!sphere_in_side_planes(blade_center, cull_radius, plane_l, plane_r, plane_b, plane_t))
        return false;

    // hi-z arrives on tex2, derive the max mip from the texture so no extra push constant is needed
    // the sphere test also rejects blades behind the camera (the side planes alone do not)
    uint hiz_w, hiz_h, hiz_mips;
    tex2.GetDimensions(0, hiz_w, hiz_h, hiz_mips);
    if (!sphere_hiz_visible(tex2, blade_center, cull_radius, float(hiz_mips - 1u)))
        return false;

    float distance_to_camera = sqrt(dist2);
    float warp_n = grass_value_noise(world_xz * (1.0f / 34.0f), 91u) * 0.7f
                 + grass_value_noise(world_xz * (1.0f / 13.0f), 53u) * 0.3f;
    // each edge warps by its own transition, a far reaching ring would otherwise tear holes hundreds of
    // metres deep into its inner seam, where the ring before it has already faded out
    float distance_warped_in  = distance_to_camera + warp_n * inner_transition * 0.55f;
    float distance_warped_out = distance_to_camera + warp_n * outer_transition * 0.55f;

    float fade_in = inner_radius > 0.0f ?
        smoothstep(
            inner_start,
            inner_radius,
            distance_warped_in
        ) :
        1.0f;
    float fade_out = 1.0f - smoothstep(
        outer_start,
        ring_radius,
        distance_warped_out
    );
    // Patch coverage can only decrease the keep probability. Reject candidates
    // already outside the distance fade before evaluating the detailed noise.
    float lod_random = hash_unit(hash_mix(h0 ^ 0xa511e9b3u));
    float fade_weight = fade_in * fade_out * cell_keep;
    if (lod_random > fade_weight)
        return false;

    // Reject unsuitable ground before the patch noise and slope taps. The
    // slope-dependent part of the same probability is applied below.
    // biome mask, same uv as the heightfield, the slot picks the channel it is gated on
    // is_transparent carries biome_min as float bits, negative disables the gate
    float biome_min = params.biome_min;
    float biome_ground = 1.0f;
    if (biome_min >= 0.0f || ground_mask != 0u)
    {
        // the mask is baked at its own resolution, so it needs its own texel centre rebase
        uint mask_w;
        uint mask_h;
        tex3.GetDimensions(mask_w, mask_h);

        float2 mask_uv = terrain_normalized_to_uv(
            terrain_world_to_normalized(world_xz, params.terrain_mapping),
            float2(mask_w, mask_h)
        );

        // ground type gate, the same bitfield the mesh placer reads. the dominant surface layer rides
        // in alpha as an index rather than a weight, so it takes a point tap, a bilinear one across the
        // boundary between two layers averages out to a value that names a third
        if (ground_mask != 0u)
        {
            float index_a = tex3.SampleLevel(samplers[sampler_point_clamp], mask_uv, 0).a;
            uint  dominant = min(
                (uint)(index_a * 255.0f + 0.5f),
                grass_ground_layer_max - 1u
            );
            if ((ground_mask & (1u << dominant)) == 0u)
            {
                return false;
            }
        }

        // a slot with the biome set to ignore still wants the ground gate above, so this is nested
        // rather than being the condition on the whole block
        if (biome_min >= 0.0f)
        {
            float4 mask     = tex3.SampleLevel(samplers[sampler_bilinear_clamp], mask_uv, 0);
            float grass_w   = mask_channel == 0u ? mask.r : (mask_channel == 1u ? mask.g : mask.b);
            if (grass_w < biome_min)
            {
                return false;
            }

            // the mask decides where grass can live, the patch field decides where it did, so the
            // mask is remapped to saturate just past its own gate instead of being used as the density
            // directly. a meadow core reading 0.6 used to throw away four blades in ten for nothing
            biome_ground = saturate((grass_w - biome_min) / grass_biome_gain);
            float biome_roll = hash_unit(hash_mix(h0 ^ 0x27d4eb2du));
            if (biome_roll > biome_ground)
            {
                return false;
            }
        }
    }

    // Road geometry is authoritative: reject it before patch noise and slope taps.
    if (on_road(world_xz)) return false;

    // Evaluate patch noise only for candidates in the camera frustum. It is keyed
    // off world space only, never off the lod or the camera, so a pocket keeps the same outline
    // across every ring and the lod seams stay invisible
    float patch = grass_patch_weight(
        world_xz,
        patch_size,
        patch_coverage,
        patch_edge,
        patch_invert
    );

    float lod_weight = fade_weight * patch;
    if (lod_random > lod_weight)
    {
        return false;
    }

    // Smooth scale emergence/recession across ring fade bands so distant grass blades, wildflowers,
    // and understory plants grow silently from tiny seeds instead of popping in at full size.
    float edge_fade = fade_in * fade_out;
    float ring_grow = 1.0f;
    if (edge_fade < 0.999f)
    {
        float edge_margin = (lod_weight - lod_random) / max(cell_keep * patch, 1e-4f);
        ring_grow         = smoothstep(0.0f, 0.34f, edge_margin);
    }

    // slope reject, two forward taps reusing the centre height, paid only by visible blades
    float3 surface_normal = sample_terrain_normal(
        world_xz,
        height_size,
        params.terrain_mapping,
        local_y
    );
    // cos is monotonic over the band so the floor is the larger cosine, a floor of zero degrees is open
    bool below_slope_floor = min_slope_cos < 1.0f && surface_normal.y > min_slope_cos;
    if (surface_normal.y < max_slope_cos || below_slope_floor)
    {
        return false;
    }

    // habitat gate (woodland/grove/scrub), lets gpu-scattered understory follow the exact same forest footprint as trees
    uint habitat = (uint)(grass_lod_parameters[slot_index].z + 0.5f);
    if (habitat >= 1u && habitat <= 3u)
    {
        float above_sea = world_y - grass_lod_parameters[slot_index].w;
        float slope_deg = acos(saturate(surface_normal.y)) * 57.2957795f;
        float woodland  = habitat_woodland(world_xz.x, world_xz.y, above_sea, slope_deg, 0.5f, 0.5f);
        float h_weight  = woodland;
        if (habitat == 2u)
        {
            h_weight = habitat_grove(world_xz.x, world_xz.y, above_sea, slope_deg, woodland);
        }
        else if (habitat == 3u)
        {
            h_weight = (0.30f + 0.70f * (1.0f - woodland)) * habitat_smooth((habitat_noise(world_xz.x / 70.0f, world_xz.y / 70.0f, 521u) - 0.25f) / 0.40f);
        }
        float habitat_roll = hash_unit(hash_mix(h0 ^ 0x517cc1b7u));
        if (habitat_roll > h_weight)
        {
            return false;
        }
    }

    // height fade and slope bias scale the keep probability rather than gating it, matching the cpu
    // placer where the weight scales the instance count
    float shape = 1.0f;
    if (height_fade > 0.0f)
    {
        shape *= saturate((world_y - height_min) / height_fade);
    }
    if (slope_bias != 0.0f)
    {
        float slope       = acos(saturate(surface_normal.y));
        float slope_floor = acos(saturate(min_slope_cos));
        float slope_roof  = acos(saturate(max_slope_cos));
        float t           = saturate((slope - slope_floor) / max(slope_roof - slope_floor, 1e-3f));
        shape            *= slope_bias > 0.0f ? pow(t, slope_bias) : pow(1.0f - t, -slope_bias);
    }
    if (shape < 1.0f)
    {
        float shape_roll = hash_unit(hash_mix(h0 ^ 0x3c6ef372u));
        if (shape_roll > shape)
        {
            return false;
        }
    }

    if (biome_min >= 0.0f)
    {
        float slope_fit = saturate((acos(saturate(max_slope_cos)) - acos(saturate(surface_normal.y))) / grass_slope_fade);
        float biome_roll = hash_unit(hash_mix(h0 ^ 0x27d4eb2du));
        if (biome_roll > biome_ground * slope_fit) return false;
    }

    // coherent multi-scale world-space growth field so meadows, flower beds, and understory colonies
    // undulate naturally in tufts and waves instead of hitting one flat mown ceiling across the terrain.
    // keyed strictly on world_xz and slot_index so lod rings stay seamless with zero popping
    uint   growth_seed  = 0x4b1d7u + slot_index * 919u;
    float  wave_macro   = grass_value_noise(world_xz * 0.075f, growth_seed) * 0.5f + 0.5f;
    float2 growth_warp  = float2(
        grass_value_noise(world_xz * 0.14f, growth_seed + 11u),
        grass_value_noise(world_xz * 0.14f, growth_seed + 29u)
    ) * 1.15f;
    float  wave_tuft    = grass_value_noise((world_xz + growth_warp) * 0.36f, growth_seed + 53u) * 0.5f + 0.5f;
    float  wave_micro   = grass_value_noise(world_xz * 1.18f, growth_seed + 97u) * 0.5f + 0.5f;
    float  growth_field = smoothstep(0.14f, 0.88f, 0.40f * wave_macro + 0.42f * wave_tuft + 0.18f * wave_micro);

    // break up regular cell spacing on detail foliage (flowers, ferns, herbs) into organic clumps
    if (slot_index > 0u)
    {
        float colony_keep = lerp(0.60f, 1.0f, smoothstep(0.15f, 0.72f, wave_tuft * 0.72f + wave_micro * 0.28f));
        float colony_roll = hash_unit(hash_mix(h0 ^ 0x6f2a9c13u));
        if (colony_roll > colony_keep)
        {
            return false;
        }
    }

    // blend coherent tuft height with per-instance diversity and a small share of proud emergent stems/blades,
    // staying strictly inside the authored [scale_01_min, scale_01_max] bounds so nothing grows out of scale
    float tuft_curve  = pow(growth_field, 1.18f);
    float jitter_mix  = slot_index == 0u ? 0.25f : 0.36f;
    float emergent    = pow(sc, 3.6f) * (slot_index == 0u ? 0.28f : 0.24f) * (0.25f + 0.75f * tuft_curve);
    float sc_shaped   = saturate(lerp(tuft_curve, sc, jitter_mix) + emergent);
    float scale_01    = lerp(scale_01_min, scale_01_max, sc_shaped);

    // a pocket that keeps full height right up to its edge reads as mown, so the fringe tapers back
    // toward the small end of the range. the packed scale is logarithmic, so this interpolates the
    // exponent and the blades shrink geometrically, which is how a tuft actually thins out
    scale_01 = lerp(scale_01_min, scale_01, saturate(patch * 1.4f));

    // a blade about to be thinned out or crossing a ring fade band is already down to a fraction of
    // its height, so it leaves and arrives among taller neighbours instead of popping at full size,
    // the packed scale is logarithmic
    float fade_scale = lerp(grass_thin_exit_scale, 1.0f, thin_grow) * lerp(0.12f, 1.0f, ring_grow);
    scale_01        += log2(fade_scale) / (2.0f * 6.643856f);

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

    candidate.position    = float3(world_xz.x, world_y, world_xz.y);
    candidate.normal      = surface_normal;
    candidate.yaw_01      = ys;
    candidate.scale_01    = scale_01;
    candidate.cull_radius = cull_radius;
    candidate.hash        = h0;
    return true;
}

struct GrassCellContext
{
    GrassPlacementParams params;
    uint   lod_index;
    uint   slot_index;
    uint   mask_channel;
    uint   count_index;
    uint   blades_per_cell;
    uint   slot0_capacity;
    uint   slot0_seed;
    uint   habitat;
    int    world_cell_x;
    int    world_cell_z;
    float  cell_keep;
    float  scale_01_min;
    float  scale_01_max;
    float  tilt_deg;
    float  tilt_tan;
    float  cull_radius;
    float  cull_height;
    float  inner_transition;
    float  outer_transition;
    float  inner_start;
    float  outer_start;
    float  reject_outer;
    float  thin_start;
    float  thin_exponent;
    float  patch_size;
    bool   patch_invert;
    float  patch_coverage;
    float  patch_edge;
    float  hiz_max_mip;
    float2 camera_xz_anchor;
    float2 cell_min;
    float2 height_size;
    float2 mask_size;
    float4 plane_l;
    float4 plane_r;
    float4 plane_b;
    float4 plane_t;
};

bool grass_prepare_cell(
    GrassPlacementParams params,
    uint2                cell_id,
    uint                 first_blade,
    out GrassCellContext cell,
    out uint             max_active_blades
)
{
    cell = (GrassCellContext)0;
    max_active_blades = 0u;

    float cell_size     = params.cell_size;
    float ring_radius   = params.ring_radius;
    float inner_radius  = params.inner_radius;
    uint cells_per_axis = (uint)(2.0f * ceil(ring_radius / cell_size)) + 2u;
    if (cell_id.x >= cells_per_axis || cell_id.y >= cells_per_axis)
    {
        return false;
    }

    uint packed_index   = params.packed_index;
    uint lod_index      = packed_index & 0x3u;
    uint slot_index     = (packed_index >> 4) & 0xFu;
    uint mask_channel   = (packed_index >> 8) & 0xFu;
    uint count_index    = slot_index * 3u + lod_index;

    float patch_size_signed = params.patch_size;
    float patch_size        = abs(patch_size_signed);
    float patch_coverage    = saturate(params.patch_coverage);

    float ring_area      = PI * (ring_radius * ring_radius - inner_radius * inner_radius);
    float cells_in_ring  = ring_area / max(cell_size * cell_size, 1e-6f);
    float patch_boost    = grass_patch_density_boost(patch_size, patch_coverage);
    float total_boost    = min(patch_boost * grass_frustum_density_boost(), grass_max_boost);
    float per_cell       = float(params.max_instances_per_lod) * GRASS_FILL_MARGIN * total_boost / max(cells_in_ring, 1.0f);
    uint blades_per_cell = max(1u, (uint)ceil(per_cell));
    if (first_blade >= blades_per_cell)
    {
        return false;
    }

    float2 camera_xz_anchor = get_camera_position().xz;
    int half_cells   = (int)(cells_per_axis / 2u);
    int world_cell_x = (int)floor(camera_xz_anchor.x / cell_size) + ((int)cell_id.x - half_cells);
    int world_cell_z = (int)floor(camera_xz_anchor.y / cell_size) + ((int)cell_id.y - half_cells);

    float inner_transition = max(cell_size * 10.0f, inner_radius * 0.5f);
    float outer_transition = max(cell_size * 10.0f, ring_radius * 0.42f);
    float inner_start      = max(0.0f, inner_radius - inner_transition);
    float outer_start      = max(inner_radius, ring_radius - outer_transition);
    float reject_outer     = ring_radius + outer_transition * 0.15f;

    float2 cell_min     = float2(world_cell_x, world_cell_z) * cell_size;
    float2 cell_max     = cell_min + cell_size;
    float2 cell_nearest = clamp(camera_xz_anchor, cell_min, cell_max) - camera_xz_anchor;
    float  cell_min_d2  = dot(cell_nearest, cell_nearest);
    if (cell_min_d2 > reject_outer * reject_outer)
    {
        return false;
    }
    if (inner_start > 0.0f)
    {
        float2 cell_farthest = max(abs(cell_min - camera_xz_anchor), abs(cell_max - camera_xz_anchor));
        if (dot(cell_farthest, cell_farthest) < inner_start * inner_start)
        {
            return false;
        }
    }

    max_active_blades   = blades_per_cell;
    float thin_start    = grass_lod_parameters[5u + count_index].z;
    float thin_exponent = grass_lod_parameters[5u + count_index].w;
    if (thin_start > 0.0f)
    {
        uint thin_cap     = (uint)ceil(grass_distance_keep(cell_min_d2, thin_start, thin_exponent) * float(blades_per_cell));
        max_active_blades = min(max_active_blades, thin_cap);
        if (first_blade >= max_active_blades)
        {
            return false;
        }
    }

    float scale_01_min  = float((packed_index >> 12) & 0xFFu) / 255.0f;
    float scale_01_max  = float((packed_index >> 20) & 0xFFu) / 255.0f;
    float tilt_deg      = float((packed_index >> 28) & 0xFu) * (45.0f / 15.0f);
    float largest_scale = exp2(lerp(-6.643856f, 6.643856f, scale_01_max));
    float cull_radius   = (params.root_radius > 0.0f ? params.root_radius : grass_cull_radius) * largest_scale;
    float cull_height   = params.root_radius > 0.0f ? 0.0f : grass_cull_half_height * largest_scale;

    // Conservative cell-level frustum + Hi-Z occlusion reject (single tap for all blades in the cell).
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);
    {
        float cell_y_min  = params.height_min - cull_radius;
        float cell_y_max  = params.height_max + cull_height + cull_radius;
        float2 cell_x_min = cell_min - cull_radius;
        float2 cell_x_max = cell_max + cull_radius;

        float3 p_l = float3(plane_l.x >= 0.0f ? cell_x_max.x : cell_x_min.x, plane_l.y >= 0.0f ? cell_y_max : cell_y_min, plane_l.z >= 0.0f ? cell_x_max.y : cell_x_min.y);
        if (dot(plane_l.xyz, p_l) + plane_l.w < 0.0f)
        {
            return false;
        }
        float3 p_r = float3(plane_r.x >= 0.0f ? cell_x_max.x : cell_x_min.x, plane_r.y >= 0.0f ? cell_y_max : cell_y_min, plane_r.z >= 0.0f ? cell_x_max.y : cell_x_min.y);
        if (dot(plane_r.xyz, p_r) + plane_r.w < 0.0f)
        {
            return false;
        }
    }

    uint height_w, height_h;
    tex.GetDimensions(height_w, height_h);
    float2 height_size = float2(height_w, height_h);

    uint hiz_w, hiz_h, hiz_mips;
    tex2.GetDimensions(0, hiz_w, hiz_h, hiz_mips);
    float hiz_max_mip = float(hiz_mips - 1u);

    // When each thread evaluates multiple blades in a cell, reject the whole cell with a single
    // heightmap + Hi-Z tap upfront. When grass_blades_per_thread == 1, skip the extra cell-center
    // tap so each thread pays only 1 heightmap + 1 Hi-Z tap on the blade itself.
    if (grass_blades_per_thread > 1u)
    {
        float cell_valid;
        float2 cell_center_xz = cell_min + cell_size * 0.5f;
        float cell_center_y   = sample_terrain_height(cell_center_xz, height_size, params.terrain_mapping, cell_valid) + params.seat_y;
        if (cell_valid >= 0.5f)
        {
            float3 cell_center_ws = float3(cell_center_xz.x, cell_center_y + cull_height, cell_center_xz.y);
            // Tight conservative bound covering cell corners and slopes up to 45 deg without poking through walls.
            float cell_sphere_r   = cell_size * 0.95f + cull_radius + cull_height + 0.35f;
            if (!sphere_in_side_planes(cell_center_ws, cell_sphere_r, plane_l, plane_r, plane_b, plane_t))
            {
                return false;
            }
            if (!sphere_hiz_visible(tex2, cell_center_ws, cell_sphere_r, hiz_max_mip))
            {
                return false;
            }
        }
    }

    uint mask_w = 1u, mask_h = 1u;
    if (params.biome_min >= 0.0f || params.ground_mask != 0u)
    {
        tex3.GetDimensions(mask_w, mask_h);
    }

    cell.params           = params;
    cell.lod_index        = lod_index;
    cell.slot_index       = slot_index;
    cell.mask_channel     = mask_channel;
    cell.count_index      = count_index;
    cell.blades_per_cell  = blades_per_cell;
    cell.cell_keep        = saturate(per_cell / float(blades_per_cell));
    cell.world_cell_x     = world_cell_x;
    cell.world_cell_z     = world_cell_z;
    cell.scale_01_min     = scale_01_min;
    cell.scale_01_max     = scale_01_max;
    cell.tilt_deg         = tilt_deg;
    cell.tilt_tan         = tilt_deg > 0.0f ? tan(radians(tilt_deg)) : 0.0f;
    cell.cull_radius      = cull_radius;
    cell.cull_height      = cull_height;
    cell.inner_transition = inner_transition;
    cell.outer_transition = outer_transition;
    cell.inner_start      = inner_start;
    cell.outer_start      = outer_start;
    cell.reject_outer     = reject_outer;
    cell.thin_start       = thin_start;
    cell.thin_exponent    = thin_exponent;
    cell.patch_size       = patch_size;
    cell.patch_invert     = patch_size_signed < 0.0f;
    cell.patch_coverage   = patch_coverage;
    cell.patch_edge       = saturate(params.patch_edge);
    cell.hiz_max_mip      = hiz_max_mip;
    cell.camera_xz_anchor = camera_xz_anchor;
    cell.cell_min         = cell_min;
    cell.height_size      = height_size;
    cell.mask_size        = float2(mask_w, mask_h);
    cell.plane_l          = plane_l;
    cell.plane_r          = plane_r;
    cell.plane_b          = plane_b;
    cell.plane_t          = plane_t;
    cell.habitat          = (uint)(grass_lod_parameters[slot_index].z + 0.5f);
    if (slot_index == 0u)
    {
        float maximum_boost = min(patch_boost / 0.18f, grass_max_boost);
        cell.slot0_capacity = max(1u, (uint)ceil(float(params.max_instances_per_lod) * GRASS_FILL_MARGIN * maximum_boost / max(cells_in_ring, 1.0f)));
        cell.slot0_seed     = hash_u32((uint)world_cell_x, (uint)world_cell_z, lod_index * 2654435761u);
    }
    return true;
}

bool grass_place_candidate_in_cell(
    GrassCellContext   cell,
    uint               blade_index,
    out GrassCandidate candidate
)
{
    candidate = (GrassCandidate)0;

    uint h0 = hash_u32(
        (uint)cell.world_cell_x,
        (uint)cell.world_cell_z,
        cell.lod_index * 2654435761u + blade_index * 0x9e3779b9u + cell.slot_index * 0x85ebca6bu
    );
    uint h1 = hash_mix(h0 ^ 0x9e3779b9u);
    uint h2 = hash_mix(h1 ^ 0x85ebca6bu);
    uint h3 = hash_mix(h2 ^ 0xc2b2ae35u);

    float jx = hash_unit(h0);
    float jz = hash_unit(h1);
    float ys = hash_unit(h2);
    float sc = hash_unit(h3);

    float2 cell_position = float2(jx, jz);
    if (cell.slot_index == 0u)
    {
        cell_position = grass_stratified_position(blade_index, cell.slot0_capacity, cell_position, cell.slot0_seed);
    }
    float2 world_xz  = cell.cell_min + cell_position * cell.params.cell_size;
    float2 to_camera = world_xz - cell.camera_xz_anchor;
    float  dist2     = dot(to_camera, to_camera);
    if (dist2 < cell.inner_start * cell.inner_start || dist2 > cell.reject_outer * cell.reject_outer)
    {
        return false;
    }

    // Zero-fetch probability + fade + patch rejection before touching terrain height, biome mask, or Hi-Z.
    float lod_random = hash_unit(hash_mix(h0 ^ 0xa511e9b3u));
    if (lod_random > cell.cell_keep)
    {
        return false;
    }

    float distance_to_camera = sqrt(dist2);
    const bool need_in_fade  = cell.params.inner_radius > 0.0f && distance_to_camera < (cell.params.inner_radius + cell.inner_transition * 0.55f);
    const bool need_out_fade = distance_to_camera > (cell.outer_start - cell.outer_transition * 0.55f);
    float fade_in  = 1.0f;
    float fade_out = 1.0f;
    if (need_in_fade || need_out_fade)
    {
        float warp_n = grass_value_noise(world_xz * (1.0f / 34.0f), 91u) * 0.7f
                     + grass_value_noise(world_xz * (1.0f / 13.0f), 53u) * 0.3f;
        if (need_in_fade)
        {
            fade_in = smoothstep(cell.inner_start, cell.params.inner_radius, distance_to_camera + warp_n * cell.inner_transition * 0.55f);
        }
        if (need_out_fade)
        {
            fade_out = 1.0f - smoothstep(cell.outer_start, cell.params.ring_radius, distance_to_camera + warp_n * cell.outer_transition * 0.55f);
        }
    }
    float fade_weight = fade_in * fade_out * cell.cell_keep;
    if (lod_random > fade_weight)
    {
        return false;
    }

    float patch      = grass_patch_weight(world_xz, cell.patch_size, cell.patch_coverage, cell.patch_edge, cell.patch_invert);
    float lod_weight = fade_weight * patch;
    if (lod_random > lod_weight)
    {
        return false;
    }

    float biome_min    = cell.params.biome_min;
    float biome_ground = 1.0f;
    if (biome_min >= 0.0f || cell.params.ground_mask != 0u)
    {
        float2 mask_uv = terrain_normalized_to_uv(
            terrain_world_to_normalized(world_xz, cell.params.terrain_mapping),
            cell.mask_size
        );
        if (cell.params.ground_mask != 0u)
        {
            float index_a  = tex3.SampleLevel(samplers[sampler_point_clamp], mask_uv, 0).a;
            uint  dominant = min((uint)(index_a * 255.0f + 0.5f), grass_ground_layer_max - 1u);
            if ((cell.params.ground_mask & (1u << dominant)) == 0u)
            {
                return false;
            }
        }
        if (biome_min >= 0.0f)
        {
            float4 mask   = tex3.SampleLevel(samplers[sampler_bilinear_clamp], mask_uv, 0);
            float grass_w = cell.mask_channel == 0u ? mask.r : (cell.mask_channel == 1u ? mask.g : mask.b);
            if (grass_w < biome_min)
            {
                return false;
            }
            biome_ground = saturate((grass_w - biome_min) / grass_biome_gain);
            float biome_roll = hash_unit(hash_mix(h0 ^ 0x27d4eb2du));
            if (biome_roll > biome_ground)
            {
                return false;
            }
        }
    }

    if (on_road(world_xz))
    {
        return false;
    }

    float valid;
    float local_y = sample_terrain_height(world_xz, cell.height_size, cell.params.terrain_mapping, valid);
    float world_y = local_y + cell.params.seat_y;
    if (valid < 0.5f || world_y < cell.params.height_min || world_y > cell.params.height_max)
    {
        return false;
    }

    float thin_grow = 1.0f;
    if (cell.thin_start > 0.0f)
    {
        float camera_dy = world_y - get_camera_position().y;
        float keep      = grass_distance_keep(dist2 + camera_dy * camera_dy, cell.thin_start, cell.thin_exponent);
        float rank      = (float(blade_index) + 0.5f) / float(cell.blades_per_cell);
        if (rank >= keep)
        {
            return false;
        }
        thin_grow = saturate((keep - rank) / (keep * grass_thin_exit_share));
    }

    float3 blade_center = float3(world_xz.x, world_y + cell.cull_height, world_xz.y);
    if (!sphere_in_side_planes(blade_center, cell.cull_radius, cell.plane_l, cell.plane_r, cell.plane_b, cell.plane_t))
    {
        return false;
    }
    if (!sphere_hiz_visible(tex2, blade_center, cell.cull_radius, cell.hiz_max_mip))
    {
        return false;
    }

    float edge_fade = fade_in * fade_out;
    float ring_grow = 1.0f;
    if (edge_fade < 0.999f)
    {
        float edge_margin = (lod_weight - lod_random) / max(cell.cell_keep * patch, 1e-4f);
        ring_grow         = smoothstep(0.0f, 0.34f, edge_margin);
    }

    float3 surface_normal = sample_terrain_normal(world_xz, cell.height_size, cell.params.terrain_mapping, local_y);
    bool below_slope_floor = cell.params.min_slope_cos < 1.0f && surface_normal.y > cell.params.min_slope_cos;
    if (surface_normal.y < cell.params.max_slope_cos || below_slope_floor)
    {
        return false;
    }

    float slope_rad = 0.0f;
    bool need_slope = (cell.habitat >= 1u && cell.habitat <= 3u) || cell.params.slope_bias != 0.0f || biome_min >= 0.0f;
    if (need_slope)
    {
        slope_rad = acos(saturate(surface_normal.y));
    }

    if (cell.habitat >= 1u && cell.habitat <= 3u)
    {
        float above_sea = world_y - grass_lod_parameters[cell.slot_index].w;
        float slope_deg = slope_rad * 57.2957795f;
        float woodland  = habitat_woodland(world_xz.x, world_xz.y, above_sea, slope_deg, 0.5f, 0.5f);
        float h_weight  = woodland;
        if (cell.habitat == 2u)
        {
            h_weight = habitat_grove(world_xz.x, world_xz.y, above_sea, slope_deg, woodland);
        }
        else if (cell.habitat == 3u)
        {
            h_weight = (0.30f + 0.70f * (1.0f - woodland)) * habitat_smooth((habitat_noise(world_xz.x / 70.0f, world_xz.y / 70.0f, 521u) - 0.25f) / 0.40f);
        }
        float habitat_roll = hash_unit(hash_mix(h0 ^ 0x517cc1b7u));
        if (habitat_roll > h_weight)
        {
            return false;
        }
    }

    float shape = 1.0f;
    if (cell.params.height_fade > 0.0f)
    {
        shape *= saturate((world_y - cell.params.height_min) / cell.params.height_fade);
    }
    if (cell.params.slope_bias != 0.0f)
    {
        float slope_floor = acos(saturate(cell.params.min_slope_cos));
        float slope_roof  = acos(saturate(cell.params.max_slope_cos));
        float t           = saturate((slope_rad - slope_floor) / max(slope_roof - slope_floor, 1e-3f));
        shape            *= cell.params.slope_bias > 0.0f ? pow(t, cell.params.slope_bias) : pow(1.0f - t, -cell.params.slope_bias);
    }
    if (shape < 1.0f)
    {
        float shape_roll = hash_unit(hash_mix(h0 ^ 0x3c6ef372u));
        if (shape_roll > shape)
        {
            return false;
        }
    }

    if (biome_min >= 0.0f)
    {
        float slope_fit  = saturate((acos(saturate(cell.params.max_slope_cos)) - slope_rad) / grass_slope_fade);
        float biome_roll = hash_unit(hash_mix(h0 ^ 0x27d4eb2du));
        if (biome_roll > biome_ground * slope_fit)
        {
            return false;
        }
    }

    uint   growth_seed  = 0x4b1d7u + cell.slot_index * 919u;
    float  wave_macro   = grass_value_noise(world_xz * 0.075f, growth_seed) * 0.5f + 0.5f;
    float2 growth_warp  = float2(
        grass_value_noise(world_xz * 0.14f, growth_seed + 11u),
        grass_value_noise(world_xz * 0.14f, growth_seed + 29u)
    ) * 1.15f;
    float  wave_tuft    = grass_value_noise((world_xz + growth_warp) * 0.36f, growth_seed + 53u) * 0.5f + 0.5f;
    float  wave_micro   = grass_value_noise(world_xz * 1.18f, growth_seed + 97u) * 0.5f + 0.5f;
    float  growth_field = smoothstep(0.14f, 0.88f, 0.40f * wave_macro + 0.42f * wave_tuft + 0.18f * wave_micro);

    if (cell.slot_index > 0u)
    {
        float colony_keep = lerp(0.60f, 1.0f, smoothstep(0.15f, 0.72f, wave_tuft * 0.72f + wave_micro * 0.28f));
        float colony_roll = hash_unit(hash_mix(h0 ^ 0x6f2a9c13u));
        if (colony_roll > colony_keep)
        {
            return false;
        }
    }

    float tuft_curve = pow(growth_field, 1.18f);
    float jitter_mix = cell.slot_index == 0u ? 0.25f : 0.36f;
    float emergent   = pow(sc, 3.6f) * (cell.slot_index == 0u ? 0.28f : 0.24f) * (0.25f + 0.75f * tuft_curve);
    float sc_shaped  = saturate(lerp(tuft_curve, sc, jitter_mix) + emergent);
    float scale_01   = lerp(cell.scale_01_min, cell.scale_01_max, sc_shaped);
    scale_01         = lerp(cell.scale_01_min, scale_01, saturate(patch * 1.4f));
    float fade_scale = lerp(grass_thin_exit_scale, 1.0f, thin_grow) * lerp(0.12f, 1.0f, ring_grow);
    scale_01        += log2(fade_scale) / (2.0f * 6.643856f);

    if (cell.tilt_deg > 0.0f)
    {
        float2 lean = float2(
            hash_unit(hash_mix(h0 ^ 0x1b56c4e9u)),
            hash_unit(hash_mix(h0 ^ 0x632be5abu))
        ) * 2.0f - 1.0f;

        surface_normal = normalize(surface_normal + float3(lean.x, 0.0f, lean.y) * cell.tilt_tan);
    }

    candidate.position    = float3(world_xz.x, world_y, world_xz.y);
    candidate.normal      = surface_normal;
    candidate.yaw_01      = ys;
    candidate.scale_01    = scale_01;
    candidate.cull_radius = cell.cull_radius;
    candidate.hash        = h0;
    return true;
}

// whether a near ring blade can take the coarse geometry without its silhouette error showing, only lod 0 asks
#endif // SPARTAN_GRASS_PLACEMENT
