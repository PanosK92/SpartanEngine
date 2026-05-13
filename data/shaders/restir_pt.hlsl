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

static const uint  INITIAL_CANDIDATE_SAMPLES   = 16;
static const float MIN_COS_AT_PRIMARY          = 1e-3f;
static const float RUSSIAN_ROULETTE_PROB       = 0.85f;
static const uint  RUSSIAN_ROULETTE_START      = 2;
static const float SKY_MIP_LEVEL               = 2.0f;
static const float SUN_CONE_HALF_ANGLE         = 0.015f;
static const float SUN_SAMPLE_PROBABILITY      = 0.5f;
// MIN_AREA_LIGHT_SOLID_ANGLE moved to restir_reservoir.hlsl since it is shared with the spatial pass nee

struct [raypayload] PathPayload
{
    float3 hit_position     : read(caller) : write(closesthit);
    float3 hit_normal       : read(caller) : write(closesthit);
    float3 geometric_normal : read(caller) : write(closesthit);
    float3 albedo           : read(caller) : write(closesthit);
    float3 emission         : read(caller) : write(closesthit, miss);
    float  roughness        : read(caller) : write(closesthit);
    float  metallic         : read(caller) : write(closesthit);
    float  triangle_area    : read(caller) : write(closesthit);
    bool   hit              : read(caller) : write(closesthit, miss);
};

// trace_shadow_ray moved to restir_reservoir.hlsl so it is callable from spatial / temporal passes too

float3 probe_emission_estimate(MaterialParameters mat)
{
    if (mat.emissive_from_albedo() || mat.has_texture_emissive())
        return mat.color.rgb;
    return float3(0.0f, 0.0f, 0.0f);
}

// env nee density at a given direction for mis against brdf bounces that miss into the sky
float sky_nee_pdf_at(float3 dir, float3 shading_normal)
{
    float3 sun_dir = float3(0, 1, 0);
    bool   has_sun = false;
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    if (light_count > 0)
    {
        LightParameters p = light_parameters[0];
        if ((p.flags & (1u << 0)) != 0 && p.intensity > 0.0f)
        {
            sun_dir = -p.direction;
            has_sun = true;
        }
    }

    float sun_cos_max  = cos(SUN_CONE_HALF_ANGLE);
    float sun_cone_pdf = 1.0f / (2.0f * PI * (1.0f - sun_cos_max));
    float sun_prob     = has_sun ? SUN_SAMPLE_PROBABILITY : 0.0f;

    float pdf_cos = max(dot(dir, shading_normal), 0.0f) / PI;
    float pdf_sun = (has_sun && dot(dir, sun_dir) >= sun_cos_max) ? sun_cone_pdf : 0.0f;
    return (1.0f - sun_prob) * pdf_cos + sun_prob * pdf_sun;
}

