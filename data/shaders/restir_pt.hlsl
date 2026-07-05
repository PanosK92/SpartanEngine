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

// upper bounds on the per pixel ris pool sizes, live counts come from the get_restir_* helpers
// rr and sun/sky sampling constants and helpers live in restir_reservoir.hlsl, shared with the
// replay shift so the initial trace and the replay evaluate the same integrand
static const uint  INITIAL_CANDIDATE_SAMPLES_MAX   = 8;
static const uint  LIGHT_RIS_CANDIDATE_SAMPLES_MAX = 64;
static const float MIN_COS_AT_PRIMARY              = 1e-3f;

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

// power proportional light pick weight, lin 2022 6.1, all four light types are eligible
float light_pick_weight(LightParameters l)
{
    if (l.intensity <= 0.0f)
        return 0.0f;

    bool is_directional = (l.flags & (1u << 0)) != 0;
    bool is_point       = (l.flags & (1u << 1)) != 0;
    bool is_spot        = (l.flags & (1u << 2)) != 0;
    bool is_area        = (l.flags & (1u << 6)) != 0;

    float lum = max(luminance(l.color.rgb), 1e-3f);
    if (is_directional)
    {
        return l.intensity * lum;
    }
    if (is_area)
    {
        float emitter_area = max(l.area_width * l.area_height, 0.0001f);
        return l.intensity * lum * emitter_area;
    }
    if (is_point || is_spot)
    {
        // dirac local light, weight by intensity, luminance and cone fraction
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

// total nee pick weight, the loop is o(light_count)
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

// importance pick pdf for a known light index, used by the brdf strategy mis denominator
float light_pick_pdf_for_index(uint light_idx, float total_weight)
{
    if (total_weight <= 0.0f)
        return 0.0f;
    return light_pick_weight(light_parameters[light_idx]) / total_weight;
}

// binary search over the cdf, returns the triangle whose cumulative weight first exceeds u
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

// samples a candidate whose rc sits on an area sampled point of an emissive triangle
// returns the rc position, emitted radiance and solid angle pdf at the primary vertex
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

    // uniform barycentric on the triangle
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

    // emitter is single sided, geometry behind the normal does not emit
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

// sky_nee_pdf_at, direct_lighting_at_vertex and sample_sky live in restir_reservoir.hlsl,
// shared with the replay shift so both evaluate the same integrand with the same rng draws

// traces the suffix past rc with the rc bsdf factored out, lin 2022 5
// throughput starts at 1/pdf_at_rc so the caller can re-multiply by f_rc at shift time
// out_first_dir is the direction leaving rc into the suffix
void trace_rc_suffix(
    PathPayload rc,
    float3 rc_view_dir,
    uint max_bounces_remaining,
    inout uint seed,
    out float3 out_L_post,
    out float3 out_first_dir,
    out float out_first_pdf)
{
    out_L_post    = float3(0, 0, 0);
    out_first_dir = float3(0, 0, 0);
    out_first_pdf = 0.0f;

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
        if (bounce >= RESTIR_RR_START)
        {
            // constant probability so the decision only depends on the shared seed draw,
            // identical under replay at any destination, lin 2026 6.2.4
            if (random_float(seed) > RESTIR_RR_CONTINUATION)
                break;
            throughput /= RESTIR_RR_CONTINUATION;
        }

        float2 xi = random_float2(seed);
        float  pdf;
        float3 nd = sample_brdf(cur.albedo, cur.roughness, cur.metallic, cur.hit_normal, view_dir, xi, pdf, 1.0f);

        if (pdf < RESTIR_MIN_PDF || dot(nd, cur.hit_normal) <= 0.0f || any(isnan(nd)))
            break;

        if (first_iter)
        {
            // factor out f_rc at rc, throughput becomes 1/pdf for re-multiply at shift time
            out_first_dir = nd;
            out_first_pdf = pdf;
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

        // emissive triangle hit via brdf bounce, mis against the env probe at the previous
        // vertex which can also reach this emitter through its cosine hemisphere
        float w_emissive = power_heuristic(prev_brdf_pdf, sky_nee_pdf_at(nd, prev_normal));
        out_L_post += throughput * next.emission * w_emissive;
        // suffix vertices past rc use the full brdf
        out_L_post += throughput * direct_lighting_at_vertex(
            next.hit_position, next.hit_normal, next.geometric_normal,
            -nd, next.albedo, next.roughness, next.metallic, 1.0f, seed);

        cur      = next;
        view_dir = -nd;
    }
}

// gathers the rc nee + emission (lambert only, view independent) and the suffix radiance past rc
void accumulate_subpath_at_rc(
    PathPayload rc,
    float3 rc_view_dir,
    uint max_bounces,
    inout uint seed,
    out float3 out_L_nee,
    out float3 out_L_post,
    out float3 out_first_dir,
    out float out_first_pdf)
{
    // emtri strategy carries emission when active, zero here to avoid double counting
    out_L_nee = is_emtri_pool_active() ? float3(0, 0, 0) : rc.emission;
    // diffuse only brdf at rc keeps the stored nee view independent for reuse at any dst
    out_L_nee += direct_lighting_at_vertex(
        rc.hit_position, rc.hit_normal, rc.geometric_normal,
        rc_view_dir, rc.albedo, rc.roughness, rc.metallic, 0.0f, seed);

    out_L_post    = float3(0, 0, 0);
    out_first_dir = float3(0, 0, 0);
    out_first_pdf = 0.0f;

    if (max_bounces < 2)
        return;

    trace_rc_suffix(rc, rc_view_dir, max_bounces - 1, seed, out_L_post, out_first_dir, out_first_pdf);
}

// builds a candidate by directly sampling an analytical light or the sun cone
// rc is the sampled light point, source_pdf is in solid angle at the primary
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

    // power proportional pick, lin 2022 6.1, consistent with light_pick_pdf_for_index
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
        // continuous sun cone, sole owner of the sun since sky reads exclude the disc
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

        // no continuation past rc, rc_L_post stays zero
        // directional intensity is irradiance, scaling by 1/solid_angle turns it into cone radiance so the estimator integrates back to the authored energy, matches the delta sun in direct_lighting_at_vertex
        s.flags      |= PATH_FLAG_SKY | PATH_FLAG_NEE;
        s.rc_pos      = sampled;
        s.rc_normal   = -sampled;
        s.rc_L_nee    = light.color.rgb * light.intensity * sun_cone_pdf;
        s.rc_L_post   = float3(0, 0, 0);
        source_pdf    = pick_pdf * sun_cone_pdf;
        return s;
    }

    if (is_area && light.area_width > 0.0f && light.area_height > 0.0f)
    {
        // urena 2013 solid angle sampling, source pdf is in solid angle directly
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

        // area light, rc is the emitter surface, no continuation past rc
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
        // dirac local light, attenuation and spot cone folded into rc_L_nee, single strategy mis
        float3 to        = light.position - primary_pos;
        float  light_dist = length(to);
        if (light_dist < 1e-3f)
            return s;

        float3 dir = to / light_dist;
        if (dot(dir, primary_normal) <= MIN_COS_AT_PRIMARY)
            return s;

        if (light.range <= 0.0f || light_dist >= light.range)
        {
            return s;
        }

        float attenuation = 1.0f / (light_dist * light_dist + 0.0001f);

        if (is_spot)
        {
            float cos_angle = dot(-dir, light.direction);
            float cos_outer = cos(light.angle);
            float cos_inner = cos(light.angle * 0.9f);
            float spot      = saturate((cos_angle - cos_outer) / max(cos_inner - cos_outer, 1e-4f));
            attenuation    *= spot * spot;
            if (attenuation <= 0.0f)
            {
                return s;
            }
        }

        s.rc_pos      = light.position;
        s.rc_normal   = -dir;
        s.rc_L_nee    = light.color.rgb * light.intensity * attenuation;
        s.rc_L_post   = float3(0, 0, 0);
        s.flags      |= PATH_FLAG_HAS_RC | PATH_FLAG_NEE;

        // unit solid angle pdf keeps dirac candidates on the same scale as the area branch
        source_pdf = pick_pdf;
        return s;
    }

    return s;
}

