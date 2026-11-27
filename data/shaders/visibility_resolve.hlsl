/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#define VISIBILITY_RESOLVE
#define INDIRECT_DRAW
#define CACHE_MESH_TREE_WIND
#define CACHE_MESH_TREE_WIND_PER_THREAD
#include "g_buffer.hlsl"
//==========================================

// tile-classified visibility buffer material resolve:
// reconstructs exact perspective-correct barycentrics and analytic screen-space derivatives
// directly from the winning primitive's 3 vertices in the global geometry arena, then evaluates
// the shared g_buffer.hlsl material pipeline once per visible pixel with zero 2x2 quad overshading.

[[vk::image_format("unknown")]] RWTexture2D<float4> rw_gbuffer_albedo   : register(u80);
[[vk::image_format("rg32ui")]]  RWTexture2D<uint2>  rw_gbuffer_normal   : register(u81);
[[vk::image_format("r32ui")]]   RWTexture2D<uint>   rw_gbuffer_material : register(u82);
[[vk::image_format("unknown")]] RWTexture2D<float4> rw_gbuffer_velocity : register(u83);
[[vk::image_format("unknown")]] RWTexture2D<float4> rw_gbuffer_emissive : register(u84);

StructuredBuffer<uint>              visibility_tiles   : register(t96);
Texture2D<uint>                     tex_visibility     : register(t97);
StructuredBuffer<SurvivingInstance> impostor_instances : register(t98);

struct BarycentricDeriv
{
    float3 lambda;
    float3 dlambda_dx;
    float3 dlambda_dy;
    bool   is_front_face;
};

// Homogeneous adjoint solver (Burns & Hunt 2013 / Nanite): exact perspective-correct barycentrics
// and screen-space partial derivatives with zero division by clip.w (safe across the near plane).
BarycentricDeriv solve_barycentrics_edges(float3 e0, float3 e1, float3 e2, float2 pixel_ndc, float2 resolution)
{
    float3 p = float3(pixel_ndc, 1.0f);
    float3 b = float3(dot(e0, p), dot(e1, p), dot(e2, p));
    float  s = b.x + b.y + b.z;

    float  inv_s  = abs(s) > 1e-20f ? (1.0f / s) : 0.0f;
    float3 lambda = b * inv_s;

    float  dx_ndc =  2.0f / resolution.x;
    float  dy_ndc = -2.0f / resolution.y;
    float3 db_dx  = float3(e0.x, e1.x, e2.x) * dx_ndc;
    float3 db_dy  = float3(e0.y, e1.y, e2.y) * dy_ndc;
    float  ds_dx  = db_dx.x + db_dx.y + db_dx.z;
    float  ds_dy  = db_dy.x + db_dy.y + db_dy.z;

    BarycentricDeriv out_d;
    out_d.lambda        = lambda;
    out_d.dlambda_dx    = (db_dx - lambda * ds_dx) * inv_s;
    out_d.dlambda_dy    = (db_dy - lambda * ds_dy) * inv_s;
    out_d.is_front_face = s < 0.0f;
    return out_d;
}

BarycentricDeriv solve_barycentrics(float4 c0, float4 c1, float4 c2, float2 pixel_ndc, float2 resolution)
{
    float3 v0 = float3(c0.x, c0.y, c0.w);
    float3 v1 = float3(c1.x, c1.y, c1.w);
    float3 v2 = float3(c2.x, c2.y, c2.w);

    float3 e0 = cross(v1, v2);
    float3 e1 = cross(v2, v0);
    float3 e2 = cross(v0, v1);

    return solve_barycentrics_edges(e0, e1, e2, pixel_ndc, resolution);
}