// evaluates direct lighting (analytical lights + environment probe) at a surface vertex
// returns the contribution in the direction of view_dir (i.e. back toward the previous vertex)
// always uses the full brdf so metallic/glossy surfaces receive proper nee at every vertex
// note: stored rc_radiance is therefore view-dependent at rc (w.r.t. src's incoming direction)
// the bias introduced during reconnection shift is bounded by the rc roughness gate
float3 direct_lighting_at_vertex(
    float3 shading_pos,
    float3 shading_normal,
    float3 geometric_normal,
    float3 view_dir,
    float3 albedo,
    float roughness,
    float metallic,
    inout uint seed)
{
    float3 total = float3(0, 0, 0);
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    float shading_offset = compute_ray_offset(shading_pos);
    float3 ray_origin_light = shading_pos + geometric_normal * shading_offset;

    // analytical lights
    for (uint light_idx = 0; light_idx < light_count; light_idx++)
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
        float  light_pdf    = 1.0f;
        float  attenuation  = 1.0f;

        if (is_directional)
        {
            light_dir  = -light.direction;
            light_dist = 1000.0f;
            light_pdf  = 1.0f;
        }
        else if (is_area && light.area_width > 0.0f && light.area_height > 0.0f)
        {
            float3 light_normal = light.direction;
            float3 light_right, light_up;
            build_orthonormal_basis_fast(light_normal, light_right, light_up);

            float half_width  = light.area_width * 0.5f;
            float half_height = light.area_height * 0.5f;

            float3 light_center = light.position;
            float3 p0 = light_center - light_right * half_width - light_up * half_height;
            float3 p1 = light_center + light_right * half_width - light_up * half_height;
            float3 p2 = light_center + light_right * half_width + light_up * half_height;
            float3 p3 = light_center - light_right * half_width + light_up * half_height;

            float3 v0 = normalize(p0 - shading_pos);
            float3 v1 = normalize(p1 - shading_pos);
            float3 v2 = normalize(p2 - shading_pos);
            float3 v3 = normalize(p3 - shading_pos);

            float solid_angle_approx = 0.0f;
            {
                float a1 = acos(clamp(dot(v0, v1), -1.0f, 1.0f));
                float a2 = acos(clamp(dot(v1, v2), -1.0f, 1.0f));
                float a3 = acos(clamp(dot(v2, v0), -1.0f, 1.0f));
                float s  = (a1 + a2 + a3) * 0.5f;
                float excess1 = 4.0f * atan(sqrt(max(0.0f, tan(s * 0.5f) * tan((s - a1) * 0.5f) * tan((s - a2) * 0.5f) * tan((s - a3) * 0.5f))));

                float b1 = acos(clamp(dot(v0, v2), -1.0f, 1.0f));
                float b2 = acos(clamp(dot(v2, v3), -1.0f, 1.0f));
                float b3 = acos(clamp(dot(v3, v0), -1.0f, 1.0f));
                float t  = (b1 + b2 + b3) * 0.5f;
                float excess2 = 4.0f * atan(sqrt(max(0.0f, tan(t * 0.5f) * tan((t - b1) * 0.5f) * tan((t - b2) * 0.5f) * tan((t - b3) * 0.5f))));

                solid_angle_approx = excess1 + excess2;
            }

            float2 xi = random_float2(seed);
            float3 light_sample_pos = light_center
                + light_right * (xi.x - 0.5f) * light.area_width
                + light_up * (xi.y - 0.5f) * light.area_height;

            float3 to_light = light_sample_pos - shading_pos;
            light_dist      = length(to_light);
            light_dir       = to_light / light_dist;

            float cos_light = dot(-light_dir, light_normal);
            if (cos_light <= 0.0f)
                continue;

            float area = light.area_width * light.area_height;
            if (solid_angle_approx > MIN_AREA_LIGHT_SOLID_ANGLE)
            {
                light_pdf = 1.0f / solid_angle_approx;
            }
            else
            {
                float solid_angle = (area * cos_light) / (light_dist * light_dist);
                solid_angle       = max(solid_angle, MIN_AREA_LIGHT_SOLID_ANGLE);
                light_pdf         = 1.0f / solid_angle;
            }
            // no extra distance falloff, the 1/d^2 term is already baked into the solid-angle pdf
            attenuation = 1.0f;
        }
        else if (is_point || is_spot)
        {
            float3 to_light = light.position - shading_pos;
            light_dist      = length(to_light);
            light_dir       = to_light / light_dist;
            light_pdf       = 1.0f;

            float range_factor = saturate(1.0f - light_dist / max(light.range, 0.01f));
            attenuation = range_factor * range_factor / max(light_dist * light_dist, 0.01f);

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

        float n_dot_l = dot(shading_normal, light_dir);
        if (n_dot_l <= 0.0f)
            continue;

        if (!trace_shadow_ray(ray_origin_light, light_dir, light_dist))
            continue;

        float  brdf_pdf;
        float3 brdf = evaluate_brdf(albedo, roughness, metallic, shading_normal, view_dir, light_dir, brdf_pdf);

        float mis_weight = is_area ? power_heuristic(light_pdf, brdf_pdf) : 1.0f;

        float3 Li = light_color * light.intensity * attenuation;
        total += brdf * Li * mis_weight / max(light_pdf, 1e-6f);
    }

    // environment probe (sun cone + cosine mixture sampling)
    {
        float2 env_xi = random_float2(seed);

        float3 sun_dir = float3(0, 1, 0);
        bool   has_sun = false;
        if (light_count > 0)
        {
            LightParameters primary_light = light_parameters[0];
            if ((primary_light.flags & (1u << 0)) != 0 && primary_light.intensity > 0.0f)
            {
                sun_dir = -primary_light.direction;
                has_sun = true;
            }
        }

        float sun_cos_max   = cos(SUN_CONE_HALF_ANGLE);
        float sun_cone_pdf  = 1.0f / (2.0f * PI * (1.0f - sun_cos_max));
        float sun_prob      = has_sun ? SUN_SAMPLE_PROBABILITY : 0.0f;

        float3 env_dir;
        float  env_pdf_cos;
        float  env_pdf_sun;
        float  strategy_xi = random_float(seed);

        if (has_sun && strategy_xi < sun_prob)
        {
            float phi     = 2.0f * PI * env_xi.x;
            float cos_th  = lerp(sun_cos_max, 1.0f, env_xi.y);
            float sin_th  = sqrt(max(0.0f, 1.0f - cos_th * cos_th));
            float3 local  = float3(cos(phi) * sin_th, sin(phi) * sin_th, cos_th);
            env_dir       = local_to_world(local, sun_dir);

            float cos_to_sun = dot(env_dir, sun_dir);
            env_pdf_sun = (cos_to_sun >= sun_cos_max) ? sun_cone_pdf : 0.0f;
            env_pdf_cos = max(dot(env_dir, shading_normal), 0.0f) / PI;
        }
        else
        {
            float3 env_local = sample_cosine_hemisphere(env_xi, env_pdf_cos);
            env_dir = local_to_world(env_local, shading_normal);

            float cos_to_sun = has_sun ? dot(env_dir, sun_dir) : -1.0f;
            env_pdf_sun = (has_sun && cos_to_sun >= sun_cos_max) ? sun_cone_pdf : 0.0f;
        }

        float env_pdf = (1.0f - sun_prob) * env_pdf_cos + sun_prob * env_pdf_sun;
        float env_n_dot_l = dot(shading_normal, env_dir);

        if (env_n_dot_l > 0.0f && env_pdf > RESTIR_MIN_PDF)
        {
            float probe_offset = compute_ray_offset(shading_pos);
            RayDesc probe_ray;
            probe_ray.Origin    = shading_pos + geometric_normal * probe_offset;
            probe_ray.Direction = env_dir;
            probe_ray.TMin      = probe_offset;
            probe_ray.TMax      = 10000.0f;

            RayQuery<RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> probe_query;
            probe_query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, probe_ray);
            probe_query.Proceed();

            if (probe_query.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                uint probe_instance = probe_query.CommittedInstanceID();
                MaterialParameters probe_mat = material_parameters[probe_instance];
                float3 probe_emission = probe_emission_estimate(probe_mat);

                if (luminance(probe_emission) > 0.0f)
                {
                    float  brdf_pdf_probe;
                    float3 brdf_probe = evaluate_brdf(albedo, roughness, metallic, shading_normal, view_dir, env_dir, brdf_pdf_probe);

                    float mis_weight = power_heuristic(env_pdf, brdf_pdf_probe);
                    total += brdf_probe * probe_emission * mis_weight / env_pdf;
                }
            }
            else
            {
                float2 env_uv       = direction_sphere_uv(env_dir);
                float3 env_radiance = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), env_uv, SKY_MIP_LEVEL).rgb;
                env_radiance = clamp_sky_radiance(env_radiance);

                float  brdf_pdf_env;
                float3 brdf_env = evaluate_brdf(albedo, roughness, metallic, shading_normal, view_dir, env_dir, brdf_pdf_env);

                float mis_weight_env = power_heuristic(env_pdf, brdf_pdf_env);
                total += brdf_env * env_radiance * mis_weight_env / env_pdf;
            }
        }
    }

    return total;
}

