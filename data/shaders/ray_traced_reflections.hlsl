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
    
    // skip rough surfaces
    float4 material_sample = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float roughness        = material_sample.r;
    if (roughness >= 0.9f)
    {
        tex_uav[launch_id]  = float4(0, 0, 0, -1);
        tex_uav2[launch_id] = float4(0, 0, 0, 0);
        tex_uav3[launch_id] = float4(0, 0, 0, 0);
        return;
    }
    
    // reflection ray setup
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 V         = normalize(buffer_frame.camera_position - pos_ws);
    float3 R         = reflect(-V, normal_ws);
    
    // ray origin offset
    float camera_distance = length(buffer_frame.camera_position - pos_ws);
    float base_offset     = 0.0001f + camera_distance * 0.00005f;
    float n_dot_v         = saturate(dot(normal_ws, V));
    float grazing_factor  = 1.0f - n_dot_v;
    float3 ray_origin     = pos_ws + normal_ws * base_offset + R * base_offset * grazing_factor * 0.5f;
    
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
    if (geometry_infos[0].vertex_count == 0xFFFFFFFF)
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

uint64_t make_address(uint2 addr)
{
    return uint64_t(addr.x) | (uint64_t(addr.y) << 32);
}

static const uint VERTEX_STRIDE = 44; // pos(12) + uv(8) + normal(12) + tangent(12)

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
    
    // fetch triangle indices
    uint64_t index_addr   = make_address(geo.index_buffer_address);
    uint primitive_index  = PrimitiveIndex();
    uint index_offset     = (geo.index_offset + primitive_index * 3) * 4;
    
    uint i0 = vk::RawBufferLoad<uint>(index_addr + index_offset + 0);
    uint i1 = vk::RawBufferLoad<uint>(index_addr + index_offset + 4);
    uint i2 = vk::RawBufferLoad<uint>(index_addr + index_offset + 8);
    
    // fetch vertex data
    uint64_t vertex_addr = make_address(geo.vertex_buffer_address);
    uint v0_offset       = (geo.vertex_offset + i0) * VERTEX_STRIDE;
    uint v1_offset       = (geo.vertex_offset + i1) * VERTEX_STRIDE;
    uint v2_offset       = (geo.vertex_offset + i2) * VERTEX_STRIDE;
    
    float2 uv0 = vk::RawBufferLoad<float2>(vertex_addr + v0_offset + 12);
    float2 uv1 = vk::RawBufferLoad<float2>(vertex_addr + v1_offset + 12);
    float2 uv2 = vk::RawBufferLoad<float2>(vertex_addr + v2_offset + 12);
    
    float3 n0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 20);
    float3 n1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 20);
    float3 n2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 20);
    
    float3 t0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 32);
    float3 t1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 32);
    float3 t2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 32);
    
    // barycentric interpolation
    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    float2 texcoord       = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    float3 normal_object  = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 tangent_object = normalize(t0 * bary.x + t1 * bary.y + t2 * bary.z);
    
    // world space transform
    float3x3 obj_to_world = (float3x3)ObjectToWorld4x3();
    float3 normal_world   = normalize(mul(normal_object, obj_to_world));
    float3 tangent_world  = normalize(mul(tangent_object, obj_to_world));
    
    // world space uv transformation
    float3 hit_pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    if (any(mat.world_space_uv))
    {
        float3 abs_normal = abs(normal_world);
        float2 uv_world   = hit_pos.yz * abs_normal.x + 
                            hit_pos.xz * abs_normal.y + 
                            hit_pos.xy * abs_normal.z;
        uv_world          = uv_world * mat.tiling + mat.offset;
        
        // branchless inversion
        float2 invert_mask = step(0.5f, mat.invert_uv);
        texcoord           = lerp(uv_world, 1.0f - frac(uv_world) + floor(uv_world), invert_mask);
    }
    else
    {
        texcoord = texcoord * mat.tiling + mat.offset;
    }
    
    // normal mapping
    if (mat.has_texture_normal())
    {
        uint normal_texture_index = material_index + material_texture_index_normal;
        float3 normal_sample      = material_textures[normal_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, 0).xyz;
        normal_sample             = normal_sample * 2.0f - 1.0f;
        
        float3 bitangent_world = normalize(cross(normal_world, tangent_world));
        float3x3 tbn           = float3x3(tangent_world, bitangent_world, normal_world);
        normal_world           = normalize(mul(normal_sample, tbn));
    }
    
    // albedo
    float3 albedo = mat.color.rgb;
    if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = material_index + material_texture_index_albedo;
        float hit_distance = RayTCurrent();
        float mip_level    = clamp(log2(max(hit_distance * 0.5f, 1.0f)), 0.0f, 4.0f);
        
        float4 sampled_albedo = material_textures[albedo_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        if (sampled_albedo.a > 0.01f)
            albedo = sampled_albedo.rgb * mat.color.rgb;
    }
    
    payload.position       = hit_pos;
    payload.hit_distance   = RayTCurrent();
    payload.normal         = normal_world;
    payload.material_index = float(material_index);
    payload.albedo         = albedo;
    payload.roughness      = mat.roughness;
}
