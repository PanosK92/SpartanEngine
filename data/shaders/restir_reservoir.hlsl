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
// the runtime knobs (m cap, max path length, light/initial candidates, rc min roughness, w
// clamp, validation period) come from cb_frame.restir_pt_* via the helpers below, the static
// values here are absolute upper bounds for the unrolled loop counts and conservative fallbacks
static const uint  RESTIR_MAX_PATH_LENGTH    = 8;
static const uint  RESTIR_M_CAP              = 32;
static const uint  RESTIR_SPATIAL_SAMPLES    = 8;

// runtime knob accessors, clamped on the cpu and on read so stale frames cannot escape the
// safe range, the casts to uint round toward zero but the cvars only emit integral values
float get_restir_m_cap()               { return max(buffer_frame.restir_pt_m_cap, 1.0f); }
uint  get_restir_max_path_length()     { return clamp((uint)buffer_frame.restir_pt_max_path_length, 2u, RESTIR_MAX_PATH_LENGTH); }
uint  get_restir_light_candidates()    { return max((uint)buffer_frame.restir_pt_light_candidates, 1u); }
uint  get_restir_initial_candidates()  { return max((uint)buffer_frame.restir_pt_initial_candidates, 1u); }
float get_restir_rc_min_roughness()    { return saturate(buffer_frame.restir_pt_rc_min_roughness); }
float get_restir_w_clamp()             { return max(buffer_frame.restir_pt_w_clamp, 1.0f); }
uint  get_restir_validation_period()   { return (uint)max(buffer_frame.restir_pt_validation_period, 0.0f); }
static const float RESTIR_DEPTH_THRESHOLD    = 0.05f;
static const float RESTIR_NORMAL_THRESHOLD   = 0.75f;
// no soft decay, the validity gate already hard rejects on disocclusion and the m cap above
// is now low enough that we adapt within a fraction of a second on lighting changes
static const float RESTIR_TEMPORAL_DECAY     = 1.0f;
static const float RESTIR_RAY_T_MIN          = 0.001f;

// sky / environment
// sun disk radiance can far exceed any sane clamp, so this only catches numerical blowups
static const float RESTIR_SKY_RADIANCE_CLAMP = 200.0f;
static const float RESTIR_SKY_W_CLAMP        = 1500.0f;
static const float RESTIR_SKY_DISTANCE       = 1e10f;

// reconnection criteria (lin 2022 hybrid shift, reconnection leg)
static const float RESTIR_RC_MIN_DISTANCE    = 0.1f;
static const float RESTIR_RC_MIN_ROUGHNESS   = 0.2f;
static const float RESTIR_RC_COS_FRONT       = 0.05f;
// specular ownership boundary at the primary vertex, below this roughness rt reflections owns
// the specular lobe (its single bounce is high quality on near mirrors and the random replay
// shift collapses anyway), at or above it restir samples the full brdf so glossy surfaces get
// proper indirect specular through reservoir reuse, this matches the lin 2022 hybrid shift's
// own gate on rough vs near specular vertices
static const float RESTIR_PRIMARY_SPECULAR_OWNED_MIN = 0.2f;
// large jacobians signal a near-degenerate shift, the shift result is rejected as failed
// instead of being clamped so the bias from squashing the energy into a clamp does not
// accumulate over neighbors; this acts as a safety guard rather than a soft firefly cap
static const float RESTIR_JACOBIAN_REJECT    = 64.0f;

// brdf / numerics
static const float RESTIR_MIN_PDF            = 1e-6f;
// generous safety net only, the pairwise mis + visibility checks + m cap already bound energy
// and the hybrid shift gives the estimator the dynamic range it needs for concentrated emitters
// raised from 500 to 1500 since we no longer apply a source side firefly clamp on rc_radiance
// and the variance aware denoiser handles bright single sample fireflies inside the spatial
// filter weights, lowering the W clamp aggressively here would re-introduce darkening bias
static const float RESTIR_W_CLAMP_DEFAULT    = 1500.0f;

// nee
static const float MIN_AREA_LIGHT_SOLID_ANGLE = 1e-4f;

// path flags
static const uint PATH_FLAG_SKY      = 1 << 0;  // rc is the sky dome, rc_pos stores a unit direction
static const uint PATH_FLAG_HAS_RC   = 1 << 1;  // reconnection vertex is valid for reconnection shift
static const uint PATH_FLAG_SPECULAR = 1 << 2;  // diagnostic: primary surface is specular-leaning
static const uint PATH_FLAG_NEE      = 1 << 3;  // candidate came from light nee strategy, used for gris balance mis at the initial ris pool

