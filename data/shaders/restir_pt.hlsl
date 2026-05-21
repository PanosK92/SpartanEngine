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

// upper bounds on the per pixel ris pool sizes, the live counts come from get_restir_*
// helpers in restir_reservoir.hlsl which return paper-faithful hardcoded values (1 brdf
// candidate, 8 nee candidates), the caps below only bound the unrolled loop size
static const uint  INITIAL_CANDIDATE_SAMPLES_MAX   = 8;
static const uint  LIGHT_RIS_CANDIDATE_SAMPLES_MAX = 64;
static const float MIN_COS_AT_PRIMARY              = 1e-3f;
static const float RUSSIAN_ROULETTE_PROB           = 0.95f;
// start russian roulette later (bounce 4) so the first few bounces are deterministic, with
// path budget 5 this leaves only the last bounce subject to rr which is enough to terminate
// long dim paths cleanly without inflating throughput on near-camera bounces
static const uint  RUSSIAN_ROULETTE_START          = 4;
// dim-throughput firefly guard, low continuation prob means rare survivors get throughput
// inflated by 1/p, which is the dominant source of "boiling splotches" on shadowed indirect
// paths, lin 2022 and standard production path tracers clamp p well above zero, raising the
// floor from 0.1 (10x boost) to 0.25 (4x boost max) makes firefly survivors much less
// energetic and removes the chromatic dancing on dim crevices
static const float RUSSIAN_ROULETTE_MIN_PROB       = 0.25f;
static const float SKY_MIP_LEVEL               = 2.0f;
// sun cone half angle, ~0.27 degrees matches the real sun's angular radius (vs the previous
// 0.015 rad / ~0.86 degrees which was about 3x larger and produced shadows that were too soft
static const float SUN_CONE_HALF_ANGLE         = 0.0047f;
// MIN_AREA_LIGHT_SOLID_ANGLE moved to restir_reservoir.hlsl since it is shared with the spatial pass nee

// computes the sun-vs-cosine-hemisphere mixture weight from actual sun radiance, replaces the
// previous fixed 0.5 magic constant which wasted samples on a sun-direction cone for dim or
// missing suns and undersampled the cone for very bright suns, the weight is the balance
// heuristic on solid-angle-scaled radiance with a tiny floor so a non-zero sun never gets
// fully ignored
float sun_sample_probability(bool has_sun, float sun_intensity, float3 sun_color, float sun_cos_max)
{
    if (!has_sun)
    {
        return 0.0f;
    }
    float sun_omega   = 2.0f * PI * (1.0f - sun_cos_max);
    float sun_radiance = luminance(sun_color) * max(sun_intensity, 0.0f);
    float sun_w       = sun_radiance * sun_omega;
    // sky reference: hemispherical integral of a unit-luminance ambient probe, gives a stable
    // denominator that does not require sampling the probe to estimate average radiance
    float sky_w       = 2.0f * PI;
    float prob        = sun_w / max(sun_w + sky_w, 1e-6f);
    return clamp(prob, 0.05f, 0.95f);
}

struct [raypayload] PathPayload
{
    float3 hit_position     : read(caller) : write(closesthit);
    float3 hit_normal       : read(caller) : write(closesthit);
    float3 geometric_normal : read(caller) : write(closesthit);
    float3 albedo           : read(caller) : write(closesthit);
    float3 emission         : read(caller) : write(closesthit, miss);
    float  roughness        : read(caller) : write(closesthit);
    float  metallic         : read(caller) : write(closesthit);
    bool   hit              : read(caller) : write(closesthit, miss);
};

// trace_shadow_ray moved to restir_reservoir.hlsl so it is callable from spatial / temporal passes too

float3 probe_emission_estimate(MaterialParameters mat)
{
    if (mat.emissive_from_albedo() || mat.has_texture_emissive())
        return mat.color.rgb;
    return float3(0.0f, 0.0f, 0.0f);
}

// power-proportional light pick weight, lin 2022 §6.1 / restir di reference
// all four light types (directional, point, spot, area) are eligible, point/spot weights are
// scaled by 1/distance_to_camera^2 approximation via 1/range^2 so a faraway lamp does not
// dominate the picker for a primary near another light, this is the standard veach 1997
// "important light" heuristic adapted for the dirac case where exact 1/r^2 at the primary
// would require per-pixel weight evaluation (too expensive for restir's once-per-frame
// light budget)
float light_pick_weight(LightParameters l)
{
    if (l.intensity <= 0.0f)
        return 0.0f;

    bool is_directional = (l.flags & (1u << 0)) != 0;
    bool is_point       = (l.flags & (1u << 1)) != 0;
    bool is_spot        = (l.flags & (1u << 2)) != 0;
    bool is_area        = (l.flags & (1u << 6)) != 0;

    float lum = max(luminance(l.color.rgb), 1e-3f);
    if (is_directional || is_area)
    {
        return l.intensity * lum;
    }
    if (is_point || is_spot)
    {
        // dirac local light, weight by intensity * luminance * cone fraction, range is a
        // cutoff distance not a power proxy so the previous range^2 multiplier biased the
        // picker toward long range dim lamps which then dominated the ris stream with
        // candidates that contributed little at the primary, spot lights are weighted by
        // their cone solid angle so a narrow cone gets proportionally less budget than a
        // uniform sphere of equal intensity
        float cone_factor = 1.0f;
        if (is_spot)
        {
            float cos_outer = cos(l.angle);
            cone_factor     = max(1.0f - cos_outer, 0.05f);
        }
        return l.intensity * lum * cone_factor;
    }
    return 0.0f;
}

// total nee pick weight, called once per evaluation, the loop is O(light_count) so this stays
// cheap for typical scenes (<50 lights), large open worlds with many lights would benefit from
// a cpu-built prefix sum buffer
float compute_total_light_weight()
{
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    float total = 0.0f;
    for (uint i = 0; i < light_count; i++)
    {
        total += light_pick_weight(light_parameters[i]);
    }
    return total;
}

