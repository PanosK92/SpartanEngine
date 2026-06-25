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

// conservative world-space bound used to cull a blade against the camera frustum and the occluder hi-z
// covers the tallest scaled blade plus wind sway and the intra-cell scatter, generous enough that no
// on-screen blade is ever wrongly rejected so visible density stays identical
static const float grass_cull_half_height = 0.5f;
static const float grass_cull_radius      = 1.5f;

// gpu procedural grass placement (ghost of tsushima style)
//
// one thread per cell in a 2d ring around the camera, the ring lives entirely inside grass_instances
// each accepted cell is atomically committed into the per-lod section of the buffer and the per-lod
// counter in grass_count is bumped, the args compute later turns that count into DrawIndexedIndirect
//
// determinism: the cell grid is snapped to world space using floor(camera / cell_size), the hash
// inside each cell is keyed off world-space integer coordinates, so the same blade lands at the same
// world position regardless of the camera path. only ring-boundary cells appear and disappear.
//
// push constant layout (PassBufferData.values, 12 floats total):
//   values[0] = (cell_size, ring_radius, lod_base_in_instances, max_instances_per_lod)
//   values[1] = (height_min, height_max, max_slope_cos, lod_index)
//   values[2] = (camera_xz_snapped.x, camera_xz_snapped.z, terrain_extent_x, terrain_extent_z)
// terrain heightmap is bound to tex (t7), R32_Float, world-space y per texel
// terrain is centered at origin in xz, so world_xz to uv is (world_xz / terrain_extent) + 0.5

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

// single centre sample, gates the cheap height reject and the frustum/hi-z visibility cull
// before the four neighbor taps that the slope reject needs, so off-screen blades pay one tap not five
float sample_terrain_height(float2 world_xz, float2 terrain_extent, out float valid)
{
    // terrain is centered at origin in xz, so a world position maps to uv = world_xz / extent + 0.5
    float2 uv = world_xz / terrain_extent + 0.5f;

    if (any(uv < 0.0f) || any(uv > 1.0f))
    {
        valid = 0.0f;
        return 0.0f;
    }

    valid = 1.0f;
    return tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).r;
}