// a path sample stores the suffix of a path beginning at the current pixel's primary hit
// paper-faithful split of the radiance leaving rc toward the source primary (lin 2022 §5):
//
//   L_at_rc = L_nee + f_rc(in_at_rc, rc_outgoing_dir) * L_post
//
// where L_nee is the direct lighting collected by nee at rc (with the rc bsdf baked in for
// each nee direction, that residual view-dep is bounded by the rc roughness gate at >= 0.2),
// L_post is the radiance arriving at rc from direction rc_outgoing_dir produced by the
// suffix bounces past rc (with the rc bsdf factored out), and rc_outgoing_dir is the
// direction leaving rc into the suffix
//
// at shift time the reconnection shift evaluates f_rc with the destination's incoming
// direction at rc (-dir_dst), so the indirect specular at rc is correctly view-dependent
// and the rc_radiance baked-in bias that used to dominate broken indirect specular is gone
//
// rc_pos is a world-space reconnection vertex (PATH_FLAG_HAS_RC) or a unit sky direction (PATH_FLAG_SKY)
// rc_normal is the rc surface normal (or the negative sky direction for sky samples)
// rc_outgoing_dir is the direction leaving rc into the suffix (unit vector), unused when L_post is zero
// rc_L_post is radiance at rc from direction rc_outgoing_dir, bsdf at rc factored out (zero for sky/area-light samples that have no continuation past rc)
// rc_L_nee is direct lighting at rc (rc bsdf baked in for surface rc, full radiance for sky/area light samples)
// rc_albedo / rc_roughness / rc_metallic are the rc material parameters used to re-evaluate f_rc at shift time
//
// src_pos / src_normal / src_albedo / src_roughness / src_metallic capture the *source* pixel's
// primary surface at the time the reservoir was generated, so temporal and spatial passes can
// evaluate the source's primary brdf and jacobian without sampling the current g-buffer at a
// reprojected pixel (which is wrong for moving objects and is the main cause of motion ghosting)
struct PathSample
{
    float3 rc_pos;
    float3 rc_normal;
    float3 rc_outgoing_dir;
    float3 rc_L_post;
    float3 rc_L_nee;
    float3 rc_albedo;
    float  rc_roughness;
    float  rc_metallic;
    float3 src_pos;
    float3 src_normal;
    float3 src_albedo;
    float  src_roughness;
    float  src_metallic;
    uint   seed_path;
    uint   path_length;
    uint   rc_length;
    uint   flags;
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

uint pack_path_info(uint path_length, uint rc_length, uint flags)
{
    return (path_length & 0xFFu) | ((rc_length & 0xFFu) << 8u) | ((flags & 0xFFFFu) << 16u);
}

void unpack_path_info(uint packed, out uint path_length, out uint rc_length, out uint flags)
{
    path_length = packed & 0xFFu;
    rc_length   = (packed >> 8u) & 0xFFu;
    flags       = (packed >> 16u) & 0xFFFFu;
}

// reservoir texture packing (6 x RGBA32F = 24 floats), the 4th and 5th textures carry the rc
// material params and the source primary g-buffer so temporal and spatial passes can evaluate
// f_rc at the destination's incoming direction (paper-faithful indirect specular, lin 2022 §5)
// and the source brdf / jacobian without sampling the current g-buffer at a reprojected pixel
// (which is wrong for moving objects and is the main cause of motion ghosting)
//
// further compression to 4 RGBA32F (r11g11b10 for hdr radiance, fp16 positions, uint8 albedo)
// is feasible bandwidth-wise but trades hdr precision on rc_L_post / rc_L_nee and world-space
// precision on rc_pos / src_pos for a 33% bandwidth reduction, deferred until profiling shows
// the reservoir read/write loop is actually the bottleneck
//
// tex0.xyz = rc_pos
// tex0.w   = asfloat(pack_f16x2(rc_normal_oct.x, rc_normal_oct.y))
// tex1.xyz = rc_L_post
// tex1.w   = asfloat(pack_f16x2(rc_outgoing_oct.x, rc_outgoing_oct.y))
// tex2.x   = asfloat(seed_path)
// tex2.y   = asfloat(packed: path_length | rc_length | flags)
// tex2.z   = weight_sum
// tex2.w   = M
// tex3.x   = W
// tex3.y   = target_pdf
// tex3.z   = asfloat(pack_f16x2(rc_roughness, confidence))
// tex3.w   = asfloat(pack_uint8x4(rc_albedo.r, rc_albedo.g, rc_albedo.b, rc_metallic))
// tex4.xyz = rc_L_nee
// tex4.w   = asfloat(pack_uint8x4(src_albedo.r, src_albedo.g, src_albedo.b, src_metallic))
// tex5.x   = asfloat(pack_f16x2(src_pos.x, src_pos.y))
// tex5.y   = asfloat(pack_f16x2(src_pos.z, src_normal_oct.x))
// tex5.z   = asfloat(pack_f16x2(src_normal_oct.y, src_roughness))
// tex5.w   = asfloat(pack_f16x2(age, 0.0f))
//
// src_pos is stored as absolute world space in f16, which gives ~1cm precision near origin
// degrading to ~1m at 1 km out, more than enough for the jacobian distance ratios used in the
// shifts, this trades reservoir memory for less ghosting on moving objects
float pack_f16x2_to_float(float a, float b)
{
    uint packed = f32tof16(a) | (f32tof16(b) << 16u);
    return asfloat(packed);
}

float2 unpack_float_to_f16x2(float p)
{
    uint packed = asuint(p);
    return float2(f16tof32(packed & 0xFFFFu), f16tof32(packed >> 16u));
}

float pack_uint8x4_to_float(float r, float g, float b, float a)
{
    uint pr = uint(saturate(r) * 255.0f + 0.5f);
    uint pg = uint(saturate(g) * 255.0f + 0.5f);
    uint pb = uint(saturate(b) * 255.0f + 0.5f);
    uint pa = uint(saturate(a) * 255.0f + 0.5f);
    return asfloat(pr | (pg << 8u) | (pb << 16u) | (pa << 24u));
}

float4 unpack_float_to_uint8x4(float p)
{
    uint packed = asuint(p);
    return float4(
        float((packed      ) & 0xFFu) / 255.0f,
        float((packed >>  8u) & 0xFFu) / 255.0f,
        float((packed >> 16u) & 0xFFu) / 255.0f,
        float((packed >> 24u) & 0xFFu) / 255.0f
    );
}

void pack_reservoir(Reservoir r, out float4 tex0, out float4 tex1, out float4 tex2, out float4 tex3, out float4 tex4, out float4 tex5)
{
    float2 rc_normal_oct  = octahedral_encode(r.sample.rc_normal);
    float2 rc_out_oct     = octahedral_encode(r.sample.rc_outgoing_dir);
    float2 src_normal_oct = octahedral_encode(r.sample.src_normal);

    tex0 = float4(r.sample.rc_pos, pack_f16x2_to_float(rc_normal_oct.x, rc_normal_oct.y));
    tex1 = float4(r.sample.rc_L_post, pack_f16x2_to_float(rc_out_oct.x, rc_out_oct.y));
    tex2 = float4(
        asfloat(r.sample.seed_path),
        asfloat(pack_path_info(r.sample.path_length, r.sample.rc_length, r.sample.flags)),
        r.weight_sum,
        r.M
    );
    tex3 = float4(
        r.W,
        r.target_pdf,
        pack_f16x2_to_float(r.sample.rc_roughness, r.confidence),
        pack_uint8x4_to_float(r.sample.rc_albedo.r, r.sample.rc_albedo.g, r.sample.rc_albedo.b, r.sample.rc_metallic)
    );
    tex4 = float4(
        r.sample.rc_L_nee,
        pack_uint8x4_to_float(r.sample.src_albedo.r, r.sample.src_albedo.g, r.sample.src_albedo.b, r.sample.src_metallic)
    );
    tex5 = float4(
        pack_f16x2_to_float(r.sample.src_pos.x, r.sample.src_pos.y),
        pack_f16x2_to_float(r.sample.src_pos.z, src_normal_oct.x),
        pack_f16x2_to_float(src_normal_oct.y,   r.sample.src_roughness),
        pack_f16x2_to_float(r.age,              0.0f)
    );
}

Reservoir unpack_reservoir(float4 tex0, float4 tex1, float4 tex2, float4 tex3, float4 tex4, float4 tex5)
{
    Reservoir r;

    float2 rc_normal_oct = unpack_float_to_f16x2(tex0.w);
    float2 rc_out_oct    = unpack_float_to_f16x2(tex1.w);

    r.sample.rc_pos          = tex0.xyz;
    r.sample.rc_normal       = octahedral_decode(rc_normal_oct);
    r.sample.rc_L_post       = tex1.xyz;
    r.sample.rc_outgoing_dir = octahedral_decode(rc_out_oct);
    r.sample.seed_path       = asuint(tex2.x);

    uint packed_info = asuint(tex2.y);
    unpack_path_info(packed_info, r.sample.path_length, r.sample.rc_length, r.sample.flags);

    float2 rc_rough_conf = unpack_float_to_f16x2(tex3.z);
    float4 rc_albedo_met = unpack_float_to_uint8x4(tex3.w);

    r.sample.rc_roughness = max(rc_rough_conf.x, 0.04f);
    r.sample.rc_albedo    = rc_albedo_met.rgb;
    r.sample.rc_metallic  = rc_albedo_met.a;
    r.confidence          = saturate(rc_rough_conf.y);

    r.sample.rc_L_nee     = tex4.xyz;
    float4 src_albedo_met = unpack_float_to_uint8x4(tex4.w);
    r.sample.src_albedo   = src_albedo_met.rgb;
    r.sample.src_metallic = src_albedo_met.a;

    float2 pos_xy       = unpack_float_to_f16x2(tex5.x);
    float2 pos_z_norm_x = unpack_float_to_f16x2(tex5.y);
    float2 norm_y_rough = unpack_float_to_f16x2(tex5.z);
    float2 age_pad      = unpack_float_to_f16x2(tex5.w);

    r.sample.src_pos       = float3(pos_xy.x, pos_xy.y, pos_z_norm_x.x);
    r.sample.src_normal    = octahedral_decode(float2(pos_z_norm_x.y, norm_y_rough.x));
    r.sample.src_roughness = max(norm_y_rough.y, 0.04f);

    r.weight_sum = tex2.z;
    r.M          = tex2.w;
    r.W          = tex3.x;
    r.target_pdf = tex3.y;
    r.age        = age_pad.x;

    return r;
}

bool is_sky_sample(PathSample s)     { return (s.flags & PATH_FLAG_SKY)    != 0; }
bool has_reconnection(PathSample s)  { return (s.flags & PATH_FLAG_HAS_RC) != 0; }
bool is_nee_sample(PathSample s)     { return (s.flags & PATH_FLAG_NEE)    != 0; }

bool is_reservoir_valid(Reservoir r)
{
    if (any(isnan(r.sample.rc_pos))    || any(isinf(r.sample.rc_pos)))    return false;
    if (any(isnan(r.sample.rc_L_post)) || any(isinf(r.sample.rc_L_post))) return false;
    if (any(isnan(r.sample.rc_L_nee))  || any(isinf(r.sample.rc_L_nee)))  return false;
    if (any(isnan(r.sample.rc_normal)) || any(isinf(r.sample.rc_normal))) return false;
    if (isnan(r.W) || isinf(r.W) || r.W < 0)                              return false;
    if (isnan(r.M) || r.M < 0)                                            return false;
    if (r.sample.rc_length > RESTIR_MAX_PATH_LENGTH)                      return false;
    return true;
}

Reservoir create_empty_reservoir()
{
    Reservoir r;
    r.sample.rc_pos          = float3(0, 0, 0);
    r.sample.rc_normal       = float3(0, 1, 0);
    r.sample.rc_outgoing_dir = float3(0, 1, 0);
    r.sample.rc_L_post       = float3(0, 0, 0);
    r.sample.rc_L_nee        = float3(0, 0, 0);
    r.sample.rc_albedo       = float3(0, 0, 0);
    r.sample.rc_roughness    = 1.0f;
    r.sample.rc_metallic     = 0.0f;
    r.sample.src_pos         = float3(0, 0, 0);
    r.sample.src_normal      = float3(0, 1, 0);
    r.sample.src_albedo      = float3(0, 0, 0);
    r.sample.src_roughness   = 1.0f;
    r.sample.src_metallic    = 0.0f;
    r.sample.seed_path       = 0;
    r.sample.path_length     = 0;
    r.sample.rc_length       = 0;
    r.sample.flags           = 0;
    r.weight_sum             = 0;
    r.M                      = 0;
    r.W                      = 0;
    r.target_pdf             = 0;
    r.age                    = 0;
    r.confidence             = 0;
    return r;
}

float get_w_clamp_for_sample(PathSample s)
{
    float w = get_restir_w_clamp();
    // sky samples always use a high clamp because the sun disk radiance can far exceed it,
    // we never want to darken sun glints to fight a bias-free clamp
    return is_sky_sample(s) ? max(w, RESTIR_SKY_W_CLAMP) : w;
}

bool update_reservoir(inout Reservoir reservoir, PathSample new_sample, float weight, float random_value)
{
    if (weight <= 0.0f || isnan(weight) || isinf(weight))
    {
        reservoir.M += 1.0f;
        return false;
    }

    reservoir.weight_sum += weight;
    reservoir.M          += 1.0f;

    if (random_value * reservoir.weight_sum < weight)
    {
        reservoir.sample = new_sample;
        reservoir.age    = 0.0f;
        return true;
    }
    return false;
}

// caps M without touching weight_sum or W in the paper-form W = weight_sum / target_pdf
// the cap only affects future m_i = M_i / sum ratios used during stream combining, the
// unbiased estimator is preserved (scaling weight_sum here would darken the estimate by max_M/M)
void clamp_reservoir_M(inout Reservoir reservoir, float max_M)
{
    if (reservoir.M > max_M)
        reservoir.M = max_M;
}

// rng
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

float2 random_float2(inout uint seed) { return float2(random_float(seed), random_float(seed)); }
float3 random_float3(inout uint seed) { return float3(random_float(seed), random_float(seed), random_float(seed)); }

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

// sampling helpers
float3 sample_cosine_hemisphere(float2 xi, out float pdf)
{
    float phi       = 2.0f * PI * xi.x;
    float cos_theta = sqrt(xi.y);
    float sin_theta = sqrt(1.0f - xi.y);

    pdf = cos_theta / PI;
    return float3(cos(phi) * sin_theta, sin(phi) * sin_theta, cos_theta);
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

float3 world_to_local(float3 world_dir, float3 n)
{
    float3 t, b;
    build_orthonormal_basis_fast(n, t, b);
    return float3(dot(world_dir, t), dot(world_dir, b), dot(world_dir, n));
}

// visible normal distribution function sampling (heitz 2018, sampling the ggx distribution of
// visible normals), this concentrates samples around microfacets that face the view direction
// removing the low pdf wasted samples that nf sampling produces at grazing angles, the result
// has strictly lower variance than the legacy ndf sampling for non zero roughness
// ve is the view direction in local tangent space, n = (0,0,1)
// alpha is the ggx alpha at this surface, callers pass restir_d_ggx_alpha(roughness) so the
// sampler matches the engine's brdf model exactly, mismatched alpha leads to a sampler whose
// pdf does not equal the brdf density and inflates variance (especially at glancing angles)
float3 sample_ggx_vndf(float3 ve, float2 xi, float alpha)
{
    float a = max(alpha, 1e-3f);

    float3 vh = normalize(float3(a * ve.x, a * ve.y, ve.z));

    float  lensq = vh.x * vh.x + vh.y * vh.y;
    float3 t1    = (lensq > 0.0f) ? (float3(-vh.y, vh.x, 0.0f) * rsqrt(lensq)) : float3(1.0f, 0.0f, 0.0f);
    float3 t2    = cross(vh, t1);

    float r        = sqrt(xi.x);
    float phi      = 2.0f * PI * xi.y;
    float t1_coeff = r * cos(phi);
    float t2_coeff = r * sin(phi);
    float s        = 0.5f * (1.0f + vh.z);
    t2_coeff       = (1.0f - s) * sqrt(1.0f - t1_coeff * t1_coeff) + s * t2_coeff;

    float3 nh = t1_coeff * t1 + t2_coeff * t2 + sqrt(max(0.0f, 1.0f - t1_coeff * t1_coeff - t2_coeff * t2_coeff)) * vh;

    return normalize(float3(a * nh.x, a * nh.y, max(0.0f, nh.z)));
}

float power_heuristic(float pdf_a, float pdf_b)
{
    float a2 = pdf_a * pdf_a;
    float b2 = pdf_b * pdf_b;
    return a2 / max(a2 + b2, 1e-6f);
}

float3 clamp_sky_radiance(float3 radiance)
{
    float lum = dot(radiance, float3(0.299f, 0.587f, 0.114f));
    if (lum > RESTIR_SKY_RADIANCE_CLAMP)
        radiance *= RESTIR_SKY_RADIANCE_CLAMP / lum;
    return radiance;
}

// ray offset for self intersection avoidance, scaled with the dominant world-space coordinate
// since float precision degrades with absolute magnitude (Wachter & Binder "A Fast and Robust
// Method for Avoiding Self-Intersection" simplified form), the small camera-distance term
// keeps long rays from wobbling at glancing angles, the floor of 2e-4 handles points near the
// origin where the magnitude term collapses to zero
float compute_ray_offset(float3 pos_ws)
{
    float p_mag = max(abs(pos_ws.x), max(abs(pos_ws.y), abs(pos_ws.z)));
    float dist  = length(pos_ws - get_camera_position());
    float ofs   = max(max(p_mag * 1e-4f, dist * 1e-4f), 2e-4f);
    return min(ofs, 1e-2f);
}

float3 soft_saturate_radiance(float3 radiance, float threshold)
{
    float lum = dot(radiance, float3(0.299f, 0.587f, 0.114f));
    if (lum > threshold)
    {
        float scale = threshold + (lum - threshold) / (1.0f + (lum - threshold) / threshold);
        radiance *= scale / lum;
    }
    return radiance;
}

// diffuse-vs-specular selection probability used by the importance-sampled brdf
// derived from approximate lobe energy at this view angle, the specular weight is the schlick
// fresnel reflectance and the diffuse weight is the remaining albedo-tinted energy scaled by
// (1 - metallic), this replaces the previous hand-tuned (0.1, 0.9) lerp with a value that
// tracks actual lobe contribution and stays well-behaved across the full material range
float compute_spec_probability(float3 albedo, float roughness, float metallic, float n_dot_v)
{
    float albedo_lum = max(luminance(albedo), 1e-3f);
    float f0_scalar  = lerp(0.04f, albedo_lum, metallic);
    float fresnel    = f0_scalar + (1.0f - f0_scalar) * pow(1.0f - n_dot_v, 5.0f);
    // smooth surfaces concentrate energy in the lobe and benefit from more specular sampling,
    // rough surfaces spread energy and need more diffuse samples, this nudges the split a bit
    float gloss_bias = lerp(0.85f, 1.15f, 1.0f - roughness);
    float spec_w     = fresnel * gloss_bias;
    float diff_w     = albedo_lum * (1.0f - metallic) * (1.0f - fresnel);
    float total      = spec_w + diff_w;
    float spec_prob  = total > 0.0f ? spec_w / total : 0.5f;
    return clamp(spec_prob, 0.05f, 0.95f);
}

// brdf helpers ported from brdf.hlsl so restir matches the engine's main shading exactly
// (same diffuse_burley + d_ggx with the cod-wwii roughness remap + v_smithggx + fdez-aguera
// multiscatter compensation). these are inline copies, not includes, since brdf.hlsl pulls in
// the Surface/AngularInfo structs which aren't available in the compute / raytrace passes
float restir_pow5(float x)
{
    float x2 = x * x;
    return x2 * x2 * x;
}

float restir_d_ggx_alpha(float roughness)
{
    float gloss = 1.0f - roughness;
    return sqrt(2.0f / (1.0f + pow(2.0f, 18.0f * gloss)));
}

float3 restir_compute_f90(float3 f0)
{
    return saturate(50.0f * dot(f0, 1.0f / 3.0f));
}

float3 restir_f_schlick(float3 f0, float3 f90, float v_dot_h)
{
    return f0 + (f90 - f0) * restir_pow5(1.0f - v_dot_h);
}

float restir_f_schlick_scalar(float f0, float f90, float v_dot_h)
{
    return f0 + (f90 - f0) * restir_pow5(1.0f - v_dot_h);
}

float restir_v_smithggx(float n_dot_v, float n_dot_l, float a2)
{
    float ggxv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0f - a2) + a2);
    float ggxl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0f - a2) + a2);
    return 0.5f / max(ggxv + ggxl, 1e-6f);
}

