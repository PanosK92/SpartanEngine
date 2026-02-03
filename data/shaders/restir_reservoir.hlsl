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

#ifndef SPARTAN_RESTIR_RESERVOIR
#define SPARTAN_RESTIR_RESERVOIR

// core parameters
static const uint RESTIR_MAX_PATH_LENGTH     = 3;
static const uint RESTIR_M_CAP               = 32;
static const uint RESTIR_SPATIAL_SAMPLES     = 8;
static const float RESTIR_SPATIAL_RADIUS     = 16.0f;
static const float RESTIR_DEPTH_THRESHOLD    = 0.05f;
static const float RESTIR_NORMAL_THRESHOLD   = 0.9f;
static const float RESTIR_TEMPORAL_DECAY     = 0.9f;
static const float RESTIR_RAY_NORMAL_OFFSET  = 0.01f;
static const float RESTIR_RAY_T_MIN          = 0.001f;

// sky/environment
static const float RESTIR_SKY_RADIANCE_CLAMP = 5.0f;
static const float RESTIR_SKY_W_CLAMP        = 1.5f;
static const float RESTIR_SKY_DIR_THRESHOLD  = 0.95f;
static const float RESTIR_ENV_SAMPLE_PROB    = 0.3f;
static const float RESTIR_SKY_DISTANCE       = 1e10f;

// visibility/geometry
static const float RESTIR_VIS_COS_FRONT      = 0.1f;
static const float RESTIR_VIS_COS_BACK       = 0.15f;
static const float RESTIR_VIS_MIN_DIST       = 0.02f;
static const float RESTIR_VIS_PLANE_MIN      = 0.001f;

// BRDF thresholds
static const float RESTIR_MIN_ROUGHNESS      = 0.04f;
static const float RESTIR_MIN_PDF            = 1e-6f;
static const float RESTIR_W_CLAMP_DEFAULT    = 1.5f;
static const float RESTIR_SPECULAR_THRESHOLD = 0.2f;

// firefly suppression
static const float RESTIR_SOFT_CLAMP_SKY     = 10.0f;
static const float RESTIR_SOFT_CLAMP_DEFAULT = 12.0f;

struct PathSample
{
    float3 hit_position;
    float3 hit_normal;
    float3 direction;
    float3 radiance;
    uint   path_length;
    uint   flags;
    float  pdf;
};

struct Reservoir
{
    PathSample sample;
    float      weight_sum;
    float      M;
    float      W;
    float      target_pdf;
    float      age;
    float      confidence;
};

static const uint PATH_FLAG_SPECULAR = 1 << 0;
static const uint PATH_FLAG_DIFFUSE  = 1 << 1;
static const uint PATH_FLAG_CAUSTIC  = 1 << 2;
static const uint PATH_FLAG_DELTA    = 1 << 3;
static const uint PATH_FLAG_SKY      = 1 << 4;

float2 octahedral_encode(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.z < 0.0f)
    {
        float2 sign_not_zero = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
        n.xy = (1.0f - abs(n.yx)) * sign_not_zero;
    }
    return n.xy;
}

float3 octahedral_decode(float2 e)
{
    float3 n = float3(e.xy, 1.0f - abs(e.x) - abs(e.y));
    if (n.z < 0.0f)
    {
        float2 sign_not_zero = float2(n.x >= 0.0f ? 1.0f : -1.0f, n.y >= 0.0f ? 1.0f : -1.0f);
        n.xy = (1.0f - abs(n.yx)) * sign_not_zero;
    }
    return normalize(n);
}

uint pack_path_info(uint path_length, uint flags)
{
    return (path_length & 0xFFFF) | ((flags & 0xFFFF) << 16);
}

void unpack_path_info(uint packed, out uint path_length, out uint flags)
{
    path_length = packed & 0xFFFF;
    flags = (packed >> 16) & 0xFFFF;
}

void pack_reservoir(Reservoir r, out float4 tex0, out float4 tex1, out float4 tex2, out float4 tex3, out float4 tex4)
{
    float2 normal_oct    = octahedral_encode(r.sample.hit_normal);
    float2 direction_oct = octahedral_encode(r.sample.direction);

    tex0 = float4(r.sample.hit_position, normal_oct.x);
    tex1 = float4(normal_oct.y, direction_oct.xy, r.sample.radiance.x);
    tex2 = float4(r.sample.radiance.yz, r.sample.pdf, r.weight_sum);
    tex3 = float4(r.M, r.W, r.target_pdf, asfloat(pack_path_info(r.sample.path_length, r.sample.flags)));
    tex4 = float4(r.age, r.confidence, 0, 0);
}