// two forward taps for the surface slope, reuses the centre height so a surviving blade pays three terrain
// samples total (one centre in sample_terrain_height plus these two) instead of five
float sample_terrain_slope_cos(float2 world_xz, float2 terrain_extent, float y_c)
{
    float2 uv = world_xz / terrain_extent + 0.5f;

    // sample exactly one texel apart for the slope, sub-texel offsets get smeared by the bilinear filter
    // and starve the difference of any signal, query the heightmap dimensions so the offsets are
    // correct regardless of how the terrain component sized the dense heightmap
    uint width;
    uint height;
    tex.GetDimensions(width, height);
    float texel_uv_x        = 1.0f / float(max(width,  1u));
    float texel_uv_z        = 1.0f / float(max(height, 1u));
    float world_per_texel_x = terrain_extent.x * texel_uv_x;
    float world_per_texel_z = terrain_extent.y * texel_uv_z;

    float y_r = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(texel_uv_x, 0.0f), 0).r;
    float y_u = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv + float2(0.0f, texel_uv_z), 0).r;

    // forward differences in world units, world height delta divided by the world distance to the forward sample
    float dy_dx = (y_r - y_c) / world_per_texel_x;
    float dy_dz = (y_u - y_c) / world_per_texel_z;

    // standard right-handed surface normal, y is the up axis, return its y as the slope cosine
    float3 normal = normalize(float3(-dy_dx, 1.0f, -dy_dz));
    return normal.y;
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
    uint  lod_index     = (uint)buffer_pass.values[1].w;

    float2 camera_xz_snapped = buffer_pass.values[2].xy;
    float2 terrain_extent    = buffer_pass.values[2].zw;

    // stratified scatter: the world is divided into a grid of cells, each cell holds blades_per_cell
    // blades, and each blade is placed at a uniformly random position INSIDE the cell, independently of
    // every other blade. each cell is a stratum, blades inside a stratum are pure random scatter so
    // there is no per-blade lattice, blades between strata add up to a globally uniform density so
    // there is no per-cell lattice either, this is the cheapest stable approximation of a poisson disk
    // distribution that produces even coverage at any view angle. blade_index lives in dispatch_thread_id.z
    // so cells_per_axis stays a simple 2d grid sized to the ring
    uint cells_per_axis  = (uint)(2.0f * ceil(ring_radius / cell_size));
    float ring_area      = PI * ring_radius * ring_radius;
    float cells_in_ring  = ring_area / max(cell_size * cell_size, 1e-6f);
    uint  blades_per_cell = max(1u, (uint)floor(float(max_instances_per_lod) / max(cells_in_ring, 1.0f)));
    if (dispatch_thread_id.x >= cells_per_axis ||
        dispatch_thread_id.y >= cells_per_axis ||
        dispatch_thread_id.z >= blades_per_cell)
        return;

    // signed cell index in the camera-relative grid, centered on the snapped camera position
    int half_cells = (int)(cells_per_axis / 2u);
    int cell_x_s   = (int)dispatch_thread_id.x - half_cells;
    int cell_z_s   = (int)dispatch_thread_id.y - half_cells;

    // integer world coordinates of the cell, stable as the camera moves
    int world_cell_x = (int)floor(camera_xz_snapped.x / cell_size) + cell_x_s;
    int world_cell_z = (int)floor(camera_xz_snapped.y / cell_size) + cell_z_s;

    // hash combines the cell coordinates, the blade index inside the cell, and the lod seed,
    // each (cell, blade) pair gets an independent stable hash, no two blades collide and there is
    // no per-cell pattern because blade_index decorrelates blades that share a cell
    uint blade_index = dispatch_thread_id.z;
    uint h0  = hash_u32((uint)world_cell_x, (uint)world_cell_z, lod_index * 2654435761u + blade_index * 0x9e3779b9u);
    float jx = hash_unit(h0);
    float jz = hash_unit(h0 * 16807u + 1u);
    float ys = hash_unit(h0 * 48271u + 3u);
    float sc = hash_unit(h0 * 277803737u + 5u);

    // uniform random position inside the cell, the cell is the stratum, every blade in a cell rolls
    // its own jx/jz so the cell interior is filled with pure random scatter rather than a single
    // jittered grid point, which is what kills the doll-hair look
    float2 cell_origin_xz = float2(world_cell_x, world_cell_z) * cell_size;
    float2 world_xz       = cell_origin_xz + float2(jx, jz) * cell_size;

    // ring distance reject, the ring is round so corner cells of the dispatch square get culled here
    float2 to_camera = world_xz - camera_xz_snapped;
    float  dist2     = dot(to_camera, to_camera);
    if (dist2 > ring_radius * ring_radius)
        return;

    // height reject, single centre tap, also yields world_y for the visibility cull below
    float valid;
    float world_y = sample_terrain_height(world_xz, terrain_extent, valid);
    if (valid < 0.5f || world_y < height_min || world_y > height_max)
        return;

    // visibility cull, frustum + occluder hi-z on a conservative blade sphere, only blades the camera
    // can actually see survive so on-screen density is unchanged while off-screen and occluded blades
    // are dropped before the expensive slope taps, vertex shading and the atomic allocate
    float3 blade_center = float3(world_xz.x, world_y + grass_cull_half_height, world_xz.y);
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);
    if (!sphere_in_side_planes(blade_center, grass_cull_radius, plane_l, plane_r, plane_b, plane_t))
        return;

    // hi-z arrives on tex2, derive the max mip from the texture so no extra push constant is needed
    // the sphere test also rejects blades behind the camera (the side planes alone do not)
    uint hiz_w, hiz_h, hiz_mips;
    tex2.GetDimensions(0, hiz_w, hiz_h, hiz_mips);
    if (!sphere_hiz_visible(tex2, blade_center, grass_cull_radius, float(hiz_mips - 1u)))
        return;

    // slope reject, two forward taps reusing the centre height, paid only by visible blades
    float slope_cos = sample_terrain_slope_cos(world_xz, terrain_extent, world_y);
    if (slope_cos < max_slope_cos)
        return;

    // grass blades only need a rough up vector to align to the surface, a flat up is enough
    float3 up = float3(0.0f, 1.0f, 0.0f);

    // scale: compose_instance_transform decodes scale_01 via exp2(lerp(log2(0.01), log2(100), t)),
    // so scale_01 = 0.5 yields world scale 1.0 and a +/-0.07 window covers a perceptually balanced
    // range around it, center is biased slightly above 0.5 so the minimum blade does not shrink into
    // a barely-visible sliver while the maximum stays under ~3x to keep the lawn from looking patchy
    float scale_01 = 0.54f + (sc - 0.5f) * 0.14f; // world scale roughly in [0.58, 2.75]

    // atomic-allocate a slot inside this lod's section, bail out cleanly once full
    // blades_per_cell was sized from cells_in_ring with floor() so the upper bound on writes is
    // floor(cap / cells_in_ring) * cells_in_ring <= cap, the clamp here is just a defensive guard
    uint local_slot;
    InterlockedAdd(grass_count[lod_index], 1u, local_slot);
    if (local_slot >= max_instances_per_lod)
        return;

    uint global_slot = lod_base + local_slot;
    grass_instances[global_slot] = build_grass_instance(
        float3(world_xz.x, world_y, world_xz.y),
        up,
        ys,
        scale_01
    );
}
