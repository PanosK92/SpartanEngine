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

//= INCLUDES ===================
#include "common.hlsl"
#include "restir_reservoir.hlsl"
//==============================

// restir pt configuration for initial sampling
static const uint INITIAL_CANDIDATE_SAMPLES = 4;  // m samples for ris
static const float RUSSIAN_ROULETTE_PROB    = 0.8f;

// unified ray payload for path tracing and shadow queries
struct [raypayload] PathPayload
{
    float3 hit_position : read(caller) : write(closesthit);
    float3 hit_normal   : read(caller) : write(closesthit);
    float3 albedo       : read(caller) : write(closesthit);
    float  roughness    : read(caller) : write(closesthit);
    float  metallic     : read(caller) : write(closesthit);
    bool   hit          : read(caller) : write(closesthit, miss);
};

// helper to reconstruct uint64 address from two uint32 values
uint64_t make_address(uint2 addr)
{
    return uint64_t(addr.x) | (uint64_t(addr.y) << 32);
}

// vertex stride in bytes (float3 pos + float2 tex + float3 nor + float3 tan = 44 bytes)
static const uint VERTEX_STRIDE = 44;

// reservoir uav textures
RWTexture2D<float4> tex_reservoir0 : register(u21); // hit_position.xyz, hit_normal.x
RWTexture2D<float4> tex_reservoir1 : register(u22); // hit_normal.yz, direction.xy
RWTexture2D<float4> tex_reservoir2 : register(u23); // direction.z, radiance.xyz
RWTexture2D<float4> tex_reservoir3 : register(u24); // throughput.xyz, weight_sum
RWTexture2D<float4> tex_reservoir4 : register(u25); // M, W, target_pdf, packed_flags

// previous frame reservoirs (for temporal)
Texture2D<float4> tex_reservoir_prev0 : register(t21);
Texture2D<float4> tex_reservoir_prev1 : register(t22);
Texture2D<float4> tex_reservoir_prev2 : register(t23);
Texture2D<float4> tex_reservoir_prev3 : register(t24);
Texture2D<float4> tex_reservoir_prev4 : register(t25);

// evaluate brdf (simplified disney for path tracing)
float3 evaluate_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float3 l, out float pdf)
{
    float3 h     = normalize(v + l);
    float n_dot_l = max(dot(n, l), 0.0f);
    float n_dot_v = max(dot(n, v), 0.0f);
    float n_dot_h = max(dot(n, h), 0.0f);
    float v_dot_h = max(dot(v, h), 0.0f);
    
    // disney diffuse
    float fd90 = 0.5f + 2.0f * v_dot_h * v_dot_h * roughness;
    float fd_v = 1.0f + (fd90 - 1.0f) * pow(1.0f - n_dot_v, 5.0f);
    float fd_l = 1.0f + (fd90 - 1.0f) * pow(1.0f - n_dot_l, 5.0f);
    float3 diffuse = albedo * (1.0f / PI) * fd_v * fd_l;
    
    // ggx specular
    float alpha  = roughness * roughness;
    float alpha2 = alpha * alpha;
    
    // d - ggx ndf
    float d_denom = n_dot_h * n_dot_h * (alpha2 - 1.0f) + 1.0f;
    float d = alpha2 / (PI * d_denom * d_denom + 1e-6f);
    
    // g - smith ggx
    float k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
    float g_v = n_dot_v / (n_dot_v * (1.0f - k) + k);
    float g_l = n_dot_l / (n_dot_l * (1.0f - k) + k);
    float g = g_v * g_l;
    
    // f - schlick fresnel
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 f = f0 + (1.0f - f0) * pow(1.0f - v_dot_h, 5.0f);
    
    float3 specular = (d * g * f) / (4.0f * n_dot_v * n_dot_l + 1e-6f);
    
    // combine
    float3 kd = (1.0f - f) * (1.0f - metallic);
    float3 brdf = kd * diffuse + specular;
    
    // pdf - mix of cosine and ggx
    float diffuse_pdf = n_dot_l / PI;
    float spec_pdf = d * n_dot_h / (4.0f * v_dot_h + 1e-6f);
    pdf = lerp(diffuse_pdf, spec_pdf, 0.5f);
    
    return brdf * n_dot_l;
}