Reservoir unpack_reservoir(float4 tex0, float4 tex1, float4 tex2, float4 tex3, float4 tex4)
{
    Reservoir r;
    r.sample.hit_position = tex0.xyz;
    r.sample.hit_normal   = octahedral_decode(float2(tex0.w, tex1.x));
    r.sample.direction    = octahedral_decode(tex1.yz);
    r.sample.radiance     = float3(tex1.w, tex2.xy);
    r.sample.pdf          = tex2.z;
    r.weight_sum          = tex2.w;
    r.M                   = tex3.x;
    r.W                   = tex3.y;
    r.target_pdf          = tex3.z;
    r.age                 = tex4.x;
    r.confidence          = tex4.y;

    uint packed_info = asuint(tex3.w);
    unpack_path_info(packed_info, r.sample.path_length, r.sample.flags);

    return r;
}

bool is_reservoir_valid(Reservoir r)
{
    if (any(isnan(r.sample.hit_position)) || any(isinf(r.sample.hit_position)))
        return false;
    if (any(isnan(r.sample.radiance)) || any(isinf(r.sample.radiance)))
        return false;
    if (any(isnan(r.sample.hit_normal)) || any(isinf(r.sample.hit_normal)))
        return false;
    if (isnan(r.W) || isinf(r.W) || r.W < 0)
        return false;
    if (isnan(r.M) || r.M < 0)
        return false;

    float normal_len = length(r.sample.hit_normal);
    if (normal_len < 0.5f || normal_len > 1.5f)
        return false;

    return true;
}

Reservoir create_empty_reservoir()
{
    Reservoir r;
    r.sample.hit_position = float3(0, 0, 0);
    r.sample.hit_normal   = float3(0, 1, 0);
    r.sample.direction    = float3(0, 0, 1);
    r.sample.radiance     = float3(0, 0, 0);
    r.sample.path_length  = 0;
    r.sample.flags        = 0;
    r.sample.pdf          = 0;
    r.weight_sum          = 0;
    r.M                   = 0;
    r.W                   = 0;
    r.target_pdf          = 0;
    r.age                 = 0;
    r.confidence          = 0;
    return r;
}

float calculate_target_pdf(float3 radiance)
{
    float lum = dot(radiance, float3(0.299, 0.587, 0.114));
    lum = clamp(lum, 0.0f, 65504.0f);
    float compressed = log(1.0f + lum) / (1.0f + log(1.0f + lum) * 0.5f);
    return max(compressed, 1e-6f);
}

float calculate_target_pdf_with_brdf(float3 radiance, float3 sample_dir, float3 shading_normal, float3 view_dir,
                                      float3 albedo, float roughness, float metallic)
{
    float base_target = calculate_target_pdf(radiance);

    float n_dot_l = dot(shading_normal, sample_dir);
    if (n_dot_l <= 0.0f)
        return 0.0f;

    float3 h      = normalize(view_dir + sample_dir);
    float n_dot_v = max(dot(shading_normal, view_dir), 0.001f);
    float n_dot_h = max(dot(shading_normal, h), 0.0f);
    float v_dot_h = max(dot(view_dir, h), 0.0f);

    float3 diffuse_response = albedo * (1.0f - metallic) * n_dot_l;

    float alpha   = max(roughness * roughness, 0.001f);
    float alpha2  = alpha * alpha;
    float d_denom = n_dot_h * n_dot_h * (alpha2 - 1.0f) + 1.0f;
    float d       = alpha2 / (3.14159265f * d_denom * d_denom + 1e-6f);

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 f  = f0 + (1.0f - f0) * pow(1.0f - v_dot_h, 5.0f);

    float spec_weight = d * dot(f, float3(0.299, 0.587, 0.114)) * n_dot_l;
    float diff_weight = dot(diffuse_response, float3(0.299, 0.587, 0.114));

    float brdf_weight = clamp(diff_weight + spec_weight, 0.0f, 10.0f);

    return base_target * max(brdf_weight, 0.01f);
}