// importance pick pdf for a known light index, returns the same probability that the importance
// picker assigns to this light, used by the brdf strategy's mis denominator on sky candidates
float light_pick_pdf_for_index(uint light_idx, float total_weight)
{
    if (total_weight <= 0.0f)
        return 0.0f;
    return light_pick_weight(light_parameters[light_idx]) / total_weight;
}

// computes the nee strategy density at a brdf-sampled candidate, in solid angle at primary
// returns the sun cone density for sky candidates that fall inside the cone, zero otherwise
// area lights are not in the bvh so the brdf strategy can never produce paths whose rc lies
// on an area light rectangle, point/spot are dirac and likewise cannot be hit by a brdf
// bounce, so the nee strategy contributes zero to the brdf-candidate mis denominator for
// those light types and the brdf candidate gets its full single-strategy mis weight
float light_nee_pdf_for_candidate(PathSample candidate, float3 primary_pos)
{
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    if (light_count == 0)
        return 0.0f;

    if (!is_sky_sample(candidate))
        return 0.0f;

    LightParameters p = light_parameters[0];
    bool is_directional = (p.flags & (1u << 0)) != 0;
    if (!is_directional || p.intensity <= 0.0f)
        return 0.0f;

    float3 sun_dir     = -p.direction;
    float  sun_cos_max = cos(SUN_CONE_HALF_ANGLE);
    if (dot(candidate.rc_pos, sun_dir) < sun_cos_max)
        return 0.0f;

    float total = compute_total_light_weight();
    if (total <= 0.0f)
        return 0.0f;

    float pick_pdf     = light_pick_pdf_for_index(0u, total);
    float sun_cone_pdf = 1.0f / (2.0f * PI * (1.0f - sun_cos_max));
    return pick_pdf * sun_cone_pdf;
}

// emissive triangle nee pool active check, the cpu writes the count into the frame cb each
// frame after walking renderables with non zero emission, when this returns false the ris
// loop skips the emtri strategy entirely so a scene with only analytical lights and the sun
// runs the path tracer exactly as it did before this feature
bool is_emtri_pool_active()
{
    return buffer_frame.restir_pt_emissive_tri_count > 0.5f;
}

// binary search over the cdf, returns the triangle index whose cumulative weight first
// exceeds u, all triangles share the same cdf prefix sum so the lookup is o(log n)
uint emtri_pick_index(float u, uint count)
{
    uint lo = 0u;
    uint hi = count;
    while (lo < hi)
    {
        uint mid = (lo + hi) >> 1u;
        if (emissive_triangles[mid].cdf < u)
        {
            lo = mid + 1u;
        }
        else
        {
            hi = mid;
        }
    }
    return min(lo, count - 1u);
}

// samples a candidate path whose rc vertex sits on a uniformly area sampled point of an
// emissive triangle picked with probability weight_i / total_weight, returns the rc position,
// the emitted radiance and the solid angle pdf at the primary vertex, the resulting candidate
// is single strategy under mis because the brdf bounce cannot reproduce an identical sample
// (different stored l_nee), the emtri pool effectively partitions the integrand with the
// brdf strategy via the rc.emission zeroing logic below, this trades a small amount of bias
// for a large variance reduction on scenes with bright glowing surfaces
PathSample sample_emissive_tri_candidate(
    float3 primary_pos,
    float3 primary_normal,
    inout uint seed,
    out float source_pdf)
{
    PathSample s;
    s.rc_pos          = float3(0, 0, 0);
    s.rc_normal       = float3(0, 1, 0);
    s.rc_outgoing_dir = float3(0, 1, 0);
    s.rc_L_post       = float3(0, 0, 0);
    s.rc_L_nee        = float3(0, 0, 0);
    s.rc_albedo       = float3(0, 0, 0);
    s.rc_roughness    = 1.0f;
    s.rc_metallic     = 0.0f;
    s.src_pos         = float3(0, 0, 0);
    s.src_normal      = float3(0, 1, 0);
    s.src_albedo      = float3(0, 0, 0);
    s.src_roughness   = 1.0f;
    s.src_metallic    = 0.0f;
    s.seed_path       = seed;
    s.path_length     = 1;
    s.rc_length       = 2;
    s.flags           = 0;
    source_pdf        = 0.0f;

    uint emtri_count = (uint)buffer_frame.restir_pt_emissive_tri_count;
    if (emtri_count == 0u)
    {
        return s;
    }

    // last triangle's cdf is the total weight, the cpu builds it as a running prefix sum
    float total_weight = emissive_triangles[emtri_count - 1u].cdf;
    if (total_weight <= 0.0f)
    {
        return s;
    }

    float pick_xi = random_float(seed) * total_weight;
    uint  tri_idx = emtri_pick_index(pick_xi, emtri_count);
    EmissiveTriangle tri = emissive_triangles[tri_idx];
    if (tri.area <= 0.0f || tri.weight <= 0.0f)
    {
        return s;
    }

    // uniform barycentric on triangle, paper standard 1 - sqrt(xi.x) transformation gives
    // an unbiased uniform area distribution from two independent unit uniforms
    float2 xi = random_float2(seed);
    float  su = sqrt(max(xi.x, 0.0f));
    float  b0 = 1.0f - su;
    float  b1 = su * (1.0f - xi.y);
    float  b2 = su * xi.y;

    float3 sampled_pos = tri.v0 * b0 + tri.v1 * b1 + tri.v2 * b2;
    float3 to          = sampled_pos - primary_pos;
    float  dist        = length(to);
    if (dist < 1e-3f)
    {
        return s;
    }

    float3 dir       = to / dist;
    float  cos_at_p  = dot(dir, primary_normal);
    if (cos_at_p <= MIN_COS_AT_PRIMARY)
    {
        return s;
    }

    // emitter is single sided, geometry behind the normal does not emit, also lets the cos
    // term in the solid angle conversion stay positive without abs
    float cos_at_e = dot(-dir, tri.normal);
    if (cos_at_e <= 0.0f)
    {
        return s;
    }

    // solid angle pdf = pick_prob * area_pdf * jacobian(area to solid angle)
    //                 = (w_i / total_w) * (1 / area) * (dist^2 / cos_at_e)
    float pick_prob = tri.weight / total_weight;
    float area_pdf  = 1.0f / tri.area;
    float sa_jac    = (dist * dist) / max(cos_at_e, 1e-4f);
    source_pdf      = pick_prob * area_pdf * sa_jac;

    s.flags    |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;
    s.rc_pos    = sampled_pos;
    s.rc_normal = tri.normal;
    s.rc_L_nee  = tri.emission;
    s.rc_L_post = float3(0, 0, 0);
    return s;
}

