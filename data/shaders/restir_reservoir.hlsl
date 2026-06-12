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

// core parameters, m_cap ramps from a moving baseline to the static target as the camera holds still
static const uint  RESTIR_MAX_PATH_LENGTH    = 5;
static const uint  RESTIR_M_CAP_MIN          = 32;
static const uint  RESTIR_M_CAP_MAX          = 128;
static const uint  RESTIR_SPATIAL_SAMPLES    = 8;

float get_restir_m_cap()
{
    float t    = float(buffer_frame.time) - buffer_frame.camera_last_movement_time;
    float ramp = saturate((t - 0.5f) / 1.0f);
    return lerp(float(RESTIR_M_CAP_MIN), float(RESTIR_M_CAP_MAX), ramp);
}
uint  get_restir_max_path_length()     { return RESTIR_MAX_PATH_LENGTH; }
uint  get_restir_light_candidates()    { return 16u; }
uint  get_restir_initial_candidates()  { return 8u; }
uint  get_restir_emtri_candidates()    { return 8u; }
// rc roughness floor, below this reconnection bias grows so near mirrors fall back to replay
float get_restir_rc_min_roughness()    { return 0.2f; }
// single sample w cap from cb, trades firefly safety for highlight energy
float get_restir_w_clamp()             { return buffer_frame.restir_pt_w_clamp; }
uint  get_restir_validation_period()   { return 8u; }
// depth and normal gates for spatial reuse and temporal validity, ~26 deg keeps reuse on continuous surfaces
static const float RESTIR_DEPTH_THRESHOLD    = 0.03f;
static const float RESTIR_NORMAL_THRESHOLD   = 0.9f;
static const float RESTIR_TEMPORAL_DECAY     = 1.0f;
static const float RESTIR_RAY_T_MIN          = 0.001f;

// sky / environment, both bands derive from the surface w clamp so one knob scales the hdr range
static const float RESTIR_SKY_RADIANCE_CLAMP_FACTOR = 4.0f;
static const float RESTIR_SKY_W_CLAMP_FACTOR        = 10.0f;
static const float RESTIR_SKY_DISTANCE              = 1e10f;

// reconnection criteria, distance and cos floors are absolute safety guards
static const float RESTIR_RC_MIN_DISTANCE    = 0.1f;
static const float RESTIR_RC_COS_FRONT       = 0.05f;
// reject geometrically extreme shifts in both directions, the jacobian participates in the
// pairwise mis denominators so this is only a numerical safety guard, not a bias knob
static const float RESTIR_JACOBIAN_REJECT    = 8.0f;

// brdf / numerics
static const float RESTIR_MIN_PDF            = 1e-6f;

// nee
static const float MIN_AREA_LIGHT_SOLID_ANGLE = 1e-4f;

// urena, fajardo, king 2013 spherical rectangle solid angle sampling
// rect_origin is the min corner, ex / ey the edge vectors, returns the sampled point and solid angle
void sample_spherical_rectangle(
    float3 origin,
    float3 rect_origin,
    float3 ex,
    float3 ey,
    float2 xi,
    out float3 out_pos,
    out float  out_solid_angle)
{
    float3 d   = rect_origin - origin;
    float  exl = max(length(ex), 1e-6f);
    float  eyl = max(length(ey), 1e-6f);
    float3 x   = ex / exl;
    float3 y   = ey / eyl;
    float3 z   = cross(x, y);

    float x0 = dot(d, x);
    float y0 = dot(d, y);
    float z0 = dot(d, z);
    if (z0 > 0.0f)
    {
        z0 = -z0;
        z  = -z;
    }

    float x1 = x0 + exl;
    float y1 = y0 + eyl;

    float3 v00 = float3(x0, y0, z0);
    float3 v10 = float3(x1, y0, z0);
    float3 v11 = float3(x1, y1, z0);
    float3 v01 = float3(x0, y1, z0);

    float3 n0 = normalize(cross(v00, v10));
    float3 n1 = normalize(cross(v10, v11));
    float3 n2 = normalize(cross(v11, v01));
    float3 n3 = normalize(cross(v01, v00));

    float g0 = acos(clamp(-dot(n0, n1), -1.0f, 1.0f));
    float g1 = acos(clamp(-dot(n1, n2), -1.0f, 1.0f));
    float g2 = acos(clamp(-dot(n2, n3), -1.0f, 1.0f));
    float g3 = acos(clamp(-dot(n3, n0), -1.0f, 1.0f));

    float b0 = n0.z;
    float b1 = n2.z;
    float k  = 2.0f * PI - g2 - g3;

    out_solid_angle = max(g0 + g1 - k, 0.0f);

    float au   = xi.x * out_solid_angle + k;
    float fu   = (cos(au) * b0 - b1) / max(sin(au), 1e-7f);
    float sgn  = fu > 0.0f ? 1.0f : -1.0f;
    float cu   = clamp(sgn / sqrt(fu * fu + b0 * b0), -1.0f, 1.0f);

    float xu_denom = max(sqrt(max(1.0f - cu * cu, 0.0f)), 1e-7f);
    float xu       = clamp(-(cu * z0) / xu_denom, x0, x1);

    float dsq = xu * xu + z0 * z0;
    float h0  = y0 / sqrt(dsq + y0 * y0);
    float h1  = y1 / sqrt(dsq + y1 * y1);
    float hv  = h0 + xi.y * (h1 - h0);
    float hv2 = hv * hv;
    float yv  = (hv2 < 1.0f - 1e-6f) ? (hv * sqrt(dsq) / sqrt(1.0f - hv2)) : y1;

    out_pos = origin + xu * x + yv * y + z0 * z;
}

