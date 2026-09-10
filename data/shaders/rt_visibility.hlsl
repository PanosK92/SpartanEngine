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
    float alpha    = material_textures[geo.material_index + material_texture_index_albedo].SampleLevel(
        GET_SAMPLER(sampler_bilinear_wrap),
        texcoord,
        mip
    ).a;
    return alpha > get_alpha_threshold(hit_position);
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

#endif
#endif
