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

//= INCLUDES =========
#include "common.hlsl"
//====================

// ray traced shadow payload - simple shadow query
struct [raypayload] ShadowPayload
{
    float shadow : read(caller, closesthit, miss) : write(caller, closesthit, miss); // 1 = lit, 0 = shadow
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
        // no geometry - fully lit
        tex_uav[launch_id] = float4(1.0f, 1.0f, 1.0f, 1.0f);
        return;
    }
    
    // get world position and normal
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    
    // get directional light direction (first light is assumed to be directional)
    float3 light_dir = -light_parameters[0].direction;
    
    // check if surface faces away from light (self-shadowing)
    float n_dot_l = dot(normal_ws, light_dir);
    if (n_dot_l <= 0.0f)
    {
        // surface faces away from light - in shadow
        tex_uav[launch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    
    // offset ray origin along normal to avoid self-intersection
    // scale offset based on distance from camera for better precision
    float camera_distance = length(buffer_frame.camera_position - pos_ws);
    float base_offset     = 0.01f + camera_distance * 0.0001f;
    float3 ray_origin     = pos_ws + normal_ws * base_offset;
    
    // setup shadow ray toward light
    RayDesc ray;
    ray.Origin    = ray_origin;
    ray.Direction = normalize(light_dir);
    ray.TMin      = 0.001f;
    ray.TMax      = 10000.0f; // directional lights are infinitely far
    
    // trace shadow ray
    ShadowPayload payload;
    payload.shadow = 1.0f; // assume lit unless we hit something
    
    // use RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH for shadow rays (we only care if something is hit)
    // use RAY_FLAG_SKIP_CLOSEST_HIT_SHADER to skip the closest hit shader (we only need miss/any hit)
    TraceRay(tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 1, 0, ray, payload);
    
    // output shadow value
    tex_uav[launch_id] = float4(payload.shadow, payload.shadow, payload.shadow, 1.0f);
}

[shader("miss")]
void miss(inout ShadowPayload payload : SV_RayPayload)
{
    // ray didn't hit anything - surface is lit
    payload.shadow = 1.0f;
}

[shader("closesthit")]
void closest_hit(inout ShadowPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // ray hit geometry - surface is in shadow
    payload.shadow = 0.0f;
}
