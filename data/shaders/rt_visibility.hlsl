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
#include "subsurface_scattering.hlsl"

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

// the blas is built once from the full detail mesh, but the g-buffer draws whichever lod the distance selects,
// a coarse lod's flat triangles sit slightly inside the curved full detail surface, so a ray starting there hits it.
// a lod is only drawn when it is within r.geometry_error_pixels of the full detail mesh on screen, so moving the
// origin out by that distance puts it outside the blas surface
float rt_lod_shadow_offset(float camera_distance)
{
    float pixel_size = 2.0f * camera_distance / (abs(buffer_frame.projection[1][1]) * get_render_resolution_active().y);
    return buffer_frame.geometry_error_pixels * pixel_size;
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
    query.TraceRayInline(tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0x01, ray);
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
    // Cull back-facing triangles on terrain (the only instances without CULL_DISABLE) so coarse
    // terrain BLAS proxies floating above refined VG terrain cannot self-shadow from underneath.
    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0x01, ray);
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

// Deterministic Beer-Lambert sunlight penetration through a tree/shrub canopy for foliage receivers.
// Solid occluders (terrain, mountains, buildings, tree trunks, non-foliage cutouts) still block 100%,
// while intervening translucent leaf cards attenuate light smoothly instead of turning shaded crowns pitch black.
// Returns float2(transmission, blocker_distance).
float2 rt_trace_canopy_transmission(float3 origin, float3 direction, float t_max)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.002f;
    ray.TMax      = max(t_max, ray.TMin);

    float optical_depth     = 0.0f;
    float nearest_foliage_t = t_max;
    bool  hit_foliage       = false;

    RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0x01, ray);
    while (query.Proceed())
    {
        if (query.CandidateType() != CANDIDATE_NON_OPAQUE_TRIANGLE)
        {
            continue;
        }
        uint instance = query.CandidateInstanceIndex();
        uint primitive = query.CandidatePrimitiveIndex();
        float distance = query.CandidateTriangleRayT();
        float3 position = origin + direction * distance;
        MaterialParameters material = material_parameters[geometry_infos[instance].material_index];
        if (material.is_alpha_tested() && !rt_candidate_opaque(instance, primitive, query.CandidateTriangleBarycentrics(), position))
        {
            continue;
        }
        const bool hit_is_foliage = (material.flags & ((1u << 11) | (1u << 12) | (1u << 21))) != 0u;
        if (hit_is_foliage && material.subsurface_scattering > 0.0f)
        {
            // Attenuate deterministically per translucent leaf sheet (zero per-frame flicker for SIGMA)
            if (optical_depth < 4.8f)
            {
                optical_depth += lerp(0.95f, 0.52f, saturate(material.subsurface_scattering));
            }
            nearest_foliage_t = min(nearest_foliage_t, distance);
            hit_foliage       = true;
            continue;
        }
        query.CommitNonOpaqueTriangleHit();
    }

    // Any solid occluder (terrain, mountain, building, trunk, fence) blocks direct sun completely
    if (query.CommittedStatus() != COMMITTED_NOTHING)
    {
        return float2(0.0f, query.CommittedRayT());
    }
    if (!hit_foliage)
    {
        return float2(1.0f, 0.0f);
    }
    // Single outer leaf layers stay translucent while deep multi-layered crowns cast rich, grounded shadows
    float transmission = max(exp(-optical_depth), 0.03f);
    return float2(transmission, nearest_foliage_t);
}

// todo: the 50 is repeated in path_tracing.hlsl, restir_path.hlsl, reflections_apply.hlsl and ModelImporter.cpp, move it to one
// shared constant, and decide on one color clamp, reflections_apply.hlsl keeps 2% where the ray traced paths allow 0
float3 rt_glass_tint(float3 color, float absorption, float distance)
{
    float exponent = max(absorption, 0.0f) * max(distance, 0.0f) * 50.0f;
    return exponent > 0.0f ? pow(saturate(color), exponent) : 1.0f;
}