// samples a sky color along a direction (used when a bounce misses geometry)
float3 sample_sky(float3 dir)
{
    float2 uv = direction_sphere_uv(dir);
    float3 sky = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uv, SKY_MIP_LEVEL).rgb;
    return clamp_sky_radiance(sky);
}

// traces the sub-path starting from a surface and returns the outgoing radiance toward the
// previous vertex (used both for the rc vertex suffix and for continuation bounces)
// every vertex uses its real brdf for nee and bounce sampling; the rc vertex's incoming
// direction is src's primary, so the stored radiance is view-dependent at rc w.r.t. the
// reconnection shift, but the rc roughness gate bounds the resulting bias during reuse
// max_bounces is the remaining path budget after this vertex (including this vertex's nee)
float3 accumulate_subpath_radiance(
    PathPayload start,
    float3 start_view_dir,
    uint max_bounces,
    inout uint seed)
{
    float3 total      = float3(0, 0, 0);
    float3 throughput = float3(1, 1, 1);

    total += start.emission;
    total += direct_lighting_at_vertex(
        start.hit_position, start.hit_normal, start.geometric_normal,
        start_view_dir, start.albedo, start.roughness, start.metallic, seed);

    if (max_bounces < 2)
        return total;

    // unified bounce loop: full brdf sampling + evaluation at every vertex including the rc vertex
    PathPayload cur      = start;
    float3      view_dir = start_view_dir;
    float       prev_brdf_pdf = 0.0f;
    float3      prev_normal   = start.hit_normal;

    for (uint bounce = 1; bounce < max_bounces; bounce++)
    {
        if (bounce >= RUSSIAN_ROULETTE_START)
        {
            float continuation_prob = min(luminance(throughput), RUSSIAN_ROULETTE_PROB);
            if (random_float(seed) > continuation_prob)
                break;
            throughput /= continuation_prob;
        }

        float2 xi = random_float2(seed);
        float  pdf;
        float3 nd = sample_brdf(cur.albedo, cur.roughness, cur.metallic, cur.hit_normal, view_dir, xi, pdf);

        if (pdf < RESTIR_MIN_PDF || dot(nd, cur.hit_normal) <= 0.0f || any(isnan(nd)))
            break;

        float  unused_pdf;
        float3 brdf = evaluate_brdf(cur.albedo, cur.roughness, cur.metallic, cur.hit_normal, view_dir, nd, unused_pdf);
        throughput    *= brdf / pdf;
        prev_brdf_pdf  = pdf;
        prev_normal    = cur.hit_normal;

        float ofs = compute_ray_offset(cur.hit_position);
        RayDesc ray;
        ray.Origin    = cur.hit_position + cur.geometric_normal * ofs;
        ray.Direction = nd;
        ray.TMin      = RESTIR_RAY_T_MIN;
        ray.TMax      = 1000.0f;

        PathPayload next;
        next.hit = false;
        TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, next);

        if (!next.hit)
        {
            float w = power_heuristic(prev_brdf_pdf, sky_nee_pdf_at(nd, prev_normal));
            total += throughput * sample_sky(nd) * w;
            break;
        }

        total += throughput * next.emission;
        total += throughput * direct_lighting_at_vertex(
            next.hit_position, next.hit_normal, next.geometric_normal,
            -nd, next.albedo, next.roughness, next.metallic, seed);

        cur      = next;
        view_dir = -nd;
    }

    return total;
}