// sample brdf direction
float3 sample_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float2 xi, out float pdf)
{
    float3 t, b;
    build_orthonormal_basis_fast(n, t, b);
    
    // probabilistically choose between diffuse and specular sampling
    if (xi.x < 0.5f)
    {
        // cosine-weighted diffuse sampling
        xi.x *= 2.0f;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf);
        return local_to_world(local_dir, n);
    }
    else
    {
        // ggx importance sampling
        xi.x = (xi.x - 0.5f) * 2.0f;
        float3 h = sample_ggx(xi, max(roughness, 0.04f), pdf);
        float3 h_world = local_to_world(h, n);
        return reflect(-v, h_world);
    }
}

// trace shadow ray to check visibility
bool trace_shadow_ray(float3 origin, float3 direction, float max_dist)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = 0.001f;
    ray.TMax      = max_dist - 0.001f;
    
    PathPayload payload;
    payload.hit = false;
    
    // trace ray - if we hit something, the closest_hit shader sets hit=true
    TraceRay(tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 1, 0, ray, payload);
    
    // if ray missed (hit=false), light is visible
    return !payload.hit;
}

// trace path and accumulate radiance
PathSample trace_path(float3 origin, float3 direction, inout uint seed)
{
    PathSample sample;
    sample.direction   = direction;
    sample.radiance    = float3(0, 0, 0);
    sample.throughput  = float3(1, 1, 1);
    sample.path_length = 0;
    sample.flags       = 0;
    sample.pdf         = 1.0f;
    
    float3 ray_origin = origin;
    float3 ray_dir    = direction;
    float3 throughput = float3(1, 1, 1);
    bool first_hit    = true;
    
    for (uint bounce = 0; bounce < RESTIR_MAX_PATH_LENGTH; bounce++)
    {
        RayDesc ray;
        ray.Origin    = ray_origin;
        ray.Direction = ray_dir;
        ray.TMin      = 0.001f;
        ray.TMax      = 1000.0f;
        
        PathPayload payload;
        payload.hit = false;
        
        TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 2, 0, ray, payload);
        
        if (!payload.hit)
        {
            // miss - add sky contribution
            float3 sky_dir = ray_dir;
            float sky_gradient = saturate(sky_dir.y);
            float3 sky_color = lerp(float3(0.8f, 0.85f, 0.9f), float3(0.3f, 0.5f, 0.9f), sky_gradient);
            
            // get sun contribution
            float3 light_dir   = -light_parameters[0].direction;
            float sun_factor   = saturate(light_dir.y);
            float3 sky_radiance = sky_color * sun_factor * 0.5f;
            
            sample.radiance += throughput * sky_radiance;
            break;
        }
        
        // store first hit info for reconnection
        if (first_hit)
        {
            sample.hit_position = payload.hit_position;
            sample.hit_normal   = payload.hit_normal;
            first_hit = false;
        }
        
        sample.path_length = bounce + 1;
        
        // direct lighting at hit point
        float3 light_dir      = -light_parameters[0].direction;
        float3 light_color    = light_parameters[0].color.rgb;
        float  light_intensity = light_parameters[0].intensity * 0.0001f;
        
        float n_dot_l = saturate(dot(payload.hit_normal, light_dir));
        if (n_dot_l > 0)
        {
            // shadow ray
            if (trace_shadow_ray(payload.hit_position + payload.hit_normal * 0.01f, light_dir, 1000.0f))
            {
                float brdf_pdf;
                float3 view_dir = -ray_dir;
                float3 brdf = evaluate_brdf(payload.albedo, payload.roughness, payload.metallic,
                                            payload.hit_normal, view_dir, light_dir, brdf_pdf);
                sample.radiance += throughput * brdf * light_color * light_intensity;
            }
        }
        
        // russian roulette after first few bounces
        if (bounce >= 2)
        {
            float p = max(max(throughput.r, throughput.g), throughput.b);
            p = min(p, RUSSIAN_ROULETTE_PROB);
            if (random_float(seed) > p)
                break;
            throughput /= p;
        }
        
        // sample next direction
        float3 view_dir = -ray_dir;
        float2 xi = random_float2(seed);
        float pdf;
        float3 new_dir = sample_brdf(payload.albedo, payload.roughness, payload.metallic,
                                      payload.hit_normal, view_dir, xi, pdf);
        
        if (pdf < 1e-6f || dot(new_dir, payload.hit_normal) <= 0)
            break;
        
        // evaluate brdf for throughput
        float brdf_pdf;
        float3 brdf = evaluate_brdf(payload.albedo, payload.roughness, payload.metallic,
                                     payload.hit_normal, view_dir, new_dir, brdf_pdf);
        
        throughput *= brdf / max(pdf, 1e-6f);
        sample.pdf *= pdf;
        
        // clamp throughput to prevent fireflies
        float max_throughput = max(max(throughput.r, throughput.g), throughput.b);
        if (max_throughput > 10.0f)
            throughput *= 10.0f / max_throughput;
        
        // update ray
        ray_origin = payload.hit_position + payload.hit_normal * 0.01f;
        ray_dir    = new_dir;
    }
    
    sample.throughput = throughput;
    return sample;
}