// path flags
static const uint PATH_FLAG_SKY      = 1 << 0;  // rc is the sky dome, rc_pos stores a unit direction
static const uint PATH_FLAG_HAS_RC   = 1 << 1;  // reconnection vertex is valid for the reconnection shift
static const uint PATH_FLAG_SPECULAR = 1 << 2;  // diagnostic, primary surface is specular leaning
static const uint PATH_FLAG_NEE      = 1 << 3;  // candidate came from the light nee strategy

// suffix of a path starting at the primary hit, lin 2022 5 split of the radiance leaving rc
//   L_at_rc = L_nee + f_rc(in_at_rc, rc_outgoing_dir) * L_post
// L_nee is view independent (lambert only nee at rc), f_rc is re-evaluated at the dst incoming
// direction at shift time so indirect specular stays view dependent
// rc_pos is a world space vertex (HAS_RC) or a unit sky direction (SKY)
// src_* captures the source pixel primary surface so reuse passes avoid sampling a reprojected g-buffer
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

// reservoir texture packing, 6 x RGBA32F = 24 floats
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
// src_pos is absolute world space f16, enough precision for the jacobian distance ratios
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
    // target_pdf is the own domain density in the pairwise mis shares, reject corrupt values
    if (isnan(r.target_pdf) || isinf(r.target_pdf) || r.target_pdf < 0)   return false;
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
    // sky samples use a higher clamp since sun disk radiance exceeds the surface band
    return is_sky_sample(s) ? w * RESTIR_SKY_W_CLAMP_FACTOR : w;
}

