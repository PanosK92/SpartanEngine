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

static const uint INITIAL_CANDIDATE_SAMPLES = 6;
static const float RUSSIAN_ROULETTE_PROB    = 0.85f; // slightly higher for better convergence in interiors
static const uint RUSSIAN_ROULETTE_START    = 3;     // start RR after bounce 3 instead of 2
static const uint VERTEX_STRIDE             = 44;
static const float MIN_AREA_LIGHT_SOLID_ANGLE = 1e-4f; // prevent fireflies from tiny area lights

/*------------------------------------------------------------------------------
    RAY PAYLOAD
------------------------------------------------------------------------------*/
struct [raypayload] PathPayload
{
    float3 hit_position : read(caller) : write(closesthit);
    float3 hit_normal   : read(caller) : write(closesthit);
    float3 albedo       : read(caller) : write(closesthit);
    float3 emission     : read(caller) : write(closesthit, miss);
    float  roughness    : read(caller) : write(closesthit);
    float  metallic     : read(caller) : write(closesthit);
    bool   hit          : read(caller) : write(closesthit, miss);
};

/*------------------------------------------------------------------------------
    RESOURCES
------------------------------------------------------------------------------*/
RWTexture2D<float4> tex_reservoir0 : register(u21);
RWTexture2D<float4> tex_reservoir1 : register(u22);
RWTexture2D<float4> tex_reservoir2 : register(u23);
RWTexture2D<float4> tex_reservoir3 : register(u24);
RWTexture2D<float4> tex_reservoir4 : register(u25);

Texture2D<float4> tex_reservoir_prev0 : register(t21);
Texture2D<float4> tex_reservoir_prev1 : register(t22);
Texture2D<float4> tex_reservoir_prev2 : register(t23);
Texture2D<float4> tex_reservoir_prev3 : register(t24);
Texture2D<float4> tex_reservoir_prev4 : register(t25);

uint64_t make_address(uint2 addr)
{
    return uint64_t(addr.x) | (uint64_t(addr.y) << 32);
}

