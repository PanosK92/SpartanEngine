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

// restir configuration
static const uint RESTIR_MAX_PATH_LENGTH     = 4;    // max bounces for path tracing
static const uint RESTIR_M_CAP               = 20;   // cap on temporal samples to prevent unbounded growth
static const uint RESTIR_SPATIAL_SAMPLES     = 5;    // number of spatial neighbors to sample
static const float RESTIR_SPATIAL_RADIUS     = 30.0f; // radius in pixels for spatial resampling
static const float RESTIR_DEPTH_THRESHOLD    = 0.1f;  // relative depth threshold for neighbor rejection
static const float RESTIR_NORMAL_THRESHOLD   = 0.9f;  // dot product threshold for normal similarity

// path sample - represents a full light transport path
struct PathSample
{
    float3 hit_position;     // primary hit position (for reconnection)
    float3 hit_normal;       // primary hit normal
    float3 direction;        // initial ray direction from camera
    float3 radiance;         // path contribution (L * f / pdf)
    float3 throughput;       // accumulated path throughput
    uint   path_length;      // number of bounces
    uint   flags;            // path flags (caustic, specular, etc)
    float  pdf;              // probability density of this path
};

// reservoir - weighted reservoir sampling container
struct Reservoir
{
    PathSample sample;       // selected sample
    float      weight_sum;   // sum of all sample weights (w_sum)
    float      M;            // number of samples seen
    float      W;            // final unbiased contribution weight
    
    // target pdf for mis - p_hat(sample) based on luminance
    float target_pdf;
};

// pack/unpack reservoir to/from textures for efficient storage
// we use 4 rgba32f textures to store a complete reservoir

// reservoir data layout:
// tex0: hit_position.xyz, hit_normal.x
// tex1: hit_normal.yz, direction.xy
// tex2: direction.z, radiance.xyz
// tex3: throughput.xyz, weight_sum
// tex4: M, W, target_pdf, path_length | flags

void pack_reservoir(Reservoir r, out float4 tex0, out float4 tex1, out float4 tex2, out float4 tex3, out float4 tex4)
{
    tex0 = float4(r.sample.hit_position, r.sample.hit_normal.x);
    tex1 = float4(r.sample.hit_normal.yz, r.sample.direction.xy);
    tex2 = float4(r.sample.direction.z, r.sample.radiance);
    tex3 = float4(r.sample.throughput, r.weight_sum);
    tex4 = float4(r.M, r.W, r.target_pdf, asfloat((r.sample.path_length & 0xFFFF) | ((r.sample.flags & 0xFFFF) << 16)));
}

Reservoir unpack_reservoir(float4 tex0, float4 tex1, float4 tex2, float4 tex3, float4 tex4)
{
    Reservoir r;
    r.sample.hit_position = tex0.xyz;
    r.sample.hit_normal   = float3(tex0.w, tex1.xy);
    r.sample.direction    = float3(tex1.zw, tex2.x);
    r.sample.radiance     = tex2.yzw;
    r.sample.throughput   = tex3.xyz;
    r.sample.pdf          = 1.0f; // reconstructed from weights
    r.weight_sum          = tex3.w;
    r.M                   = tex4.x;
    r.W                   = tex4.y;
    r.target_pdf          = tex4.z;
    
    uint packed = asuint(tex4.w);
    r.sample.path_length = packed & 0xFFFF;
    r.sample.flags       = (packed >> 16) & 0xFFFF;
    
    return r;
}

// initialize empty reservoir
Reservoir create_empty_reservoir()
{
    Reservoir r;
    r.sample.hit_position = float3(0, 0, 0);
    r.sample.hit_normal   = float3(0, 1, 0);
    r.sample.direction    = float3(0, 0, 1);
    r.sample.radiance     = float3(0, 0, 0);
    r.sample.throughput   = float3(1, 1, 1);
    r.sample.path_length  = 0;
    r.sample.flags        = 0;
    r.sample.pdf          = 0;
    r.weight_sum          = 0;
    r.M                   = 0;
    r.W                   = 0;
    r.target_pdf          = 0;
    return r;
}

// calculate target pdf (p_hat) based on luminance
float calculate_target_pdf(float3 radiance)
{
    // luminance-based target function
    return max(dot(radiance, float3(0.299, 0.587, 0.114)), 1e-6f);
}

// update reservoir with a new sample using weighted reservoir sampling (wrs)
// returns true if the new sample was selected
bool update_reservoir(inout Reservoir reservoir, PathSample new_sample, float weight, float random_value)
{
    reservoir.weight_sum += weight;
    reservoir.M += 1.0f;
    
    // probabilistically select this sample
    if (random_value * reservoir.weight_sum < weight)
    {
        reservoir.sample = new_sample;
        return true;
    }
    return false;
}