// streaming ris sample insert, lin 2022 algorithm 1
// zero weight candidates do not bump M so they do not skew downstream pairwise mis denominators
bool update_reservoir(inout Reservoir reservoir, PathSample new_sample, float weight, float random_value)
{
    if (weight <= 0.0f || isnan(weight) || isinf(weight))
    {
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

// caps M without touching weight_sum or W, only affects future stream combine ratios
void clamp_reservoir_M(inout Reservoir reservoir, float max_M)
{
    if (reservoir.M > max_M)
        reservoir.M = max_M;
}

// unified disocclusion gate for the reservoir temporal pass and the denoiser accumulator
// reprojects the current position through the previous view projection and compares expected
// vs actual previous depth so a moving surface is not mistaken for a disocclusion
// prev_depth_tex must be bound to gbuffer_depth_previous and prev_normal_tex to
// gbuffer_normal_previous, confidence returns a [0,1] reuse factor
bool evaluate_disocclusion(
    Texture2D prev_depth_tex,
    Texture2D prev_normal_tex,
    float2 current_uv,
    float2 prev_uv,
    float3 current_position,
    float3 current_normal,
    float2 resolution,
    float reproj_tol_min,
    float reproj_tol_max,
    float normal_min,
    float normal_max,
    float depth_min,
    float depth_max,
    float motion_reference,
    out float confidence)
{
    confidence = 0.0f;

    if (prev_uv.x < 0.0f || prev_uv.x > 1.0f || prev_uv.y < 0.0f || prev_uv.y > 1.0f)
        return false;

    float4 prev_clip        = mul(float4(current_position, 1.0f), get_view_projection_previous());
    float3 prev_ndc         = prev_clip.xyz / max(prev_clip.w, 1e-9f);
    float2 expected_prev_uv = prev_ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    float2 reproj_diff      = abs(prev_uv - expected_prev_uv) * resolution;
    float  reproj_dist      = length(reproj_diff);

    float2 motion        = (current_uv - prev_uv) * resolution;
    float  motion_len    = length(motion);
    float  motion_factor = saturate(motion_len / max(motion_reference, 1.0f));

    float reproj_tol = lerp(reproj_tol_min, reproj_tol_max, motion_factor);
    if (reproj_dist > reproj_tol)
        return false;

    // previous frame normal at prev_uv, the current normal buffer holds a different surface
    // there whenever anything moved, fail closed on uninitialized (cleared) history
    float3 prev_normal = prev_normal_tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).xyz;
    if (dot(prev_normal, prev_normal) < 1e-4f)
        return false;
    prev_normal = normalize(prev_normal);
    float normal_similarity = dot(current_normal, prev_normal);
    float  normal_threshold  = lerp(normal_min, normal_max, motion_factor);
    if (normal_similarity < normal_threshold)
        return false;

    float prev_depth_raw = prev_depth_tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).r;
    if (prev_depth_raw <= 0.0f)
        return false;

    float expected_prev_depth = linearize_depth(prev_ndc.z);
    float prev_depth_linear   = linearize_depth(prev_depth_raw);
    float depth_delta         = abs(prev_depth_linear - expected_prev_depth) / max(expected_prev_depth, 1e-3f);
    float depth_limit         = lerp(depth_min, depth_max, motion_factor);
    if (depth_delta > depth_limit)
        return false;

    float reproj_conf = saturate(1.0f - reproj_dist / reproj_tol);
    float normal_conf = saturate((normal_similarity - normal_threshold) / max(1.0f - normal_threshold, 1e-4f));
    float motion_conf = saturate(1.0f - motion_len / max(motion_reference, 1.0f));
    float depth_conf  = saturate(1.0f - depth_delta / depth_limit);
    confidence        = reproj_conf * normal_conf * motion_conf * depth_conf;

    return true;
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

// visible ggx normal sampling, heitz 2018, ve is the view direction in local tangent space
// alpha must match the brdf model via restir_d_ggx_alpha or the pdf and integrand densities diverge
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

// scalar target for ris and pairwise mis, l1/3 treats rgb equally so chromatic gi is not biased
float target_scalar(float3 f)
{
    return max(f.r + f.g + f.b, 0.0f) * (1.0f / 3.0f);
}

// smooth saturator for the reservoir w cap, pass through below c and asymptote at 2c above
// avoids the flicker a hard min creates when w bounces across the threshold between frames
float soft_clamp_w(float w, float c)
{
    if (c <= 0.0f)
        return 0.0f;
    if (w <= c)
        return w;
    return c + (w - c) / (1.0f + (w - c) / c);
}

// soft luminance compressor, preserves chromaticity and approaches threshold asymptotically
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

float3 clamp_sky_radiance(float3 radiance)
{
    float threshold = get_restir_w_clamp() * RESTIR_SKY_RADIANCE_CLAMP_FACTOR;
    return soft_saturate_radiance(radiance, threshold);
}

// ray offset for self intersection avoidance, scales with position magnitude and camera distance
// since float precision degrades with magnitude, wachter and binder simplified form
float compute_ray_offset(float3 pos_ws)
{
    float p_mag = max(abs(pos_ws.x), max(abs(pos_ws.y), abs(pos_ws.z)));
    float dist  = length(pos_ws - get_camera_position());
    float ofs   = max(max(p_mag * 1e-4f, dist * 1e-4f), 2e-4f);
    return min(ofs, 1e-2f);
}

// diffuse vs specular selection probability for the importance sampled brdf, from lobe energy
float compute_spec_probability(float3 albedo, float roughness, float metallic, float n_dot_v)
{
    float albedo_lum = max(luminance(albedo), 1e-3f);
    float f0_scalar  = lerp(0.04f, albedo_lum, metallic);
    float fresnel    = f0_scalar + (1.0f - f0_scalar) * pow(1.0f - n_dot_v, 5.0f);
    // smooth surfaces favor specular sampling, rough surfaces favor diffuse
    float gloss_bias = lerp(0.85f, 1.15f, 1.0f - roughness);
    float spec_w     = fresnel * gloss_bias;
    float diff_w     = albedo_lum * (1.0f - metallic) * (1.0f - fresnel);
    float total      = spec_w + diff_w;
    float spec_prob  = total > 0.0f ? spec_w / total : 0.5f;
    return clamp(spec_prob, 0.05f, 0.95f);
}

// brdf helpers ported from brdf.hlsl so restir matches the engine shading exactly
// inline copies, not includes, since brdf.hlsl needs structs unavailable in compute and raytrace passes
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

// full ggx+burley brdf returning f_r * cos and the matching mixture pdf, matches brdf.hlsl
// specular_blend [0,1] fades the specular lobe, 0 is pure diffuse, 1 is the full mixture
float3 evaluate_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float3 l, out float pdf, float specular_blend)
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
    float n_dot_v = dot(n, v);
    float n_dot_h = max(dot(n, h), 0.0f);
    float v_dot_h = max(dot(v, h), 0.0f);

    if (n_dot_l <= 0.0f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    // grazing view reject, below this cos the ggx pdf and smith g1 denominators inflate into fireflies
    if (n_dot_v <= 0.05f)
    {
        pdf = 0.0f;
        return float3(0, 0, 0);
    }

    float3 f0     = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 f90    = restir_compute_f90(f0);
    float3 f_term = restir_f_schlick(f0, f90, v_dot_h);

    float3 diffuse_color = albedo * (1.0f - metallic);
    float3 diffuse       = restir_diffuse_burley(diffuse_color, roughness, n_dot_v, n_dot_l, v_dot_h);
    float3 diffuse_cos   = diffuse * (1.0f - f_term) * n_dot_l;

    if (specular_blend <= 0.0f)
    {
        // diffuse only, pdf is the cosine hemisphere pdf to match sample_brdf
        pdf = n_dot_l / PI;
        return diffuse_cos;
    }

    // ggx specular, the same alpha feeds the d term, the pdf and the vndf sampler so densities match
    float  a       = restir_d_ggx_alpha(roughness);
    float  a2      = a * a;
    float  d_term  = restir_d_ggx(n_dot_h, a2);
    float  v_term  = restir_v_smithggx(n_dot_v, n_dot_l, a2);
    float3 fr      = d_term * v_term * f_term;
    fr            *= restir_compute_multiscatter_energy(f0, n_dot_v, roughness);
    float3 specular_cos = fr * n_dot_l;

    // vndf pdf, heitz 2018 eq 17, the effective spec pick fades with specular_blend
    float n_dot_v_abs = max(n_dot_v, 1e-3f);
    float g1_v_pdf    = 2.0f * n_dot_v_abs / (n_dot_v_abs + sqrt(a2 + (1.0f - a2) * n_dot_v_abs * n_dot_v_abs));
    float diffuse_pdf = n_dot_l / PI;
    float spec_pdf    = d_term * g1_v_pdf / (4.0f * n_dot_v_abs);
    float spec_prob_full = compute_spec_probability(albedo, roughness, metallic, n_dot_v);
    float spec_prob_eff  = saturate(spec_prob_full * specular_blend);
    pdf = (1.0f - spec_prob_eff) * diffuse_pdf + spec_prob_eff * spec_pdf;

    return diffuse_cos + specular_cos * specular_blend;
}