// traces a path from the primary, captures the reconnection vertex and the suffix radiance
PathSample trace_path_from_primary(
    float3 primary_pos,
    float3 primary_normal,
    float primary_roughness,
    float3 dir,
    float dir_pdf,
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
    // store the seed used for xi so the random replay shift can re-derive the same prefix
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
        // brdf bounce missed into sky, store sky radiance in rc_L_nee
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
    float  first_pdf;
    accumulate_subpath_at_rc(hit, -dir, max(get_restir_max_path_length(), 2u) - 1u, seed, L_nee, L_post, first_outgoing_dir, first_pdf);

    if (any(isnan(L_nee))  || any(isinf(L_nee)))  L_nee  = float3(0, 0, 0);
    if (any(isnan(L_post)) || any(isinf(L_post))) L_post = float3(0, 0, 0);
    L_nee  = max(L_nee,  0.0f);
    L_post = max(L_post, 0.0f);

    // stored radiance is left unmodified so the resampler scores the true integrand
    s.rc_L_nee        = L_nee;
    s.rc_L_post       = L_post;
    s.rc_outgoing_dir = first_outgoing_dir;
    s.path_length     = 2;

    // scene independent reconnection criteria, lin 2026 4
    // dual ray footprint thresholds bound the area density change at rc and the angular density
    // change of the rc outgoing lobe, plus a single vertex roughness gate at the vertex before rc
    float rc_min_roughness    = get_restir_rc_min_roughness();
    float dist_sq             = dot(hit.hit_position - primary_pos, hit.hit_position - primary_pos);
    float footprint_threshold = RESTIR_RC_FOOTPRINT_C * restir_primary_footprint_sq(primary_pos, primary_normal);

    // forward footprint, reciprocal area density of rc when traced from the primary
    float cos_at_rc    = abs(dot(hit.geometric_normal, dir));
    float fp_forward   = dist_sq / max(dir_pdf * cos_at_rc, 1e-6f);

    // inverse footprint, reciprocal area density of the primary when traced back from rc,
    // skipped for terminal paths where reconnection cannot change the outgoing density
    float fp_inverse = 1e30f;
    if (first_pdf > RESTIR_MIN_PDF)
    {
        float cos_at_primary = abs(dot(primary_normal, dir));
        fp_inverse           = dist_sq / max(first_pdf * cos_at_primary, 1e-6f);
    }

    bool rc_valid = (primary_roughness >= rc_min_roughness)
                 && (min(fp_forward, fp_inverse) >= footprint_threshold)
                 && (dist_sq >= RESTIR_RC_MIN_DISTANCE * RESTIR_RC_MIN_DISTANCE);
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

    // ris streaming with gris balance mis over a mixed pool of brdf and nee strategies
    // per sample weight is lin 2022 algorithm 1, w_i = target / sum_t(N_t * p_t(y))
    const uint  n_brdf_count  = clamp(get_restir_initial_candidates(), 1u, INITIAL_CANDIDATE_SAMPLES_MAX);
    const uint  n_light_count = clamp(get_restir_light_candidates(),   1u, LIGHT_RIS_CANDIDATE_SAMPLES_MAX);
    // emtri strategy only adds candidates when the cpu has populated the nee pool
    const uint  n_emtri_count = is_emtri_pool_active() ? clamp(get_restir_emtri_candidates(), 1u, LIGHT_RIS_CANDIDATE_SAMPLES_MAX) : 0u;
    const float n_brdf        = float(n_brdf_count);
    const float n_light       = float(n_light_count);
    const float n_emtri       = float(n_emtri_count);

    for (uint i = 0; i < n_brdf_count; i++)
    {
        // capture the seed before xi so the replay shift can replay the same primary sample
        // primary specular is owned by rt reflections, restir samples only the diffuse lobe
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
            candidate = trace_path_from_primary(pos_ws, normal_ws, roughness, dir, source_pdf, replay_seed, seed);
            float target_pdf = target_pdf_self(candidate, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
            if (target_pdf > 0.0f)
            {
                // single strategy weight, the sun free sky is only reachable through brdf sampling
                weight = target_pdf / (n_brdf * source_pdf);
            }
        }

        update_reservoir(reservoir, candidate, weight, random_float(seed));
    }

    // additional ris stream over direct light samples, sun cone and area lights
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
                // single strategy weight, analytic lights are not in the bvh and the sun disc
                // is excluded from every sky read so no other strategy reaches this integrand
                light_weight = target_pdf / (n_light * light_source_pdf);
            }
        }

        update_reservoir(reservoir, light_candidate, light_weight, random_float(seed));
    }

    // emissive triangle nee strategy, area sampling of the global emissive pool
    // single strategy weight, while the pool is active brdf paths zero their rc emission in
    // accumulate_subpath_at_rc so emtri is the only technique carrying this contribution,
    // mixing in the brdf density here would shrink the weights and lose emissive energy
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
                emtri_weight = target_pdf / (n_emtri * emtri_source_pdf);
            }
        }

        update_reservoir(reservoir, emtri_candidate, emtri_weight, random_float(seed));
    }

    // no 1/M divide, the balance mis weights already encode the 1/N_s normalization
    float final_target = target_pdf_self(reservoir.sample, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    reservoir.target_pdf = final_target;
    reservoir.W = (final_target > 0.0f) ? (reservoir.weight_sum / final_target) : 0.0f;

    // post ris visibility test, kill an occluded chosen sample before temporal accumulation
    bool visibility_rejected = false;
    if (reservoir.W > 0.0f && !trace_shift_visibility(reservoir.sample, pos_ws, normal_ws))
    {
        reservoir            = create_empty_reservoir();
        final_target         = 0.0f;
        visibility_rejected  = true;
    }

    // soft saturator, see soft_clamp_w in restir_reservoir.hlsl
    float w_clamp = get_w_clamp_for_sample(reservoir.sample);
    reservoir.W   = soft_clamp_w(reservoir.W, w_clamp);

    float total_candidates     = n_brdf + n_light + n_emtri;
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
    float3 hit_position = WorldRayOrigin() + WorldRayDirection() * RayTCurrent();
    if (mat.is_terrain())
    {
        // terrain maps planar world xz with tiling as repeats per meter, matches the raster path
        texcoord = hit_position.xz;
    }
    else if (geo.uv_world_space > 0.0f)
    {
        texcoord = compute_world_space_uv(hit_position, normal_world);
    }
    texcoord = texcoord * geo.uv_tiling + geo.uv_offset;

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
        if (mat.is_albedo_srgb())
        {
            sampled.rgb = srgb_to_linear(sampled.rgb);
        }
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
        if (mat.is_emissive_srgb())
        {
            emission = srgb_to_linear(emission);
        }
    }
    if (mat.emissive_from_albedo())
    {
        emission += albedo;
    }

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
