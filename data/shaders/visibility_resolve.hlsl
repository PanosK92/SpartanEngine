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
BarycentricDeriv solve_barycentrics(float4 c0, float4 c1, float4 c2, float2 pixel_ndc, float2 resolution)
{
    float3 v0 = float3(c0.x, c0.y, c0.w);
    float3 v1 = float3(c1.x, c1.y, c1.w);
    float3 v2 = float3(c2.x, c2.y, c2.w);

    float3 e0 = cross(v1, v2);
    float3 e1 = cross(v2, v0);
    float3 e2 = cross(v0, v1);

    float3 p = float3(pixel_ndc, 1.0f);
    float3 b = float3(dot(e0, p), dot(e1, p), dot(e2, p));
    float  s = b.x + b.y + b.z;

    float  inv_s = abs(s) > 1e-20f ? (1.0f / s) : 0.0f;
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

    uint meshlet_capacity, meshlet_stride;
    meshlet_instances.GetDimensions(meshlet_capacity, meshlet_stride);
    const uint mi_idx   = (vis_id >> VIS_TRI_BITS) & VIS_MESHLET_MASK;
    const bool is_alpha = mi_idx >= (meshlet_capacity >> 1u);
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

    const uint tri_id      = vis_id & VIS_TRI_MASK;
    MeshletInstance mi     = meshlet_instances[mi_idx];
    DrawData draw          = indirect_draw_data[meshlet_instance_draw_index(mi)];
    MeshletBounds mb       = meshlet_bounds[mi.meshlet_index];
    _draw                  = draw;

    cache_mesh_instance(draw, mi.instance_index, meshlet_instance_wind_slot(mi, draw.instance_offset));

    const uint first_vertex = draw.meshlet_vertex_base + meshlet_decode_first_vertex(mb);
    const uint corner       = draw.meshlet_micro_base + mb.first_micro + tri_id * 3u;
    const uint vid0         = meshlet_vertices[first_vertex + meshlet_micro_index_load(corner + 0u)] + draw.geometry_vertex_offset;
    const uint vid1         = meshlet_vertices[first_vertex + meshlet_micro_index_load(corner + 1u)] + draw.geometry_vertex_offset;
    const uint vid2         = meshlet_vertices[first_vertex + meshlet_micro_index_load(corner + 2u)] + draw.geometry_vertex_offset;

    Vertex_PosUvNorTan in0 = pull_vertex(vid0, mi.instance_index, draw.instance_offset);
    Vertex_PosUvNorTan in1 = pull_vertex(vid1, mi.instance_index, draw.instance_offset);
    Vertex_PosUvNorTan in2 = pull_vertex(vid2, mi.instance_index, draw.instance_offset);

    float3 w0 = 0.0f, w0p = 0.0f, r0 = 0.0f, r0p = 0.0f;
    float3 w1 = 0.0f, w1p = 0.0f, r1 = 0.0f, r1p = 0.0f;
    float3 w2 = 0.0f, w2p = 0.0f, r2 = 0.0f, r2p = 0.0f;

    gbuffer_vertex v0 = transform_to_world_space(in0, mi.instance_index, draw.transform, w0, w0p, r0, r0p);
    gbuffer_vertex v1 = transform_to_world_space(in1, mi.instance_index, draw.transform, w1, w1p, r1, r1p);
    gbuffer_vertex v2 = transform_to_world_space(in2, mi.instance_index, draw.transform, w2, w2p, r2, r2p);

    float4 clip0 = mul(float4(r0, 1.0f), buffer_frame.view_projection_relative);
    float4 clip1 = mul(float4(r1, 1.0f), buffer_frame.view_projection_relative);
    float4 clip2 = mul(float4(r2, 1.0f), buffer_frame.view_projection_relative);

    float2 pixel_center = float2(pixel) + 0.5f;
    float2 pixel_ndc    = uv_to_ndc(pixel_center / resolution);
    BarycentricDeriv bd = solve_barycentrics(clip0, clip1, clip2, pixel_ndc, resolution);

    float2 duv10 = v1.uv_misc.xy - v0.uv_misc.xy;
    float2 duv20 = v2.uv_misc.xy - v0.uv_misc.xy;
    vis_duv_dx   = duv10 * bd.dlambda_dx.y + duv20 * bd.dlambda_dx.z;
    vis_duv_dy   = duv10 * bd.dlambda_dy.y + duv20 * bd.dlambda_dy.z;

    float3 e10_rel    = r1 - r0;
    float3 e20_rel    = r2 - r0;
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

    float3 pos_rel_prev = r0p + (r1p - r0p) * bd.lambda.y + (r2p - r0p) * bd.lambda.z;

    gbuffer_vertex vertex;
    vertex.position          = float4(pixel_center, tex_depth[pixel].r, 1.0f);
    vertex.position_previous = mul(float4(pos_rel_prev, 1.0f), buffer_frame.view_projection_previous_relative);
    vertex.normal            = interp_n;
    vertex.tangent           = v0.tangent * bd.lambda.x + v1.tangent * bd.lambda.y + v2.tangent * bd.lambda.z;
    vertex.uv_misc           = v0.uv_misc * bd.lambda.x + v1.uv_misc * bd.lambda.y + v2.uv_misc * bd.lambda.z;
    vertex.width_percent     = v0.width_percent * bd.lambda.x + v1.width_percent * bd.lambda.y + v2.width_percent * bd.lambda.z;
    vertex.material_index    = draw.material_index;
    vertex.view_id           = 0u;
    vertex.ocean_world_xz    = v0.ocean_world_xz * bd.lambda.x + v1.ocean_world_xz * bd.lambda.y + v2.ocean_world_xz * bd.lambda.z;
    vertex.uv_xform_ts       = float4(draw.uv_tiling, draw.uv_offset);
    vertex.uv_xform_ir       = float4(draw.uv_invert, draw.uv_rotation, draw.uv_world_space);
    vertex.motion_blur_id    = draw.motion_blur_id;
    vertex.draw_flags        = draw.flags;
    vertex.decal_range       = uint2(draw.decal_offset, draw.decal_count);

    gbuffer out_gb = evaluate_gbuffer(vertex, bd.is_front_face, dpdx_world, dpdy_world, face_normal, dndx_normal, dndy_normal);
    write_gbuffer_outputs(pixel, out_gb);
#endif
}