// samples a direction from the diffuse + blend*specular mixture, returns direction and mixture pdf
// specular_blend matches evaluate_brdf so the two stay numerically consistent
float3 sample_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float2 xi, out float pdf, float specular_blend)
{
    if (specular_blend <= 0.0f)
    {
        float  pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        pdf = pdf_diffuse;
        return local_to_world(local_dir, n);
    }

    // grazing view reject at the same cos threshold as evaluate_brdf
    float n_dot_v_raw = dot(n, v);
    if (n_dot_v_raw <= 0.05f)
    {
        pdf = 0.0f;
        return n;
    }
    float n_dot_v        = n_dot_v_raw;
    float spec_prob_full = compute_spec_probability(albedo, roughness, metallic, n_dot_v);
    float spec_prob      = saturate(spec_prob_full * specular_blend);
    float prob_diffuse   = 1.0f - spec_prob;

    float3 l;
    if (xi.x < prob_diffuse)
    {
        xi.x = xi.x / max(prob_diffuse, 1e-6f);
        float pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        l = local_to_world(local_dir, n);
    }
    else
    {
        xi.x = (xi.x - prob_diffuse) / max(1.0f - prob_diffuse, 1e-6f);
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
    // vndf pdf for the reflection direction, heitz 2018 eq 17
    float a           = restir_d_ggx_alpha(roughness);
    float a2          = a * a;
    float d           = restir_d_ggx(n_dot_h, a2);
    float n_dot_v_abs = max(abs(dot(n, v)), 1e-3f);
    float g1_v        = 2.0f * n_dot_v_abs / (n_dot_v_abs + sqrt(a2 + (1.0f - a2) * n_dot_v_abs * n_dot_v_abs));
    float spec_pdf    = d * g1_v / (4.0f * n_dot_v_abs);

    pdf = prob_diffuse * diffuse_pdf + spec_prob * spec_pdf;
    return l;
}

// full brdf*cos at the primary vertex for shift and target pdf evaluation
float3 eval_surface_brdf_cos(float3 albedo, float roughness, float metallic, float3 normal, float3 view_dir, float3 dir, float specular_blend)
{
    float n_dot_l = dot(normal, dir);
    if (n_dot_l <= 0.0f)
        return float3(0, 0, 0);

    float unused_pdf;
    return evaluate_brdf(albedo, roughness, metallic, normal, view_dir, dir, unused_pdf, specular_blend);
}

// rt reflections owns the entire primary specular lobe at every roughness, so restir is diffuse
// only at the primary and returns 0 here unconditionally, this used to be a roughness blend band
// (sharp lobe to rt, glossy lobe to restir) but rt reflections now jitters its ray across the ggx
// lobe and denoises it so it produces correct rough reflections too, there is no roughness left at
// which restir should own specular, staying diffuse only also avoids reusing a peaked specular
// target across pixels which makes the reservoir weight explode into blotches and keeps primary gi
// albedo proportional so the half res demod and upsample stay exact, suffix bounces past the
// primary still use the full bsdf since multi bounce glossy interreflection is not something rt
// reflections (a single primary bounce) can replace
float restir_primary_specular_blend(float roughness)
{
    return 0.0f;
}

// lin 2022 reconnection conditions, the rc roughness gate is enforced at sample construction
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

// radiance leaving rc toward dst primary, L_at_rc = L_nee + f_rc(dst_view, rc_outgoing_dir) * L_post
// dir_primary_to_rc is the unit direction from dst primary to rc, incoming at rc is its negative
float3 rc_outgoing_radiance(PathSample src, float3 dir_primary_to_rc)
{
    // sky and area light nee samples carry no continuation past rc, short circuit
    if (dot(src.rc_L_post, src.rc_L_post) <= 0.0f)
    {
        return src.rc_L_nee;
    }

    float3 view_at_rc = -dir_primary_to_rc;
    // rc is a suffix vertex so the full brdf is active
    float3 f_rc      = eval_surface_brdf_cos(src.rc_albedo, src.rc_roughness, src.rc_metallic,
                                             src.rc_normal, view_at_rc, src.rc_outgoing_dir,
                                             1.0f);
    return src.rc_L_nee + f_rc * src.rc_L_post;
}

// reconnection shift from source primary to destination, ok=false on degenerate geometry
// visibility is checked separately so non visibility critical passes can skip the ray cast
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

        float3 brdf_cos = eval_surface_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir, restir_primary_specular_blend(dst_roughness));
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

    float3 brdf_cos = eval_surface_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir_dst, restir_primary_specular_blend(dst_roughness));
    if (all(brdf_cos <= 0.0f))
        return result;

    // solid angle jacobian at rc, (cos_dst * dist_src^2) / (cos_src * dist_dst^2)
    float jacobian = (cos_rc_dst * dist_src_sq) / max(cos_rc_src * dist_dst_sq, 1e-6f);
    if (jacobian < 1.0f / RESTIR_JACOBIAN_REJECT || jacobian > RESTIR_JACOBIAN_REJECT || isnan(jacobian) || isinf(jacobian))
        return result;

    // re-evaluate f_rc at the dst incoming direction so indirect specular stays view dependent
    result.f_dst    = brdf_cos * rc_outgoing_radiance(src, dir_dst);
    result.jacobian = jacobian;
    result.ok       = true;
    return result;
}

