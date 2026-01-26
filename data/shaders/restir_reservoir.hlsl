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

// configuration
static const uint RESTIR_MAX_PATH_LENGTH     = 3;
static const uint RESTIR_M_CAP               = 16;
static const uint RESTIR_SPATIAL_SAMPLES     = 6;
static const float RESTIR_SPATIAL_RADIUS     = 16.0f;
static const float RESTIR_DEPTH_THRESHOLD    = 0.05f;
static const float RESTIR_NORMAL_THRESHOLD   = 0.9f;
static const float RESTIR_TEMPORAL_DECAY     = 0.9f;
static const float RESTIR_RAY_NORMAL_OFFSET  = 0.01f;
static const float RESTIR_RAY_T_MIN          = 0.001f;

// path sample data
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

// reservoir for weighted sample storage
struct Reservoir
{
    PathSample sample;
    float      weight_sum;
    float      M;
    float      W;
    float      target_pdf;
};

static const uint PATH_FLAG_SPECULAR = 1 << 0;
static const uint PATH_FLAG_DIFFUSE  = 1 << 1;
static const uint PATH_FLAG_CAUSTIC  = 1 << 2;
static const uint PATH_FLAG_DELTA    = 1 << 3;

// octahedral encoding for compact normal storage
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

// reservoir packing into 5 float4 textures
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
    tex4 = float4(0, 0, 0, 0);
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
    return r;
}

// luminance-based target pdf with reinhard compression
float calculate_target_pdf(float3 radiance)
{
    float lum = dot(radiance, float3(0.299, 0.587, 0.114));
    lum = clamp(lum, 0.0f, 65504.0f);
    
    // use reinhard compression without sqrt to avoid over-weighting dim samples
    // sqrt was causing W to amplify excessively when target_pdf was small
    float compressed = lum / (1.0f + lum * 0.1f);
    return max(compressed, 1e-6f);
}

// weighted reservoir sampling update
bool update_reservoir(inout Reservoir reservoir, PathSample new_sample, float weight, float random_value)
{
    reservoir.weight_sum += weight;
    reservoir.M += 1.0f;

    if (random_value * reservoir.weight_sum < weight)
    {
        reservoir.sample = new_sample;
        return true;
    }
    return false;
}

// merge source reservoir into destination
bool merge_reservoir(inout Reservoir dst, Reservoir src, float target_pdf_at_dst, float random_value)
{
    float weight = target_pdf_at_dst * src.W * src.M;

    dst.weight_sum += weight;
    dst.M += src.M;

    if (random_value * dst.weight_sum < weight)
    {
        dst.sample     = src.sample;
        dst.target_pdf = target_pdf_at_dst;
        return true;
    }
    return false;
}

// compute final reservoir weight
void finalize_reservoir(inout Reservoir reservoir)
{
    float target_pdf = calculate_target_pdf(reservoir.sample.radiance);
    reservoir.target_pdf = target_pdf;

    if (target_pdf > 0 && reservoir.M > 0)
        reservoir.W = reservoir.weight_sum / (target_pdf * reservoir.M);
    else
        reservoir.W = 0;

    // tighter clamp to prevent energy amplification
    reservoir.W = min(reservoir.W, 5.0f);
}

// cap M to prevent unbounded temporal accumulation
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

// pcg hash for random number generation
uint pcg_hash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
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
    return pcg_hash(pixel.x ^ pcg_hash(pixel.y ^ pcg_hash(frame)));
}

// decorrelated seed per pass to avoid sampling artifacts
uint create_seed_for_pass(uint2 pixel, uint frame, uint pass_id)
{
    uint pass_salt = pcg_hash(pass_id * 0x9E3779B9u);
    return pcg_hash(pixel.x ^ pcg_hash(pixel.y ^ pcg_hash(frame ^ pass_salt)));
}

// cosine-weighted hemisphere sampling
float3 sample_cosine_hemisphere(float2 xi, out float pdf)
{
    float phi       = 2.0f * 3.14159265f * xi.x;
    float cos_theta = sqrt(xi.y);
    float sin_theta = sqrt(1.0f - xi.y);

    pdf = cos_theta / 3.14159265f;
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

// ggx importance sampling
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

// frisvad's method for orthonormal basis
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

// surface similarity for neighbor validation
bool surface_similarity_check(float3 pos1, float3 normal1, float depth1, float3 pos2, float3 normal2, float depth2)
{
    if (dot(normal1, normal2) < RESTIR_NORMAL_THRESHOLD)
        return false;

    float depth_ratio = depth1 / max(depth2, 1e-6f);
    if (abs(depth_ratio - 1.0f) > RESTIR_DEPTH_THRESHOLD)
        return false;

    return true;
}

// jacobian for solid angle measure conversion during reuse
float compute_jacobian(float3 sample_pos, float3 original_shading_pos, float3 new_shading_pos, float3 sample_normal)
{
    float3 dir_original = sample_pos - original_shading_pos;
    float3 dir_new      = sample_pos - new_shading_pos;

    float dist_original_sq = dot(dir_original, dir_original);
    float dist_new_sq      = dot(dir_new, dir_new);

    if (dist_original_sq < 1e-6f || dist_new_sq < 1e-6f)
        return 0.0f;

    float dist_original = sqrt(dist_original_sq);
    float dist_new      = sqrt(dist_new_sq);

    dir_original /= dist_original;
    dir_new      /= dist_new;

    float cos_original = abs(dot(sample_normal, -dir_original));
    float cos_new      = abs(dot(sample_normal, -dir_new));

    if (cos_original < 0.1f || cos_new < 0.1f)
        return 0.0f;

    float jacobian = (cos_new * dist_original_sq) / (cos_original * dist_new_sq + 1e-6f);
    
    // tight clamp and smooth falloff to prevent splotchy artifacts
    // jacobians far from 1.0 indicate the sample is being stretched significantly
    float deviation = abs(jacobian - 1.0f);
    float falloff   = 1.0f / (1.0f + deviation * 2.0f);
    
    return clamp(jacobian * falloff, 0.0f, 2.0f);
}

// balance heuristic for mis
float power_heuristic(float pdf_a, float pdf_b)
{
    float a2 = pdf_a * pdf_a;
    float b2 = pdf_b * pdf_b;
    return a2 / max(a2 + b2, 1e-6f);
}

#endif // SPARTAN_RESTIR_RESERVOIR
