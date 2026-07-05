/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
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

//= INCLUDES =========
#include "common.hlsl"
//====================

#define DEBUG_RAY_TRACING 0 // 1 = green hit, red miss, blue no geometry

// upper bound on the ggx alpha for the reflection ray spread, caps divergence on rough surfaces
static const float k_reflection_alpha_max = 0.6f;

struct [raypayload] Payload
{
    float3 position       : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float  hit_distance   : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 normal         : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float  material_index : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float3 albedo         : read(caller, closesthit, miss) : write(caller, closesthit, miss);
    float  roughness      : read(caller, closesthit, miss) : write(caller, closesthit, miss);
#if DEBUG_RAY_TRACING == 1
    bool hit              : read(caller) : write(closesthit, miss);
#endif
};

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

    // source roughness drives the ray spread, the vndf sampler is mirror sharp at roughness 0
    float4 normal_sample = tex_normal.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    uint material_index  = uint(normal_sample.a);
    MaterialParameters mat = material_parameters[material_index];
    float roughness = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    roughness       = lerp(roughness, mat.clearcoat_roughness, saturate(mat.clearcoat));
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
    
    Payload payload;
    payload.position       = float3(0, 0, 0);
    payload.hit_distance   = 0.0f;
    payload.normal         = float3(0, 0, 0);
    payload.material_index = 0.0f;
    payload.albedo         = float3(0, 0, 0);
    payload.roughness      = 0.0f;
#if DEBUG_RAY_TRACING == 1
    payload.hit = false;
#endif
    
    // ensure geometry_infos is in pipeline layout
    if (geometry_infos[0].vertex_offset == 0xFFFFFFFF)
        return;
    
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    
#if DEBUG_RAY_TRACING == 1
    tex_uav[launch_id] = payload.hit ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
#else
    tex_uav[launch_id]  = float4(payload.position, payload.hit_distance);
    tex_uav2[launch_id] = float4(payload.normal, payload.material_index);
    tex_uav3[launch_id] = float4(payload.albedo, payload.roughness);
#endif
}

[shader("miss")]
void miss(inout Payload payload : SV_RayPayload)
{
#if DEBUG_RAY_TRACING == 1
    payload.hit = false;
#endif
    
    // store ray direction for sky sampling
    payload.position     = WorldRayDirection();
    payload.hit_distance = 0.0f;
    payload.normal       = float3(0, 0, 0);
    payload.albedo       = float3(0, 0, 0);
    payload.roughness    = 0.0f;
}