float calculate_target_pdf_sky(float3 radiance, float3 sample_dir, float3 shading_normal, float3 view_dir,
                                float3 albedo, float roughness, float metallic)
{
    float base_target = calculate_target_pdf(radiance);

    float n_dot_l = dot(shading_normal, sample_dir);
    if (n_dot_l <= 0.0f)
        return 0.0f;

    float3 h      = normalize(view_dir + sample_dir);
    float n_dot_v = max(dot(shading_normal, view_dir), 0.001f);
    float n_dot_h = max(dot(shading_normal, h), 0.0f);
    float v_dot_h = max(dot(view_dir, h), 0.0f);

    float3 diffuse_response = albedo * (1.0f - metallic) * n_dot_l;

    float alpha   = max(roughness * roughness, 0.001f);
    float alpha2  = alpha * alpha;
    float d_denom = n_dot_h * n_dot_h * (alpha2 - 1.0f) + 1.0f;
    float d       = alpha2 / (3.14159265f * d_denom * d_denom + 1e-6f);

    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 f  = f0 + (1.0f - f0) * pow(1.0f - v_dot_h, 5.0f);

    float spec_weight = d * dot(f, float3(0.299, 0.587, 0.114)) * n_dot_l;
    float diff_weight = dot(diffuse_response, float3(0.299, 0.587, 0.114));

    float brdf_weight = clamp(diff_weight + spec_weight, 0.0f, 10.0f);

    return base_target * max(brdf_weight, 0.01f);
}

float calculate_target_pdf_with_geometry(float3 radiance, float3 shading_pos, float3 shading_normal, float3 view_dir,
                                          float3 sample_hit_pos, float3 sample_hit_normal,
                                          float3 albedo, float roughness, float metallic)
{
    float3 to_sample = sample_hit_pos - shading_pos;
    float dist_sq = dot(to_sample, to_sample);
    if (dist_sq < 1e-6f)
        return 0.0f;

    float dist = sqrt(dist_sq);
    float3 sample_dir = to_sample / dist;

    float brdf_target = calculate_target_pdf_with_brdf(radiance, sample_dir, shading_normal, view_dir,
                                                        albedo, roughness, metallic);
    if (brdf_target <= 0.0f)
        return 0.0f;

    float cos_at_sample = max(dot(sample_hit_normal, -sample_dir), 0.0f);

    // distance-aware minimum prevents geometry term explosion in corners
    float min_dist_sq = max(0.1f, dist * 0.01f);
    float geometry_term = cos_at_sample / max(dist_sq, min_dist_sq);

    float max_geometry = lerp(10.0f, 5.0f, saturate(dist / 50.0f));
    geometry_term = min(geometry_term, max_geometry);

    return brdf_target * max(geometry_term, 0.01f);
}

bool is_sky_sample(PathSample s)
{
    return (s.flags & PATH_FLAG_SKY) != 0;
}

float compute_sky_direction_similarity(float3 dir1, float3 dir2)
{
    float cos_angle = dot(dir1, dir2);
    if (cos_angle < RESTIR_SKY_DIR_THRESHOLD)
        return 0.0f;
    return saturate((cos_angle - RESTIR_SKY_DIR_THRESHOLD) / (1.0f - RESTIR_SKY_DIR_THRESHOLD));
}

float get_w_clamp_for_sample(PathSample s)
{
    if (is_sky_sample(s))
        return RESTIR_SKY_W_CLAMP;
    return RESTIR_W_CLAMP_DEFAULT;
}

bool update_reservoir(inout Reservoir reservoir, PathSample new_sample, float weight, float random_value)
{
    reservoir.weight_sum += weight;
    reservoir.M += 1.0f;

    if (random_value * reservoir.weight_sum < weight)
    {
        reservoir.sample = new_sample;
        reservoir.age    = 0.0f;
        return true;
    }
    return false;
}

bool merge_reservoir(inout Reservoir dst, Reservoir src, float target_pdf_at_dst, float random_value)
{
    float weight = target_pdf_at_dst * src.W * src.M;

    dst.weight_sum += weight;
    dst.M += src.M;

    if (random_value * dst.weight_sum < weight)
    {
        dst.sample     = src.sample;
        dst.target_pdf = target_pdf_at_dst;
        dst.age        = src.age;
        return true;
    }
    return false;
}

void finalize_reservoir(inout Reservoir reservoir)
{
    float target_pdf = calculate_target_pdf(reservoir.sample.radiance);
    reservoir.target_pdf = target_pdf;

    if (target_pdf > 0 && reservoir.M > 0)
        reservoir.W = reservoir.weight_sum / (target_pdf * reservoir.M);
    else
        reservoir.W = 0;

    float w_clamp = get_w_clamp_for_sample(reservoir.sample);
    reservoir.W = min(reservoir.W, w_clamp);
}