float restir_d_ggx(float n_dot_h, float a2)
{
    float d = n_dot_h * n_dot_h * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * d * d + 1e-6f);
}

float3 restir_diffuse_burley(float3 diffuse_color, float roughness, float n_dot_v, float n_dot_l, float v_dot_h)
{
    float f90           = 0.5f + 2.0f * v_dot_h * v_dot_h * roughness;
    float light_scatter = restir_f_schlick_scalar(1.0f, f90, n_dot_l);
    float view_scatter  = restir_f_schlick_scalar(1.0f, f90, n_dot_v);
    return diffuse_color * (light_scatter * view_scatter * (1.0f / PI));
}

float3 restir_compute_multiscatter_energy(float3 f0, float n_dot_v, float roughness)
{
    float  dhr        = lerp(1.0f - roughness * 0.7f, 1.0f, restir_pow5(1.0f - n_dot_v));
    float3 energy_loss = (1.0f - dhr) * (1.0f - f0);
    return 1.0f + f0 * energy_loss / max(1.0f - energy_loss, 1e-6f);
}

// full ggx+burley brdf evaluator returning f_r * cos(n,l) and the matching mixture pdf
// matches the engine's main shading model (brdf.hlsl) so restir and direct light shading
// produce consistent results on the same surface material
// when primary_diffuse_only is true, the specular lobe is zeroed and the pdf collapses to the
// cosine-hemisphere pdf, so the primary surface is treated as pure diffuse, primary specular
// is then exclusively owned by the ray traced reflections pipeline to avoid double counting
float3 evaluate_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float3 l, out float pdf, bool primary_diffuse_only)
{
    float3 h_unnorm = v + l;
    float h_len_sq  = dot(h_unnorm, h_unnorm);

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

    if (n_dot_l <= 0.0f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    // fresnel is needed by both diffuse attenuation and the optional specular lobe
    float3 f0     = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 f90    = restir_compute_f90(f0);
    float3 f_term = restir_f_schlick(f0, f90, v_dot_h);

    // burley diffuse, attenuated by fresnel and metallic in the same way light.hlsl does
    float3 diffuse_color = albedo * (1.0f - metallic);
    float3 diffuse       = restir_diffuse_burley(diffuse_color, roughness, n_dot_v, n_dot_l, v_dot_h);
    float3 diffuse_cos   = diffuse * (1.0f - f_term) * n_dot_l;

    if (primary_diffuse_only)
    {
        // diffuse only path, pdf collapses to the cosine hemisphere pdf since sample_brdf
        // also routes to the cosine sampler when called with primary_diffuse_only = true
        pdf = n_dot_l / PI;
        return diffuse_cos;
    }

    // ggx specular with cod-wwii roughness remap and fdez-aguera multiscatter
    // the same alpha is used for the brdf D term, the importance sampling pdf, and the vndf
    // sampler in sample_brdf so the sampler density matches the integrand density exactly,
    // mismatched alpha is the dominant source of glossy gi variance ("fizzy" highlights)
    float  a       = restir_d_ggx_alpha(roughness);
    float  a2      = a * a;
    float  d_term  = restir_d_ggx(n_dot_h, a2);
    float  v_term  = restir_v_smithggx(n_dot_v, n_dot_l, a2);
    float3 fr      = d_term * v_term * f_term;
    fr            *= restir_compute_multiscatter_energy(f0, n_dot_v, roughness);
    // v_smithggx already absorbs the 4*n_dot_l*n_dot_v denominator, fr is per-(steradian * cos)
    float3 specular_cos = fr * n_dot_l;

    // importance sampling pdf for the vndf sampler in sample_brdf
    // pdf_l = D(h) * G1(v) / (4 * cos(theta_v)) (heitz 2018 eq. 17)
    float n_dot_v_abs = max(n_dot_v, 1e-3f);
    float g1_v_pdf    = 2.0f * n_dot_v_abs / (n_dot_v_abs + sqrt(a2 + (1.0f - a2) * n_dot_v_abs * n_dot_v_abs));
    float diffuse_pdf = n_dot_l / PI;
    float spec_pdf    = d_term * g1_v_pdf / (4.0f * n_dot_v_abs);
    float spec_prob   = compute_spec_probability(albedo, roughness, metallic, n_dot_v);
    pdf = (1.0f - spec_prob) * diffuse_pdf + spec_prob * spec_pdf;

    return diffuse_cos + specular_cos;
}

// samples a direction proportional to diffuse+specular mixture, returns the sampled direction
// and the mixture pdf so the caller can compute brdf*cos / pdf
// when primary_diffuse_only is true, sampling collapses to pure cosine hemisphere so the
// estimator stays consistent with evaluate_brdf's diffuse only branch
float3 sample_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float2 xi, out float pdf, bool primary_diffuse_only)
{
    if (primary_diffuse_only)
    {
        float  pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        pdf = pdf_diffuse;
        return local_to_world(local_dir, n);
    }

    float n_dot_v      = max(dot(n, v), 0.001f);
    float spec_prob    = compute_spec_probability(albedo, roughness, metallic, n_dot_v);
    float prob_diffuse = 1.0f - spec_prob;

    float3 l;
    if (xi.x < prob_diffuse)
    {
        xi.x = xi.x / prob_diffuse;
        float pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        l = local_to_world(local_dir, n);
    }
    else
    {
        xi.x = (xi.x - prob_diffuse) / (1.0f - prob_diffuse);
        // vndf sampling concentrates microfacet normals around those visible from v, removing
        // the dim grazing samples that legacy ndf sampling produced and lowering variance for
        // glossy surfaces at oblique view angles, alpha is the cod-wwii remap so sampler and
        // integrand share a single ggx lobe shape (matches evaluate_brdf and brdf.hlsl)
        float3 v_local = world_to_local(v, n);
        v_local.z      = max(v_local.z, 1e-4f);
        float3 h_local = sample_ggx_vndf(v_local, xi, restir_d_ggx_alpha(roughness));
        float3 h_world = local_to_world(h_local, n);
        l = reflect(-v, h_world);
    }

    float n_dot_l     = max(dot(n, l), 0.001f);
    float diffuse_pdf = n_dot_l / PI;

    float3 h_unnorm = v + l;
    float h_len_sq  = dot(h_unnorm, h_unnorm);

    if (h_len_sq < 1e-6f)
    {
        pdf = diffuse_pdf * prob_diffuse;
        return l;
    }

    float3 h       = h_unnorm * rsqrt(h_len_sq);
    float n_dot_h  = max(dot(n, h), 0.001f);
    // vndf pdf for the reflection direction l, see heitz 2018 eq. 17
    // pdf(l) = D(h) * G1(v) / (4 * cos(theta_v))
    float a           = restir_d_ggx_alpha(roughness);
    float a2          = a * a;
    float d           = restir_d_ggx(n_dot_h, a2);
    float n_dot_v_abs = max(abs(dot(n, v)), 1e-3f);
    float g1_v        = 2.0f * n_dot_v_abs / (n_dot_v_abs + sqrt(a2 + (1.0f - a2) * n_dot_v_abs * n_dot_v_abs));
    float spec_pdf    = d * g1_v / (4.0f * n_dot_v_abs);

    pdf = prob_diffuse * diffuse_pdf + spec_prob * spec_pdf;
    return l;
}

