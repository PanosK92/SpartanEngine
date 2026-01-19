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

// gi payload - carries bounced light back
struct [raypayload] GIPayload
{
    float3 radiance     : read(caller) : write(caller, closesthit, miss); // bounced light color
    uint   bounce_count : read(closesthit) : write(caller);               // current bounce depth (set by caller before trace)
};

// helper to reconstruct uint64 address from two uint32 values
uint64_t make_address(uint2 addr)
{
    return uint64_t(addr.x) | (uint64_t(addr.y) << 32);
}

// vertex stride in bytes (float3 pos + float2 tex + float3 nor + float3 tan = 44 bytes)
static const uint VERTEX_STRIDE = 44;

// cosine-weighted hemisphere sampling
float3 cosine_sample_hemisphere(float2 xi)
{
    float phi = 2.0f * PI * xi.x;
    float cos_theta = sqrt(xi.y);
    float sin_theta = sqrt(1.0f - xi.y);
    
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

// build orthonormal basis from normal
void build_orthonormal_basis(float3 n, out float3 t, out float3 b)
{
    float3 up = abs(n.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

// transform direction from tangent space to world space
float3 tangent_to_world(float3 dir, float3 n, float3 t, float3 b)
{
    return normalize(t * dir.x + b * dir.y + n * dir.z);
}

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
        // no geometry - no gi
        tex_uav[launch_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }
    
    // get world position and normal
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    
    // generate random direction using interleaved gradient noise
    float2 noise;
    noise.x = noise_interleaved_gradient(float2(launch_id) + float2(0.0f, 0.0f), true);
    noise.y = noise_interleaved_gradient(float2(launch_id) + float2(100.0f, 50.0f), true);
    
    // sample direction in hemisphere
    float3 local_dir = cosine_sample_hemisphere(noise);
    
    // build tbn and transform to world space
    float3 tangent, bitangent;
    build_orthonormal_basis(normal_ws, tangent, bitangent);
    float3 ray_dir = tangent_to_world(local_dir, normal_ws, tangent, bitangent);
    
    // offset ray origin along normal to avoid self-intersection
    float camera_distance = length(buffer_frame.camera_position - pos_ws);
    float base_offset     = 0.01f + camera_distance * 0.0001f;
    float3 ray_origin     = pos_ws + normal_ws * base_offset;
    
    // setup gi ray
    RayDesc ray;
    ray.Origin    = ray_origin;
    ray.Direction = ray_dir;
    ray.TMin      = 0.001f;
    ray.TMax      = 100.0f; // 100m range for gi
    
    // trace
    GIPayload payload;
    payload.radiance     = float3(0, 0, 0);
    payload.bounce_count = 0; // start at first bounce
    
    // touch geometry_infos to ensure it's included in the pipeline layout
    if (geometry_infos[0].vertex_count == 0xFFFFFFFF)
        return;
    
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);
    
    // output gi contribution
    tex_uav[launch_id] = float4(payload.radiance, 1.0f);
}

[shader("miss")]
void miss(inout GIPayload payload : SV_RayPayload)
{
    // ray escaped to sky - use sky color as indirect illumination
    float3 ray_dir = WorldRayDirection();
    
    // sky gradient based on ray direction (brighter at horizon, blue at zenith)
    float sky_gradient = saturate(ray_dir.y);
    float3 sky_zenith  = float3(0.3f, 0.5f, 0.9f);  // blue sky
    float3 sky_horizon = float3(0.8f, 0.85f, 0.9f); // bright horizon
    float3 sky_color   = lerp(sky_horizon, sky_zenith, sky_gradient);
    
    // get sun contribution for sky brightness
    float3 light_dir   = -light_parameters[0].direction;
    float3 light_color = light_parameters[0].color.rgb;
    float sun_factor   = saturate(light_dir.y); // sun elevation
    
    // sky illumination scales with sun
    payload.radiance = sky_color * sun_factor * 0.5f;
}

[shader("closesthit")]
void closest_hit(inout GIPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    // get material from instance
    uint material_index     = InstanceID();
    MaterialParameters mat  = material_parameters[material_index];
    
    // get geometry info for this instance
    uint instance_index = InstanceIndex();
    GeometryInfo geo    = geometry_infos[instance_index];
    
    // get triangle indices using buffer device address
    uint64_t index_addr   = make_address(geo.index_buffer_address);
    uint primitive_index  = PrimitiveIndex();
    uint index_offset     = (geo.index_offset + primitive_index * 3) * 4;
    
    uint i0 = vk::RawBufferLoad<uint>(index_addr + index_offset + 0);
    uint i1 = vk::RawBufferLoad<uint>(index_addr + index_offset + 4);
    uint i2 = vk::RawBufferLoad<uint>(index_addr + index_offset + 8);
    
    // get vertex buffer address
    uint64_t vertex_addr = make_address(geo.vertex_buffer_address);
    uint v0_offset       = (geo.vertex_offset + i0) * VERTEX_STRIDE;
    uint v1_offset       = (geo.vertex_offset + i1) * VERTEX_STRIDE;
    uint v2_offset       = (geo.vertex_offset + i2) * VERTEX_STRIDE;
    
    // load normals (offset 20 in vertex)
    float3 n0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 20);
    float3 n1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 20);
    float3 n2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 20);
    
    // load texcoords (offset 12 in vertex)
    float2 uv0 = vk::RawBufferLoad<float2>(vertex_addr + v0_offset + 12);
    float2 uv1 = vk::RawBufferLoad<float2>(vertex_addr + v1_offset + 12);
    float2 uv2 = vk::RawBufferLoad<float2>(vertex_addr + v2_offset + 12);
    
    // barycentric interpolation
    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y, attribs.barycentrics.x, attribs.barycentrics.y);
    
    float3 normal_object = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float2 texcoord      = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    
    // transform normal to world space
    float3x3 obj_to_world = (float3x3)ObjectToWorld4x3();
    float3 normal_world   = normalize(mul(normal_object, obj_to_world));
    
    // apply material tiling
    texcoord = texcoord * mat.tiling + mat.offset;
    
    // get base albedo
    float3 albedo = mat.color.rgb;
    
    // sample albedo texture if available
    if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = material_index + material_texture_index_albedo;
        float hit_distance        = RayTCurrent();
        float mip_level           = log2(max(hit_distance * 0.5f, 1.0f));
        mip_level                 = clamp(mip_level, 0.0f, 4.0f);
        
        float4 sampled_albedo = material_textures[albedo_texture_index].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        if (sampled_albedo.a > 0.01f)
        {
            albedo = sampled_albedo.rgb * mat.color.rgb;
        }
    }
    
    // compute hit position and distance
    float hit_distance  = RayTCurrent();
    float3 hit_position = WorldRayOrigin() + WorldRayDirection() * hit_distance;
    
    // get directional light for direct lighting at hit point
    float3 light_dir   = -light_parameters[0].direction;
    float3 light_color = light_parameters[0].color.rgb;
    float light_intensity = light_parameters[0].intensity;
    
    // simple lambertian direct lighting at hit point
    float n_dot_l = saturate(dot(normal_world, light_dir));
    
    // scale light intensity reasonably (intensity is in physical units)
    float scaled_intensity = light_intensity * 0.0001f; // scale down from lux/lumens
    float3 direct_light = albedo * light_color * n_dot_l * scaled_intensity;
    
    // sky ambient contribution (hemisphere above the hit point)
    float sky_visibility = saturate(normal_world.y * 0.5f + 0.5f);
    float3 sky_ambient   = float3(0.4f, 0.5f, 0.7f) * sky_visibility * 0.2f;
    float3 ambient       = albedo * sky_ambient;
    
    // second bounce - trace another ray if we haven't exceeded max bounces
    float3 second_bounce = float3(0, 0, 0);
    if (payload.bounce_count < 1) // allow one more bounce (total 2 bounces)
    {
        // generate random direction for second bounce using hit position as seed
        float2 noise2;
        noise2.x = frac(sin(dot(hit_position.xy, float2(12.9898f, 78.233f))) * 43758.5453f);
        noise2.y = frac(sin(dot(hit_position.yz, float2(39.346f, 11.135f))) * 43758.5453f);
        
        // cosine-weighted direction
        float phi2       = 2.0f * PI * noise2.x;
        float cos_theta2 = sqrt(noise2.y);
        float sin_theta2 = sqrt(1.0f - noise2.y);
        float3 local_dir2 = float3(cos(phi2) * sin_theta2, sin(phi2) * sin_theta2, cos_theta2);
        
        // build tbn from hit normal
        float3 up2 = abs(normal_world.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
        float3 t2  = normalize(cross(up2, normal_world));
        float3 b2  = cross(normal_world, t2);
        float3 ray_dir2 = normalize(t2 * local_dir2.x + b2 * local_dir2.y + normal_world * local_dir2.z);
        
        // setup second bounce ray
        RayDesc ray2;
        ray2.Origin    = hit_position + normal_world * 0.01f;
        ray2.Direction = ray_dir2;
        ray2.TMin      = 0.001f;
        ray2.TMax      = 50.0f; // shorter range for second bounce
        
        // trace second bounce
        GIPayload payload2;
        payload2.radiance     = float3(0, 0, 0);
        payload2.bounce_count = payload.bounce_count + 1;
        
        TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray2, payload2);
        
        // attenuate second bounce by albedo (energy transfer)
        second_bounce = payload2.radiance * albedo * 0.5f;
    }
    
    // distance-based falloff for indirect light
    // gi contribution decreases with distance (but not as aggressively as 1/r^2)
    float distance_falloff = 1.0f / (1.0f + hit_distance * 0.01f);
    
    // combine and output bounced radiance
    float3 bounced = (direct_light + ambient + second_bounce) * distance_falloff;
    
    // boost the gi contribution for visibility
    payload.radiance = bounced * 2.0f;
}