// random replay shift, lin 2022 hybrid shift random replay leg
// rebuilds the primary bounce at dst with the source xi and retraces the suffix, jacobian is the
// ratio of primary brdf pdfs, handles paths reconnection cannot carry, near mirror and specular prefix

// forward declarations, the helpers are defined later in this header
bool trace_shift_visibility(PathSample src, float3 dst_pos, float3 dst_normal);
bool trace_shadow_ray(float3 origin, float3 direction, float max_dist);

// shared path tracing parameters, single copy used by both the initial trace and the replay
// shift so the two evaluate the same integrand
static const float RUSSIAN_ROULETTE_PROB     = 0.95f;
static const uint  RUSSIAN_ROULETTE_START    = 4;
static const float RUSSIAN_ROULETTE_MIN_PROB = 0.1f;
static const float SKY_MIP_LEVEL             = 2.0f;
// sun cone half angle, matches the real sun angular radius
static const float SUN_CONE_HALF_ANGLE       = 0.0047f;

// sun vs cosine hemisphere mixture weight, balance heuristic on solid angle scaled radiance
float sun_sample_probability(bool has_sun, float sun_intensity, float3 sun_color, float sun_cos_max)
{
    if (!has_sun)
    {
        return 0.0f;
    }
    float sun_omega    = 2.0f * PI * (1.0f - sun_cos_max);
    float sun_radiance = luminance(sun_color) * max(sun_intensity, 0.0f);
    float sun_w        = sun_radiance * sun_omega;
    // sky reference, hemispherical integral of a unit luminance probe, stable denominator
    float sky_w        = 2.0f * PI;
    float prob         = sun_w / max(sun_w + sky_w, 1e-6f);
    return clamp(prob, 0.05f, 0.95f);
}

// samples a sky color along a direction (used when a bounce misses geometry)
float3 sample_sky(float3 dir)
{
    float2 uv  = direction_sphere_uv(dir);
    float3 sky = tex3.SampleLevel(GET_SAMPLER(sampler_trilinear_clamp), uv, SKY_MIP_LEVEL).rgb;
    return clamp_sky_radiance(sky);
}

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

float3 probe_emission_estimate(MaterialParameters mat)
{
    if (mat.emissive_from_albedo() || mat.has_texture_emissive())
        return mat.color.rgb;
    return float3(0.0f, 0.0f, 0.0f);
}

// emissive triangle nee pool active check, false skips the emtri strategy entirely
bool is_emtri_pool_active()
{
    return buffer_frame.restir_pt_emissive_tri_count > 0.5f;
}

// direct lighting (analytical lights + environment probe) at a surface vertex toward view_dir
// specular_blend weights the specular lobe, 1 keeps the full brdf, 0 leaves view independent diffuse for rc
// single copy shared by the initial trace and the replay shift, the rng draw order is part of
// the replay contract, any change here changes the replayed paths too which keeps both in sync
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

            // urena 2013 spherical rectangle solid angle sampling
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

        // rc uses lambert only so the stored nee stays view independent
        float  brdf_pdf;
        float3 brdf = evaluate_brdf(albedo, roughness, metallic, shading_normal, view_dir, light_dir, brdf_pdf, specular_blend);

        // analytical lights are not in the bvh, single strategy mis weight of 1 is correct
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