#ifndef IMPOSTOR_SPECIALIZED
// Lean tree wind displacement for visibility resolve (matches common_vertex_processing.hlsl bit-for-bit
// while skipping water/grass/flower/impostor/skidmark branches that never land in the meshlet visibility buffer).
void apply_vis_tree_wind(
    bool     has_alpha_mask,
    float4x4 xform,
    TreeWindState state,
    float    time_offset,
    bool     update_basis,
    inout float3 position_world,
    inout float3 normal_world,
    inout float3 tangent_world
)
{
    float3 instance_up   = normalize(xform[1].xyz);
    float3 instance_pos  = xform[3].xyz;
    float3 position_rest = position_world;
    float  response      = tree_wind_response();
    if (response <= 0.0f)
    {
        apply_vegetation_interaction(position_rest.xz, instance_pos, instance_up, vegetation_flex(position_rest - instance_pos, instance_up), time_offset, position_world);
        return;
    }

    float  time        = (float)buffer_frame.time + time_offset;
    wind_sample ws     = state.wind;
    float2 inst        = state.phase_freq;
    float3 axis        = state.axis;
    float3 offset      = position_world - instance_pos;
    float  height      = max(0.0f, dot(offset, instance_up));
    float  root_weight = smoothstep(0.0f, 0.4f, height);

    float slow_weight       = height / (height + 5.0f);
    float fast_weight       = 1.0f / (1.0f + height * 0.4f);
    float fast_wave         = sin(time * (3.2f + inst.y * 0.21f) + inst.x) * 0.6f + sin(time * (5.3f + inst.y * 0.17f) + inst.x * 1.7f) * 0.4f;
    float fast_drive        = response * (0.6f + 1.2f * ws.gust) * fast_wave * 4.0f * DEG_TO_RAD;
    float trunk_angle       = (state.trunk_drive * slow_weight + fast_drive * fast_weight) * root_weight;
    float3x3 trunk_rotation = rotation_matrix(axis, trunk_angle);

    float3 radial        = offset - instance_up * dot(offset, instance_up);
    float  radius        = length(radial);
    float  branch_weight = smoothstep(0.1f, 2.0f, radius) * root_weight;
    float  branch_phase  = inst.x + dot(offset, float3(0.37f, 0.19f, 0.29f));
    float  branch_wave   = sin(time * (1.15f + inst.y * 0.14f) + branch_phase) * 0.65f + sin(time * (1.83f + inst.y * 0.11f) + branch_phase * 0.83f) * 0.35f;
    float  branch_angle  = response * branch_weight * (1.2f + 2.5f * ws.gust + branch_wave * 1.2f) * DEG_TO_RAD;
    float3 branch_pivot  = instance_up * dot(offset, instance_up);

    if (has_alpha_mask)
    {
        float3 camera       = time_offset < 0.0f ? buffer_frame.camera_position_previous : buffer_frame.camera_position;
        float  detail       = 1.0f - smoothstep(25.0f, 90.0f, length(camera - instance_pos));
        float  flutter      = sin(time * 8.3f + branch_phase * 2.0f) * sin(time * 3.7f + branch_phase);
        float  flutter_angle = response * branch_weight * detail * (0.35f + ws.gust * 0.65f) * (flutter + ws.micro * 0.3f) * 0.045f / max(radius, 1.0f);
        branch_angle += flutter_angle;
    }

    float3x3 combined = rotation_matrix(axis, trunk_angle + branch_angle);
    position_world    = instance_pos + mul(trunk_rotation, branch_pivot) + mul(combined, offset - branch_pivot);
    if (update_basis)
    {
        normal_world  = normalize(mul(combined, normal_world));
        tangent_world = normalize(mul(combined, tangent_world));
    }

    apply_vegetation_interaction(position_rest.xz, instance_pos, instance_up, vegetation_flex(position_rest - instance_pos, instance_up), time_offset, position_world);
}

struct VisTriVertex
{
    float3 rel;
    float3 rel_prev;
    float3 normal;
    float3 tangent;
    float3 uv_h; // xy = uv, z = height_percent
    float  width_percent;
    float2 world_xz;
};

