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

struct [raypayload] Payload
{
    float3 color : read(caller, closesthit, miss) : write(caller, closesthit, miss);
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
        tex_uav[launch_id] = float4(0, 0, 0, 0);
        return;
    }
    
    // compute reflection ray in world space
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 V         = normalize(buffer_frame.camera_position - pos_ws);
    float3 R         = reflect(-V, normal_ws);
    
    // offset along normal and reflection direction to escape source surface
    float3 ray_origin = pos_ws + normal_ws * 0.05f + R * 0.05f;
    
    // setup ray - use TMin to skip very close hits (self-intersection)
    RayDesc ray;
    ray.Origin    = ray_origin;
    ray.Direction = normalize(R);
    ray.TMin      = 0.01f;
    ray.TMax      = 1000.0f;
    
    // trace
    Payload payload;
    payload.color = float3(0, 0, 0);
    TraceRay(tlas, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 1, 0, ray, payload);
    
    // output raw reflection color
    tex_uav[launch_id] = float4(payload.color, 1.0f);
}

[shader("miss")]
void miss(inout Payload payload : SV_RayPayload)
{
    // sample skysphere for missed rays
    float3 ray_dir = WorldRayDirection();
    float2 uv      = direction_sphere_uv(ray_dir);
    payload.color  = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uv, 0).rgb;
}

[shader("closesthit")]
void closest_hit(inout Payload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // material index from tlas instance custom data
    uint material_index = InstanceID();
    MaterialParameters mat = material_parameters[material_index];
    
    // hit position
    float3 hit_pos = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    float3 V       = -WorldRayDirection();
    
    // base albedo from material
    float3 albedo = mat.color.rgb;
    
    // procedural uvs from world position (xz plane)
    float2 procedural_uv = frac(hit_pos.xz * mat.tiling);
    
    // sample albedo from bindless texture array
    uint albedo_texture_index = material_index + material_texture_index_albedo;
    float4 sampled_albedo     = material_textures[albedo_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), procedural_uv, 2);
    
    // use texture if valid
    if (sampled_albedo.a > 0.01f)
    {
        albedo = sampled_albedo.rgb * mat.color.rgb;
    }
    
    // simple directional lighting
    float3 light_dir     = normalize(float3(0.5f, 1.0f, 0.3f));
    float3 pseudo_normal = V;
    float n_dot_l        = saturate(dot(pseudo_normal, light_dir) * 0.5f + 0.5f); // half-lambert
    
    // ambient + diffuse
    float3 ambient   = float3(0.15f, 0.17f, 0.2f);
    float3 lit_color = albedo * (ambient + n_dot_l * 0.85f);
    
    payload.color = lit_color;
}