// inline hit record for the replay suffix retrace, mirrors PathPayload but lives here for compute contexts
struct InlineHit
{
    bool   hit;
    float3 hit_position;
    float3 hit_normal;
    float3 geometric_normal;
    float3 albedo;
    float3 emission;
    float  roughness;
    float  metallic;
};

// replica of the closest hit shader for an inline RayQuery hit, must stay in sync with
// closest_hit in restir_pt.hlsl so a replayed path sees the same surface data as the original
void inline_pull_hit_data(
    uint  instance_id,
    uint  instance_index,
    uint  primitive_index,
    float2 bary_xy,
    float3 ray_origin,
    float3 ray_dir,
    float  hit_t,
    float4x3 obj_to_world_4x3,
    float4x3 world_to_obj_4x3,
    out InlineHit hit_out)
{
    // instance_id is the material index, instance_index is the tlas array index, they differ
    MaterialParameters mat = material_parameters[instance_id];
    GeometryInfo       geo = geometry_infos[instance_index];

    uint index_base = geo.index_offset + primitive_index * 3u;
    uint i0 = geometry_indices[index_base + 0u];
    uint i1 = geometry_indices[index_base + 1u];
    uint i2 = geometry_indices[index_base + 2u];

    PulledVertex pv0 = geometry_vertices[geo.vertex_offset + i0];
    PulledVertex pv1 = geometry_vertices[geo.vertex_offset + i1];
    PulledVertex pv2 = geometry_vertices[geo.vertex_offset + i2];

    float3 bary = float3(1.0f - bary_xy.x - bary_xy.y, bary_xy.x, bary_xy.y);

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

    float3x3 obj_to_world_3x3 = (float3x3)obj_to_world_4x3;
    float3x3 world_to_obj_3x3 = (float3x3)world_to_obj_4x3;
    float3   normal_world     = normalize(mul(normal_object, transpose(world_to_obj_3x3)));
    float3   tangent_world    = normalize(mul(tangent_object, obj_to_world_3x3));

    float3 hit_position = ray_origin + ray_dir * hit_t;
    if (geo.uv_world_space > 0.0f)
    {
        texcoord = compute_world_space_uv(hit_position, normal_world);
    }
    texcoord = texcoord * geo.uv_tiling + geo.uv_offset;
    if (geo.uv_rotation != 0.0f)
        texcoord = rotate_uv_90(texcoord, geo.uv_rotation);

    // distance based mip selection, identical to closest_hit
    float mip_level = clamp(log2(max(hit_t * 0.5f, 1.0f)), 0.0f, 4.0f);

    float3 edge1_world = mul(pv1.position - pv0.position, obj_to_world_3x3);
    float3 edge2_world = mul(pv2.position - pv0.position, obj_to_world_3x3);
    float3 geometric_normal = normalize(cross(edge1_world, edge2_world));
    if (dot(geometric_normal, ray_dir) > 0.0f)
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
        uint normal_idx = instance_id + material_texture_index_normal;
        float3 normal_sample = material_textures[normal_idx].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).rgb;

        normal_sample = normal_sample * 2.0f - 1.0f;
        normal_sample.xy *= mat.normal;

        float3 bitangent = normalize(cross(geometric_normal, tangent_world));
        float3x3 tbn     = float3x3(tangent_world, bitangent, geometric_normal);

        normal_world = normalize(mul(normal_sample, tbn));
        if (dot(normal_world, geometric_normal) < 0.0f)
            normal_world = -normal_world;
    }

    float3 albedo = mat.color.rgb;
    if (mat.has_texture_albedo())
    {
        uint   albedo_idx = instance_id + material_texture_index_albedo;
        float4 s          = material_textures[albedo_idx].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level);
        albedo = s.rgb * mat.color.rgb;
    }
    albedo = saturate(albedo);

    float roughness = mat.roughness;
    if (mat.has_texture_roughness())
    {
        uint roughness_idx = instance_id + material_texture_index_roughness;
        roughness *= material_textures[roughness_idx].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).g;
    }
    roughness = max(roughness, 0.04f);

    float metallic = mat.metalness;
    if (mat.has_texture_metalness())
    {
        uint metallic_idx = instance_id + material_texture_index_metalness;
        metallic *= material_textures[metallic_idx].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).r;
    }

    float3 emission = float3(0, 0, 0);
    if (mat.has_texture_emissive())
    {
        uint emissive_idx = instance_id + material_texture_index_emission;
        emission = material_textures[emissive_idx].SampleLevel(GET_SAMPLER(sampler_bilinear_wrap), texcoord, mip_level).rgb;
    }
    if (mat.emissive_from_albedo())
        emission += albedo;

    hit_out.hit              = true;
    hit_out.hit_position     = hit_position;
    hit_out.hit_normal       = normal_world;
    hit_out.geometric_normal = geometric_normal;
    hit_out.albedo           = albedo;
    hit_out.emission         = emission;
    hit_out.roughness        = roughness;
    hit_out.metallic         = metallic;
}