VisTriVertex transform_vis_vertex(
    uint               vertex_id,
    DrawData           draw,
    MaterialParameters material,
    Surface            surface,
    float4x4           xform,
    float4x4           xform_prev,
    float4x4           xform_rel,
    float4x4           xform_rel_prev,
    float3x3           cofactor,
    float              orientation,
    bool               has_wind
)
{
    PulledVertex pulled = load_pulled_vertex(vertex_id);

    float2 input_uv      = unpack_vertex_uv(pulled.uv);
    float3 input_normal  = unpack_vertex_oct(pulled.normal);
    float3 input_tangent = unpack_vertex_oct(pulled.tangent);

    float2 uv = input_uv * draw.uv_tiling + draw.uv_offset;
    if (draw.uv_invert.x > 0.5f) uv.x = 2.0f * floor(uv.x) + 1.0f - uv.x;
    if (draw.uv_invert.y > 0.5f) uv.y = 2.0f * floor(uv.y) + 1.0f - uv.y;
    if (draw.uv_rotation != 0.0f) uv = rotate_uv_90(uv, draw.uv_rotation);

    float width_percent  = saturate((pulled.position.x + material.local_width * 0.5f) / material.local_width);
    float height_percent = saturate(pulled.position.y / max(material.local_height, 1e-4f));
    if (surface.is_foliage())
    {
        if (draw.geometry_bounds_diag > 0.25f)
        {
            float3 b_ext     = max(draw.geometry_bounds_extent, float3(0.25f, 0.25f, 0.25f));
            float3 b_center  = draw.geometry_bounds_min + b_ext * float3(0.5f, 0.56f, 0.5f);
            float3 b_half    = max(b_ext * 0.5f, float3(0.25f, 0.25f, 0.25f));
            float  r_ellip   = length((pulled.position - b_center) / b_half);
            float  h_frac    = saturate((pulled.position.y - draw.geometry_bounds_min.y) / b_ext.y);
            float  radial_ao = smoothstep(0.18f, 1.04f, r_ellip);
            float  height_ao = lerp(0.46f, 1.0f, smoothstep(0.06f, 0.88f, h_frac));
            width_percent    = clamp(lerp(0.18f, 1.0f, radial_ao) * height_ao, 0.16f, 1.0f);
        }
        else
        {
            width_percent = 1.0f;
        }
    }

    float3 pos_local      = pulled.position;
    float3 pos_local_prev = pos_local;
    if (draw.previous_vertex_offset != 0u)
    {
        pos_local_prev = load_pulled_vertex(vertex_id + draw.previous_vertex_offset).position;
    }

    float3 pos_rel      = mul(float4(pos_local, 1.0f), xform_rel).xyz;
    float3 pos_rel_prev = mul(float4(pos_local_prev, 1.0f), xform_rel_prev).xyz;

    float3 normal_world  = normalize(mul(input_normal, cofactor) * orientation);
    float3 tangent_raw   = mul(input_tangent, (float3x3)xform);
    float3 tangent_world = normalize(tangent_raw - normal_world * dot(tangent_raw, normal_world));

    float2 world_xz = pos_rel.xz + buffer_frame.camera_position.xz;
    if (surface.is_terrain())
    {
        float3 pos_world = mul(float4(pos_local, 1.0f), xform).xyz;
        world_xz         = pos_world.xz;
        uv               = pos_world.xz * draw.uv_tiling;
    }

    if (has_wind)
    {
        float3 pos_world      = mul(float4(pos_local, 1.0f), xform).xyz;
        float3 pos_world_prev = mul(float4(pos_local_prev, 1.0f), xform_prev).xyz;
        world_xz              = pos_world.xz;
        float3 pos_unmod      = pos_world;
        float3 pos_unmod_prev = pos_world_prev;
        bool   has_alpha_mask = surface.has_texture_alpha_mask();
        apply_vis_tree_wind(has_alpha_mask, xform, mesh_tree_wind_current, 0.0f, true, pos_world, normal_world, tangent_world);
        float3 dummy_n = normal_world;
        float3 dummy_t = tangent_world;
        apply_vis_tree_wind(has_alpha_mask, xform_prev, mesh_tree_wind_previous, -buffer_frame.delta_time, false, pos_world_prev, dummy_n, dummy_t);
        pos_rel      += pos_world - pos_unmod;
        pos_rel_prev += pos_world_prev - pos_unmod_prev;
    }

    VisTriVertex o;
    o.rel           = pos_rel;
    o.rel_prev      = pos_rel_prev;
    o.normal        = normal_world;
    o.tangent       = tangent_world;
    o.uv_h          = float3(uv, height_percent);
    o.width_percent = width_percent;
    o.world_xz      = world_xz;
    return o;
}
#endif