void clamp_reservoir_M(inout Reservoir reservoir, float max_M)
{
    if (reservoir.M > max_M)
    {
        float scale = max_M / reservoir.M;
        reservoir.weight_sum *= scale;
        reservoir.M = max_M;

        if (reservoir.target_pdf > 0 && reservoir.M > 0)
            reservoir.W = reservoir.weight_sum / (reservoir.target_pdf * reservoir.M);
    }
}

uint pcg_hash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint xxhash32(uint seed)
{
    const uint PRIME1 = 2654435761u;
    const uint PRIME2 = 2246822519u;
    const uint PRIME3 = 3266489917u;

    uint h = seed + PRIME3;
    h = (h ^ (h >> 15)) * PRIME2;
    h = (h ^ (h >> 13)) * PRIME3;
    return h ^ (h >> 16);
}

float random_float(inout uint seed)
{
    seed = pcg_hash(seed);
    return float(seed) / 4294967295.0f;
}

float2 random_float2(inout uint seed)
{
    return float2(random_float(seed), random_float(seed));
}

float3 random_float3(inout uint seed)
{
    return float3(random_float(seed), random_float(seed), random_float(seed));
}

uint create_seed(uint2 pixel, uint frame)
{
    uint h = xxhash32(pixel.x);
    h = pcg_hash(h ^ xxhash32(pixel.y));
    h = pcg_hash(h ^ xxhash32(frame));
    return h;
}

uint create_seed_for_pass(uint2 pixel, uint frame, uint pass_id)
{
    const uint GOLDEN_RATIO = 0x9E3779B9u;
    const uint PASS_PRIMES[4] = { 0x85EBCA77u, 0xC2B2AE3Du, 0x27D4EB2Fu, 0x165667B1u };

    uint pass_salt = (pass_id < 4) ? PASS_PRIMES[pass_id] : xxhash32(pass_id * GOLDEN_RATIO);

    uint h = xxhash32(pixel.x ^ pass_salt);
    h = pcg_hash(h ^ xxhash32(pixel.y));
    h = pcg_hash(h ^ xxhash32(frame ^ (pass_salt >> 16)));
    return h;
}

float3 sample_cosine_hemisphere(float2 xi, out float pdf)
{
    float phi       = 2.0f * 3.14159265f * xi.x;
    float cos_theta = sqrt(xi.y);
    float sin_theta = sqrt(1.0f - xi.y);

    pdf = cos_theta / 3.14159265f;
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

float3 sample_ggx(float2 xi, float roughness, out float pdf)
{
    float a  = roughness * roughness;
    float a2 = a * a;

    float phi       = 2.0f * 3.14159265f * xi.x;
    float cos_theta = sqrt((1.0f - xi.y) / (1.0f + (a2 - 1.0f) * xi.y));
    float sin_theta = sqrt(1.0f - cos_theta * cos_theta);

    float3 h = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);

    float d = (a2 - 1.0f) * cos_theta * cos_theta + 1.0f;
    pdf = a2 * cos_theta / (3.14159265f * d * d);

    return h;
}

void build_orthonormal_basis_fast(float3 n, out float3 t, out float3 b)
{
    if (n.z < -0.9999999f)
    {
        t = float3(0, -1, 0);
        b = float3(-1, 0, 0);
    }
    else
    {
        float a = 1.0f / (1.0f + n.z);
        float d = -n.x * n.y * a;
        t = float3(1.0f - n.x * n.x * a, d, -n.x);
        b = float3(d, 1.0f - n.y * n.y * a, -n.y);
    }
}

float3 local_to_world(float3 local_dir, float3 n)
{
    float3 t, b;
    build_orthonormal_basis_fast(n, t, b);
    return normalize(t * local_dir.x + b * local_dir.y + n * local_dir.z);
}

bool surface_similarity_check(float3 pos1, float3 normal1, float depth1, float3 pos2, float3 normal2, float depth2)
{
    if (dot(normal1, normal2) < RESTIR_NORMAL_THRESHOLD)
        return false;

    float depth_ratio = depth1 / max(depth2, 1e-6f);
    if (abs(depth_ratio - 1.0f) > RESTIR_DEPTH_THRESHOLD)
        return false;

    return true;
}