// traces a full path from the primary vertex given the first indirect direction; captures
// x2 as the reconnection vertex candidate and accumulates the suffix radiance from x2
// the caller samples dir via sample_brdf so the source pdf matches the primary brdf lobe
PathSample trace_path_from_primary(
    float3 primary_pos,
    float3 primary_normal,
    float primary_roughness,
    float3 dir,
    inout uint seed)
{
    PathSample s;
    s.rc_pos          = float3(0, 0, 0);
    s.rc_normal       = float3(0, 1, 0);
    s.rc_radiance     = float3(0, 0, 0);
    s.rc_prev_pos     = primary_pos;
    s.rc_outgoing_dir = float3(0, 1, 0);
    s.seed_path       = seed;
    s.path_length     = 0;
    s.rc_length       = 0;
    s.flags           = 0;

    if (dot(dir, primary_normal) <= 0.0f)
        return s;

    float primary_offset = compute_ray_offset(primary_pos);
    RayDesc ray;
    ray.Origin    = primary_pos + primary_normal * primary_offset;
    ray.Direction = dir;
    ray.TMin      = RESTIR_RAY_T_MIN;
    ray.TMax      = 1000.0f;

    PathPayload hit;
    hit.hit = false;
    TraceRay(tlas, RAY_FLAG_NONE, 0xFF, 0, 1, 0, ray, hit);

    if (!hit.hit)
    {
        s.flags      |= PATH_FLAG_SKY;
        s.rc_pos      = dir;
        s.rc_normal   = -dir;
        s.rc_radiance = sample_sky(dir);
        s.rc_length   = 0;
        s.path_length = 1;
        return s;
    }

    s.rc_pos      = hit.hit_position;
    s.rc_normal   = hit.geometric_normal;
    s.rc_length   = 2;

    float3 suffix = accumulate_subpath_radiance(hit, -dir, RESTIR_MAX_PATH_LENGTH - 1, seed);
    s.rc_radiance = soft_saturate_radiance(suffix, RESTIR_FIREFLY_LUMA);
    s.path_length = 2;

    // reconnection validity: the rc vertex must be rough (stored radiance is view-dependent
    // at rc w.r.t. src's incoming, and roughness bounds the shift error) and distant enough
    // to keep the solid-angle jacobian well-conditioned
    float dist_sq = dot(hit.hit_position - primary_pos, hit.hit_position - primary_pos);
    bool rc_valid = (hit.roughness >= RESTIR_RC_MIN_ROUGHNESS)
                 && (dist_sq       >= RESTIR_RC_MIN_DISTANCE * RESTIR_RC_MIN_DISTANCE);
    if (rc_valid)
        s.flags |= PATH_FLAG_HAS_RC;

    if (primary_roughness < RESTIR_RC_MIN_ROUGHNESS)
        s.flags |= PATH_FLAG_SPECULAR;

    return s;
}