void write_gbuffer_outputs(uint2 pixel, gbuffer out_gb)
{
    rw_gbuffer_albedo[pixel]   = out_gb.albedo;
    rw_gbuffer_normal[pixel]   = out_gb.normal;
    rw_gbuffer_material[pixel] = out_gb.material;
    rw_gbuffer_velocity[pixel] = out_gb.velocity;
    rw_gbuffer_emissive[pixel] = float4(out_gb.emissive, 0.0f);
}

[numthreads(VIS_TILE_SIZE, VIS_TILE_SIZE, 1)]
void main_cs(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
    const uint category    = pass_uint(pass_visibility::category);
    const uint tile_stride = pass_uint(pass_visibility::tile_stride);
    const uint packed_tile = visibility_tiles[category * tile_stride + group_id.x];
    const uint2 pixel      = uint2(packed_tile & 0xFFFFu, packed_tile >> 16u) * VIS_TILE_SIZE + thread_id.xy;

    const float2 resolution = get_render_resolution_active();
    if (pixel.x >= (uint)resolution.x || pixel.y >= (uint)resolution.y)
    {
        return;
    }

    const uint vis_id = tex_visibility[pixel];
    if (vis_id == 0u)
    {
        return;
    }

#ifdef IMPOSTOR_SPECIALIZED
    if ((vis_id & VIS_FLAG_IMPOSTOR) == 0u)
    {
        return;
    }

    const uint impostor_idx = vis_id & VIS_IMPOSTOR_MASK;
    SurvivingInstance si    = impostor_instances[impostor_idx];
    DrawData draw           = indirect_draw_data[si.draw_index];
    _draw                   = draw;

    bool is_per_instance = (draw.flags & 2u) != 0u;
    float4x4 instance    = is_per_instance
        ? pull_instance_transform(draw.instance_offset, si.instance_index)
        : float4x4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);

    float4x4 transform          = mul(instance, draw.transform);
    float4x4 transform_previous = mul(instance, draw.transform_previous);

    uint frames         = draw.impostor_layout & 0xFFu;
    float3x3 basis      = (float3x3)transform;
    float3 center_world = mul(float4(draw.impostor_center, 1.0f), transform).xyz;
    float3 axis_scale   = float3(dot(basis[0], basis[0]), dot(basis[1], basis[1]), dot(basis[2], basis[2]));
    float3 view_local   = normalize(mul(basis, buffer_frame.camera_position - center_world) / max(axis_scale, 1e-8f));
    uint2 frame         = impostor_nearest_frame(view_local, frames);
    float3 direction    = impostor_frame_direction(frame, frames);
    float3 right;
    float3 up;
    impostor_frame_basis(direction, right, up);

    float4 uv_rect = impostor_frame_bounds(draw, frame, frames);
    // Corners 0, 2, 1 define the coplanar impostor quad plane with CW front-facing winding.
    float2 uv0 = float2(uv_rect.x, uv_rect.y);
    float2 uv1 = float2(uv_rect.z, uv_rect.w);
    float2 uv2 = float2(uv_rect.z, uv_rect.y);

    float2 off0 = (uv0 * 2.0f - 1.0f) * draw.impostor_radius;
    float2 off1 = (uv1 * 2.0f - 1.0f) * draw.impostor_radius;
    float2 off2 = (uv2 * 2.0f - 1.0f) * draw.impostor_radius;

    float3 local0 = draw.impostor_center + right * off0.x + up * off0.y;
    float3 local1 = draw.impostor_center + right * off1.x + up * off1.y;
    float3 local2 = draw.impostor_center + right * off2.x + up * off2.y;

    float3 world0 = mul(float4(local0, 1.0f), transform).xyz;
    float3 world1 = mul(float4(local1, 1.0f), transform).xyz;
    float3 world2 = mul(float4(local2, 1.0f), transform).xyz;

    float3 prev0 = mul(float4(local0, 1.0f), transform_previous).xyz - buffer_frame.camera_position_previous;
    float3 prev1 = mul(float4(local1, 1.0f), transform_previous).xyz - buffer_frame.camera_position_previous;
    float3 prev2 = mul(float4(local2, 1.0f), transform_previous).xyz - buffer_frame.camera_position_previous;

    float3 rel0 = world0 - buffer_frame.camera_position;
    float3 rel1 = world1 - buffer_frame.camera_position;
    float3 rel2 = world2 - buffer_frame.camera_position;

    float4 clip0 = mul(float4(rel0, 1.0f), buffer_frame.view_projection_relative);
    float4 clip1 = mul(float4(rel1, 1.0f), buffer_frame.view_projection_relative);
    float4 clip2 = mul(float4(rel2, 1.0f), buffer_frame.view_projection_relative);

    float2 pixel_center = float2(pixel) + 0.5f;
    float2 pixel_ndc    = uv_to_ndc(pixel_center / resolution);
    BarycentricDeriv bd = solve_barycentrics(clip0, clip1, clip2, pixel_ndc, resolution);

    float2 input_uv  = uv0 + (uv1 - uv0) * bd.lambda.y + (uv2 - uv0) * bd.lambda.z;
    float2 duv_dx    = (uv1 - uv0) * bd.dlambda_dx.y + (uv2 - uv0) * bd.dlambda_dx.z;
    float2 duv_dy    = (uv1 - uv0) * bd.dlambda_dy.y + (uv2 - uv0) * bd.dlambda_dy.z;
    vis_duv_dx       = duv_dx * 0.996f;
    vis_duv_dy       = duv_dy * 0.996f;

    float3 dpdx_world = (rel1 - rel0) * bd.dlambda_dx.y + (rel2 - rel0) * bd.dlambda_dx.z;
    float3 dpdy_world = (rel1 - rel0) * bd.dlambda_dy.y + (rel2 - rel0) * bd.dlambda_dy.z;
    float3 pos_rel_prev = prev0 + (prev1 - prev0) * bd.lambda.y + (prev2 - prev0) * bd.lambda.z;

    float3x3 cofactor;
    cofactor[0]       = cross(basis[1], basis[2]);
    cofactor[1]       = cross(basis[2], basis[0]);
    cofactor[2]       = cross(basis[0], basis[1]);
    float orientation = dot(basis[0], cofactor[0]) < 0.0f ? -1.0f : 1.0f;

    float3 normal_world  = normalize(mul(direction, cofactor) * orientation);
    float3 card_tangent  = mul(right, basis);
    float3 tangent_world = normalize(card_tangent - normal_world * dot(card_tangent, normal_world));

    gbuffer_vertex vertex;
    vertex.position          = float4(pixel_center, tex_depth[pixel].r, 1.0f);
    vertex.position_previous = mul(float4(pos_rel_prev, 1.0f), buffer_frame.view_projection_previous_relative);
    vertex.normal            = normal_world;
    vertex.tangent           = tangent_world;
    vertex.uv_misc           = float4(float2(frame) + 0.002f + input_uv * 0.996f, -1.0f, (float)si.instance_index);
    vertex.width_percent     = 0.0f;
    vertex.material_index    = draw.material_index;
    vertex.view_id           = 0u;
    vertex.ocean_world_xz    = (world0 * bd.lambda.x + world1 * bd.lambda.y + world2 * bd.lambda.z).xz;
    vertex.uv_xform_ts       = float4(draw.uv_tiling, draw.uv_offset);
    vertex.uv_xform_ir       = float4(draw.uv_invert, draw.uv_rotation, draw.uv_world_space);
    vertex.motion_blur_id    = draw.motion_blur_id;
    vertex.draw_flags        = draw.flags;
    vertex.decal_range       = uint2(draw.decal_offset, draw.decal_count);

    gbuffer out_gb = evaluate_gbuffer(vertex, true, dpdx_world, dpdy_world, normal_world, float3(0.0f, 0.0f, 0.0f), float3(0.0f, 0.0f, 0.0f));
    write_gbuffer_outputs(pixel, out_gb);