// full brdf*cos evaluator used at the primary vertex during shift and target-pdf evaluation
// includes both diffuse and ggx specular, so metallic primaries receive proper gi contribution
// (the reflection pipeline for pure mirror lobes should reconcile any overlap at shade time)
// when primary_diffuse_only is true, the specular lobe is dropped, primary specular is then
// owned by the ray traced reflections pipeline to avoid double counting
float3 eval_surface_brdf_cos(float3 albedo, float roughness, float metallic, float3 normal, float3 view_dir, float3 dir, bool primary_diffuse_only)
{
    float n_dot_l = dot(normal, dir);
    if (n_dot_l <= 0.0f)
        return float3(0, 0, 0);

    float unused_pdf;
    return evaluate_brdf(albedo, roughness, metallic, normal, view_dir, dir, unused_pdf, primary_diffuse_only);
}

// energy split at the primary vertex (lin 2022 paper-faithful hybrid):
//   roughness >= RESTIR_PRIMARY_SPECULAR_OWNED_MIN  -> restir samples diffuse + specular
//   roughness <  RESTIR_PRIMARY_SPECULAR_OWNED_MIN  -> restir samples diffuse only,
//     ray traced reflections (or specular ibl as fallback) owns the near mirror lobe
// using the same threshold as the rc gate keeps the energy ownership consistent with the
// reconnection shift's own validity domain, so we never sample a primary lobe whose paths
// the shift would reject anyway
bool restir_primary_diffuse_only(float roughness)
{
    if (is_ray_traced_reflections_enabled() && roughness < RESTIR_PRIMARY_SPECULAR_OWNED_MIN)
    {
        return true;
    }
    return false;
}