// sample_spherical_rectangle moved to restir_reservoir.hlsl since it is shared with the
// replay shift's inline next event estimation in the spatial / temporal compute contexts

// env nee density at a given direction for mis against brdf bounces that miss into the sky
float sky_nee_pdf_at(float3 dir, float3 shading_normal)
{
    float3 sun_dir       = float3(0, 1, 0);
    float  sun_intensity = 0.0f;
    float3 sun_color     = float3(1, 1, 1);
    bool   has_sun       = false;
    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    if (light_count > 0)
    {
        LightParameters p = light_parameters[0];
        if ((p.flags & (1u << 0)) != 0 && p.intensity > 0.0f)
        {
            sun_dir       = -p.direction;
            sun_intensity = p.intensity;
            sun_color     = p.color.rgb;
            has_sun       = true;
        }
    }

    float sun_cos_max  = cos(SUN_CONE_HALF_ANGLE);
    float sun_cone_pdf = 1.0f / (2.0f * PI * (1.0f - sun_cos_max));
    float sun_prob     = sun_sample_probability(has_sun, sun_intensity, sun_color, sun_cos_max);

    float pdf_cos = max(dot(dir, shading_normal), 0.0f) / PI;
    float pdf_sun = (has_sun && dot(dir, sun_dir) >= sun_cos_max) ? sun_cone_pdf : 0.0f;
    return (1.0f - sun_prob) * pdf_cos + sun_prob * pdf_sun;
}

