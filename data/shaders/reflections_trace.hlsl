/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef DEBUG_RAY_TRACING
#define DEBUG_RAY_TRACING 0 // 1 = green hit, red miss, blue no geometry
#endif

//= INCLUDES ==================
#include "common.hlsl"
#include "common_ray_surface.hlsl"
//=============================

// upper bound on the ggx alpha for the reflection ray spread, caps divergence on rough surfaces
static const float k_reflection_alpha_max = 0.6f;

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv         = (launch_id + 0.5f) / launch_size;

    // early out for sky
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
#if DEBUG_RAY_TRACING == 1
        tex_uav[launch_id] = float4(0, 0, 1, 1);
#else
        tex_uav[launch_id]  = float4(0, 0, 0, 0);
        tex_uav2[launch_id] = float4(0, 0, 0, 0);
        tex_uav3[launch_id] = float4(0, 0, 0, 0);
#endif
        return;
    }

    // rt reflections own the full primary specular lobe, restir contributes diffuse only primary gi
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 V         = normalize(get_camera_position() - pos_ws);

    // the lobe the tracer carries, its roughness drives the ray spread, the vndf sampler is mirror sharp at roughness 0
    float4 normal_sample   = tex_normal.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    MaterialParameters mat = material_parameters[unpack_material_index(normal_sample.a)];
    float coat;
    float roughness = get_rt_reflection_roughness(uv, coat);

    // skip near-diffuse lobes, apply fades them out and ibl covers the rest
    if (get_rt_reflection_weight(roughness) <= 0.0f)
    {
#if DEBUG_RAY_TRACING == 1
        tex_uav[launch_id] = float4(0, 0, 1, 1);
#else
        tex_uav[launch_id]  = float4(0.0f, 0.0f, 0.0f, -1.0f);
        tex_uav2[launch_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_uav3[launch_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
#endif
        return;
    }

    float alpha     = min(ggx_alpha_from_roughness(roughness), k_reflection_alpha_max);

    // per pixel per frame low discrepancy sample, r2 frame rotation for the denoiser to accumulate
    float  frame_index = (float)buffer_frame.frame;
    float2 xi;
    xi.x = frac(hash(float2(launch_id))         + frame_index * 0.7548776662f);
    xi.y = frac(hash(float2(launch_id) + 31.7f) + frame_index * 0.5698402909f);

    // sample a microfacet normal and reflect about it to turn the mirror ray into a glossy lobe
    float3 tangent   = normalize(cross(abs(normal_ws.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f), normal_ws));
    float3 bitangent = cross(normal_ws, tangent);
    float3 v_local   = float3(dot(V, tangent), dot(V, bitangent), dot(V, normal_ws));
    v_local.z        = max(v_local.z, 1e-4f);
    float3 h_local   = ggx_vndf_sample(v_local, xi, alpha);
    float3 H         = h_local.x * tangent + h_local.y * bitangent + h_local.z * normal_ws;
    float3 R         = reflect(-V, H);

    // a grazing sample can push the ray below the surface, fall back to the mirror direction
    if (dot(R, normal_ws) <= 0.0f)
    {
        R = reflect(-V, normal_ws);
    }

    // the sea mostly reflects sky, and misses already sample it prefiltered by roughness, a jittered lobe only adds sparkle noise
    // the sea surface is not in the tlas either, a wave slope reflecting below the horizon would pass through it and return the seabed
    // on real water that ray meets the next wave and mirrors the sky above it, so fold it back over the horizon
    bool is_water = (mat.flags & (1u << 13)) != 0;
    if (is_water && buffer_frame.ocean_enabled > 0.5f)
    {
        R   = reflect(-V, normal_ws);
        R.y = max(abs(R.y), 0.01f);
    }

    // ray origin offset scaled with camera distance, pushed along the reflection at grazing angles
    float camera_distance = length(get_camera_position() - pos_ws);
    float base_offset     = 0.001f + camera_distance * 0.0001f;
    float n_dot_v         = saturate(dot(normal_ws, V));
    float grazing_factor  = 1.0f - n_dot_v;
    float3 ray_origin     = pos_ws + normal_ws * base_offset + R * base_offset * grazing_factor * 2.0f;

    RayDesc ray;
    ray.Origin    = ray_origin;
    ray.Direction = normalize(R);
    ray.TMin      = 0.0001f;
    ray.TMax      = 1000.0f;

    // the pixel's cone leaves the surface as wide as the pixel, and spreads by the pixel angle plus
    // the lobe width, textures at the hit are prefiltered to what the denoised lobe resolves anyway
    float pixel_angle = 2.0f * tan(buffer_frame.camera_fov * 0.5f) / get_render_resolution_active().x;
    float2 cone       = float2(camera_distance * pixel_angle, pixel_angle + alpha);

    HitPayload hit_record;

    // ensure geometry_infos is in pipeline layout
    if (geometry_infos[0].vertex_offset == 0xFFFFFFFF)
        return;

    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, hit_record);

#if DEBUG_RAY_TRACING == 1
    tex_uav[launch_id] = hit_record.hit_distance >= 0.0f ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
#else
    float hit_distance = hit_record.hit_distance;
    uint instance      = hit_record.instance_index;
    uint primitive     = hit_record.primitive_index;
    uint barycentrics  = hit_record.barycentrics_packed;
    if (hit_distance < 0.0f)
    {
        // zero distance marks a miss, the direction goes in position for sky sampling
        tex_uav[launch_id]  = float4(ray.Direction, 0.0f);
        tex_uav2[launch_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_uav3[launch_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_uav4[launch_id] = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    RaySurface surface  = ray_surface_reconstruct(hit_distance, instance, primitive, unpack_hit_barycentrics(barycentrics), ray, cone);
    tex_uav[launch_id]  = float4(surface.position, surface.hit_distance);
    tex_uav2[launch_id] = float4(surface.normal, pack_material_index(surface.material_index));
    tex_uav3[launch_id] = float4(surface.albedo, surface.roughness);
    tex_uav4[launch_id] = float4(surface.emission, surface.metallic);
#endif
}