// ray generation - initial sampling with ris
[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv         = (launch_id + 0.5f) / launch_size;
    
    // touch geometry_infos to ensure it's included in the pipeline layout
    if (geometry_infos[0].vertex_count == 0xFFFFFFFF)
        return;
    
    // check if there's geometry at this pixel
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
        // no geometry - output empty reservoir
        Reservoir empty = create_empty_reservoir();
        float4 t0, t1, t2, t3, t4;
        pack_reservoir(empty, t0, t1, t2, t3, t4);
        tex_reservoir0[launch_id] = t0;
        tex_reservoir1[launch_id] = t1;
        tex_reservoir2[launch_id] = t2;
        tex_reservoir3[launch_id] = t3;
        tex_reservoir4[launch_id] = t4;
        tex_uav[launch_id] = float4(0, 0, 0, 1);
        return;
    }
    
    // initialize rng
    uint seed = create_seed(launch_id, buffer_frame.frame);
    
    // get world position and normal from g-buffer
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 view_dir  = normalize(buffer_frame.camera_position - pos_ws);
    
    // create reservoir for ris
    Reservoir reservoir = create_empty_reservoir();
    
    // generate candidate samples and use ris to select
    for (uint i = 0; i < INITIAL_CANDIDATE_SAMPLES; i++)
    {
        // sample initial direction
        float2 xi = random_float2(seed);
        float pdf;
        float3 ray_dir = sample_brdf(float3(0.5f, 0.5f, 0.5f), 0.5f, 0.0f, normal_ws, view_dir, xi, pdf);
        
        if (dot(ray_dir, normal_ws) <= 0)
            continue;
        
        // trace full path
        float3 ray_origin = pos_ws + normal_ws * 0.01f;
        PathSample candidate = trace_path(ray_origin, ray_dir, seed);
        candidate.direction = ray_dir;
        candidate.pdf = pdf;
        
        // calculate weight for ris: w = p_hat / p_source
        float target_pdf = calculate_target_pdf(candidate.radiance);
        float weight = target_pdf / max(pdf, 1e-6f);
        
        // update reservoir
        float rand = random_float(seed);
        update_reservoir(reservoir, candidate, weight, rand);
    }
    
    // finalize reservoir weight
    finalize_reservoir(reservoir);
    
    // write reservoir to textures
    float4 t0, t1, t2, t3, t4;
    pack_reservoir(reservoir, t0, t1, t2, t3, t4);
    tex_reservoir0[launch_id] = t0;
    tex_reservoir1[launch_id] = t1;
    tex_reservoir2[launch_id] = t2;
    tex_reservoir3[launch_id] = t3;
    tex_reservoir4[launch_id] = t4;
    
    // output initial gi contribution (will be refined by temporal/spatial passes)
    float3 gi = reservoir.sample.radiance * reservoir.W;
    tex_uav[launch_id] = float4(gi, 1.0f);
}