// lin 2022 reconnection conditions for reusing src's rc at a destination surface
// the rc vertex roughness gate is enforced at sample construction, so at reuse time we only
// need the stored rc flag and a minimum separation to keep the jacobian bounded
bool can_reconnect_at_dst(PathSample src, float3 dst_pos, float dst_roughness)
{
    if (!has_reconnection(src))
        return false;

    float3 to_rc = src.rc_pos - dst_pos;
    if (dot(to_rc, to_rc) < RESTIR_RC_MIN_DISTANCE * RESTIR_RC_MIN_DISTANCE)
        return false;

    return true;
}

struct ShiftResult
{
    float3 f_dst;
    float  jacobian;
    bool   ok;
};

// computes the radiance leaving rc toward dst's primary, paper-faithful split:
//   L_at_rc(dst_view) = L_nee + f_rc(dst_view, rc_outgoing_dir) * L_post
// for sky and area-light nee samples L_post is zero so the f_rc term vanishes regardless of
// the rc material params (the stored rc_albedo / rc_roughness / rc_metallic are ignored)
// dir_primary_to_rc is the unit direction from dst's primary to rc, the incoming direction
// at rc is therefore -dir_primary_to_rc (light flows toward primary, view flows away)
float3 rc_outgoing_radiance(PathSample src, float3 dir_primary_to_rc)
{
    // sky and area-light nee samples carry no continuation past rc, rc_L_post is exactly zero
    // and the rc material params are unused, short-circuit to avoid evaluating a meaningless
    // f_rc against a fake rc_normal (which is -sun_dir for sky) and rc_outgoing_dir
    if (dot(src.rc_L_post, src.rc_L_post) <= 0.0f)
    {
        return src.rc_L_nee;
    }

    float3 view_at_rc = -dir_primary_to_rc;
    float3 f_rc      = eval_surface_brdf_cos(src.rc_albedo, src.rc_roughness, src.rc_metallic,
                                             src.rc_normal, view_at_rc, src.rc_outgoing_dir,
                                             false);
    return src.rc_L_nee + f_rc * src.rc_L_post;
}

