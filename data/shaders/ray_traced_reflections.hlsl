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

// debug visualization toggle
// 0 = normal rendering
// 1 = green = hit, red = miss, blue = no geometry
// 2 = visualize UVs (R=U, G=V)
// 3 = visualize normals  
// 4 = visualize instance index
// 5 = material color only (no texture)
// 6 = visualize primitive index
// 7 = visualize index values (diagnose index buffer reading)
// 8 = visualize vertex offset (diagnose vertex buffer reading)
// 9 = visualize raw geometry info (check if offsets are correct per instance)
#define DEBUG_RAY_TRACING 0

struct [raypayload] Payload
{
    float3 color : read(caller, closesthit, miss) : write(caller, closesthit, miss);
#if DEBUG_RAY_TRACING == 1
    bool hit     : read(caller) : write(closesthit, miss);
#endif
};

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv         = (launch_id + 0.5f) / launch_size;
    
    // check if there's geometry at this pixel
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
#if DEBUG_RAY_TRACING == 1
        tex_uav[launch_id] = float4(0, 0, 1, 1); // blue = no geometry
#else
        // sample skysphere (tex3) for pixels with no geometry
        // use far plane position to compute view direction (avoids issues with depth=0)
        float3 far_pos     = get_position(1.0f, uv);
        float3 ray_dir     = normalize(far_pos - buffer_frame.camera_position);
        float2 sky_uv      = direction_sphere_uv(ray_dir);
        float3 sky_col     = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), sky_uv, 0).rgb;
        tex_uav[launch_id] = float4(sky_col, 0.0f);
#endif
        return;
    }
    
    // check surface roughness - skip ray tracing for fully rough (diffuse) surfaces
    float4 material_sample = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float roughness        = material_sample.r;
    if (roughness >= 0.9f) // near-fully rough surfaces don't reflect
    {
        tex_uav[launch_id] = float4(0, 0, 0, 0);
        return;
    }
    
    // compute reflection ray in world space
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 V         = normalize(buffer_frame.camera_position - pos_ws);
    float3 R         = reflect(-V, normal_ws);
    
    // scale offset based on distance from camera - closer surfaces need smaller offsets
    float camera_distance = length(buffer_frame.camera_position - pos_ws);
    float base_offset     = 0.0001f + camera_distance * 0.00005f; // scales from 0.0001 to ~0.005 at 100m
    
    // offset along normal and slightly along reflection direction for grazing angles
    float n_dot_v         = saturate(dot(normal_ws, V));
    float grazing_factor  = 1.0f - n_dot_v; // higher at grazing angles
    float3 ray_origin     = pos_ws + normal_ws * base_offset + R * base_offset * grazing_factor * 0.5f;
    
    // setup ray with minimal TMin to catch small nearby geometry
    RayDesc ray;
    ray.Origin    = ray_origin;
    ray.Direction = normalize(R);
    ray.TMin      = 0.0001f;
    ray.TMax      = 1000.0f;
    
    // trace
    Payload payload;
    payload.color = float3(0, 0, 0);
#if DEBUG_RAY_TRACING == 1
    payload.hit   = false;
#endif
    
    // touch geometry_infos to ensure it's included in the pipeline layout (used in closest_hit)
    // this is a no-op that the compiler won't optimize away due to the buffer access
    if (geometry_infos[0].vertex_count == 0xFFFFFFFF)
        return;
    
    // don't cull back faces - corners and crevices often need to show back-facing geometry
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    
    // output - debug mode 1 shows green for hits, red for misses
#if DEBUG_RAY_TRACING == 1
    tex_uav[launch_id] = payload.hit ? float4(0, 1, 0, 1) : float4(1, 0, 0, 1);
#else
    tex_uav[launch_id] = float4(payload.color, 1.0f);
#endif
}

[shader("miss")]
void miss(inout Payload payload : SV_RayPayload)
{
#if DEBUG_RAY_TRACING == 1
    payload.hit = false;
#endif
    
    // sample skysphere for missed rays
    float3 ray_dir = WorldRayDirection();
    float2 uv      = direction_sphere_uv(ray_dir);
    payload.color  = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uv, 0).rgb;
}

// helper to reconstruct uint64 address from two uint32 values
uint64_t make_address(uint2 addr)
{
    return uint64_t(addr.x) | (uint64_t(addr.y) << 32);
}

// vertex stride in bytes (float3 pos + float2 tex + float3 nor + float3 tan = 44 bytes)
static const uint VERTEX_STRIDE = 44;