// combine two reservoirs (for spatial/temporal resampling)
// uses the resampled importance sampling (ris) merge
bool merge_reservoir(inout Reservoir dst, Reservoir src, float target_pdf_at_dst, float random_value)
{
    // calculate mis weight for source reservoir
    float weight = target_pdf_at_dst * src.W * src.M;
    
    dst.weight_sum += weight;
    dst.M += src.M;
    
    // probabilistically select source's sample
    if (random_value * dst.weight_sum < weight)
    {
        dst.sample     = src.sample;
        dst.target_pdf = target_pdf_at_dst;
        return true;
    }
    return false;
}

// finalize reservoir weight after all samples have been added
void finalize_reservoir(inout Reservoir reservoir)
{
    float target_pdf = calculate_target_pdf(reservoir.sample.radiance);
    reservoir.target_pdf = target_pdf;
    
    // W = (1/p_hat) * (1/M) * sum(w_i)
    // this gives us an unbiased estimator
    if (target_pdf > 0 && reservoir.M > 0)
    {
        reservoir.W = reservoir.weight_sum / (target_pdf * reservoir.M);
    }
    else
    {
        reservoir.W = 0;
    }
}

// clamp reservoir M to prevent temporal weight explosion
void clamp_reservoir_M(inout Reservoir reservoir, float max_M)
{
    if (reservoir.M > max_M)
    {
        float scale = max_M / reservoir.M;
        reservoir.weight_sum *= scale;
        reservoir.M = max_M;
    }
}

// pcg hash for high-quality random numbers
uint pcg_hash(uint seed)
{
    uint state = seed * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

// blue noise enhanced random sampling
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

// create seed from pixel position and frame
uint create_seed(uint2 pixel, uint frame)
{
    return pixel.x + pixel.y * 1920 + frame * 1920 * 1080;
}

// cosine-weighted hemisphere sampling with pdf
float3 sample_cosine_hemisphere(float2 xi, out float pdf)
{
    float phi       = 2.0f * 3.14159265f * xi.x;
    float cos_theta = sqrt(xi.y);
    float sin_theta = sqrt(1.0f - xi.y);
    
    pdf = cos_theta / 3.14159265f; // cosine-weighted pdf
    
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
}

// ggx importance sampling for specular
float3 sample_ggx(float2 xi, float roughness, out float pdf)
{
    float a  = roughness * roughness;
    float a2 = a * a;
    
    float phi       = 2.0f * 3.14159265f * xi.x;
    float cos_theta = sqrt((1.0f - xi.y) / (1.0f + (a2 - 1.0f) * xi.y));
    float sin_theta = sqrt(1.0f - cos_theta * cos_theta);
    
    float3 h = float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
    
    // pdf of half-vector
    float d = (a2 - 1.0f) * cos_theta * cos_theta + 1.0f;
    pdf = a2 * cos_theta / (3.14159265f * d * d);
    
    return h;
}

// build orthonormal basis from normal (frisvad's method)
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

// transform from local to world space
float3 local_to_world(float3 local_dir, float3 n)
{
    float3 t, b;
    build_orthonormal_basis_fast(n, t, b);
    return normalize(t * local_dir.x + b * local_dir.y + n * local_dir.z);
}

// check if two surfaces are similar enough for resampling
bool surface_similarity_check(float3 pos1, float3 normal1, float depth1,
                               float3 pos2, float3 normal2, float depth2)
{
    // normal similarity
    float normal_sim = dot(normal1, normal2);
    if (normal_sim < RESTIR_NORMAL_THRESHOLD)
        return false;
    
    // relative depth check
    float depth_ratio = depth1 / max(depth2, 1e-6f);
    if (abs(depth_ratio - 1.0f) > RESTIR_DEPTH_THRESHOLD)
        return false;
    
    return true;
}

// visibility check between two points (shadow ray test)
// this is a placeholder - actual implementation uses TraceRay
bool visibility_check(float3 from, float3 to)
{
    return true; // implemented in main shader with actual ray tracing
}

// power heuristic for mis (balance heuristic with beta=2)
float power_heuristic(float pdf_a, float pdf_b)
{
    float a2 = pdf_a * pdf_a;
    float b2 = pdf_b * pdf_b;
    return a2 / max(a2 + b2, 1e-6f);
}

// path flags
static const uint PATH_FLAG_SPECULAR = 1 << 0;
static const uint PATH_FLAG_DIFFUSE  = 1 << 1;
static const uint PATH_FLAG_CAUSTIC  = 1 << 2;
static const uint PATH_FLAG_DELTA    = 1 << 3;

#endif // SPARTAN_RESTIR_RESERVOIR