// reconnection shift from source (its primary is src_primary_pos) to destination
// returns ok=false if surfaces don't satisfy reconnection conditions or geometry is degenerate
// visibility is checked separately via trace_shift_visibility so non-visibility-critical passes
// (like target_pdf evaluation at a neighbor) can skip the ray cast
ShiftResult try_reconnection_shift(
    PathSample src,
    float3 src_primary_pos,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult result;
    result.f_dst    = float3(0, 0, 0);
    result.jacobian = 0.0f;
    result.ok       = false;

    if (is_sky_sample(src))
    {
        float3 dir = src.rc_pos;
        if (dot(dst_normal, dir) <= RESTIR_RC_COS_FRONT)
            return result;

        float3 brdf_cos = eval_surface_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir, restir_primary_diffuse_only(dst_roughness));
        if (all(brdf_cos <= 0.0f))
            return result;

        result.f_dst    = brdf_cos * rc_outgoing_radiance(src, dir);
        result.jacobian = 1.0f;
        result.ok       = true;
        return result;
    }

    if (!can_reconnect_at_dst(src, dst_pos, dst_roughness))
        return result;

    float3 rc_from_src = src.rc_pos - src_primary_pos;
    float3 rc_from_dst = src.rc_pos - dst_pos;

    float dist_src_sq = dot(rc_from_src, rc_from_src);
    float dist_dst_sq = dot(rc_from_dst, rc_from_dst);

    if (dist_src_sq < 1e-4f || dist_dst_sq < RESTIR_RC_MIN_DISTANCE * RESTIR_RC_MIN_DISTANCE)
        return result;

    float dist_src = sqrt(dist_src_sq);
    float dist_dst = sqrt(dist_dst_sq);

    float3 dir_src = rc_from_src / dist_src;
    float3 dir_dst = rc_from_dst / dist_dst;

    if (dot(dst_normal, dir_dst) <= RESTIR_RC_COS_FRONT)
        return result;

    float cos_rc_src = dot(src.rc_normal, -dir_src);
    float cos_rc_dst = dot(src.rc_normal, -dir_dst);

    if (cos_rc_src <= RESTIR_RC_COS_FRONT || cos_rc_dst <= RESTIR_RC_COS_FRONT)
        return result;

    float3 brdf_cos = eval_surface_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir_dst, restir_primary_diffuse_only(dst_roughness));
    if (all(brdf_cos <= 0.0f))
        return result;

    // solid-angle jacobian at rc: (cos_dst * dist_src^2) / (cos_src * dist_dst^2)
    float jacobian = (cos_rc_dst * dist_src_sq) / max(cos_rc_src * dist_dst_sq, 1e-6f);
    // reject the shift outright when the jacobian indicates a near-degenerate connection,
    // a single rejected stream contributes nothing instead of darkening the result via a clamp
    if (jacobian <= 0.0f || jacobian > RESTIR_JACOBIAN_REJECT || isnan(jacobian) || isinf(jacobian))
        return result;

    // paper-faithful rc evaluation: re-evaluate f_rc at dst's incoming direction, this is the
    // term that carries indirect specular at rc, baking it at src direction (the previous
    // implementation's behavior) was the dominant source of broken indirect specular reuse
    result.f_dst    = brdf_cos * rc_outgoing_radiance(src, dir_dst);
    result.jacobian = jacobian;
    result.ok       = true;
    return result;
}