// closest hit shader for path tracing
[shader("closesthit")]
void closest_hit(inout PathPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    payload.hit = true;
    
    // get material from instance
    uint material_index    = InstanceID();
    MaterialParameters mat = material_parameters[material_index];
    
    // get geometry info for this instance
    uint instance_index = InstanceIndex();
    GeometryInfo geo    = geometry_infos[instance_index];
    
    // get triangle indices
    uint64_t index_addr  = make_address(geo.index_buffer_address);
    uint primitive_index = PrimitiveIndex();
    uint index_offset    = (geo.index_offset + primitive_index * 3) * 4;
    
    uint i0 = vk::RawBufferLoad<uint>(index_addr + index_offset + 0);
    uint i1 = vk::RawBufferLoad<uint>(index_addr + index_offset + 4);
    uint i2 = vk::RawBufferLoad<uint>(index_addr + index_offset + 8);
    
    // get vertex buffer address
    uint64_t vertex_addr = make_address(geo.vertex_buffer_address);
    uint v0_offset = (geo.vertex_offset + i0) * VERTEX_STRIDE;
    uint v1_offset = (geo.vertex_offset + i1) * VERTEX_STRIDE;
    uint v2_offset = (geo.vertex_offset + i2) * VERTEX_STRIDE;
    
    // load normals (offset 20 in vertex)
    float3 n0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 20);
    float3 n1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 20);
    float3 n2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 20);
    
    // load texcoords (offset 12 in vertex)
    float2 uv0 = vk::RawBufferLoad<float2>(vertex_addr + v0_offset + 12);
    float2 uv1 = vk::RawBufferLoad<float2>(vertex_addr + v1_offset + 12);
    float2 uv2 = vk::RawBufferLoad<float2>(vertex_addr + v2_offset + 12);
    
    // barycentric interpolation
    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y, 
                         attribs.barycentrics.x, attribs.barycentrics.y);
    
    float3 normal_object = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float2 texcoord      = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;
    
    // transform normal to world space
    float3x3 obj_to_world = (float3x3)ObjectToWorld4x3();
    float3 normal_world   = normalize(mul(normal_object, obj_to_world));
    
    // apply material tiling
    texcoord = texcoord * mat.tiling + mat.offset;
    
    // get albedo
    float3 albedo = mat.color.rgb;
    if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = material_index + material_texture_index_albedo;
        float hit_distance = RayTCurrent();
        float mip_level = clamp(log2(max(hit_distance * 0.5f, 1.0f)), 0.0f, 4.0f);
        
        float4 sampled = material_textures[albedo_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        if (sampled.a > 0.01f)
            albedo = sampled.rgb * mat.color.rgb;
    }
    
    // compute hit position
    float hit_distance  = RayTCurrent();
    float3 hit_position = WorldRayOrigin() + WorldRayDirection() * hit_distance;
    
    // fill payload
    payload.hit_position = hit_position;
    payload.hit_normal   = normal_world;
    payload.albedo       = albedo;
    payload.roughness    = mat.roughness;
    payload.metallic     = mat.metallness;
}

// miss shader
[shader("miss")]
void miss(inout PathPayload payload : SV_RayPayload)
{
    payload.hit = false;
}