// evaluates direct lighting (analytical lights + environment probe) at a surface vertex
// returns the contribution in the direction of view_dir (i.e. back toward the previous vertex)
// specular_blend is a continuous [0, 1] weight on the specular lobe forwarded to evaluate_brdf
//   blend = 1 keeps the full brdf, this is correct for suffix vertices past rc which are
//     baked into rc_L_post and not individually reused across pixels
//   blend = 0 drops the specular lobe leaving only burley diffuse, this is the correct
//     choice for the rc vertex itself because the resulting nee radiance must be
//     view-independent so the reconnection shift can reuse it at any dst primary without
//     re-evaluating the rc brdf against the dst-correct incoming direction, the rc roughness
//     gate makes the missing specular lobe a bounded, negligible energy loss at the same vertex
//   intermediate values produce a smooth ramp matching the primary-vertex lobe handoff used
//     by ray traced reflections (see restir_primary_specular_blend)
float3 direct_lighting_at_vertex(
    float3 shading_pos,
    float3 shading_normal,
    float3 geometric_normal,
    float3 view_dir,
    float3 albedo,
    float roughness,
    float metallic,
    float  specular_blend,
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

            // urena 2013 spherical rectangle solid-angle sampling, identical to the primary
            // nee path so importance sampling is consistent at every vertex, the rectangle
            // is parametrized as { rect_origin + s*ex + t*ey | s,t in [0,1] }
            float3 ex          = light_right * light.area_width;
            float3 ey          = light_up    * light.area_height;
            float3 rect_origin = light.position - 0.5f * ex - 0.5f * ey;

            float2 xi = random_float2(seed);
            float3 light_sample_pos;
            float  solid_angle;
            sample_spherical_rectangle(shading_pos, rect_origin, ex, ey, xi, light_sample_pos, solid_angle);

            if (solid_angle < MIN_AREA_LIGHT_SOLID_ANGLE)
                continue;

            float3 to_light = light_sample_pos - shading_pos;
            light_dist      = length(to_light);
            if (light_dist < 1e-3f)
                continue;
            light_dir       = to_light / light_dist;

            float cos_light = dot(-light_dir, light_normal);
            if (cos_light <= 0.0f)
                continue;

            light_pdf   = 1.0f / solid_angle;
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

        // secondary vertex always uses the full brdf, primary specular routing only applies
        // at the primary vertex itself which is shaded by self_shift_evaluate or the shifts
        // at the rc vertex itself we use lambert only so the stored nee stays view-independent
        float  brdf_pdf;
        float3 brdf = evaluate_brdf(albedo, roughness, metallic, shading_normal, view_dir, light_dir, brdf_pdf, specular_blend);

        // analytical lights are not in the bvh, the brdf bounce can never sample to their
        // direction so there is no second strategy to mis with, applying power_heuristic against
        // a fictitious brdf strategy darkens the contribution by ~50%, single-strategy weight
        // of 1.0 is the correct mis here, the env probe block below keeps power_heuristic
        // because the brdf-into-sky branch in accumulate_subpath_radiance does sample sky
        float mis_weight = 1.0f;

        float3 Li = light_color * light.intensity * attenuation;
        total += brdf * Li * mis_weight / max(light_pdf, 1e-6f);
    }

    // environment probe (sun cone + cosine mixture sampling)
    {
        float2 env_xi = random_float2(seed);

        float3 sun_dir       = float3(0, 1, 0);
        float  sun_intensity = 0.0f;
        float3 sun_color     = float3(1, 1, 1);
        bool   has_sun       = false;
        if (light_count > 0)
        {
            LightParameters primary_light = light_parameters[0];
            if ((primary_light.flags & (1u << 0)) != 0 && primary_light.intensity > 0.0f)
            {
                sun_dir       = -primary_light.direction;
                sun_intensity = primary_light.intensity;
                sun_color     = primary_light.color.rgb;
                has_sun       = true;
            }
        }

        float sun_cos_max   = cos(SUN_CONE_HALF_ANGLE);
        float sun_cone_pdf  = 1.0f / (2.0f * PI * (1.0f - sun_cos_max));
        float sun_prob      = sun_sample_probability(has_sun, sun_intensity, sun_color, sun_cos_max);

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
                    float3 brdf_probe = evaluate_brdf(albedo, roughness, metallic, shading_normal, view_dir, env_dir, brdf_pdf_probe, specular_blend);

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
                float3 brdf_env = evaluate_brdf(albedo, roughness, metallic, shading_normal, view_dir, env_dir, brdf_pdf_env, specular_blend);

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

// traces the suffix past the rc vertex with the rc bsdf factored out, this enables paper-
// faithful indirect specular re-evaluation at shift time (lin 2022 §5), throughput is
// initialized to 1/pdf_at_rc so the standard brdf*cos*L/pdf monte carlo estimator at rc
// becomes f_rc(in, rc_outgoing_dir) * L_post when the caller multiplies by f_rc later
// max_bounces_remaining is the path budget past rc (rc itself does not consume budget here,
// the rc nee + emission live in the L_nee component computed by accumulate_subpath_at_rc)
// out_first_dir is the direction leaving rc into the suffix (rc_outgoing_dir for storage)
void trace_rc_suffix(
    PathPayload rc,
    float3 rc_view_dir,
    uint max_bounces_remaining,
    inout uint seed,
    out float3 out_L_post,
    out float3 out_first_dir)
{
    out_L_post    = float3(0, 0, 0);
    out_first_dir = float3(0, 0, 0);

    if (max_bounces_remaining < 1)
        return;

    PathPayload cur            = rc;
    float3      view_dir       = rc_view_dir;
    float3      throughput     = float3(1, 1, 1);
    float       prev_brdf_pdf  = 0.0f;
    float3      prev_normal    = rc.hit_normal;
    bool        first_iter     = true;

    for (uint bounce = 0; bounce < max_bounces_remaining; bounce++)
    {
        if (bounce >= RUSSIAN_ROULETTE_START)
        {
            // anticipate next bounce attenuation by folding the current vertex albedo into
            // the continuation factor, this veach-style adjusted rr terminates paths whose
            // future reflectance will be dim, rather than only looking at the throughput
            // accumulated so far, the same xi is consumed for the rr test regardless
            float3 next_throughput   = throughput * cur.albedo;
            float  continuation_prob = clamp(luminance(next_throughput), RUSSIAN_ROULETTE_MIN_PROB, RUSSIAN_ROULETTE_PROB);
            if (random_float(seed) > continuation_prob)
                break;
            throughput /= continuation_prob;
        }

        float2 xi = random_float2(seed);
        float  pdf;
        float3 nd = sample_brdf(cur.albedo, cur.roughness, cur.metallic, cur.hit_normal, view_dir, xi, pdf, 1.0f);

        if (pdf < RESTIR_MIN_PDF || dot(nd, cur.hit_normal) <= 0.0f || any(isnan(nd)))
            break;

        if (first_iter)
        {
            // factor out f_rc at the rc vertex, throughput becomes 1/pdf so the caller can
            // re-multiply by f_rc(dst_in, rc_outgoing_dir) at shift time, this is the entire
            // point of the rc storage refactor and removes the view-dep bias on indirect
            // specular reuse that the previous rc_radiance baked in
            out_first_dir = nd;
            throughput    = float3(1, 1, 1) / pdf;
            first_iter    = false;
        }
        else
        {
            float  unused_pdf;
            float3 brdf = evaluate_brdf(cur.albedo, cur.roughness, cur.metallic, cur.hit_normal, view_dir, nd, unused_pdf, 1.0f);
            throughput *= brdf / pdf;
        }

        prev_brdf_pdf = pdf;
        prev_normal   = cur.hit_normal;

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
            out_L_post += throughput * sample_sky(nd) * w;
            break;
        }

        // emissive triangle hit via brdf bounce, collected single-strategy here, lin 2022
        // suggests pairing this with an explicit emissive-triangle nee pool (mis between brdf-
        // hit and area-sampled emissive triangles) for low variance on small bright emitters,
        // that requires a cpu-built emissive triangle structured buffer which is not part of
        // this rewrite, the bounce-emission path is unbiased so this is a variance-only win
        out_L_post += throughput * next.emission;
        // suffix vertices past rc, full brdf is correct here because L_post is reused as a
        // whole only via the rc bsdf factor at shift time, individual suffix vertices are not
        // re-evaluated against a different primary direction
        out_L_post += throughput * direct_lighting_at_vertex(
            next.hit_position, next.hit_normal, next.geometric_normal,
            -nd, next.albedo, next.roughness, next.metallic, 1.0f, seed);

        cur      = next;
        view_dir = -nd;
    }
}

// gathers the rc vertex's nee + emission contribution (with the rc brdf reduced to lambert
// only so the stored radiance is view-independent and reuses correctly at any dst primary,
// missing specular-lobe energy at rc is bounded by the get_restir_rc_min_roughness floor)
// and the suffix radiance past rc (with the rc brdf factored out into rc_outgoing_dir for
// paper-faithful reconnection shifts)
void accumulate_subpath_at_rc(
    PathPayload rc,
    float3 rc_view_dir,
    uint max_bounces,
    inout uint seed,
    out float3 out_L_nee,
    out float3 out_L_post,
    out float3 out_first_dir)
{
    // partition emission contribution between strategies, when the emtri nee pool is active
    // the emtri strategy carries direct emission from primary to an emissive triangle, so
    // the brdf strategy zeros rc.emission to avoid double counting paths that land on the
    // same triangle by chance, the emtri pool drops emission contribution for triangles past
    // its cap so we keep a small floor to handle the rare miss
    out_L_nee = is_emtri_pool_active() ? float3(0, 0, 0) : rc.emission;
    // diffuse-only brdf at rc keeps the stored nee radiance view-independent so the
    // reconnection shift can reuse it at any dst primary without re-evaluating the rc brdf
    // against the dst-correct incoming direction, the rc roughness gate at 0.3 bounds the
    // missing specular lobe energy at this same vertex
    out_L_nee += direct_lighting_at_vertex(
        rc.hit_position, rc.hit_normal, rc.geometric_normal,
        rc_view_dir, rc.albedo, rc.roughness, rc.metallic, 0.0f, seed);

    out_L_post    = float3(0, 0, 0);
    out_first_dir = float3(0, 0, 0);

    if (max_bounces < 2)
        return;

    trace_rc_suffix(rc, rc_view_dir, max_bounces - 1, seed, out_L_post, out_first_dir);
}