// random replay shift (lin 2022 hybrid shift, random-replay leg):
// rebuilds the primary-vertex bounce at dst using the same xi that produced the src path,
// then checks if the replayed bounce lands close enough to the stored rc to reuse the suffix.
// the jacobian is the ratio of primary brdf pdfs at src and dst evaluated on each branch's
// own sampled direction, which is the standard density change-of-variables factor for
// random-replay shifts (volume preserving in xi-space)
//
// this is the fallback for samples that lack PATH_FLAG_HAS_RC (the reconnection shift's
// roughness or distance gate failed); a single inline ray is cast from dst primary so the
// added cost is paid only when the cheaper reconnection shift is not applicable
//
// rc_tolerance is the world-space slack on the rc match, scaled by the rc distance to keep
// the relative tolerance well-bounded on far surfaces. visibility is implicit: if the
// replayed ray hits within tolerance the connection is unoccluded by construction
static const float RESTIR_REPLAY_RC_TOLERANCE_REL = 0.05f;
static const float RESTIR_REPLAY_RC_TOLERANCE_MIN = 0.05f;

// forward declaration so the replay shift can call into the segment visibility helper which
// is defined later in this header
bool trace_shift_visibility(PathSample src, float3 dst_pos, float3 dst_normal);

ShiftResult try_random_replay_shift(
    PathSample src,
    float3 src_pos,
    float3 src_normal,
    float3 src_view_dir,
    float3 src_albedo,
    float src_roughness,
    float src_metallic,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult result;
    result.f_dst    = float3(0, 0, 0);
    result.jacobian = 0.0f;
    result.ok       = false;

    // sky samples have no scene-space rc to align with, so the reconnection shift already
    // handles them correctly with a constant jacobian; replay would just duplicate that
    if (is_sky_sample(src))
        return result;

    // replay xi at the stored seed; the seed is captured before the original xi was consumed
    uint   replay_seed = src.seed_path;
    float2 xi          = random_float2(replay_seed);

    // each side picks its own primary lobe ownership based on its own roughness, the jacobian
    // pdf_src / pdf_dst then captures the change of variables exactly because each pdf reflects
    // the actual sampling distribution at that side
    bool src_diffuse_only = restir_primary_diffuse_only(src_roughness);
    bool dst_diffuse_only = restir_primary_diffuse_only(dst_roughness);

    // sample brdf at src to get src's primary direction and pdf for the jacobian
    float  pdf_src;
    float3 dir_src = sample_brdf(src_albedo, src_roughness, src_metallic, src_normal, src_view_dir, xi, pdf_src, src_diffuse_only);
    if (pdf_src < RESTIR_MIN_PDF || dot(dir_src, src_normal) <= 0.0f)
        return result;

    // sample brdf at dst with the same xi for the replayed bounce
    float  pdf_dst;
    float3 dir_dst = sample_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, xi, pdf_dst, dst_diffuse_only);
    if (pdf_dst < RESTIR_MIN_PDF || dot(dir_dst, dst_normal) <= 0.0f)
        return result;

    float offset = compute_ray_offset(dst_pos);
    RayDesc ray;
    ray.Origin    = dst_pos + dst_normal * offset;
    ray.Direction = dir_dst;
    ray.TMin      = RESTIR_RAY_T_MIN;
    ray.TMax      = 1000.0f;

    RayQuery<RAY_FLAG_NONE> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();
    if (query.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return result;

    float  hit_t   = query.CommittedRayT();
    float3 hit_pos = ray.Origin + ray.Direction * hit_t;

    // accept the replay only if the bounce landed near the stored rc, scaled tolerance keeps
    // the test relative on far hits where small angular changes produce large position deltas
    //
    // note: this is a soft-reconnection variant of the lin 2022 random replay shift. the
    // strict paper formulation would re-trace the entire suffix from dst with replayed xi
    // (no rc proximity test, just rebuild the full path) and apply the volume-preserving
    // jacobian over xi space. the tolerance test here is a cheap fallback that rejects
    // replays whose reconnection point drifts far from the source rc, which trades a small
    // amount of rejection bias for ~10x lower cost than full suffix retrace. with the new
    // paper-faithful rc storage (rc_L_post + rc_outgoing_dir factoring f_rc out) this
    // approximation remains usable because the reconnection shift handles the dominant
    // cases, the replay shift is now only the fallback for short paths and near-specular
    // primaries where reconnection would be near-degenerate
    float3 to_rc        = src.rc_pos - hit_pos;
    float  rc_dist      = length(src.rc_pos - dst_pos);
    float  tol          = max(RESTIR_REPLAY_RC_TOLERANCE_REL * rc_dist, RESTIR_REPLAY_RC_TOLERANCE_MIN);
    if (dot(to_rc, to_rc) > tol * tol)
        return result;

    // explicit visibility check from dst to src.rc_pos, the tolerance test above only ensures
    // hit_pos is geographically close to src.rc_pos but does not catch concave geometry where
    // the segment dst to src.rc_pos crosses a wall and the suffix would attach to the wrong side
    if (!trace_shift_visibility(src, dst_pos, dst_normal))
        return result;

    // brdf at dst toward the replayed direction, multiplied by stored suffix radiance
    float  pdf_eval;
    float3 brdf_cos = evaluate_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir_dst, pdf_eval, dst_diffuse_only);
    if (all(brdf_cos <= 0.0f))
        return result;

    // jacobian for replay is pdf_src / pdf_dst; the xi -> direction map is volume preserving
    // in xi-space so the density change of variables in solid-angle is exactly this ratio
    // f_dst stays in integrand form (brdf*cos*radiance), the pdf factor goes through the
    // jacobian so the ris weight target/source remains in consistent units across shift types
    float jacobian = pdf_src / max(pdf_dst, RESTIR_MIN_PDF);
    if (jacobian <= 0.0f || jacobian > RESTIR_JACOBIAN_REJECT || isnan(jacobian) || isinf(jacobian))
        return result;

    // re-evaluate f_rc at dst's incoming direction toward rc_outgoing_dir (paper-faithful)
    result.f_dst    = brdf_cos * rc_outgoing_radiance(src, dir_dst);
    result.jacobian = jacobian;
    result.ok       = true;
    return result;
}

