/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// Inline shadow rays shared by surfaces, fog and particles. Alpha tested
// instances are non opaque in the tlas, their cutouts are resolved here with
// the same albedo alpha and distance threshold as the raster cutout path.
#ifndef SPARTAN_RT_VISIBILITY
#define SPARTAN_RT_VISIBILITY
#include "common.hlsl"

#ifdef RAY_TRACING_ENABLED

bool rt_candidate_opaque(uint instance_index, uint primitive_index, float2 barycentrics, float3 hit_position)
{
    GeometryInfo geo       = geometry_infos[instance_index];
    MaterialParameters mat = material_parameters[geo.material_index];
    // world space uvs need the hit normal, cutouts never use them
    if (!mat.has_texture_albedo() || mat.is_terrain() || geo.uv_world_space > 0.0f)
    {
        return true;
    }

    uint index_base = geo.index_offset + primitive_index * 3u;
    float2 uv0 = unpack_vertex_uv(geometry_vertices[geo.vertex_offset + geometry_indices[index_base + 0u]].uv);
    float2 uv1 = unpack_vertex_uv(geometry_vertices[geo.vertex_offset + geometry_indices[index_base + 1u]].uv);
    float2 uv2 = unpack_vertex_uv(geometry_vertices[geo.vertex_offset + geometry_indices[index_base + 2u]].uv);
    float3 bary     = float3(saturate(1.0f - barycentrics.x - barycentrics.y), barycentrics);
    float2 texcoord = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    texcoord        = texcoord * geo.uv_tiling + geo.uv_offset;
    if (geo.uv_rotation != 0.0f)
    {
        texcoord = rotate_uv_90(texcoord, geo.uv_rotation);
    }

    float distance = length(hit_position - get_camera_position());
    float mip      = clamp(log2(max(distance, 1.0f)) - 2.0f, 0.0f, 4.0f);
    // Neighboring rays can hit different species/materials in the same wave.
    float alpha    = material_textures[NonUniformResourceIndex(get_material_texture_index(geo.material_index, material_texture_index_albedo))].SampleLevel(
        GET_SAMPLER(sampler_bilinear_wrap),
        texcoord,
        mip
    ).a;
    return alpha > get_alpha_threshold(hit_position, mat.alpha_cutoff);
}

// distance to the first alpha tested opaque blocker, negative when the ray is clear
float rt_trace_occluder(float3 origin, float3 direction, float t_min, float t_max)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = t_min;
    ray.TMax      = max(t_max, t_min);

    // opaque instances only, glass is 0x02, grass is 0x04 and casts through screen space depth only
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() == CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            float3 hit_position = origin + direction * query.CandidateTriangleRayT();
            if (rt_candidate_opaque(query.CandidateInstanceIndex(), query.CandidatePrimitiveIndex(), query.CandidateTriangleBarycentrics(), hit_position))
            {
                query.CommitNonOpaqueTriangleHit();
            }
        }
    }
    return query.CommittedStatus() == COMMITTED_NOTHING ? -1.0f : query.CommittedRayT();
}

float rt_trace_visibility(float3 origin, float3 direction, float t_max)
{
    return rt_trace_occluder(origin, direction, 0.001f, t_max) < 0.0f ? 1.0f : 0.0f;
}

// SIGMA_SHADOW needs binary cutout visibility and the nearest blocker distance
// to reconstruct the penumbra. Randomly transmitting through entire leaves
// changes both signals every frame, even in the interior of a static shadow.
// Leaf backlighting is handled by the receiver's subsurface scattering lobe.
float rt_trace_foliage_shadow(float3 origin, float3 direction, float t_max)
{
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.001f;
    ray.TMax = max(t_max, ray.TMin);
    // Do not accept/abort at the first hit: traversal order is not distance order.
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0x01, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
            continue;
        uint instance = query.CandidateInstanceIndex();
        uint primitive = query.CandidatePrimitiveIndex();
        float distance = query.CandidateTriangleRayT();
        float3 position = origin + direction * distance;
        MaterialParameters material = material_parameters[geometry_infos[instance].material_index];
        if (material.is_alpha_tested() && !rt_candidate_opaque(instance, primitive, query.CandidateTriangleBarycentrics(), position))
            continue;
        query.CommitNonOpaqueTriangleHit();
    }
    return query.CommittedStatus() == COMMITTED_NOTHING ? -1.0f : query.CommittedRayT();
}

#endif
#endif