/*------------------------------------------------------------------------------
    BRDF
------------------------------------------------------------------------------*/
float3 evaluate_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float3 l, out float pdf)
{
    float3 h_unnorm = v + l;
    float h_len_sq  = dot(h_unnorm, h_unnorm);

    // handle degenerate half-vector (v and l nearly opposite)
    if (h_len_sq < 1e-6f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    float3 h      = h_unnorm * rsqrt(h_len_sq);
    float n_dot_l = max(dot(n, l), 0.0f);
    float n_dot_v = max(dot(n, v), 0.001f);
    float n_dot_h = max(dot(n, h), 0.0f);
    float v_dot_h = max(dot(v, h), 0.0f);

    // early out for grazing angles
    if (n_dot_l <= 0.0f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    // lambertian diffuse
    float3 diffuse = albedo * (1.0f / PI);

    // ggx specular distribution
    float alpha  = max(roughness * roughness, 0.001f);
    float alpha2 = alpha * alpha;

    float d_denom = n_dot_h * n_dot_h * (alpha2 - 1.0f) + 1.0f;
    float d = alpha2 / (PI * d_denom * d_denom + 1e-6f);

    // smith geometry term
    float k   = alpha * 0.5f;
    float g_v = n_dot_v / (n_dot_v * (1.0f - k) + k + 1e-6f);
    float g_l = n_dot_l / (n_dot_l * (1.0f - k) + k + 1e-6f);
    float g   = g_v * g_l;

    // fresnel schlick
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 f  = f0 + (1.0f - f0) * pow(1.0f - v_dot_h, 5.0f);

    float3 specular = (d * g * f) / (4.0f * n_dot_v * n_dot_l + 1e-6f);

    // combine diffuse and specular with energy conservation
    float3 kd   = (1.0f - f) * (1.0f - metallic);
    float3 brdf = kd * diffuse + specular;

    // mixed pdf matching the sampling strategy
    float diffuse_pdf = n_dot_l / PI;
    float spec_pdf    = d * n_dot_h / (4.0f * v_dot_h + 1e-6f);
    float spec_prob   = 0.5f + 0.5f * metallic;
    pdf = (1.0f - spec_prob) * diffuse_pdf + spec_prob * spec_pdf;

    return brdf * n_dot_l;
}

float3 sample_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float2 xi, out float pdf)
{
    float3 t, b;
    build_orthonormal_basis_fast(n, t, b);

    // probability split between diffuse and specular
    float spec_prob    = 0.5f + 0.5f * metallic;
    float prob_diffuse = 1.0f - spec_prob;

    float3 l;
    if (xi.x < prob_diffuse)
    {
        // cosine-weighted hemisphere for diffuse
        xi.x = xi.x / prob_diffuse;
        float pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        l = local_to_world(local_dir, n);
    }
    else
    {
        // ggx importance sampling for specular
        xi.x = (xi.x - prob_diffuse) / (1.0f - prob_diffuse);
        float pdf_h;
        float3 h       = sample_ggx(xi, max(roughness, 0.04f), pdf_h);
        float3 h_world = local_to_world(h, n);
        l = reflect(-v, h_world);
    }

    // compute mixed pdf matching evaluate_brdf
    float n_dot_l     = max(dot(n, l), 0.001f);
    float diffuse_pdf = n_dot_l / PI;

    // specular pdf from half vector - handle degenerate case
    float3 h_unnorm = v + l;
    float h_len_sq  = dot(h_unnorm, h_unnorm);

    if (h_len_sq < 1e-6f)
    {
        pdf = diffuse_pdf * prob_diffuse;
        return l;
    }

    float3 h       = h_unnorm * rsqrt(h_len_sq);
    float n_dot_h  = max(dot(n, h), 0.001f);
    float v_dot_h  = max(dot(v, h), 0.001f);
    float alpha    = max(roughness * roughness, 0.001f);
    float alpha2   = alpha * alpha;
    float d_denom  = n_dot_h * n_dot_h * (alpha2 - 1.0f) + 1.0f;
    float d        = alpha2 / (PI * d_denom * d_denom + 1e-6f);
    float spec_pdf = d * n_dot_h / (4.0f * v_dot_h + 1e-6f);

    pdf = prob_diffuse * diffuse_pdf + spec_prob * spec_pdf;
    return l;
}

/*------------------------------------------------------------------------------
    PATH TRACING WITH NEE + MIS
------------------------------------------------------------------------------*/
bool trace_shadow_ray(float3 origin, float3 direction, float max_dist)
{
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = RESTIR_RAY_T_MIN;
    ray.TMax      = max_dist - RESTIR_RAY_T_MIN;

    PathPayload payload;
    payload.hit = false;

    TraceRay(tlas, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH, 0xFF, 0, 1, 0, ray, payload);
    return !payload.hit;
}

PathSample trace_path(float3 origin, float3 direction, inout uint seed)
{
    PathSample sample;
    sample.direction   = direction;
    sample.radiance    = float3(0, 0, 0);
    sample.path_length = 0;
    sample.flags       = 0;
    sample.pdf         = 1.0f;

    float3 ray_origin   = origin;
    float3 ray_dir      = direction;
    float3 throughput   = float3(1, 1, 1);
    bool first_hit      = true;
    float prev_brdf_pdf = 1.0f;
    bool prev_specular  = false;

    for (uint bounce = 0; bounce < RESTIR_MAX_PATH_LENGTH; bounce++)
    {
        RayDesc ray;
        ray.Origin    = ray_origin;
        ray.Direction = ray_dir;
        ray.TMin      = RESTIR_RAY_T_MIN;
        ray.TMax      = 1000.0f;

        PathPayload payload;
        payload.hit = false;

        TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, payload);

        // environment map contribution on miss
        if (!payload.hit)
        {
            float2 sky_uv       = direction_sphere_uv(ray_dir);
            float3 sky_radiance = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), sky_uv, 2).rgb;
            sample.radiance += throughput * sky_radiance;
            break;
        }

        // store first hit for reservoir
        if (first_hit)
        {
            sample.hit_position = payload.hit_position;
            sample.hit_normal   = payload.hit_normal;
            sample.flags        = (payload.roughness < 0.3f) ? PATH_FLAG_SPECULAR : PATH_FLAG_DIFFUSE;
            first_hit = false;
        }

        sample.path_length = bounce + 1;
        float3 view_dir = -ray_dir;

        // emissive surface contribution with mis
        if (any(payload.emission > 0.0f))
        {
            if (bounce == 0 || prev_specular)
            {
                // first bounce or after specular: no MIS needed
                sample.radiance += throughput * payload.emission;
            }
            else
            {
                // MIS with implicit emissive hit
                // light_pdf = distance^2 / (cos_theta * area)
                // we assume a nominal emissive area of 1m^2 for MIS balance
                // this is a heuristic since we don't track actual emissive surface areas
                static const float NOMINAL_EMISSIVE_AREA = 1.0f;
                float3 to_light     = payload.hit_position - ray_origin;
                float light_dist_sq = dot(to_light, to_light);
                float cos_light     = abs(dot(payload.hit_normal, -ray_dir));
                float light_pdf     = light_dist_sq / (max(cos_light, 0.001f) * NOMINAL_EMISSIVE_AREA);
                float mis_weight    = power_heuristic(prev_brdf_pdf, light_pdf);
                sample.radiance += throughput * payload.emission * mis_weight;
            }
        }

        // next event estimation
        float3 shading_pos = payload.hit_position + payload.hit_normal * RESTIR_RAY_NORMAL_OFFSET;

        for (uint light_idx = 0; light_idx < 4u; light_idx++)
        {
            LightParameters light = light_parameters[light_idx];

            if (light.intensity <= 0.0f)
                continue;

            uint light_flags    = light.flags;
            bool is_directional = (light_flags & (1u << 0)) != 0;
            bool is_point       = (light_flags & (1u << 1)) != 0;
            bool is_spot        = (light_flags & (1u << 2)) != 0;
            bool is_area        = (light_flags & (1u << 6)) != 0;

            float3 light_color = light.color.rgb;
            float3 light_dir;
            float  light_dist;
            float  light_pdf = 1.0f;
            float  attenuation = 1.0f;

            if (is_directional)
            {
                light_dir  = -light.direction;
                light_dist = 1000.0f;
                light_pdf  = 1.0f;
            }
            else if (is_area && light.area_width > 0.0f && light.area_height > 0.0f)
            {
                // sample random point on area light
                float2 xi = random_float2(seed);

                float3 light_normal = light.direction;
                float3 light_right, light_up;
                build_orthonormal_basis_fast(light_normal, light_right, light_up);

                float half_width  = light.area_width * 0.5f;
                float half_height = light.area_height * 0.5f;
                float3 light_sample_pos = light.position
                    + light_right * (xi.x - 0.5f) * light.area_width
                    + light_up * (xi.y - 0.5f) * light.area_height;

                float3 to_light = light_sample_pos - shading_pos;
                light_dist      = length(to_light);
                light_dir       = to_light / light_dist;

                float cos_light = dot(-light_dir, light_normal);
                if (cos_light <= 0.0f)
                    continue;

                // convert area measure to solid angle with minimum solid angle clamp
                float area         = light.area_width * light.area_height;
                float solid_angle  = (area * cos_light) / (light_dist * light_dist);
                solid_angle        = max(solid_angle, MIN_AREA_LIGHT_SOLID_ANGLE);
                light_pdf          = 1.0f / solid_angle;
                attenuation = 1.0f / (1.0f + light_dist * light_dist * 0.01f);
            }
            else if (is_point || is_spot)
            {
                float3 to_light = light.position - shading_pos;
                light_dist      = length(to_light);
                light_dir       = to_light / light_dist;

                // point/spot lights are delta distributions in solid angle
                // pdf in solid angle measure is effectively infinite, but we use distance^2
                // to convert from area measure for consistent MIS with BRDF sampling
                light_pdf = light_dist * light_dist;

                // range-based attenuation
                float range_factor = saturate(1.0f - light_dist / max(light.range, 0.01f));
                attenuation = range_factor * range_factor / (1.0f + light_dist * light_dist * 0.1f);

                // spot cone falloff
                if (is_spot)
                {
                    float cos_angle = dot(-light_dir, light.direction);
                    float cos_outer = cos(light.angle);
                    float cos_inner = cos(light.angle * 0.8f);
                    attenuation *= saturate((cos_angle - cos_outer) / (cos_inner - cos_outer));
                }
            }
            else
            {
                continue;
            }

            float n_dot_l = dot(payload.hit_normal, light_dir);
            if (n_dot_l <= 0.0f)
                continue;

            if (!trace_shadow_ray(shading_pos, light_dir, light_dist))
                continue;

            float brdf_pdf;
            float3 brdf = evaluate_brdf(payload.albedo, payload.roughness, payload.metallic,
                                        payload.hit_normal, view_dir, light_dir, brdf_pdf);

            // MIS weight: area lights use standard MIS, point/spot use distance-weighted MIS
            float mis_weight = 1.0f;
            if (is_area || is_point || is_spot)
                mis_weight = power_heuristic(light_pdf, brdf_pdf);

            float3 Li = light_color * light.intensity * attenuation;
            sample.radiance += throughput * brdf * Li * mis_weight / max(light_pdf, 1e-6f);
        }

        // russian roulette after specified bounce
        if (bounce >= RUSSIAN_ROULETTE_START)
        {
            float continuation_prob = min(luminance(throughput), RUSSIAN_ROULETTE_PROB);
            if (random_float(seed) > continuation_prob)
                break;
            throughput /= continuation_prob;
        }

        // sample next direction
        float2 xi = random_float2(seed);
        float pdf;
        float3 new_dir = sample_brdf(payload.albedo, payload.roughness, payload.metallic,
                                      payload.hit_normal, view_dir, xi, pdf);

        // defensive normalization and validation
        new_dir = normalize(new_dir);
        if (pdf < 1e-6f || dot(new_dir, payload.hit_normal) <= 0 || any(isnan(new_dir)))
            break;

        float brdf_pdf;
        float3 brdf = evaluate_brdf(payload.albedo, payload.roughness, payload.metallic,
                                     payload.hit_normal, view_dir, new_dir, brdf_pdf);

        // skip if BRDF evaluation fails (degenerate case)
        if (brdf_pdf < 1e-6f)
            break;

        // update throughput with importance weight
        throughput *= brdf / brdf_pdf;
        sample.pdf *= pdf;

        prev_brdf_pdf = pdf;
        prev_specular = (payload.roughness < 0.1f && payload.metallic > 0.5f);

        // clamp throughput for stability
        float max_throughput = max(max(throughput.r, throughput.g), throughput.b);
        if (max_throughput > 100.0f)
            throughput *= 100.0f / max_throughput;

        ray_origin = payload.hit_position + payload.hit_normal * RESTIR_RAY_NORMAL_OFFSET;
        ray_dir    = new_dir;
    }

    return sample;
}