// hybrid shift: tries the cheap reconnection shift first, falls back to random replay when
// reconnection is not applicable (no PATH_FLAG_HAS_RC); this is lin 2022's hybrid shift in
// its most common practical form, where the choice between legs is driven by the rc roughness
// gate at sample construction time
ShiftResult try_hybrid_shift(
    PathSample src,
    float3 src_primary_pos,
    float3 src_normal,
    float3 src_view_dir,
    float3 src_albedo,
    float src_roughness,
    float src_metallic,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult reconnection = try_reconnection_shift(
        src, src_primary_pos, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (reconnection.ok)
        return reconnection;

    return try_random_replay_shift(
        src,
        src_primary_pos, src_normal, src_view_dir, src_albedo, src_roughness, src_metallic,
        dst_pos,         dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
}

// visibility ray from dst primary to rc
// sky samples verify the direction is unoccluded to the sky, non-sky samples trace a segment
// requires tlas to be bound in the caller (raygen or compute with inline ray tracing)
bool trace_shift_visibility(PathSample src, float3 dst_pos, float3 dst_normal)
{
    float  offset = max(RESTIR_RAY_T_MIN, compute_ray_offset(dst_pos));
    float3 dir;
    float  t_max;

    if (is_sky_sample(src))
    {
        dir   = src.rc_pos;
        t_max = 10000.0f;
    }
    else
    {
        float3 to_rc = src.rc_pos - dst_pos;
        float  dist  = length(to_rc);
        if (dist < RESTIR_RC_MIN_DISTANCE)
            return false;

        dir   = to_rc / dist;
        t_max = max(dist - offset * 2.0f, offset);
    }

    RayDesc ray;
    ray.Origin    = dst_pos + dst_normal * offset;
    ray.Direction = dir;
    ray.TMin      = offset;
    ray.TMax      = t_max;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING;
}

// self-shift evaluation (jacobian == 1 by construction)
// bypasses can_reconnect_at_dst since at the source pixel the path contribution is always
// well-defined regardless of the rc roughness / distance gate (those only bound the jacobian)
ShiftResult self_shift_evaluate(
    PathSample src,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult result;
    result.f_dst    = float3(0, 0, 0);
    result.jacobian = 1.0f;
    result.ok       = false;

    float3 dir;
    if (is_sky_sample(src))
    {
        dir = src.rc_pos;
    }
    else
    {
        float3 to_rc = src.rc_pos - dst_pos;
        float  d2    = dot(to_rc, to_rc);
        if (d2 < 1e-8f)
            return result;
        dir = to_rc * rsqrt(d2);
    }

    if (dot(dst_normal, dir) <= 0.0f)
        return result;

    float3 brdf_cos = eval_surface_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir, restir_primary_diffuse_only(dst_roughness));
    if (all(brdf_cos <= 0.0f))
        return result;

    // self-shift can re-use the same rc_outgoing_radiance helper, the dst-incoming direction
    // is the same as the src-incoming direction by construction so f_rc evaluates to the same
    // value the path was originally constructed with, no view-dep drift here
    result.f_dst = brdf_cos * rc_outgoing_radiance(src, dir);
    result.ok    = true;
    return result;
}

// luminance of f(y) at dst via reconnection shift (used for cross-pixel reuse)
// for self-evaluation (src_primary_pos == dst_pos) use target_pdf_self instead so invalid-rc
// samples still have a non-zero target at their own pixel
float target_pdf_at_dst(
    PathSample src,
    float3 src_primary_pos,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult r = try_reconnection_shift(src, src_primary_pos, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (!r.ok)
        return 0.0f;

    float lum = dot(r.f_dst, float3(0.299f, 0.587f, 0.114f));
    return max(lum, 0.0f);
}

float target_pdf_self(
    PathSample src,
    float3 dst_pos,
    float3 dst_normal,
    float3 dst_view_dir,
    float3 dst_albedo,
    float dst_roughness,
    float dst_metallic)
{
    ShiftResult r = self_shift_evaluate(src, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (!r.ok)
        return 0.0f;

    float lum = dot(r.f_dst, float3(0.299f, 0.587f, 0.114f));
    return max(lum, 0.0f);
}

// inline ray-traced shadow ray, returns true if the segment is unoccluded
// requires tlas to be bound in the caller (raygen or compute with inline ray tracing)
bool trace_shadow_ray(float3 origin, float3 direction, float max_dist)
{
    float epsilon = max(RESTIR_RAY_T_MIN, compute_ray_offset(origin));

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = direction;
    ray.TMin      = epsilon;
    ray.TMax      = max(max_dist - epsilon, epsilon);

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING;
}

// the demodulator that shade_reservoir_path divides out so light_composition can re-apply
// surface.albedo at full resolution, this is the dominant diffuse response factor at the
// primary vertex, dividing it out and re-multiplying with the per pixel full res albedo
// recovers fine material detail that the half res shading + bilateral upsample otherwise
// blurs across neighboring texels, the 0.04 floor matches the dielectric f0 fallback so
// black materials still carry the small specular component without dividing by zero,
// must stay in sync with restir_albedo_demodulator in light_composition.hlsl
float3 restir_albedo_demodulator(float3 albedo)
{
    return max(albedo, float3(0.04f, 0.04f, 0.04f));
}

// shades the reservoir's sample at the current pixel (self-shift, jacobian == 1)
// traces the visibility ray (sky samples verify sky reachability) and returns f(y) * W
// divided by the primary's albedo demodulator so the gi texture stores lighting only,
// light_composition then multiplies the bilaterally upsampled signal by full res albedo
float3 shade_reservoir_path(Reservoir r, float3 dst_pos, float3 dst_normal, float3 dst_view_dir, float3 dst_albedo, float dst_roughness, float dst_metallic)
{
    if (r.M <= 0.0f || r.W <= 0.0f)
        return float3(0, 0, 0);

    ShiftResult shift = self_shift_evaluate(r.sample, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (!shift.ok)
        return float3(0, 0, 0);

    if (!trace_shift_visibility(r.sample, dst_pos, dst_normal))
        return float3(0, 0, 0);

    return (shift.f_dst * r.W) / restir_albedo_demodulator(dst_albedo);
}

#endif // SPARTAN_RESTIR_RESERVOIR