// inline ray cast and hit fetch, returns false on miss so the caller can handle sky escape
bool inline_trace_hit(float3 origin, float3 dir, float t_max, out InlineHit hit_out)
{
    hit_out.hit              = false;
    hit_out.hit_position     = float3(0, 0, 0);
    hit_out.hit_normal       = float3(0, 1, 0);
    hit_out.geometric_normal = float3(0, 1, 0);
    hit_out.albedo           = float3(0, 0, 0);
    hit_out.emission         = float3(0, 0, 0);
    hit_out.roughness        = 1.0f;
    hit_out.metallic         = 0.0f;

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = dir;
    ray.TMin      = RESTIR_RAY_T_MIN;
    ray.TMax      = t_max;

    RayQuery<RAY_FLAG_NONE> q;
    q.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    q.Proceed();

    if (q.CommittedStatus() != COMMITTED_TRIANGLE_HIT)
        return false;

    uint     inst_id   = q.CommittedInstanceID();
    uint     inst_idx  = q.CommittedInstanceIndex();
    uint     prim_idx  = q.CommittedPrimitiveIndex();
    float2   bary_xy   = q.CommittedTriangleBarycentrics();
    float    hit_t     = q.CommittedRayT();
    float4x3 obj_to_w  = q.CommittedObjectToWorld4x3();
    float4x3 w_to_obj  = q.CommittedWorldToObject4x3();

    inline_pull_hit_data(inst_id, inst_idx, prim_idx, bary_xy, origin, dir, hit_t, obj_to_w, w_to_obj, hit_out);
    return true;
}

// inline subpath retrace from the replayed first hit, returns radiance toward start_view_dir
// mirrors accumulate_subpath_at_rc and trace_rc_suffix in restir_pt.hlsl draw for draw so the
// same seed reproduces the original path exactly, the only structural difference is that the
// first bounce brdf stays in the throughput instead of being factored out for reconnection
float3 accumulate_replay_suffix(
    InlineHit start_hit,
    float3    start_view_dir,
    uint      max_bounces,
    inout uint seed)
{
    // emtri strategy carries emission when active, zero here to avoid double counting
    float3 result = is_emtri_pool_active() ? float3(0, 0, 0) : start_hit.emission;
    // lambert only nee at the first vertex, matches the rc construction in the initial trace
    result += direct_lighting_at_vertex(
        start_hit.hit_position, start_hit.hit_normal, start_hit.geometric_normal,
        start_view_dir, start_hit.albedo, start_hit.roughness, start_hit.metallic, 0.0f, seed);

    if (max_bounces < 2u)
        return result;

    uint max_bounces_remaining = max_bounces - 1u;

    InlineHit cur           = start_hit;
    float3    view_dir      = start_view_dir;
    float3    throughput    = float3(1, 1, 1);
    float     prev_brdf_pdf = 0.0f;
    float3    prev_normal   = start_hit.hit_normal;

    for (uint bounce = 0; bounce < max_bounces_remaining; bounce++)
    {
        if (bounce >= RUSSIAN_ROULETTE_START)
        {
            // veach style adjusted rr, fold the next vertex albedo into the continuation factor
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

        float  unused_pdf;
        float3 brdf = evaluate_brdf(cur.albedo, cur.roughness, cur.metallic, cur.hit_normal, view_dir, nd, unused_pdf, 1.0f);
        throughput *= brdf / pdf;

        prev_brdf_pdf = pdf;
        prev_normal   = cur.hit_normal;

        float ofs = compute_ray_offset(cur.hit_position);
        InlineHit next;
        if (!inline_trace_hit(cur.hit_position + cur.geometric_normal * ofs, nd, 1000.0f, next))
        {
            // sky escape with the same mis weight as the original suffix trace
            float w = power_heuristic(prev_brdf_pdf, sky_nee_pdf_at(nd, prev_normal));
            result += throughput * sample_sky(nd) * w;
            break;
        }

        // emissive triangle hit via brdf bounce, collected single strategy
        result += throughput * next.emission;
        // suffix vertices past the first use the full brdf
        result += throughput * direct_lighting_at_vertex(
            next.hit_position, next.hit_normal, next.geometric_normal,
            -nd, next.albedo, next.roughness, next.metallic, 1.0f, seed);

        cur      = next;
        view_dir = -nd;
    }

    return result;
}

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

    // sky samples are already handled by the reconnection shift with a constant jacobian
    if (is_sky_sample(src))
        return result;

    // only replay paths reconnection cannot carry, specular prefix (no rc) or near mirror primary
    // rough to rough samples with a valid rc that failed reconnection are left to fail so cost stays bounded
    float rc_min_roughness = get_restir_rc_min_roughness();
    bool specular_prefix   = !has_reconnection(src);
    bool near_mirror       = (src_roughness < rc_min_roughness) || (dst_roughness < rc_min_roughness);
    if (!specular_prefix && !near_mirror)
        return result;

    // replay xi at the stored seed, captured before the original xi was consumed
    uint   replay_seed = src.seed_path;
    float2 xi          = random_float2(replay_seed);

    // each side picks its own lobe blend so the pdf_src / pdf_dst jacobian is exact
    float src_specular_blend = restir_primary_specular_blend(src_roughness);
    float dst_specular_blend = restir_primary_specular_blend(dst_roughness);

    // src primary direction and pdf for the jacobian
    float  pdf_src;
    float3 dir_src = sample_brdf(src_albedo, src_roughness, src_metallic, src_normal, src_view_dir, xi, pdf_src, src_specular_blend);
    if (pdf_src < RESTIR_MIN_PDF || dot(dir_src, src_normal) <= 0.0f)
        return result;

    // dst replayed direction with the same xi
    float  pdf_dst;
    float3 dir_dst = sample_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, xi, pdf_dst, dst_specular_blend);
    if (pdf_dst < RESTIR_MIN_PDF || dot(dir_dst, dst_normal) <= 0.0f)
        return result;

    // trace dst primary bounce, first vertex of the replayed suffix
    float ofs = compute_ray_offset(dst_pos);
    InlineHit first_hit;
    if (!inline_trace_hit(dst_pos + dst_normal * ofs, dir_dst, 1000.0f, first_hit))
        return result;

    // dst primary brdf factor of f_dst
    float  pdf_eval;
    float3 brdf_cos = evaluate_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir_dst, pdf_eval, dst_specular_blend);
    if (all(brdf_cos <= 0.0f))
        return result;

    // jacobian is pdf_src / pdf_dst, the xi to direction map is volume preserving
    float jacobian = pdf_src / max(pdf_dst, RESTIR_MIN_PDF);
    if (jacobian < 1.0f / RESTIR_JACOBIAN_REJECT || jacobian > RESTIR_JACOBIAN_REJECT || isnan(jacobian) || isinf(jacobian))
        return result;

    // retrace the suffix from the replayed primary hit, suffix_seed continues the xi sequence
    // same bounce budget as the initial trace so the replayed integrand matches the original
    uint   suffix_seed     = replay_seed;
    float3 suffix_radiance = accumulate_replay_suffix(first_hit, -dir_dst, max(get_restir_max_path_length(), 2u) - 1u, suffix_seed);

    result.f_dst    = brdf_cos * suffix_radiance;
    result.jacobian = jacobian;
    result.ok       = true;
    return result;
}