float compute_jacobian(float3 sample_pos, float3 original_shading_pos, float3 new_shading_pos, float3 sample_normal, float3 new_receiver_normal)
{
    float3 dir_original = sample_pos - original_shading_pos;
    float3 dir_new      = sample_pos - new_shading_pos;

    float dist_original_sq = dot(dir_original, dir_original);
    float dist_new_sq      = dot(dir_new, dir_new);

    static const float MIN_DIST_SQ = 0.01f;
    if (dist_original_sq < MIN_DIST_SQ || dist_new_sq < MIN_DIST_SQ)
        return 0.0f;

    float dist_original = sqrt(dist_original_sq);
    float dist_new      = sqrt(dist_new_sq);

    dir_original /= dist_original;
    dir_new      /= dist_new;

    float cos_at_receiver = dot(new_receiver_normal, dir_new);
    if (cos_at_receiver < 0.2f)
        return 0.0f;

    float cos_original = dot(sample_normal, -dir_original);
    float cos_new      = dot(sample_normal, -dir_new);

    static const float MIN_COS_ANGLE = 0.15f;
    if (cos_original < MIN_COS_ANGLE || cos_new < MIN_COS_ANGLE)
        return 0.0f;

    // tighten similarity for large distances to prevent artifacts on flat surfaces
    float avg_dist = (dist_original + dist_new) * 0.5f;
    if (avg_dist > 50.0f)
    {
        float direction_similarity = dot(dir_original, dir_new);
        float min_similarity = lerp(0.95f, 0.99f, saturate((avg_dist - 50.0f) / 150.0f));
        if (direction_similarity < min_similarity)
            return 0.0f;
    }

    float jacobian = (cos_new * dist_original_sq) / max(cos_original * dist_new_sq, 1e-4f);

    float dist_ratio   = dist_original / dist_new;
    float base_max     = lerp(2.0f, 4.0f, saturate(dist_ratio));
    float distance_factor = saturate(avg_dist / 100.0f);
    float max_jacobian = lerp(base_max, min(base_max, 1.5f), distance_factor);

    return clamp(jacobian, 0.0f, max_jacobian);
}

float power_heuristic(float pdf_a, float pdf_b)
{
    float a2 = pdf_a * pdf_a;
    float b2 = pdf_b * pdf_b;
    return a2 / max(a2 + b2, 1e-6f);
}

float compute_environment_pdf(float3 direction)
{
    float cos_theta = abs(direction.y);
    return max(cos_theta / PI, RESTIR_MIN_PDF);
}

float3 sample_environment_direction(float2 xi, out float pdf)
{
    float phi       = 2.0f * PI * xi.x;
    float cos_theta = sqrt(xi.y);
    float sin_theta = sqrt(1.0f - xi.y);

    float3 dir = float3(cos(phi) * sin_theta, cos_theta, sin(phi) * sin_theta);

    pdf = max(cos_theta / PI, RESTIR_MIN_PDF);
    return dir;
}

float3 sample_environment_direction_uniform(float2 xi, out float pdf)
{
    float z   = 1.0f - 2.0f * xi.x;
    float r   = sqrt(max(0.0f, 1.0f - z * z));
    float phi = 2.0f * PI * xi.y;

    pdf = 1.0f / (4.0f * PI);
    return float3(r * cos(phi), r * sin(phi), z);
}

float3 clamp_sky_radiance(float3 radiance)
{
    float lum = dot(radiance, float3(0.299f, 0.587f, 0.114f));
    if (lum > RESTIR_SKY_RADIANCE_CLAMP)
        radiance *= RESTIR_SKY_RADIANCE_CLAMP / lum;
    return radiance;
}

float3 soft_clamp_gi(float3 gi, PathSample sample)
{
    if (any(isnan(gi)) || any(isinf(gi)))
        return float3(0.0f, 0.0f, 0.0f);

    float lum = dot(gi, float3(0.299f, 0.587f, 0.114f));

    // soft clamp based on sample type
    float soft_clamp;
    if (is_sky_sample(sample))
    {
        soft_clamp = 4.0f;
    }
    else
    {
        float bounce_factor = saturate(float(sample.path_length - 1) / 2.0f);
        soft_clamp = lerp(3.0f, 1.5f, bounce_factor);
    }

    if (lum > soft_clamp)
    {
        float excess = lum - soft_clamp;
        float scale  = soft_clamp + excess / (1.0f + excess / soft_clamp);
        gi *= scale / lum;
    }

    // hard clamp
    float max_lum = 5.0f;
    float final_lum = dot(gi, float3(0.299f, 0.587f, 0.114f));
    if (final_lum > max_lum)
        gi *= max_lum / final_lum;

    return gi;
}

#endif // SPARTAN_RESTIR_RESERVOIR
