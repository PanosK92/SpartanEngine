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
    
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f) // far plane, no reflection
    {
        tex_uav[launch_id] = float4(0, 0, 0, 0);
        return;
    }
    
    float3 pos_vs    = get_position_view_space(uv);
    float3 normal_ws = get_normal(uv);
    float3 normal_vs = normalize(mul(float4(normal_ws, 0.0f), buffer_frame.view).xyz);
    float3 V         = normalize(-pos_vs);     // view dir from surface to camera
    float3 R         = reflect(-V, normal_vs); // reflection dir 
    float3 pos_ws    = mul(float4(pos_vs, 1.0f), buffer_frame.view_inverted).xyz;
    float3 R_ws      = mul(float4(R, 0.0f), buffer_frame.view_inverted).xyz;
    
    RayDesc ray;
    ray.Origin = pos_ws + normal_ws * 0.01f; // offset to avoid self-intersection
    ray.Direction = normalize(R_ws);
    ray.TMin      = 0.001f;
    ray.TMax      = 10000.0f;
    
    Payload payload;
    payload.color = float3(0, 0, 0);
    
    TraceRay(tlas, 0, 0xFF, 0, 1, 0, ray, payload);
    
    tex_uav[launch_id] = float4(payload.color, 1.0f);
}

[shader("miss")]
void miss(inout Payload payload : SV_RayPayload)
{
    payload.color = float3(0, 0, 0); // black
}

[shader("closesthit")]
void closest_hit(inout Payload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // 1. Determine which instance / primitive was hit =
    uint instanceIndex  = InstanceID();     // per instance in TLAS
    uint primitiveIndex = PrimitiveIndex(); // triangle index in BLAS

    //// 2. Fetch material for this instance
    //MaterialParameters mat = material_parameters[instanceIndex];
    //
    //// 3. Interpolate vertex data
    //// todo: vertex buffers bound as SRVs
    //uint3 triVertexIndices = index_buffer[primitiveIndex * 3 + uint3(0, 1, 2)];
    //
    //float3 pos0 = vertex_positions[triVertexIndices.x];
    //float3 pos1 = vertex_positions[triVertexIndices.y];
    //float3 pos2 = vertex_positions[triVertexIndices.z];
    //
    //float3 n0 = vertex_normals[triVertexIndices.x];
    //float3 n1 = vertex_normals[triVertexIndices.y];
    //float3 n2 = vertex_normals[triVertexIndices.z];
    //
    //float2 uv0 = vertex_uvs[triVertexIndices.x];
    //float2 uv1 = vertex_uvs[triVertexIndices.y];
    //float2 uv2 = vertex_uvs[triVertexIndices.z];
    //
    //// barycentric coordinates from intersection
    //float u = attribs.Barycentrics.x;
    //float v = attribs.Barycentrics.y;
    //float w = 1.0f - u - v;
    //
    //float3 normal_vs = normalize(n0 * w + n1 * u + n2 * v);
    //float2 uv = uv0 * w + uv1 * u + uv2 * v;
    //
    //// 4. Sample diffuse texture using interpolated UV
    //float3 albedo = mat.diffuse_map.Sample(GET_SAMPLER(sampler_linear_clamp), uv).rgb;

    // 5. Set payload color
    payload.color = float3(0, 1, 0);
}