/*------------------------------------------------------------------------------
    RAY GENERATION
------------------------------------------------------------------------------*/
[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv = (launch_id + 0.5f) / launch_size;

    if (geometry_infos[0].vertex_count == 0xFFFFFFFF)
        return;

    // early out for sky pixels
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
    {
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

    uint seed = create_seed_for_pass(launch_id, buffer_frame.frame, 0); // pass 0: initial candidate generation

    // fetch surface properties
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float3 view_dir  = normalize(buffer_frame.camera_position - pos_ws);

    float4 material = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float3 albedo   = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float roughness = max(material.r, 0.04f);
    float metallic  = material.g;

    // generate initial candidates via ris
    Reservoir reservoir = create_empty_reservoir();

    for (uint i = 0; i < INITIAL_CANDIDATE_SAMPLES; i++)
    {
        float2 xi = random_float2(seed);
        float pdf;
        float3 ray_dir = sample_brdf(albedo, roughness, metallic, normal_ws, view_dir, xi, pdf);

        if (dot(ray_dir, normal_ws) <= 0 || pdf < 1e-6f)
            continue;

        float3 ray_origin     = pos_ws + normal_ws * RESTIR_RAY_NORMAL_OFFSET;
        PathSample candidate  = trace_path(ray_origin, ray_dir, seed);
        candidate.direction   = ray_dir;
        candidate.pdf         = pdf;

        float target_pdf = calculate_target_pdf(candidate.radiance);
        float weight     = target_pdf / max(pdf, 1e-6f);

        update_reservoir(reservoir, candidate, weight, random_float(seed));
    }

    finalize_reservoir(reservoir);

    // store reservoir
    float4 t0, t1, t2, t3, t4;
    pack_reservoir(reservoir, t0, t1, t2, t3, t4);
    tex_reservoir0[launch_id] = t0;
    tex_reservoir1[launch_id] = t1;
    tex_reservoir2[launch_id] = t2;
    tex_reservoir3[launch_id] = t3;
    tex_reservoir4[launch_id] = t4;

    float3 gi = reservoir.sample.radiance * reservoir.W;

    // numerical stability
    if (any(isnan(gi)) || any(isinf(gi)))
        gi = float3(0.0f, 0.0f, 0.0f);

    // clamp extreme values
    float lum = luminance(gi);
    if (lum > 200.0f)
        gi *= 200.0f / lum;

    tex_uav[launch_id] = float4(gi, 1.0f);
}

/*------------------------------------------------------------------------------
    HIT SHADER
------------------------------------------------------------------------------*/
[shader("closesthit")]
void closest_hit(inout PathPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    payload.hit = true;

    uint material_index    = InstanceID();
    MaterialParameters mat = material_parameters[material_index];

    uint instance_index = InstanceIndex();
    GeometryInfo geo    = geometry_infos[instance_index];

    // load triangle indices
    uint64_t index_addr  = make_address(geo.index_buffer_address);
    uint primitive_index = PrimitiveIndex();
    uint index_offset    = (geo.index_offset + primitive_index * 3) * 4;

    uint i0 = vk::RawBufferLoad<uint>(index_addr + index_offset + 0);
    uint i1 = vk::RawBufferLoad<uint>(index_addr + index_offset + 4);
    uint i2 = vk::RawBufferLoad<uint>(index_addr + index_offset + 8);

    // load vertex attributes
    uint64_t vertex_addr = make_address(geo.vertex_buffer_address);
    uint v0_offset = (geo.vertex_offset + i0) * VERTEX_STRIDE;
    uint v1_offset = (geo.vertex_offset + i1) * VERTEX_STRIDE;
    uint v2_offset = (geo.vertex_offset + i2) * VERTEX_STRIDE;

    float3 n0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 20);
    float3 n1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 20);
    float3 n2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 20);

    float3 t0 = vk::RawBufferLoad<float3>(vertex_addr + v0_offset + 32);
    float3 t1 = vk::RawBufferLoad<float3>(vertex_addr + v1_offset + 32);
    float3 t2 = vk::RawBufferLoad<float3>(vertex_addr + v2_offset + 32);

    float2 uv0 = vk::RawBufferLoad<float2>(vertex_addr + v0_offset + 12);
    float2 uv1 = vk::RawBufferLoad<float2>(vertex_addr + v1_offset + 12);
    float2 uv2 = vk::RawBufferLoad<float2>(vertex_addr + v2_offset + 12);

    // barycentric interpolation
    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y,
                         attribs.barycentrics.x, attribs.barycentrics.y);

    float3 normal_object  = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 tangent_object = normalize(t0 * bary.x + t1 * bary.y + t2 * bary.z);
    float2 texcoord       = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    // transform to world space
    float3x3 obj_to_world  = (float3x3)ObjectToWorld4x3();
    float3 normal_world    = normalize(mul(normal_object, obj_to_world));
    float3 tangent_world   = normalize(mul(tangent_object, obj_to_world));

    texcoord = texcoord * mat.tiling + mat.offset;

    // distance-based mip selection
    float dist      = RayTCurrent();
    float mip_level = clamp(log2(max(dist * 0.5f, 1.0f)), 0.0f, 4.0f);

    // sample albedo
    float3 albedo = mat.color.rgb;
    if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = material_index + material_texture_index_albedo;
        float4 sampled = material_textures[albedo_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        albedo = sampled.rgb * mat.color.rgb;
    }
    albedo = saturate(albedo);

    // sample roughness
    float roughness = mat.roughness;
    if (mat.has_texture_roughness())
    {
        uint roughness_texture_index = material_index + material_texture_index_roughness;
        roughness *= material_textures[roughness_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).g;
    }
    roughness = max(roughness, 0.04f);

    // sample metallic
    float metallic = mat.metallness;
    if (mat.has_texture_metalness())
    {
        uint metalness_texture_index = material_index + material_texture_index_metalness;
        metallic *= material_textures[metalness_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).r;
    }

    // apply normal map
    if (mat.has_texture_normal())
    {
        uint normal_texture_index = material_index + material_texture_index_normal;
        float3 normal_sample = material_textures[normal_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).rgb;

        normal_sample = normal_sample * 2.0f - 1.0f;
        normal_sample.xy *= mat.normal;

        float3 bitangent = cross(normal_world, tangent_world);
        float3x3 tbn     = float3x3(tangent_world, bitangent, normal_world);

        normal_world = normalize(mul(normal_sample, tbn));
    }

    // sample emission
    float3 emission = float3(0.0f, 0.0f, 0.0f);
    if (mat.has_texture_emissive())
    {
        uint emissive_texture_index = material_index + material_texture_index_emission;
        emission = material_textures[emissive_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).rgb;
    }
    if (mat.emissive_from_albedo())
        emission += albedo;

    float3 hit_position = WorldRayOrigin() + WorldRayDirection() * dist;

    payload.hit_position = hit_position;
    payload.hit_normal   = normal_world;
    payload.albedo       = albedo;
    payload.emission     = emission;
    payload.roughness    = roughness;
    payload.metallic     = metallic;
}

/*------------------------------------------------------------------------------
    MISS SHADER
------------------------------------------------------------------------------*/
[shader("miss")]
void miss(inout PathPayload payload : SV_RayPayload)
{
    payload.hit      = false;
    payload.emission = float3(0.0f, 0.0f, 0.0f);
}