[shader("closesthit")]
void closest_hit(inout Payload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
#if DEBUG_RAY_TRACING == 1
    payload.hit = true;
    return;
#endif
    
    uint material_index = InstanceID();
    MaterialParameters mat = material_parameters[material_index];
    
    uint instance_index = InstanceIndex();
    GeometryInfo geo    = geometry_infos[instance_index];
    
    // fetch triangle indices from the global index buffer
    uint primitive_index = PrimitiveIndex();
    uint index_base      = geo.index_offset + primitive_index * 3;
    uint i0 = geometry_indices[index_base + 0];
    uint i1 = geometry_indices[index_base + 1];
    uint i2 = geometry_indices[index_base + 2];
    
    // fetch vertex data from the global vertex buffer
    PulledVertex v0 = geometry_vertices[geo.vertex_offset + i0];
    PulledVertex v1 = geometry_vertices[geo.vertex_offset + i1];
    PulledVertex v2 = geometry_vertices[geo.vertex_offset + i2];

    float3 v0_normal  = unpack_vertex_oct(v0.normal);
    float3 v1_normal  = unpack_vertex_oct(v1.normal);
    float3 v2_normal  = unpack_vertex_oct(v2.normal);
    float3 v0_tangent = unpack_vertex_oct(v0.tangent);
    float3 v1_tangent = unpack_vertex_oct(v1.tangent);
    float3 v2_tangent = unpack_vertex_oct(v2.tangent);
    float2 v0_uv      = unpack_vertex_uv(v0.uv);
    float2 v1_uv      = unpack_vertex_uv(v1.uv);
    float2 v2_uv      = unpack_vertex_uv(v2.uv);
    
    // barycentric interpolation
    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    float2 texcoord       = v0_uv * bary.x + v1_uv * bary.y + v2_uv * bary.z;
    float3 normal_object  = normalize(v0_normal * bary.x + v1_normal * bary.y + v2_normal * bary.z);
    float3 tangent_object = normalize(v0_tangent * bary.x + v1_tangent * bary.y + v2_tangent * bary.z);
    
    // world space transform
    float3x3 obj_to_world = (float3x3)ObjectToWorld4x3();
    float3 normal_world   = normalize(mul(normal_object, obj_to_world));
    float3 tangent_world  = normalize(mul(tangent_object, obj_to_world));
    
    // world space uv, full uv state is per renderable from geometry_infos[InstanceIndex()]
    float3 hit_pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    if (mat.is_terrain())
    {
        // terrain maps planar world xz with tiling as repeats per meter, matches the raster path
        texcoord = hit_pos.xz * geo.uv_tiling + geo.uv_offset;
    }
    else if (geo.uv_world_space > 0.0f)
    {
        float2 uv_world = compute_world_space_uv(hit_pos, normal_world);
        uv_world        = uv_world * geo.uv_tiling + geo.uv_offset;

        // branchless inversion
        float2 invert_mask = step(0.5f, geo.uv_invert);
        texcoord           = lerp(uv_world, 1.0f - frac(uv_world) + floor(uv_world), invert_mask);
    }
    else
    {
        texcoord = texcoord * geo.uv_tiling + geo.uv_offset;
    }

    if (geo.uv_rotation != 0.0f)
        texcoord = rotate_uv_90(texcoord, geo.uv_rotation);
    
    float hit_distance      = RayTCurrent();
    float n_dot_v_hit       = saturate(dot(normal_world, -WorldRayDirection()));
    float grazing_mip_boost = lerp(2.5f, 0.0f, n_dot_v_hit);
    float distance_mip      = log2(max(hit_distance, 1.0f));
    float mip_level         = clamp(distance_mip + grazing_mip_boost, 0.0f, 7.0f);

    // normal mapping, mild mip bias to avoid specular sparkle on detailed normal maps
    if (mat.has_texture_normal())
    {
        uint  normal_texture_index = material_index + material_texture_index_normal;
        float normal_mip           = clamp(distance_mip + lerp(1.5f, 0.0f, n_dot_v_hit), 0.0f, 5.0f);
        float3 normal_sample       = material_textures[normal_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, normal_mip).xyz;
        normal_sample              = normal_sample * 2.0f - 1.0f;

        float3 bitangent_world = normalize(cross(normal_world, tangent_world));
        float3x3 tbn           = float3x3(tangent_world, bitangent_world, normal_world);
        normal_world           = normalize(mul(normal_sample, tbn));
    }
    
    // albedo, mip biased by hit distance and grazing angle to avoid texture moire
    float3 albedo = mat.color.rgb;
    if (mat.has_texture_albedo())
    {
        uint  albedo_texture_index = material_index + material_texture_index_albedo;
        float4 sampled_albedo = material_textures[albedo_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        if (mat.is_albedo_srgb())
        {
            sampled_albedo.rgb = srgb_to_linear(sampled_albedo.rgb);
        }
        if (sampled_albedo.a > 0.01f)
        {
            albedo = sampled_albedo.rgb * mat.color.rgb;
        }
    }

    float roughness = mat.roughness;
    if (mat.has_texture_roughness())
    {
        uint roughness_texture_index = material_index + material_texture_index_roughness;
        roughness *= material_textures[roughness_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).g;
    }
    roughness = max(roughness, 0.04f);
    
    payload.position       = hit_pos;
    payload.hit_distance   = RayTCurrent();
    payload.normal         = normal_world;
    payload.material_index = float(material_index);
    payload.albedo         = albedo;
    payload.roughness      = roughness;
}