#else
    if ((vis_id & VIS_FLAG_MESHLET) == 0u)
    {
        return;
    }

    const uint meshlet_half = pass_uint(pass_visibility::meshlet_half_capacity);
    const uint mi_idx_self  = (vis_id >> VIS_TRI_BITS) & VIS_MESHLET_MASK;
    const bool is_alpha     = mi_idx_self >= meshlet_half;
#ifdef ALPHA_SPECIALIZED
    if (!is_alpha)
    {
        return;
    }
#else
    if (is_alpha)
    {
        return;
    }
#endif

    // When all active lanes in the wave hit the same triangle, drive the load/transform/edge setup from
    // WaveReadLaneFirst(vis_id) so the compiler scalarizes the triangle setup onto the uniform datapath.
    const bool wave_same_tri = WaveActiveAllEqual(vis_id);
    const uint tri_vis_id    = wave_same_tri ? WaveReadLaneFirst(vis_id) : vis_id;

    const uint mi_idx      = (tri_vis_id >> VIS_TRI_BITS) & VIS_MESHLET_MASK;
    const uint tri_id      = tri_vis_id & VIS_TRI_MASK;
    MeshletInstance mi     = meshlet_instances[mi_idx];
    DrawData draw          = indirect_draw_data[meshlet_instance_draw_index(mi)];
    MeshletBounds mb       = meshlet_bounds[mi.meshlet_index];
    _draw                  = draw;

    float4x4 xform      = draw.transform;
    float4x4 xform_prev = draw.transform_previous;
    if (draw.instance_offset != 0u)
    {
        float4x4 inst = compose_packed_instance(geometry_instances[draw.instance_offset + mi.instance_index]);
        xform         = mul(inst, xform);
        xform_prev    = mul(inst, xform_prev);
    }

    MaterialParameters material = material_parameters[draw.material_index];
    Surface surface;
    surface.flags = material.flags;
    const bool has_wind = surface.has_wind_animation();
    if (has_wind)
    {
        uint cache_slot = meshlet_instance_wind_slot(mi, draw.instance_offset);
        if (cache_slot != 0u)
        {
            CachedTreeWind cached   = tree_wind_cache[cache_slot - 1u];
            mesh_tree_wind_current  = unpack_tree_wind(cached.current_axis_drive, cached.current_phase_gust_micro);
            mesh_tree_wind_previous = unpack_tree_wind(cached.previous_axis_drive, cached.previous_phase_gust_micro);
        }
        else
        {
            mesh_tree_wind_current  = evaluate_tree_wind(xform, 0.0f);
            mesh_tree_wind_previous = evaluate_tree_wind(xform_prev, -buffer_frame.delta_time);
        }
    }

    float4x4 xform_rel      = xform;
    float4x4 xform_rel_prev = xform_prev;
    xform_rel[3].xyz       -= buffer_frame.camera_position;
    xform_rel_prev[3].xyz  -= buffer_frame.camera_position_previous;

    float3x3 cofactor = float3x3(
        cross(xform[1].xyz, xform[2].xyz),
        cross(xform[2].xyz, xform[0].xyz),
        cross(xform[0].xyz, xform[1].xyz)
    );
    float orientation = dot(xform[0].xyz, cofactor[0]) < 0.0f ? -1.0f : 1.0f;

    const uint first_vertex = draw.meshlet_vertex_base + meshlet_decode_first_vertex(mb);
    const uint corner       = draw.meshlet_micro_base + mb.first_micro + tri_id * 3u;
    const uint vid0         = meshlet_vertices[first_vertex + meshlet_micro_index_load(corner + 0u)] + draw.geometry_vertex_offset;
    const uint vid1         = meshlet_vertices[first_vertex + meshlet_micro_index_load(corner + 1u)] + draw.geometry_vertex_offset;
    const uint vid2         = meshlet_vertices[first_vertex + meshlet_micro_index_load(corner + 2u)] + draw.geometry_vertex_offset;

    VisTriVertex v0 = transform_vis_vertex(vid0, draw, material, surface, xform, xform_prev, xform_rel, xform_rel_prev, cofactor, orientation, has_wind);
    VisTriVertex v1 = transform_vis_vertex(vid1, draw, material, surface, xform, xform_prev, xform_rel, xform_rel_prev, cofactor, orientation, has_wind);
    VisTriVertex v2 = transform_vis_vertex(vid2, draw, material, surface, xform, xform_prev, xform_rel, xform_rel_prev, cofactor, orientation, has_wind);

    float4 clip0 = mul(float4(v0.rel, 1.0f), buffer_frame.view_projection_relative);
    float4 clip1 = mul(float4(v1.rel, 1.0f), buffer_frame.view_projection_relative);
    float4 clip2 = mul(float4(v2.rel, 1.0f), buffer_frame.view_projection_relative);

    float3 cv0 = float3(clip0.x, clip0.y, clip0.w);
    float3 cv1 = float3(clip1.x, clip1.y, clip1.w);
    float3 cv2 = float3(clip2.x, clip2.y, clip2.w);
    float3 e0  = cross(cv1, cv2);
    float3 e1  = cross(cv2, cv0);
    float3 e2  = cross(cv0, cv1);

    float2 pixel_center = float2(pixel) + 0.5f;
    float2 pixel_ndc    = uv_to_ndc(pixel_center / resolution);
    BarycentricDeriv bd = solve_barycentrics_edges(e0, e1, e2, pixel_ndc, resolution);

    float2 duv10 = v1.uv_h.xy - v0.uv_h.xy;
    float2 duv20 = v2.uv_h.xy - v0.uv_h.xy;
    vis_duv_dx   = duv10 * bd.dlambda_dx.y + duv20 * bd.dlambda_dx.z;
    vis_duv_dy   = duv10 * bd.dlambda_dy.y + duv20 * bd.dlambda_dy.z;

    float3 e10_rel    = v1.rel - v0.rel;
    float3 e20_rel    = v2.rel - v0.rel;
    float3 dpdx_world = e10_rel * bd.dlambda_dx.y + e20_rel * bd.dlambda_dx.z;
    float3 dpdy_world = e10_rel * bd.dlambda_dy.y + e20_rel * bd.dlambda_dy.z;

    float3 dn10        = v1.normal - v0.normal;
    float3 dn20        = v2.normal - v0.normal;
    float3 dndx_normal = dn10 * bd.dlambda_dx.y + dn20 * bd.dlambda_dx.z;
    float3 dndy_normal = dn10 * bd.dlambda_dy.y + dn20 * bd.dlambda_dy.z;

    float3 tri_face_n  = cross(e10_rel, e20_rel);
    float  tri_len_sq  = dot(tri_face_n, tri_face_n);
    float3 interp_n    = v0.normal + dn10 * bd.lambda.y + dn20 * bd.lambda.z;
    float3 face_normal = tri_len_sq > 0.0f ? tri_face_n * rsqrt(tri_len_sq) : normalize(interp_n);

    float3 pos_rel_prev = v0.rel_prev + (v1.rel_prev - v0.rel_prev) * bd.lambda.y + (v2.rel_prev - v0.rel_prev) * bd.lambda.z;

    gbuffer_vertex vertex;
    vertex.position          = float4(pixel_center, tex_depth[pixel].r, 1.0f);
    vertex.position_previous = mul(float4(pos_rel_prev, 1.0f), buffer_frame.view_projection_previous_relative);
    vertex.normal            = interp_n;
    vertex.tangent           = v0.tangent * bd.lambda.x + v1.tangent * bd.lambda.y + v2.tangent * bd.lambda.z;
    vertex.uv_misc           = float4(v0.uv_h * bd.lambda.x + v1.uv_h * bd.lambda.y + v2.uv_h * bd.lambda.z, (float)mi.instance_index);
    vertex.width_percent     = v0.width_percent * bd.lambda.x + v1.width_percent * bd.lambda.y + v2.width_percent * bd.lambda.z;
    vertex.material_index    = draw.material_index;
    vertex.view_id           = 0u;
    vertex.ocean_world_xz    = v0.world_xz * bd.lambda.x + v1.world_xz * bd.lambda.y + v2.world_xz * bd.lambda.z;
    vertex.uv_xform_ts       = float4(draw.uv_tiling, draw.uv_offset);
    vertex.uv_xform_ir       = float4(draw.uv_invert, draw.uv_rotation, draw.uv_world_space);
    vertex.motion_blur_id    = draw.motion_blur_id;
    vertex.draw_flags        = draw.flags;
    vertex.decal_range       = uint2(draw.decal_offset, draw.decal_count);

    gbuffer out_gb = evaluate_gbuffer(vertex, bd.is_front_face, dpdx_world, dpdy_world, face_normal, dndx_normal, dndy_normal);
    write_gbuffer_outputs(pixel, out_gb);
#endif
}