// hybrid shift, tries the cheap reconnection shift first then falls back to random replay
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

// visibility ray from dst primary to rc, sky samples test reachability to the sky
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

// self shift evaluation, jacobian is 1, bypasses can_reconnect_at_dst since this is the source pixel
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

    float3 brdf_cos = eval_surface_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir, restir_primary_specular_blend(dst_roughness));
    if (all(brdf_cos <= 0.0f))
        return result;

    // dst incoming equals src incoming by construction so f_rc matches the original, no drift
    result.f_dst = brdf_cos * rc_outgoing_radiance(src, dir);
    result.ok    = true;
    return result;
}

// scalar target of f(y) at dst via reconnection shift, for cross pixel reuse
// use target_pdf_self at the source pixel so invalid rc samples keep a non zero target there
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

    return target_scalar(r.f_dst);
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

    return target_scalar(r.f_dst);
}

// inline ray traced shadow ray, returns true if the segment is unoccluded
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

// shades the reservoir sample at the current pixel via self shift
// returns diffuse only gi f(y) * W demodulated by primary albedo, debug view emits raw f(y) * W
float3 shade_reservoir_path(Reservoir r, float3 dst_pos, float3 dst_normal, float3 dst_view_dir, float3 dst_albedo, float dst_roughness, float dst_metallic)
{
    if (r.M <= 0.0f || r.W <= 0.0f)
        return float3(0, 0, 0);

    ShiftResult shift = self_shift_evaluate(r.sample, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (!shift.ok)
        return float3(0, 0, 0);

    if (!trace_shift_visibility(r.sample, dst_pos, dst_normal))
        return float3(0, 0, 0);

    // debug view, emit the raw estimator so the post chain can be isolated
    if (is_restir_pt_debug())
        return shift.f_dst * r.W;

    // diffuse albedo demodulation, the stored gi is albedo proportional so this yields clean
    // irradiance, the 0.1 floor bounds the divide on near black surfaces
    float3 demod = max(dst_albedo, 0.1f);
    float3 gi    = (shift.f_dst * r.W) / demod;

    // soft firefly ceiling for a stuck reservoir, preserves chromaticity instead of hard clipping
    return soft_saturate_radiance(gi, get_restir_w_clamp() * 0.05f);
}

#endif // SPARTAN_RESTIR_RESERVOIR