[shader("raygeneration")]
void ray_gen()
{
    uint2 launch_id   = DispatchRaysIndex().xy;
    uint2 launch_size = DispatchRaysDimensions().xy;
    float2 uv = (launch_id + 0.5f) / launch_size;

    if (geometry_infos[0].vertex_offset == 0xFFFFFFFF)
        return;

    // early out for sky pixels (no primary surface)
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

    uint seed = create_seed_for_pass(launch_id, buffer_frame.frame, 0);

    // gather primary surface properties from the g-buffer
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    float4 material  = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float3 albedo    = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float  roughness = max(material.r, 0.04f);
    float  metallic  = material.g;
    float3 view_dir  = normalize(get_camera_position() - pos_ws);

    Reservoir reservoir = create_empty_reservoir();

    // ris streaming over N brdf-sampled candidate paths
    // source pdf matches the primary brdf lobe (diffuse+ggx) so specular hits don't explode
    // every iteration calls update_reservoir so M counts every trial (paper-form unbiased ris)
    for (uint i = 0; i < INITIAL_CANDIDATE_SAMPLES; i++)
    {
        float2 xi = random_float2(seed);
        float  source_pdf;
        float3 dir = sample_brdf(albedo, roughness, metallic, normal_ws, view_dir, xi, source_pdf);

        bool dir_valid = (source_pdf >= RESTIR_MIN_PDF) &&
                         (dot(dir, normal_ws) >= MIN_COS_AT_PRIMARY) &&
                         !any(isnan(dir));

        PathSample candidate = (PathSample)0;
        float weight = 0.0f;

        if (dir_valid)
        {
            candidate = trace_path_from_primary(pos_ws, normal_ws, roughness, dir, seed);
            float target_pdf = target_pdf_self(candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
                weight = target_pdf / source_pdf;
        }

        update_reservoir(reservoir, candidate, weight, random_float(seed));
    }

    // finalize: m_i = 1/M for the initial pass, so weight_sum becomes (1/M) * sum(p_hat/p)
    // and W = weight_sum / p_hat_y matches the paper-form output used by downstream merges
    if (reservoir.M > 0.0f)
        reservoir.weight_sum /= reservoir.M;

    float final_target = target_pdf_self(reservoir.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    reservoir.target_pdf = final_target;
    reservoir.W = (final_target > 0.0f) ? (reservoir.weight_sum / final_target) : 0.0f;

    float w_clamp = get_w_clamp_for_sample(reservoir.sample);
    reservoir.W = min(reservoir.W, w_clamp);

    float sample_count_quality = saturate(reservoir.M / float(INITIAL_CANDIDATE_SAMPLES));
    reservoir.confidence       = (final_target > 0.0f) ? sample_count_quality : 0.0f;
    reservoir.age              = 0.0f;

    float4 t0, t1, t2, t3, t4;
    pack_reservoir(reservoir, t0, t1, t2, t3, t4);
    tex_reservoir0[launch_id] = t0;
    tex_reservoir1[launch_id] = t1;
    tex_reservoir2[launch_id] = t2;
    tex_reservoir3[launch_id] = t3;
    tex_reservoir4[launch_id] = t4;

    // tex_uav is overwritten by temporal/spatial passes so no shading is needed here
    // confidence is forwarded so passes that early-out can read a sane value
    tex_uav[launch_id] = float4(0, 0, 0, saturate(reservoir.confidence));
}

[shader("closesthit")]
void closest_hit(inout PathPayload payload : SV_RayPayload, in BuiltInTriangleIntersectionAttributes attribs : SV_IntersectionAttributes)
{
    payload.hit = true;

    uint material_index    = InstanceID();
    MaterialParameters mat = material_parameters[material_index];

    uint instance_index = InstanceIndex();
    GeometryInfo geo    = geometry_infos[instance_index];

    uint primitive_index = PrimitiveIndex();
    uint index_base      = geo.index_offset + primitive_index * 3;
    uint i0 = geometry_indices[index_base + 0];
    uint i1 = geometry_indices[index_base + 1];
    uint i2 = geometry_indices[index_base + 2];

    PulledVertex pv0 = geometry_vertices[geo.vertex_offset + i0];
    PulledVertex pv1 = geometry_vertices[geo.vertex_offset + i1];
    PulledVertex pv2 = geometry_vertices[geo.vertex_offset + i2];

    float3 bary = float3(1.0f - attribs.barycentrics.x - attribs.barycentrics.y,
                         attribs.barycentrics.x, attribs.barycentrics.y);

    float3 n0 = unpack_vertex_oct(pv0.normal);
    float3 n1 = unpack_vertex_oct(pv1.normal);
    float3 n2 = unpack_vertex_oct(pv2.normal);
    float3 t0 = unpack_vertex_oct(pv0.tangent);
    float3 t1 = unpack_vertex_oct(pv1.tangent);
    float3 t2 = unpack_vertex_oct(pv2.tangent);
    float2 uv0 = unpack_vertex_uv(pv0.uv);
    float2 uv1 = unpack_vertex_uv(pv1.uv);
    float2 uv2 = unpack_vertex_uv(pv2.uv);

    float3 normal_object  = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float3 tangent_object = normalize(t0 * bary.x + t1 * bary.y + t2 * bary.z);
    float2 texcoord       = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    float3x3 obj_to_world = (float3x3)ObjectToWorld4x3();
    float3x3 world_to_obj = (float3x3)WorldToObject4x3();
    float3 normal_world   = normalize(mul(normal_object, transpose(world_to_obj)));
    float3 tangent_world  = normalize(mul(tangent_object, obj_to_world));

    // full uv state is per-renderable, fetched from geometry_infos[InstanceIndex()]
    if (geo.uv_world_space > 0.0f)
    {
        float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
        texcoord            = compute_world_space_uv(hit_position, normal_world);
        texcoord            = texcoord * geo.uv_tiling + geo.uv_offset;
    }
    else
    {
        texcoord = texcoord * geo.uv_tiling + geo.uv_offset;
    }

    if (geo.uv_rotation != 0.0f)
        texcoord = rotate_uv_90(texcoord, geo.uv_rotation);

    float dist      = RayTCurrent();
    float mip_level = clamp(log2(max(dist * 0.5f, 1.0f)), 0.0f, 4.0f);

    float3 albedo = mat.color.rgb;
    if (mat.has_texture_albedo())
    {
        uint albedo_texture_index = material_index + material_texture_index_albedo;
        float4 sampled = material_textures[albedo_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        albedo = sampled.rgb * mat.color.rgb;
    }
    albedo = saturate(albedo);

    float roughness = mat.roughness;
    if (mat.has_texture_roughness())
    {
        uint roughness_texture_index = material_index + material_texture_index_roughness;
        roughness *= material_textures[roughness_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).g;
    }
    roughness = max(roughness, 0.04f);

    float metallic = mat.metallness;
    if (mat.has_texture_metalness())
    {
        uint metalness_texture_index = material_index + material_texture_index_metalness;
        metallic *= material_textures[metalness_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).r;
    }

    float3x3 obj_to_world_3x3 = (float3x3)ObjectToWorld4x3();
    float3 edge1_world   = mul(pv1.position - pv0.position, obj_to_world_3x3);
    float3 edge2_world   = mul(pv2.position - pv0.position, obj_to_world_3x3);
    float triangle_area  = 0.5f * length(cross(edge1_world, edge2_world));
    float3 geometric_normal = normalize(cross(edge1_world, edge2_world));

    if (dot(geometric_normal, WorldRayDirection()) > 0.0f)
        geometric_normal = -geometric_normal;
    if (dot(normal_world, geometric_normal) < 0.0f)
        normal_world = -normal_world;

    float3 tangent_projected = tangent_world - geometric_normal * dot(tangent_world, geometric_normal);
    if (dot(tangent_projected, tangent_projected) > 1e-6f)
    {
        tangent_world = normalize(tangent_projected);
    }
    else
    {
        float3 fallback_bitangent;
        build_orthonormal_basis_fast(geometric_normal, tangent_world, fallback_bitangent);
    }

    if (mat.has_texture_normal())
    {
        uint normal_texture_index = material_index + material_texture_index_normal;
        float3 normal_sample = material_textures[normal_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).rgb;

        normal_sample = normal_sample * 2.0f - 1.0f;
        normal_sample.xy *= mat.normal;

        float3 bitangent = normalize(cross(geometric_normal, tangent_world));
        float3x3 tbn     = float3x3(tangent_world, bitangent, geometric_normal);

        normal_world = normalize(mul(normal_sample, tbn));
        if (dot(normal_world, geometric_normal) < 0.0f)
            normal_world = -normal_world;
    }

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

    payload.hit_position     = hit_position;
    payload.hit_normal       = normal_world;
    payload.geometric_normal = geometric_normal;
    payload.albedo           = albedo;
    payload.emission         = emission;
    payload.roughness        = roughness;
    payload.metallic         = metallic;
    payload.triangle_area    = triangle_area;
}

[shader("miss")]
void miss(inout PathPayload payload : SV_RayPayload)
{
    payload.hit      = false;
    payload.emission = float3(0.0f, 0.0f, 0.0f);
}