[shader("closesthit")]
void closest_hit(inout Payload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
#if DEBUG_RAY_TRACING == 1
    payload.hit = true;
    return;
#endif
    
    // material index from tlas instance custom data
    uint material_index = InstanceID();
    MaterialParameters mat = material_parameters[material_index];
    
    // get geometry info for this instance
    uint instance_index = InstanceIndex();
    GeometryInfo geo    = geometry_infos[instance_index];
    
    // get triangle indices using buffer device address
    uint64_t index_addr   = make_address(geo.index_buffer_address);
    uint primitive_index  = PrimitiveIndex();
    uint index_offset     = (geo.index_offset + primitive_index * 3) * 4; // 3 indices per tri, 4 bytes per uint
    
    uint i0 = vk::RawBufferLoad<uint>(index_addr + index_offset + 0);
    uint i1 = vk::RawBufferLoad<uint>(index_addr + index_offset + 4);
    uint i2 = vk::RawBufferLoad<uint>(index_addr + index_offset + 8);
    
    // get vertex buffer address
    uint64_t vertex_addr = make_address(geo.vertex_buffer_address);
    uint v0_offset       = (geo.vertex_offset + i0) * VERTEX_STRIDE;
    uint v1_offset       = (geo.vertex_offset + i1) * VERTEX_STRIDE;
    uint v2_offset       = (geo.vertex_offset + i2) * VERTEX_STRIDE;
    
    // load vertex data (position: 0, texcoord: 12, normal: 20, tangent: 32)
    float2 uv0 = vk::RawBufferLoad<float2>(vertex_addr + v0_offset + 12);
    float2 uv1 = vk::RawBufferLoad<float2>(vertex_addr + v1_offset + 12);
    float2 uv2 = vk::RawBufferLoad<float2>(vertex_addr + v2_offset + 12);
    
    float3 n0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 20);
    float3 n1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 20);
    float3 n2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 20);
    
    // load tangents for normal mapping (offset 32 in vertex)
    float3 t0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 32);
    float3 t1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 32);
    float3 t2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 32);
    
    // barycentric interpolation
    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    float2 texcoord       = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    float3 normal_object  = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 tangent_object = normalize(t0 * bary.x + t1 * bary.y + t2 * bary.z);
    
    // transform normal and tangent to world space
    float3x3 obj_to_world = (float3x3)ObjectToWorld4x3();
    float3 normal_world   = normalize(mul(normal_object, obj_to_world));
    float3 tangent_world  = normalize(mul(tangent_object, obj_to_world));

#if DEBUG_RAY_TRACING == 2
    // visualize raw uvs (before tiling) as colors
    payload.color = float3(frac(texcoord.x), frac(texcoord.y), 0);
    return;
#elif DEBUG_RAY_TRACING == 3
    // visualize world normals
    payload.color = normal_world * 0.5f + 0.5f;
    return;
#elif DEBUG_RAY_TRACING == 4
    // visualize instance index (different color per instance)
    float hue = frac(instance_index * 0.1f);
    payload.color = float3(hue, 1.0f - hue, frac(hue * 2.0f));
    return;
#elif DEBUG_RAY_TRACING == 5
    // material color only (no texture) with simple lighting
    float3 light_dir = normalize(float3(0.5f, 1.0f, 0.3f));
    float n_dot_l    = saturate(dot(normal_world, light_dir) * 0.5f + 0.5f);
    payload.color    = mat.color.rgb * n_dot_l;
    return;
#elif DEBUG_RAY_TRACING == 6
    // visualize primitive index - helps identify triangle access patterns
    float prim_hue = frac(primitive_index * 0.001f);
    payload.color = float3(prim_hue, frac(prim_hue * 3.0f), frac(prim_hue * 7.0f));
    return;
#elif DEBUG_RAY_TRACING == 7
    // visualize index values - if mangled, indices are being read wrong
    // valid indices should give smooth color variation
    // random/noisy = index buffer reading is broken
    payload.color = float3(
        frac(float(i0) * 0.001f),
        frac(float(i1) * 0.001f),
        frac(float(i2) * 0.001f)
    );
    return;
#elif DEBUG_RAY_TRACING == 8
    // visualize computed vertex byte offset - should be smooth
    // if noisy, the vertex offset calculation is wrong
    payload.color = float3(
        frac(float(v0_offset) * 0.00001f),
        frac(float(geo.vertex_offset) * 0.001f),
        frac(float(geo.index_offset) * 0.001f)
    );
    return;
#elif DEBUG_RAY_TRACING == 9
    // visualize raw geometry info - helps check if per-instance data is correct
    // r = instance index (should show distinct colors per object)
    // g = vertex_offset (should be different for different submeshes)
    // b = index_offset (should be different for different submeshes)
    // if all objects show same color = geometry_infos not being indexed correctly
    payload.color = float3(
        frac(float(instance_index) * 0.1f),
        frac(float(geo.vertex_offset) * 0.0001f),
        frac(float(geo.index_offset) * 0.0001f)
    );
    return;
#endif
    
    // apply material tiling to uvs
    texcoord = texcoord * mat.tiling + mat.offset;
    
    // sample normal map if material has one for finer surface detail
    if (mat.has_texture_normal())
    {
        uint normal_texture_index = material_index + material_texture_index_normal;
        float3 normal_sample      = material_textures[normal_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, 0).xyz;
        normal_sample             = normal_sample * 2.0f - 1.0f; // unpack from [0,1] to [-1,1]
        
        // build tbn matrix and transform normal to world space
        float3 bitangent_world = normalize(cross(normal_world, tangent_world));
        float3x3 tbn           = float3x3(tangent_world, bitangent_world, normal_world);
        normal_world           = normalize(mul(normal_sample, tbn));
    }
    
    // base albedo from material
    float3 albedo = mat.color.rgb;
    
    // sample albedo texture if material has one
    if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = material_index + material_texture_index_albedo;
        
        // compute mip level based on hit distance for sharper close reflections
        float hit_distance = RayTCurrent();
        float mip_level    = log2(max(hit_distance * 0.5f, 1.0f)); // closer = sharper
        mip_level          = clamp(mip_level, 0.0f, 4.0f);
        
        float4 sampled_albedo = material_textures[albedo_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        
        // use texture if valid
        if (sampled_albedo.a > 0.01f)
        {
            albedo = sampled_albedo.rgb * mat.color.rgb;
        }
    }
    
    // simple directional lighting using actual surface normal
    float3 light_dir = normalize(float3(0.5f, 1.0f, 0.3f));
    float n_dot_l    = saturate(dot(normal_world, light_dir) * 0.5f + 0.5f); // half-lambert
    
    // ambient + diffuse
    float3 ambient   = float3(0.15f, 0.17f, 0.2f);
    float3 lit_color = albedo * (ambient + n_dot_l * 0.85f);
    
    payload.color = lit_color;
}