// builds a path sample candidate by directly sampling an analytical light or the sun cone
// the candidate's rc vertex is the sampled light point (or sky direction for the sun) and
// rc_radiance carries the emitted radiance toward the primary, the source_pdf returned is
// in solid-angle measure at the primary so it can be combined with brdf-sampled candidates
// in a single mixture-ris pool. point/spot delta lights are skipped: their solid-angle pdf
// is effectively infinite and they are already handled by the spatial-pass direct lighting
PathSample sample_light_candidate(
    float3 primary_pos,
    float3 primary_normal,
    inout uint seed,
    out float source_pdf)
{
    PathSample s;
    s.rc_pos          = float3(0, 0, 0);
    s.rc_normal       = float3(0, 1, 0);
    s.rc_outgoing_dir = float3(0, 1, 0);
    s.rc_L_post       = float3(0, 0, 0);
    s.rc_L_nee        = float3(0, 0, 0);
    s.rc_albedo       = float3(0, 0, 0);
    s.rc_roughness    = 1.0f;
    s.rc_metallic     = 0.0f;
    s.src_pos         = float3(0, 0, 0);
    s.src_normal      = float3(0, 1, 0);
    s.src_albedo      = float3(0, 0, 0);
    s.src_roughness   = 1.0f;
    s.src_metallic    = 0.0f;
    s.seed_path       = seed;
    s.path_length     = 1;
    s.rc_length       = 2;
    s.flags           = 0;
    source_pdf        = 0.0f;

    uint light_count = (uint)buffer_frame.restir_pt_light_count;
    if (light_count == 0)
        return s;

    // power-proportional pick, lin 2022 §6.1
    // walks the per-light weight array once to compute the total then again to draw the index,
    // matches light_pick_pdf_for_index used by the mis denominator so the source pdf is
    // consistent across sample and reweight paths, uniform pick (the previous behavior) gave
    // dim lights too much budget at the expense of bright ones, increasing variance on the
    // dominant lighter contributors which the denoiser then has to smooth
    float total_weight = compute_total_light_weight();
    if (total_weight <= 0.0f)
        return s;

    float xi  = random_float(seed) * total_weight;
    float cum = 0.0f;
    uint  light_idx = light_count - 1;
    for (uint i = 0; i < light_count; i++)
    {
        cum += light_pick_weight(light_parameters[i]);
        if (xi <= cum)
        {
            light_idx = i;
            break;
        }
    }

    LightParameters light = light_parameters[light_idx];
    if (light.intensity <= 0.0f)
        return s;

    bool is_directional = (light.flags & (1u << 0)) != 0;
    bool is_area        = (light.flags & (1u << 6)) != 0;
    float pick_pdf      = light_pick_pdf_for_index(light_idx, total_weight);
    if (pick_pdf <= 0.0f)
        return s;

    if (is_directional)
    {
        // continuous sun cone, matches sky_nee_pdf_at and light_nee_pdf_for_candidate which
        // treat the sun as a continuous emitter over the cone of half angle SUN_CONE_HALF_ANGLE
        // sampling a direction inside the cone with sun_cone_pdf gives consistent mis with the
        // brdf-into-sky branch in the bounce loop, the radiance is constant inside the cone
        float3 sun_dir = -light.direction;
        if (dot(sun_dir, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        float  sun_cos_max  = cos(SUN_CONE_HALF_ANGLE);
        float  sun_cone_pdf = 1.0f / (2.0f * PI * (1.0f - sun_cos_max));

        float2 xi      = random_float2(seed);
        float  phi     = 2.0f * PI * xi.x;
        float  cos_th  = lerp(sun_cos_max, 1.0f, xi.y);
        float  sin_th  = sqrt(max(0.0f, 1.0f - cos_th * cos_th));
        float3 local   = float3(cos(phi) * sin_th, sin(phi) * sin_th, cos_th);
        float3 sampled = local_to_world(local, sun_dir);

        if (dot(sampled, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        // sun cone, no continuation past rc, rc_L_post stays zero so the unified shift
        // formula collapses to brdf_at_dst * rc_L_nee at evaluation time
        s.flags      |= PATH_FLAG_SKY | PATH_FLAG_NEE;
        s.rc_pos      = sampled;
        s.rc_normal   = -sampled;
        s.rc_L_nee    = light.color.rgb * light.intensity;
        s.rc_L_post   = float3(0, 0, 0);
        source_pdf    = pick_pdf * sun_cone_pdf;
        return s;
    }

    if (is_area && light.area_width > 0.0f && light.area_height > 0.0f)
    {
        // urena 2013 solid-angle sampling, lower variance than area-domain sampling at
        // oblique angles where most of the rectangle's area projects to a sliver of solid
        // angle, the source pdf is in solid angle directly (no jacobian conversion needed)
        float3 light_normal = light.direction;
        float3 light_right, light_up;
        build_orthonormal_basis_fast(light_normal, light_right, light_up);

        float3 ex          = light_right * light.area_width;
        float3 ey          = light_up    * light.area_height;
        float3 rect_origin = light.position - 0.5f * ex - 0.5f * ey;

        float2 xi = random_float2(seed);
        float3 sampled_pos;
        float  solid_angle;
        sample_spherical_rectangle(primary_pos, rect_origin, ex, ey, xi, sampled_pos, solid_angle);

        if (solid_angle < MIN_AREA_LIGHT_SOLID_ANGLE)
            return s;

        float3 to   = sampled_pos - primary_pos;
        float  dist = length(to);
        if (dist < 1e-3f)
            return s;

        float3 dir       = to / dist;
        float  cos_light = dot(-dir, light_normal);
        if (cos_light <= 0.0f || dot(dir, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        // area light, rc is the light surface treated as a non-reflecting emitter, no
        // continuation past rc so rc_L_post stays zero and the rc material params are
        // unused at shift time
        s.rc_pos      = sampled_pos;
        s.rc_normal   = light_normal;
        s.rc_L_nee    = light.color.rgb * light.intensity;
        s.rc_L_post   = float3(0, 0, 0);
        s.flags      |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;

        float sa_pdf = 1.0f / solid_angle;
        source_pdf   = pick_pdf * sa_pdf;
        return s;
    }

    bool is_point = (light.flags & (1u << 1)) != 0;
    bool is_spot  = (light.flags & (1u << 2)) != 0;
    if (is_point || is_spot)
    {
        // dirac local light, rc is the light position treated as a point emitter, distance
        // attenuation and spot cone are folded into rc_L_nee so the shift-time evaluation
        // collapses to brdf_dst * rc_L_nee like the area-light branch, the BRDF strategy
        // cannot produce paths that land on a point/spot light (they have zero volume in
        // the bvh) so the MIS denominator is single-strategy and we use a unit solid-angle
        // pdf to keep ris weights in the same scale as area light candidates (the pdf
        // really is a dirac, but a single-strategy weight target/pdf does not care about
        // the absolute scale of pdf as long as it is consistent across same-strategy samples)
        float3 to        = light.position - primary_pos;
        float  light_dist = length(to);
        if (light_dist < 1e-3f)
            return s;

        float3 dir = to / light_dist;
        if (dot(dir, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        float range_factor = saturate(1.0f - light_dist / max(light.range, 0.01f));
        float attenuation  = range_factor * range_factor / max(light_dist * light_dist, 0.01f);

        if (is_spot)
        {
            float cos_angle = dot(-dir, light.direction);
            float cos_outer = cos(light.angle);
            float cos_inner = cos(light.angle * 0.8f);
            float spot      = saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 1e-4f));
            attenuation    *= spot;
            if (attenuation <= 0.0f)
                return s;
        }

        s.rc_pos      = light.position;
        s.rc_normal   = -dir;
        s.rc_L_nee    = light.color.rgb * light.intensity * attenuation;
        s.rc_L_post   = float3(0, 0, 0);
        s.flags      |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;

        // unit solid-angle pdf keeps dirac candidates on the same numerical scale as the
        // area branch so the ris ordering is stable across mixed light types, the actual
        // dirac density is infinite which would zero out weights through 1/pdf in mis,
        // single-strategy w = target/(n_light * 1) yields the correct estimator for the
        // collapsed dirac case (no brdf strategy contribution to balance against)
        source_pdf = pick_pdf;
        return s;
    }

    return s;
}

// traces a full path from the primary vertex given the first indirect direction; captures
// x2 as the reconnection vertex candidate and accumulates the suffix radiance from x2
// the caller samples dir via sample_brdf so the source pdf matches the primary brdf lobe
PathSample trace_path_from_primary(
    float3 primary_pos,
    float3 primary_normal,
    float primary_roughness,
    float3 dir,
    uint replay_seed,
    inout uint seed)
{
    PathSample s;
    s.rc_pos          = float3(0, 0, 0);
    s.rc_normal       = float3(0, 1, 0);
    s.rc_outgoing_dir = float3(0, 1, 0);
    s.rc_L_post       = float3(0, 0, 0);
    s.rc_L_nee        = float3(0, 0, 0);
    s.rc_albedo       = float3(0, 0, 0);
    s.rc_roughness    = 1.0f;
    s.rc_metallic     = 0.0f;
    s.src_pos         = float3(0, 0, 0);
    s.src_normal      = float3(0, 1, 0);
    s.src_albedo      = float3(0, 0, 0);
    s.src_roughness   = 1.0f;
    s.src_metallic    = 0.0f;
    // store the seed value that was used to generate xi for sample_brdf, so the random replay
    // shift can re-derive the same xi at a destination pixel and trace a matching prefix
    s.seed_path       = replay_seed;
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
        // brdf bounce missed into sky directly from primary, treat as a sky sample with no
        // continuation past rc, store the sky radiance in rc_L_nee so the unified shift
        // formula collapses to brdf_at_dst * rc_L_nee like the area-light nee branch
        s.flags      |= PATH_FLAG_SKY;
        s.rc_pos      = dir;
        s.rc_normal   = -dir;
        s.rc_L_nee    = sample_sky(dir);
        s.rc_L_post   = float3(0, 0, 0);
        s.rc_length   = 0;
        s.path_length = 1;
        return s;
    }

    s.rc_pos       = hit.hit_position;
    s.rc_normal    = hit.geometric_normal;
    s.rc_albedo    = hit.albedo;
    s.rc_roughness = max(hit.roughness, 0.04f);
    s.rc_metallic  = hit.metallic;
    s.rc_length    = 2;

    float3 L_nee, L_post, first_outgoing_dir;
    accumulate_subpath_at_rc(hit, -dir, max(get_restir_max_path_length(), 2u) - 1u, seed, L_nee, L_post, first_outgoing_dir);

    if (any(isnan(L_nee))  || any(isinf(L_nee)))  L_nee  = float3(0, 0, 0);
    if (any(isnan(L_post)) || any(isinf(L_post))) L_post = float3(0, 0, 0);
    L_nee  = max(L_nee,  0.0f);
    L_post = max(L_post, 0.0f);

    // stored radiance is left unmodified at sample construction so the resampler scores the
    // true integrand, per-sample firefly safety is provided downstream via the w clamp at
    // shade time (get_w_clamp_for_sample) and the denoiser's spatial firefly suppression,
    // squashing radiance pre-ris would bias the integrand the resampler is trying to estimate
    s.rc_L_nee        = L_nee;
    s.rc_L_post       = L_post;
    s.rc_outgoing_dir = first_outgoing_dir;
    s.path_length     = 2;

    // reconnection validity: the rc vertex must be rough enough that the lambert-only nee at
    // rc captures most of the energy (the dropped ggx specular lobe at rc is the remaining
    // bias source, bounded by the roughness floor), the suffix view-dep is factored out via
    // rc_L_post + rc_outgoing_dir + rc material, distance must be large enough to keep the
    // solid-angle jacobian well-conditioned
    float rc_min_roughness = get_restir_rc_min_roughness();
    float dist_sq          = dot(hit.hit_position - primary_pos, hit.hit_position - primary_pos);
    bool rc_valid          = (hit.roughness >= rc_min_roughness)
                          && (dist_sq       >= RESTIR_RC_MIN_DISTANCE * RESTIR_RC_MIN_DISTANCE);
    if (rc_valid)
        s.flags |= PATH_FLAG_HAS_RC;

    if (primary_roughness < rc_min_roughness)
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
        float4 t0, t1, t2, t3, t4, t5;
        pack_reservoir(empty, t0, t1, t2, t3, t4, t5);
        tex_reservoir0[launch_id] = t0;
        tex_reservoir1[launch_id] = t1;
        tex_reservoir2[launch_id] = t2;
        tex_reservoir3[launch_id] = t3;
        tex_reservoir4[launch_id] = t4;
        tex_reservoir5[launch_id] = t5;
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

    // ris streaming with gris-style balance mis over a mixed pool of brdf and nee strategies
    // each per-sample weight follows lin 2022 algorithm 1: w_i = m_i * target / source where
    // m_i is the multiple importance sampling balance heuristic over the n_brdf brdf samples
    // and n_light nee samples that could have produced this path, see derivation note below
    //
    // for a sample y from strategy s with N_s samples per strategy:
    //   m_s(y)   = N_s * p_s(y) / sum_t(N_t * p_t(y))
    //   w_i     = m_s * target / p_s = target / sum_t(N_t * p_t(y))
    // so for the typical case where p_other(y) = 0 (brdf rays missing any direct light, or nee
    // sampling a delta light that brdf would hit with zero density) we get the standard
    // (target / source_pdf) / N_s weight, but for an area light direction sampled by both nee
    // and brdf the balance heuristic suppresses the larger-variance strategy automatically
    const uint  n_brdf_count  = clamp(get_restir_initial_candidates(), 1u, INITIAL_CANDIDATE_SAMPLES_MAX);
    const uint  n_light_count = clamp(get_restir_light_candidates(),   1u, LIGHT_RIS_CANDIDATE_SAMPLES_MAX);
    // emtri strategy only adds candidates when the cpu has populated the nee pool, the
    // is_emtri_pool_active gate keeps non emissive scenes free of the extra loop entirely
    const uint  n_emtri_count = is_emtri_pool_active() ? clamp(get_restir_emtri_candidates(), 1u, LIGHT_RIS_CANDIDATE_SAMPLES_MAX) : 0u;
    const float n_brdf        = float(n_brdf_count);
    const float n_light       = float(n_light_count);
    const float n_emtri       = float(n_emtri_count);

    for (uint i = 0; i < n_brdf_count; i++)
    {
        // capture the seed just before consuming xi so the random replay shift can replay
        // the same primary-direction sample at a destination pixel
        // primary specular is owned by the ray traced reflections pipeline when enabled, so
        // restir samples only the diffuse lobe at the primary to avoid double counting
        uint   replay_seed = seed;
        float2 xi          = random_float2(seed);
        float  source_pdf;
        float3 dir         = sample_brdf(albedo, roughness, metallic, normal_ws, view_dir, xi, source_pdf, restir_primary_specular_blend(roughness));

        bool dir_valid = (source_pdf >= RESTIR_MIN_PDF) &&
                         (dot(dir, normal_ws) >= MIN_COS_AT_PRIMARY) &&
                         !any(isnan(dir));

        PathSample candidate = (PathSample)0;
        float weight = 0.0f;

        if (dir_valid)
        {
            candidate = trace_path_from_primary(pos_ws, normal_ws, roughness, dir, replay_seed, seed);
            float target_pdf = target_pdf_self(candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                // proper balance heuristic, the brdf strategy density is n_brdf * source_pdf
                // and the nee strategy density at this same direction is n_light * nee_pdf
                // when the brdf bounce lands on a sampleable light (area light or sun cone) the
                // nee branch lights up and the balance heuristic suppresses the brdf branch's
                // share, fixing the previous "p_light(y) approx 0" upward bias on bright lights
                float nee_pdf = light_nee_pdf_for_candidate(candidate, pos_ws);
                float mix_pdf = n_brdf * source_pdf + n_light * nee_pdf;
                if (mix_pdf > 0.0f)
                {
                    weight = target_pdf / mix_pdf;
                }
            }
        }

        update_reservoir(reservoir, candidate, weight, random_float(seed));
    }

    // additional ris stream over direct light samples (sun cone + area lights); each candidate
    // is a single primary->light path with rc_radiance = emitted radiance and source_pdf in
    // solid-angle measure at the primary
    for (uint li = 0; li < n_light_count; li++)
    {
        float light_source_pdf;
        PathSample light_candidate = sample_light_candidate(pos_ws, normal_ws, seed, light_source_pdf);

        float light_weight = 0.0f;
        if (light_source_pdf >= RESTIR_MIN_PDF)
        {
            float target_pdf = target_pdf_self(light_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                if (is_sky_sample(light_candidate))
                {
                    // sun cone, the brdf-into-sky branch in accumulate_subpath_radiance can also
                    // sample directions inside the cone, so balance with the brdf strategy density
                    // at this direction, both densities are now in solid angle so the balance
                    // heuristic is unbiased
                    float  brdf_pdf_at_sun;
                    evaluate_brdf(albedo, roughness, metallic, normal_ws, view_dir, light_candidate.rc_pos, brdf_pdf_at_sun, restir_primary_specular_blend(roughness));
                    float mix_pdf = n_light * light_source_pdf + n_brdf * brdf_pdf_at_sun;
                    if (mix_pdf > 0.0f)
                        light_weight = target_pdf / mix_pdf;
                }
                else
                {
                    // area light, not in the bvh so brdf cannot produce paths landing on the
                    // area light rectangle, the brdf strategy density in path space at this
                    // candidate is zero, single-strategy weight is the unbiased mis weight
                    light_weight = target_pdf / (n_light * light_source_pdf);
                }
            }
        }

        update_reservoir(reservoir, light_candidate, light_weight, random_float(seed));
    }

    // emissive triangle nee strategy, paper aligned area sampling of the global emissive
    // pool, the candidate's rc lands on a sampled triangle and rc_L_nee carries the emitted
    // radiance, MIS denominator uses n_emtri * source_pdf + n_brdf * brdf_pdf_at_emtri_dir,
    // the brdf pdf entry captures the case where a brdf bounce also lands on this triangle
    // direction so the balance heuristic suppresses the brdf branch's share automatically
    // and avoids over counting bright emitters that brdf would otherwise hit by chance
    for (uint ei = 0; ei < n_emtri_count; ei++)
    {
        float emtri_source_pdf;
        PathSample emtri_candidate = sample_emissive_tri_candidate(pos_ws, normal_ws, seed, emtri_source_pdf);

        float emtri_weight = 0.0f;
        if (emtri_source_pdf >= RESTIR_MIN_PDF)
        {
            float target_pdf = target_pdf_self(emtri_candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                float3 to_emtri    = emtri_candidate.rc_pos - pos_ws;
                float3 emtri_dir   = normalize(to_emtri);
                float  brdf_pdf_at = 0.0f;
                evaluate_brdf(albedo, roughness, metallic, normal_ws, view_dir, emtri_dir, brdf_pdf_at, restir_primary_specular_blend(roughness));
                float mix_pdf = n_emtri * emtri_source_pdf + n_brdf * brdf_pdf_at;
                if (mix_pdf > 0.0f)
                {
                    emtri_weight = target_pdf / mix_pdf;
                }
            }
        }

        update_reservoir(reservoir, emtri_candidate, emtri_weight, random_float(seed));
    }

    // no 1/M divide here, the balance mis weights above already encode the 1/N_s normalization,
    // so weight_sum is the unbiased integral estimate up to the final divide by target_pdf below

    float final_target = target_pdf_self(reservoir.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    reservoir.target_pdf = final_target;
    reservoir.W = (final_target > 0.0f) ? (reservoir.weight_sum / final_target) : 0.0f;

    // post-ris visibility test: kill the chosen sample if it is occluded so an obviously dead
    // path does not propagate into temporal accumulation, M and the chosen sample are also
    // cleared so the temporal validity gate sees no sample on this pixel rather than ratcheting
    // confidence up on the rejected entry
    bool visibility_rejected = false;
    if (reservoir.W > 0.0f && !trace_shift_visibility(reservoir.sample, pos_ws, normal_ws))
    {
        reservoir            = create_empty_reservoir();
        final_target         = 0.0f;
        visibility_rejected  = true;
    }

    // soft saturator, see soft_clamp_w in restir_reservoir.hlsl, the previous hard min
    // produced a step at the clamp boundary which translated to flickery brightness on
    // pixels whose natural W bounced around the threshold across frames
    float w_clamp = get_w_clamp_for_sample(reservoir.sample);
    reservoir.W   = soft_clamp_w(reservoir.W, w_clamp);

    float total_candidates     = n_brdf + n_light;
    float sample_count_quality = saturate(reservoir.M / max(total_candidates, 1.0f));
    reservoir.confidence       = (final_target > 0.0f && !visibility_rejected) ? sample_count_quality : 0.0f;
    reservoir.age              = 0.0f;

    // stamp the source primary g-buffer onto the chosen sample, all candidates from this pixel
    // share the same primary surface so we only need to write it once after ris finalization
    // downstream passes read these instead of sampling the current g-buffer at a reprojected
    // pixel, which fixes ghosting on moving objects
    reservoir.sample.src_pos       = pos_ws;
    reservoir.sample.src_normal    = normal_ws;
    reservoir.sample.src_albedo    = albedo;
    reservoir.sample.src_roughness = roughness;
    reservoir.sample.src_metallic  = metallic;

    float4 t0, t1, t2, t3, t4, t5;
    pack_reservoir(reservoir, t0, t1, t2, t3, t4, t5);
    tex_reservoir0[launch_id] = t0;
    tex_reservoir1[launch_id] = t1;
    tex_reservoir2[launch_id] = t2;
    tex_reservoir3[launch_id] = t3;
    tex_reservoir4[launch_id] = t4;
    tex_reservoir5[launch_id] = t5;

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

    float metallic = mat.metalness;
    if (mat.has_texture_metalness())
    {
        uint metalness_texture_index = material_index + material_texture_index_metalness;
        metallic *= material_textures[metalness_texture_index].SampleLevel(
            GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).r;
    }

    float3x3 obj_to_world_3x3 = (float3x3)ObjectToWorld4x3();
    float3 edge1_world   = mul(pv1.position - pv0.position, obj_to_world_3x3);
    float3 edge2_world   = mul(pv2.position - pv0.position, obj_to_world_3x3);
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
}

[shader("miss")]
void miss(inout PathPayload payload : SV_RayPayload)
{
    payload.hit      = false;
    payload.emission = float3(0.0f, 0.0f, 0.0f);
}