// world space shading normal at a candidate hit, interpolated so a tessellated sphere doesn't print its facets into the shadow
float3 rt_candidate_normal(GeometryInfo geo, uint primitive_index, float2 barycentrics)
{
    uint index_base = geo.index_offset + primitive_index * 3u;
    float3 n0       = unpack_vertex_oct(load_pulled_vertex(geo.vertex_offset + geometry_indices[index_base + 0u]).normal);
    float3 n1       = unpack_vertex_oct(load_pulled_vertex(geo.vertex_offset + geometry_indices[index_base + 1u]).normal);
    float3 n2       = unpack_vertex_oct(load_pulled_vertex(geo.vertex_offset + geometry_indices[index_base + 2u]).normal);
    float3 bary     = float3(saturate(1.0f - barycentrics.x - barycentrics.y), barycentrics);
    float3 normal   = n0 * bary.x + n1 * bary.y + n2 * bary.z;

    // normals take the inverse transpose, a non uniformly scaled instance would bend them otherwise
    float3x3 world_to_object = float3x3(geo.world_to_object_0.xyz, geo.world_to_object_1.xyz, geo.world_to_object_2.xyz);
    return normalize(mul(normal, transpose(world_to_object)));
}

// schlick, the share of light an interface lets through at this angle
float rt_fresnel_transmission(float cos_theta, float ior)
{
    float r0 = (ior - 1.0f) / (ior + 1.0f);
    r0      *= r0;
    float m  = 1.0f - saturate(cos_theta);
    float m2 = m * m;
    return 1.0f - (r0 + (1.0f - r0) * m2 * m2 * m);
}

struct RtTransmittance
{
    float3 color;    // light that makes it through, 0 once less than 1% is left
    float  distance; // distance to the nearest glass hit, negative when the ray crossed none, sigma sizes the penumbra with it
};

RtTransmittance rt_trace_transmittance(float3 origin, float3 direction, float t_max)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = max(t_max, ray.TMin);

    // glass instances are opaque in the tlas, forcing them non opaque hands every crossing to the loop,
    // the mask only holds transparent instances so opaque geometry is never forced
    float3 transmittance = 1.0f;
    float nearest        = -1.0f;
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

        // a cutout is a hole, what the cutout keeps is still glass (only transparent instances reach this loop), so it
        // falls through to the glass below like the raster, which only discards under the cutoff
        if (material.is_alpha_tested() && albedo.a <= get_alpha_threshold(position, material.alpha_cutoff))
        {
            continue;
        }

        // traversal is not in distance order, keep the closest crossing that takes light away
        float distance = query.CandidateTriangleRayT();
        nearest        = nearest < 0.0f ? distance : min(nearest, distance);

        if (material.alpha_blend > 0.5f)
        {
            transmittance *= 1.0f - saturate(material.color.a * albedo.a);
        }
        else
        {
            // the tint below takes KHR_materials_volume's authored thickness, fresnel takes the entry angle at the exit
            // too since shadow rays don't refract (exact for spheres and parallel slabs), no total internal reflection
            // since the unrefracted ray would fake a hard dark ring
            float ior          = material.ior > 1.0f ? material.ior : 1.5f;
            float cos_theta    = abs(dot(direction, rt_candidate_normal(geo, primitive, query.CandidateTriangleBarycentrics())));
            float transmission = rt_fresnel_transmission(cos_theta, ior);
            // a pane is one triangle standing in for both of its interfaces, a volume meets every interface as its own hit
            transmittance *= material.is_thin_walled() ? transmission * transmission : transmission;
            if (material.is_thin_walled())
            {
                transmittance *= rt_glass_tint(material.color.rgb * albedo.rgb, material.absorption, material.thickness);
            }
            else if (!query.CandidateTriangleFrontFace())
            {
                transmittance *= rt_glass_tint(material.color.rgb, material.absorption, material.thickness);
            }
        }

        if (all(transmittance < 0.01f))
        {
            blocked = true;
            query.Abort();
        }
    }

    RtTransmittance result;
    result.color    = blocked ? 0.0f : transmittance;
    result.distance = nearest;
    return result;
}

// distance to the nearest surface along the ray, negative when it is clear, a ray started
// just under a closed surface measures how much of the solid lies between it and the far side
float rt_trace_nearest(float3 origin, float3 direction, float t_max)
{
    return rt_trace_foliage_shadow(origin, direction, t_max);
}

// Measure a scattering slab toward the light, then test visibility beyond its far surface.
float3 rt_subsurface_transmittance(float3 position, float3 normal, float3 l, float distance, float safety, float3 radius)
{
    float max_thickness = 16.0f * max(radius.r, max(radius.g, radius.b));
    const float inset = 0.0005f;
    float3 origin = position - normal * inset;
    float exit = rt_trace_nearest(origin, l, min(distance, max_thickness));
    if (exit < 0.0f) return 0.0f;
    float clearance = distance - exit - safety;
    if (clearance > 0.002f && rt_trace_visibility(origin + l * (exit + 0.001f), l, clearance) <= 0.0f)
        return 0.0f;
    return subsurface_burley_transmittance(exit + inset, radius);
}

#endif
#endif
