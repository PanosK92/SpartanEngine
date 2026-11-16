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

// albedo texture at a candidate hit, the mip grows with the distance to the camera
float4 rt_candidate_albedo(GeometryInfo geo, uint primitive_index, float2 barycentrics, float3 hit_position)
{
    uint index_base = geo.index_offset + primitive_index * 3u;
    float2 uv0 = unpack_vertex_uv(load_pulled_vertex(geo.vertex_offset + geometry_indices[index_base + 0u]).uv);
    float2 uv1 = unpack_vertex_uv(load_pulled_vertex(geo.vertex_offset + geometry_indices[index_base + 1u]).uv);
    float2 uv2 = unpack_vertex_uv(load_pulled_vertex(geo.vertex_offset + geometry_indices[index_base + 2u]).uv);
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
    return material_textures[NonUniformResourceIndex(get_material_texture_index(geo.material_index, material_texture_index_albedo))].SampleLevel(
        GET_SAMPLER(sampler_bilinear_wrap),
        texcoord,
        mip
    );
}

// world space uvs need the hit normal, cutouts never use them
bool rt_has_albedo_texture(GeometryInfo geo, MaterialParameters mat)
{
    return mat.has_texture_albedo() && !mat.is_terrain() && geo.uv_world_space <= 0.0f;
}

bool rt_candidate_opaque(uint instance_index, uint primitive_index, float2 barycentrics, float3 hit_position)
{
    GeometryInfo geo       = geometry_infos[instance_index];
    MaterialParameters mat = material_parameters[geo.material_index];
    if (!rt_has_albedo_texture(geo, mat))
    {
        return true;
    }

    float alpha = rt_candidate_albedo(geo, primitive_index, barycentrics, hit_position).a;
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

// todo: the 50 is repeated in path_tracing.hlsl, restir_path.hlsl, reflections_apply.hlsl and ModelImporter.cpp, move it to one
// shared constant, and decide on one color clamp, reflections_apply.hlsl keeps 2% where the ray traced paths allow 0
float3 rt_glass_tint(float3 color, float absorption, float distance)
{
    float exponent = max(absorption, 0.0f) * max(distance, 0.0f) * 50.0f;
    return exponent > 0.0f ? pow(saturate(color), exponent) : 1.0f;
}

float rt_trace_transmittance(float3 origin, float3 direction, float t_max)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = max(t_max, ray.TMin);

    // glass instances are opaque in the tlas, forcing them non opaque hands every crossing to the loop,
    // the mask only holds transparent instances so opaque geometry is never forced
    float3 transmittance = 1.0f;
    bool blocked         = false;
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_FORCE_NON_OPAQUE, 0x02, ray);
    while (query.Proceed())
    {
        uint instance               = query.CandidateInstanceIndex();
        uint primitive              = query.CandidatePrimitiveIndex();
        float3 position             = origin + direction * query.CandidateTriangleRayT();
        GeometryInfo geo            = geometry_infos[instance];
        MaterialParameters material = material_parameters[geo.material_index];
        float4 albedo               = 1.0f;
        if (rt_has_albedo_texture(geo, material))
        {
            albedo = rt_candidate_albedo(geo, primitive, query.CandidateTriangleBarycentrics(), position);
        }

        if (material.is_alpha_tested())
        {
            if (albedo.a <= get_alpha_threshold(position, material.alpha_cutoff))
            {
                continue;
            }

            blocked = true;
        }
        else if (material.alpha_blend > 0.5f)
        {
            transmittance *= 1.0f - saturate(material.color.a * albedo.a);
        }
        else
        {
            float ior     = material.ior > 1.0f ? material.ior : 1.5f;
            float f0      = (ior - 1.0f) / (ior + 1.0f);
            transmittance *= 1.0f - f0 * f0;
            if (material.is_thin_walled())
            {
                transmittance *= rt_glass_tint(material.color.rgb * albedo.rgb, material.absorption, material.thickness);
            }
            else if (!query.CandidateTriangleFrontFace())
            {
                transmittance *= rt_glass_tint(material.color.rgb, material.absorption, material.thickness);
            }
        }

        if (blocked || all(transmittance < 0.01f))
        {
            blocked = true;
            query.Abort();
        }
    }

    // TODO: rgb shadows, the transmittance is rgb but the shadow masks have one channel, so colored glass casts a grey
    // shadow. the sun mask has a free channel (b), the local light slices are full (visibility, blocker distance,
    // distance to light, light size) and need a second target, sigma translucency takes rgb as well
    // TODO: also return the distance to the nearest glass hit, sigma needs it, see nrd_pack_shadows.hlsl
    return blocked ? 0.0f : dot(transmittance, float3(0.2126f, 0.7152f, 0.0722f));
}

// distance to the nearest surface along the ray, negative when it is clear, a ray started
// just under a closed surface measures how much of the solid lies between it and the far side
float rt_trace_nearest(float3 origin, float3 direction, float t_max)
{
    return rt_trace_foliage_shadow(origin, direction, t_max);
}

#endif
#endif
