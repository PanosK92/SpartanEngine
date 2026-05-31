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
// values are hardcoded so path tracing just works, lin 2022 fig 5/8 and §6 recommendations
// drove the picks, the m_cap ramps from a moving baseline up to the static target as the
// camera holds still so dynamic scenes stay responsive while still frames keep converging
static const uint  RESTIR_MAX_PATH_LENGTH    = 5;
static const uint  RESTIR_M_CAP_MIN          = 32;
static const uint  RESTIR_M_CAP_MAX          = 256;
static const uint  RESTIR_SPATIAL_SAMPLES    = 8;

// hardcoded knobs
//   m_cap          ramps from 32 (moving) to 256 (static, paper-typical for static pt), the
//                  validity gate kills disocclusions before they accumulate stale samples
//   path length    5 bounces is enough for plausible multi-bounce gi without runaway cost
//   light cands    8 nee samples covers the dominant lights, balance mis handles the rest
//   initial cands  4 brdf bounces per pixel so the indirect strategy is not starved by the
//                  light nee branch, temporal + spatial reuse then builds the pool over time
//   rc_min_rough   0.3 keeps glossy reconnection bias bounded, near mirrors fall back to replay
//   w_clamp        single sample W cap, sized for hdr scenes (sun-lit interiors hit several
//                  thousand nits on bright bounces, the previous fixed cap clipped them and
//                  systematically darkened indirect lighting), exposed as r.restir_pt_w_clamp
//                  so the user can trade firefly safety for highlight energy without a
//                  recompile, sky band derives from this via RESTIR_SKY_W_CLAMP_FACTOR so
//                  one knob controls the whole hdr range
//   validation     8 frames matches lin 2022 recommendation, kills stale samples on light change
float get_restir_m_cap()
{
    float t    = float(buffer_frame.time) - buffer_frame.camera_last_movement_time;
    float ramp = saturate((t - 0.5f) / 1.0f);
    return lerp(float(RESTIR_M_CAP_MIN), float(RESTIR_M_CAP_MAX), ramp);
}
uint  get_restir_max_path_length()     { return RESTIR_MAX_PATH_LENGTH; }
// initial ris pool sizes, lin 2022 typically runs 8-32 of each strategy, the higher counts
// lift per frame quality so the temporal pool converges in fewer frames and reduces the
// denoiser's job, the cost roughly doubles at the initial trace pass which is far cheaper
// than letting the denoiser smooth out the variance for the user
uint  get_restir_light_candidates()    { return 16u; }
uint  get_restir_initial_candidates()  { return 8u; }
// emissive triangle nee candidates per pixel per frame, paper recommends a comparable count
// to the analytical nee budget, the cpu only populates the buffer when restir is on and the
// shader skips this strategy when the pool count is zero so the loop iterations are free
uint  get_restir_emtri_candidates()    { return 8u; }
// rc roughness floor, the reconnection shift requires the rc vertex to be rough enough that
// a lambert only nee approximation captures most of its energy and the rc bsdf reuse stays
// bounded across destinations, lowering from 0.3 to 0.2 expands the applicable surface band
// by ~30% which means fewer pixels fall back to the lossier random replay shift, the missing
// specular lobe energy at rc grows but stays bounded by the new floor and bounded further by
// the suffix radiance trace which factors out the rc bsdf via rc_outgoing_dir
float get_restir_rc_min_roughness()    { return 0.2f; }
// single-sample contribution cap from cb so the user can trade firefly safety for highlight
// energy without recompiling shaders, clamped on the cpu side to keep the floor sensible
float get_restir_w_clamp()             { return buffer_frame.restir_pt_w_clamp; }
uint  get_restir_validation_period()   { return 8u; }
// tightened depth and normal gates for spatial reservoir reuse and the temporal validity
// check, the previous 0.05 / 0.75 (~41 deg) values let two facing crevice walls share each
// other's reservoirs, the indirect lighting was averaged across the crevice and the
// occluded contact lost contrast, 0.03 / 0.9 (~26 deg) keeps reuse aggressive on continuous
// surfaces while rejecting micro features that carry the contact shadow signal
static const float RESTIR_DEPTH_THRESHOLD    = 0.03f;
static const float RESTIR_NORMAL_THRESHOLD   = 0.9f;
// no soft decay, the validity gate already hard rejects on disocclusion and the m cap above
// is now low enough that we adapt within a fraction of a second on lighting changes
static const float RESTIR_TEMPORAL_DECAY     = 1.0f;
static const float RESTIR_RAY_T_MIN          = 0.001f;

// sky / environment
// sun disk radiance can far exceed any sane clamp, so this only catches numerical blowups,
// the sky W clamp is derived from the surface w clamp by a fixed 10x multiplier (sun radiance
// is typically an order of magnitude brighter than any surface bounce signal) so the same
// hardcoded surface w_clamp scales the whole hdr range proportionally
static const float RESTIR_SKY_RADIANCE_CLAMP_FACTOR = 4.0f;  // multiplier over surface w clamp
static const float RESTIR_SKY_W_CLAMP_FACTOR        = 10.0f; // multiplier over surface w clamp
static const float RESTIR_SKY_DISTANCE              = 1e10f;

// reconnection criteria (lin 2022 hybrid shift, reconnection leg)
// rc roughness floor lives in get_restir_rc_min_roughness() so it is co-located with the
// other path tracer knobs, the distance and cos floors below are absolute safety guards
static const float RESTIR_RC_MIN_DISTANCE    = 0.1f;
static const float RESTIR_RC_COS_FRONT       = 0.05f;
// large jacobians signal a near-degenerate shift (grazing or near-coincident reconnection),
// the shift is rejected as failed rather than clamped so a degenerate connection contributes
// nothing instead of a squashed value
//
// this threshold is the dominant structural firefly source: the jacobian appears once in the
// contribution (w_j = m_j * target * W_j * jacobian) but is intentionally omitted from the mis
// denominator, that omission is only harmless when the jacobian is near 1, a shift with jacobian
// ~50 is both a direct 50x amplifier of a reused sample AND under-normalized in the mis weight,
// so it does NOT average out with more samples or higher resolution (the amplification is purely
// geometric, a function of the reconnection geometry not the pixel count) and it then propagates
// through spatial reuse as a travelling blotch, valid reconnections between neighboring pixels or
// across frames have a jacobian very close to 1, so this is set back to 8 to reject the unstable
// tail (lin 2022 keeps all valid shifts, practical implementations reject the numerically
// unstable ones), lower it further toward ~4 if any resolution-independent blotches remain
static const float RESTIR_JACOBIAN_REJECT    = 8.0f;

// brdf / numerics
static const float RESTIR_MIN_PDF            = 1e-6f;

// nee
static const float MIN_AREA_LIGHT_SOLID_ANGLE = 1e-4f;

// urena, fajardo, king 2013 spherical rectangle solid-angle sampling
// samples uniformly in the solid angle subtended by a rectangle as seen from origin, replaces
// the area-domain "sample uniformly on the rectangle and project to solid angle" approach
// which has high variance for oblique angles where most of the area is grazing
// rect_origin is the corner with smallest local coords, ex / ey are the edge vectors so the
// rectangle is parametrized as { rect_origin + s*ex + t*ey | s,t in [0,1] }
// returns the sampled position on the rectangle and the solid angle subtended (so the pdf in
// solid-angle is 1 / solid_angle), the function is robust to the shading point being on the
// negative side of the rectangle's plane (it flips the local frame internally)
// lives here (rather than in restir_pt.hlsl) so the replay shift's inline next event
// estimation can call it from compute shader contexts that do not include restir_pt.hlsl
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
static const uint PATH_FLAG_HAS_RC   = 1 << 1;  // reconnection vertex is valid for reconnection shift
static const uint PATH_FLAG_SPECULAR = 1 << 2;  // diagnostic: primary surface is specular-leaning
static const uint PATH_FLAG_NEE      = 1 << 3;  // candidate came from light nee strategy, used for gris balance mis at the initial ris pool

// a path sample stores the suffix of a path beginning at the current pixel's primary hit
// paper-faithful split of the radiance leaving rc toward the source primary (lin 2022 §5):
//
//   L_at_rc = L_nee + f_rc(in_at_rc, rc_outgoing_dir) * L_post
//
// where L_nee is the direct lighting collected by nee at rc (the rc brdf used during nee
// collection is the lambert lobe only, so L_nee is view-independent w.r.t. the rc-incoming
// direction and stays exactly correct when the path is reused at a different dst primary;
// the rc roughness gate at >= 0.3 bounds the dropped specular-lobe energy at this vertex),
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
    // sky samples always use a higher clamp because the sun disk radiance can far exceed
    // the surface band, both bands derive from the single surface w_clamp via
    // RESTIR_SKY_W_CLAMP_FACTOR so one constant scales the whole hdr range
    return is_sky_sample(s) ? w * RESTIR_SKY_W_CLAMP_FACTOR : w;
}

// streaming ris sample insert (lin 2022 algorithm 1), the textbook formulation does not
// increment M on zero weight candidates because they did not actually contribute to the
// integral, inflating M skews pairwise mis denominators in temporal and spatial reuse and
// suppresses legitimately bright reused samples (the dominant cause of "dark indirect"
// when many initial brdf bounces miss into shadow), so we early-out without bumping M
// when the candidate is unusable
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

// caps M without touching weight_sum or W in the paper-form W = weight_sum / target_pdf
// the cap only affects future m_i = M_i / sum ratios used during stream combining, the
// unbiased estimator is preserved (scaling weight_sum here would darken the estimate by max_M/M)
void clamp_reservoir_M(inout Reservoir reservoir, float max_M)
{
    if (reservoir.M > max_M)
        reservoir.M = max_M;
}

// unified disocclusion gate used by both the reservoir temporal pass and the denoiser
// temporal accumulator, transforms the current surface position through the previous
// view-projection and compares the expected previous-frame depth to the actual previous
// depth sampled at prev_uv, this correctly distinguishes a moving dynamic surface
// (matching prior depth at prev_uv) from a true disocclusion (background depth behind
// the previous occluder), which the prev-depth-vs-current-depth approximation conflates
// and which caused asymmetric ghosting between the two passes
//
// requires `prev_depth_tex` to be bound to gbuffer_depth_previous (the temporal pass
// already binds this on tex slot, the denoiser pass needs to bind it too)
//
// returns true when the history is consistent enough to reuse, fills `confidence` with a
// scalar [0,1] factor that the caller multiplies into per-pass blend weights
bool evaluate_disocclusion(
    Texture2D prev_depth_tex,
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

    float3 prev_normal       = get_normal(prev_uv);
    float  normal_similarity = dot(current_normal, prev_normal);
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

// scalar target function for ris and pairwise mis weights, l1/3 treats r,g,b equally so
// chromatic indirect light (red wall bouncing on white floor, cool sky bouncing on warm
// surfaces) is not biased toward green-yellow contributions the way bt.709 luminance would,
// this is the conservative choice from lin 2022 §4 / alg. 1 for an unbiased scalar target
float target_scalar(float3 f)
{
    return max(f.r + f.g + f.b, 0.0f) * (1.0f / 3.0f);
}

// smooth scalar saturator for the reservoir W cap, pass through below c, asymptote at 2c
// above, the c0 derivative is continuous at c (slope 1 from below, slope 1 from above so
// the transition is smooth, slope falls to 0 at infinity), removes the step a hard min
// creates at the clamp boundary which produced flicker on pixels whose natural W bounced
// across the threshold from one frame to the next, max effective W rises from c to 2c so
// the firefly ceiling roughly doubles but in exchange the contribution is a smooth
// monotonic function of input W everywhere; same mathematical form as
// soft_saturate_radiance for consistency with the existing sky compressor
float soft_clamp_w(float w, float c)
{
    if (c <= 0.0f)
        return 0.0f;
    if (w <= c)
        return w;
    return c + (w - c) / (1.0f + (w - c) / c);
}

// soft luminance compressor, preserves chromaticity (rescales all channels by the same
// factor) and approaches `threshold` asymptotically from above instead of clipping hard
// at the threshold, this keeps glossy highlights and bright skydomes contributing energy
// in the long tail without becoming variance hotspots for the denoiser
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
    // sky radiance ceiling tracks the w clamp via RESTIR_SKY_RADIANCE_CLAMP_FACTOR so the
    // sun band scales with the same user-facing knob as the surface band, the soft form
    // preserves chromaticity better than a hard luminance scale and prevents fireflies in
    // the sun cone from poisoning the variance estimate at the denoiser stage
    float threshold = get_restir_w_clamp() * RESTIR_SKY_RADIANCE_CLAMP_FACTOR;
    return soft_saturate_radiance(radiance, threshold);
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
// specular_blend is a continuous [0, 1] weight on the specular lobe, 0 collapses to pure
// burley diffuse and the matching cosine pdf, 1 keeps the full mixture, intermediate values
// fade the specular contribution smoothly so the lobe handoff to ray traced reflections has
// no visible step at the partition boundary (paper lin 2022 uses a similar smooth handoff)
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

    // grazing-view reject, the previous code clamped n_dot_v to 0.001 which let the
    // 1 / (4 * cos_v) factor in the ggx pdf and the smith G1 denominator inflate up to 1000x
    // at near tangential views, that inflation was a per-frame source of fireflies for the
    // brdf strategy whenever the camera looked along a near-horizontal surface, paper recipe
    // is to drop the sample below a cos threshold of about 0.05 (87 degrees from normal)
    if (n_dot_v <= 0.05f)
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

    if (specular_blend <= 0.0f)
    {
        // diffuse only path, pdf collapses to the cosine hemisphere pdf since sample_brdf
        // also routes to the cosine sampler when specular_blend is zero
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
    // the effective specular pick probability is spec_prob_full * specular_blend so the
    // mixture pdf fades continuously as the blend interpolates, matching the integrand fade
    float n_dot_v_abs = max(n_dot_v, 1e-3f);
    float g1_v_pdf    = 2.0f * n_dot_v_abs / (n_dot_v_abs + sqrt(a2 + (1.0f - a2) * n_dot_v_abs * n_dot_v_abs));
    float diffuse_pdf = n_dot_l / PI;
    float spec_pdf    = d_term * g1_v_pdf / (4.0f * n_dot_v_abs);
    float spec_prob_full = compute_spec_probability(albedo, roughness, metallic, n_dot_v);
    float spec_prob_eff  = saturate(spec_prob_full * specular_blend);
    pdf = (1.0f - spec_prob_eff) * diffuse_pdf + spec_prob_eff * spec_pdf;

    return diffuse_cos + specular_cos * specular_blend;
}

// samples a direction proportional to diffuse+(blend*specular) mixture, returns the sampled
// direction and the mixture pdf so the caller can compute brdf*cos / pdf
// specular_blend is a continuous [0, 1] weight on the specular lobe, 0 collapses to a pure
// cosine hemisphere sampler (matches evaluate_brdf's diffuse only branch), 1 keeps the full
// vndf mixture, intermediate values pick the diffuse lobe more often by the same blend
// factor used in evaluate_brdf's mixture pdf so the two stay numerically consistent
float3 sample_brdf(float3 albedo, float roughness, float metallic, float3 n, float3 v, float2 xi, out float pdf, float specular_blend)
{
    if (specular_blend <= 0.0f)
    {
        float  pdf_diffuse;
        float3 local_dir = sample_cosine_hemisphere(xi, pdf_diffuse);
        pdf = pdf_diffuse;
        return local_to_world(local_dir, n);
    }

    // grazing-view reject, evaluate_brdf rejects below the same cos threshold so producing a
    // sampled direction here when n_dot_v is tiny just wastes a ray and burns through the
    // candidate budget on a candidate that will get zero target_pdf downstream
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
// includes both diffuse and ggx specular, so glossy primaries (roughness above the lobe
// split threshold) receive proper indirect specular through reservoir reuse
// specular_blend is the continuous [0, 1] weight forwarded to evaluate_brdf, primary specular
// outside the restir band is then owned by the ray traced reflections pipeline (the two
// pipelines blend on the same factor so they partition the lobe space without a hard step)
float3 eval_surface_brdf_cos(float3 albedo, float roughness, float metallic, float3 normal, float3 view_dir, float3 dir, float specular_blend)
{
    float n_dot_l = dot(normal, dir);
    if (n_dot_l <= 0.0f)
        return float3(0, 0, 0);

    float unused_pdf;
    return evaluate_brdf(albedo, roughness, metallic, normal, view_dir, dir, unused_pdf, specular_blend);
}

// smooth lobe split between restir pt and rt reflections at the primary vertex
// returns the [0, 1] weight applied to the specular lobe inside restir, the rt reflections
// pipeline applies the complementary (1 - blend) weight on the same factor so the two
// pipelines partition the specular lobe with zero gap and no visible step across the
// transition band, the partition is:
//   roughness <= RESTIR_SPECULAR_HANDOFF_LO -> blend = 0, rt reflections owns the full
//     specular (near mirror, reservoir reuse is poor because the lobe direction changes
//     every pixel and spatial sharing breaks down)
//   roughness >= RESTIR_SPECULAR_HANDOFF_HI -> blend = 1, restir owns the full specular
//     (broad glossy, well captured by reservoir reuse, paper lin 2022 §5)
//   in between -> linear ramp, both pipelines emit a partial specular contribution that sums
//     to the full specular term so the lobe is conserved
// the thresholds must match the gate in ray_traced_reflections.hlsl
static const float RESTIR_SPECULAR_HANDOFF_LO = 0.3f;
static const float RESTIR_SPECULAR_HANDOFF_HI = 0.4f;
// restir owns DIFFUSE-only primary gi, every primary specular lobe is routed to the ray traced
// reflections / ssr pipeline (the user-chosen handoff taken to its logical end), reasons:
//   - a peaked specular target reused across pixels (temporal + 3x spatial) makes the unbiased
//     contribution weight w explode, this is the exploding-blotch firefly on glossy floors, and
//     no amount of clamping removes it cleanly because it is a variance problem in the reuse
//   - rt reflections trace the actual scene so they show the real reflection (the car on the
//     floor) with no reuse variance, which is exactly the result wanted on those surfaces
//   - diffuse-only gi is exactly albedo-proportional so the half-res demodulation / upsample is
//     exact and preserves surface detail instead of fighting a non-albedo specular term
// the indirect / suffix bounces past the primary still use the full bsdf (specular_blend = 1),
// only the primary vertex lobe is forced diffuse here
float restir_primary_specular_blend(float roughness)
{
    return 0.0f;
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
    // rc is a suffix vertex, the lobe split only applies at the primary so the full brdf is
    // always active at rc (specular_blend = 1)
    float3 f_rc      = eval_surface_brdf_cos(src.rc_albedo, src.rc_roughness, src.rc_metallic,
                                             src.rc_normal, view_at_rc, src.rc_outgoing_dir,
                                             1.0f);
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
// the paper formulation rebuilds the primary bounce at dst using the same xi that produced
// the src path and then re-traces the full suffix from dst's actual replayed hit, using the
// freshly traced suffix radiance as f_dst, the jacobian is the volume preserving ratio of
// primary brdf pdfs (xi-space change of variables), this leg is the paper's general shift for
// any path the reconnection leg cannot carry: near-mirror primaries (where reconnection gates
// off at the primary) and glossy interreflection paths whose first bounce is specular (no rc
// vertex), the retrace continues through the specular prefix with the full bsdf and effectively
// reconnects at the first rough vertex, the suffix retrace is capped at RESTIR_REPLAY_MAX_BOUNCES
// with russian roulette so cost stays bounded to the specular fraction of pixels
static const uint RESTIR_REPLAY_MAX_BOUNCES = 3u;

// forward declarations so the replay shift's inline retrace and visibility helpers can call
// into the segment visibility / shadow ray helpers which are defined later in this header
bool trace_shift_visibility(PathSample src, float3 dst_pos, float3 dst_normal);
bool trace_shadow_ray(float3 origin, float3 direction, float max_dist);

// minimal inline hit record used by the replay shift's suffix retrace, mirrors the fields
// of PathPayload from restir_pt.hlsl but lives here so the shift is self-contained and
// callable from compute shader contexts where the raygen PathPayload is unavailable
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

// compact replica of restir_pt.hlsl's closest hit shader, pulls per triangle material and
// geometry data via the bindless arrays from a successful inline RayQuery hit, normal mapping
// is intentionally skipped (geometric normal is used as the shading normal) so the function
// stays compact, the replay shift only fires for near mirror primaries which are a small
// fraction of pixels and the missing normal map perturbation at suffix vertices is a bounded
// approximation, all other material params (albedo, roughness, metallic, emission) match the
// full closest hit pipeline so the brdf evaluation and emission accumulation stay consistent
void inline_pull_hit_data(
    uint  instance_id,
    uint  instance_index,
    uint  primitive_index,
    float2 bary_xy,
    float3 ray_origin,
    float3 ray_dir,
    float  hit_t,
    float4x3 obj_to_world_4x3,
    out InlineHit hit_out)
{
    // instance_id is the user defined material index set per blas, instance_index is the
    // index of the instance in the tlas array, these are generally different, the closest
    // hit shader in restir_pt.hlsl uses InstanceID() for material lookup and InstanceIndex()
    // for geometry info lookup, this function must do the same so the two index spaces stay
    // consistent across the raygen and inline ray paths
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
    float2 uv0 = unpack_vertex_uv(pv0.uv);
    float2 uv1 = unpack_vertex_uv(pv1.uv);
    float2 uv2 = unpack_vertex_uv(pv2.uv);

    float3 normal_object = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);
    float2 texcoord      = uv0 * bary.x + uv1 * bary.y + uv2 * bary.z;

    // 4x3 form stores rows 0..2 as world space basis vectors, casting to 3x3 takes those
    // first 3 rows, mul(v, M) then computes v_obj.x * row0 + v_obj.y * row1 + v_obj.z * row2
    // which gives the world space transform, this matches the existing closest hit shader
    float3x3 obj_to_world_3x3 = (float3x3)obj_to_world_4x3;
    float3   normal_world     = normalize(mul(normal_object, obj_to_world_3x3));

    float3 hit_position = ray_origin + ray_dir * hit_t;
    if (geo.uv_world_space > 0.0f)
    {
        texcoord = compute_world_space_uv(hit_position, normal_world);
    }
    texcoord = texcoord * geo.uv_tiling + geo.uv_offset;
    if (geo.uv_rotation != 0.0f)
        texcoord = rotate_uv_90(texcoord, geo.uv_rotation);

    float3 edge1_world = mul(pv1.position - pv0.position, obj_to_world_3x3);
    float3 edge2_world = mul(pv2.position - pv0.position, obj_to_world_3x3);
    float3 geometric_normal = normalize(cross(edge1_world, edge2_world));
    if (dot(geometric_normal, ray_dir) > 0.0f)
        geometric_normal = -geometric_normal;
    if (dot(normal_world, geometric_normal) < 0.0f)
        normal_world = -normal_world;

    // mip 0 is acceptable because the replay shift is only used at near mirror primaries
    // where the suffix bounces are sparse and per pixel filtering quality is dominated by
    // the rest of the pipeline
    float mip_level = 0.0f;

    // bindless material textures are indexed by material_index + per-type offset, the same
    // formula the closest hit shader uses, instance_id == InstanceID() carries the material
    // index here
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

// inline ray cast + closest hit data fetch, returns false on miss so the caller knows the
// ray escaped to the sky (handled separately as a sky radiance contribution)
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

    inline_pull_hit_data(inst_id, inst_idx, prim_idx, bary_xy, origin, dir, hit_t, obj_to_w, hit_out);
    return true;
}

// minimal inline next event estimation against the analytical light pool, equivalent to the
// analytical loop in direct_lighting_at_vertex (restir_pt.hlsl) but self contained so it can
// run from compute shader contexts that do not include restir_pt.hlsl, env probe nee is
// intentionally skipped here because the suffix bounce loop catches sky on a miss anyway,
// and emissive triangle nee is skipped because hit.emission already accumulates emission
// from bounce-hit emissive triangles which is the same radiance the emtri nee would carry
// (with a worse pdf which would only matter for low variance optimization, not correctness)
float3 inline_replay_nee_analytical(
    float3 hit_pos,
    float3 hit_normal,
    float3 geom_normal,
    float3 view_dir,
    float3 albedo,
    float  roughness,
    float  metallic,
    float  specular_blend,
    inout uint seed)
{
    float3 total           = float3(0, 0, 0);
    uint   light_count     = (uint)buffer_frame.restir_pt_light_count;
    float  shading_offset  = compute_ray_offset(hit_pos);
    float3 ray_origin_nee  = hit_pos + geom_normal * shading_offset;

    for (uint light_idx = 0u; light_idx < light_count; light_idx++)
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
        float  light_pdf   = 1.0f;
        float  attenuation = 1.0f;

        if (is_directional)
        {
            light_dir  = -light.direction;
            light_dist = 1000.0f;
        }
        else if (is_area && light.area_width > 0.0f && light.area_height > 0.0f)
        {
            float3 light_normal = light.direction;
            float3 light_right, light_up;
            build_orthonormal_basis_fast(light_normal, light_right, light_up);

            float3 ex          = light_right * light.area_width;
            float3 ey          = light_up    * light.area_height;
            float3 rect_origin = light.position - 0.5f * ex - 0.5f * ey;

            float2 xi = random_float2(seed);
            float3 light_sample_pos;
            float  solid_angle;
            sample_spherical_rectangle(hit_pos, rect_origin, ex, ey, xi, light_sample_pos, solid_angle);

            if (solid_angle < MIN_AREA_LIGHT_SOLID_ANGLE)
                continue;

            float3 to_light = light_sample_pos - hit_pos;
            light_dist      = length(to_light);
            if (light_dist < 1e-3f)
                continue;
            light_dir = to_light / light_dist;

            float cos_light = dot(-light_dir, light_normal);
            if (cos_light <= 0.0f)
                continue;

            light_pdf = 1.0f / solid_angle;
        }
        else if (is_point || is_spot)
        {
            float3 to_light = light.position - hit_pos;
            light_dist      = length(to_light);
            light_dir       = to_light / light_dist;

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

        float n_dot_l = dot(hit_normal, light_dir);
        if (n_dot_l <= 0.0f)
            continue;

        if (!trace_shadow_ray(ray_origin_nee, light_dir, light_dist))
            continue;

        float  brdf_pdf;
        float3 brdf = evaluate_brdf(albedo, roughness, metallic, hit_normal, view_dir, light_dir, brdf_pdf, specular_blend);

        float3 Li = light_color * light.intensity * attenuation;
        total += brdf * Li / max(light_pdf, 1e-6f);
    }

    return total;
}

// inline suffix retrace from dst's replayed primary hit, mirrors trace_rc_suffix +
// accumulate_subpath_at_rc from restir_pt.hlsl but uses inline RayQuery + inline material
// fetch so the shift stays callable from compute shader contexts, returns the radiance
// leaving start_hit toward start_view_dir, max_bounces caps the depth so cost is bounded
float3 accumulate_replay_suffix(
    InlineHit start_hit,
    float3    start_view_dir,
    uint      max_bounces,
    inout uint seed)
{
    // first vertex emission + nee contribution at the replayed primary hit
    float3 result    = start_hit.emission;
    float3 nee_at_v0 = inline_replay_nee_analytical(
        start_hit.hit_position,
        start_hit.hit_normal,
        start_hit.geometric_normal,
        start_view_dir,
        start_hit.albedo,
        start_hit.roughness,
        start_hit.metallic,
        1.0f,
        seed);
    result += nee_at_v0;

    if (max_bounces < 2u)
        return result;

    InlineHit cur        = start_hit;
    float3    view_dir   = start_view_dir;
    float3    throughput = float3(1, 1, 1);

    for (uint b = 1u; b < max_bounces; b++)
    {
        // russian roulette from bounce 2 onwards, veach-style adjusted continuation
        if (b >= 2u)
        {
            float continuation_prob = clamp(luminance(throughput * cur.albedo), 0.25f, 0.95f);
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

        float ofs = compute_ray_offset(cur.hit_position);
        InlineHit next;
        if (!inline_trace_hit(cur.hit_position + cur.geometric_normal * ofs, nd, 1000.0f, next))
            break;

        // bounce hit emission + analytical nee at the new vertex
        result += throughput * next.emission;
        result += throughput * inline_replay_nee_analytical(
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

    // sky samples have no scene-space rc to align with, so the reconnection shift already
    // handles them correctly with a constant jacobian; replay would just duplicate that
    if (is_sky_sample(src))
        return result;

    // the hybrid shift always tries reconnection first, so reaching the replay leg means
    // reconnection could not carry this sample, the two cases that genuinely need the full
    // prefix + suffix retrace are:
    //   - specular prefix: the sample has no reconnection vertex because its first bounce hit a
    //     surface below the rc roughness floor (a glossy interreflection path), lin 2022
    //     random-replays the specular prefix and reconnects at the first rough vertex, the
    //     retrace below reconstructs that naturally by continuing the bounce loop with the full
    //     bsdf, these paths were previously dropped from all reuse which is the weak / noisy
    //     indirect specular the user observed
    //   - near mirror primary: src or dst is below the rc roughness floor so reconnection gates
    //     off at the primary itself and replay is the only reuse path
    // a rough -> rough sample that still has a valid rc but failed reconnection (a transient
    // jacobian or visibility reject) is left to fail rather than pay for a full retrace, the
    // reconnection leg owns those, so total replay cost stays bounded by the specular fraction
    // of pixels rather than every pixel
    float rc_min_roughness = get_restir_rc_min_roughness();
    bool specular_prefix   = !has_reconnection(src);
    bool near_mirror       = (src_roughness < rc_min_roughness) || (dst_roughness < rc_min_roughness);
    if (!specular_prefix && !near_mirror)
        return result;

    // replay xi at the stored seed; the seed is captured before the original xi was consumed
    uint   replay_seed = src.seed_path;
    float2 xi          = random_float2(replay_seed);

    // each side picks its own primary lobe blend based on its own roughness, the jacobian
    // pdf_src / pdf_dst then captures the change of variables exactly because each pdf reflects
    // the actual sampling distribution at that side
    float src_specular_blend = restir_primary_specular_blend(src_roughness);
    float dst_specular_blend = restir_primary_specular_blend(dst_roughness);

    // sample brdf at src to get src's primary direction and pdf for the jacobian
    float  pdf_src;
    float3 dir_src = sample_brdf(src_albedo, src_roughness, src_metallic, src_normal, src_view_dir, xi, pdf_src, src_specular_blend);
    if (pdf_src < RESTIR_MIN_PDF || dot(dir_src, src_normal) <= 0.0f)
        return result;

    // sample brdf at dst with the same xi for the replayed bounce
    float  pdf_dst;
    float3 dir_dst = sample_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, xi, pdf_dst, dst_specular_blend);
    if (pdf_dst < RESTIR_MIN_PDF || dot(dir_dst, dst_normal) <= 0.0f)
        return result;

    // trace dst's primary bounce inline and pull the full closest hit data, this is the
    // first vertex of the replayed suffix path, we no longer test proximity to src.rc, the
    // suffix is re traced from this dst specific hit so the proximity test is unnecessary
    float ofs = compute_ray_offset(dst_pos);
    InlineHit first_hit;
    if (!inline_trace_hit(dst_pos + dst_normal * ofs, dir_dst, 1000.0f, first_hit))
        return result;

    // brdf at dst toward the replayed direction, this is the primary brdf factor of the
    // shift's f_dst, the suffix radiance is multiplied by it to produce the integrand value
    float  pdf_eval;
    float3 brdf_cos = evaluate_brdf(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir_dst, pdf_eval, dst_specular_blend);
    if (all(brdf_cos <= 0.0f))
        return result;

    // jacobian for replay is pdf_src / pdf_dst, the xi -> direction map is volume preserving
    // in xi-space so the density change of variables in solid-angle is exactly this ratio,
    // f_dst stays in integrand form (brdf * cos * radiance) and the pdf factor goes through
    // the jacobian so the ris weight target/source remains in consistent units across shifts
    float jacobian = pdf_src / max(pdf_dst, RESTIR_MIN_PDF);
    if (jacobian <= 0.0f || jacobian > RESTIR_JACOBIAN_REJECT || isnan(jacobian) || isinf(jacobian))
        return result;

    // strict paper formulation, retrace the suffix from dst's replayed primary hit and use
    // the freshly accumulated radiance as the suffix term, bounded by RESTIR_REPLAY_MAX_BOUNCES
    // with russian roulette termination inside, replay_seed continues from where the primary
    // xi was consumed so the suffix xi sequence matches the source path's suffix structure
    uint   suffix_seed     = replay_seed;
    float3 suffix_radiance = accumulate_replay_suffix(first_hit, -dir_dst, RESTIR_REPLAY_MAX_BOUNCES, suffix_seed);

    result.f_dst    = brdf_cos * suffix_radiance;
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

    float3 brdf_cos = eval_surface_brdf_cos(dst_albedo, dst_roughness, dst_metallic, dst_normal, dst_view_dir, dir, restir_primary_specular_blend(dst_roughness));
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

// shades the reservoir's sample at the current pixel (self-shift, jacobian == 1)
// traces the visibility ray (sky samples verify sky reachability) and returns the diffuse-only
// gi f(y) * W demodulated by the primary albedo so the half-res gi texture stores albedo-free
// lighting, light_composition bilaterally upsamples it and re-modulates with the full-res albedo,
// the debug view emits the raw f(y) * W instead so the post chain can be isolated
float3 shade_reservoir_path(Reservoir r, float3 dst_pos, float3 dst_normal, float3 dst_view_dir, float3 dst_albedo, float dst_roughness, float dst_metallic)
{
    if (r.M <= 0.0f || r.W <= 0.0f)
        return float3(0, 0, 0);

    ShiftResult shift = self_shift_evaluate(r.sample, dst_pos, dst_normal, dst_view_dir, dst_albedo, dst_roughness, dst_metallic);
    if (!shift.ok)
        return float3(0, 0, 0);

    if (!trace_shift_visibility(r.sample, dst_pos, dst_normal))
        return float3(0, 0, 0);

    // debug view: emit the raw estimator f(y) * W so the post chain (demod, denoiser, upsample)
    // can be isolated from the resampling estimator itself
    if (is_restir_pt_debug())
        return shift.f_dst * r.W;

    // diffuse-albedo demodulation, restir now owns diffuse-only primary gi (all primary specular
    // is routed to ray traced reflections, see restir_primary_specular_blend) so the stored
    // signal is exactly albedo-proportional and dividing by the per-channel albedo yields clean
    // irradiance-like lighting, the half-res bilateral upsample then interpolates a smooth
    // albedo-free signal that light_composition re-modulates with the full-res albedo to restore
    // surface / texture detail, the 0.1 floor bounds the divide on near-black surfaces and stays
    // exact through re-modulation (a black tile has ~zero diffuse gi so it cannot blow up here)
    float3 demod = max(dst_albedo, 0.1f);
    float3 gi    = (shift.f_dst * r.W) / demod;

    // single firefly safety net at the contribution level, a soft ceiling that bounds the
    // brightness of a stuck reservoir (a dim diffuse path that found a bright emitter at low pdf
    // and then persists because the emitter stays visible through §6.4 validation), it preserves
    // chromaticity and asymptotes rather than hard-clipping so legitimate bright bounces still
    // contribute, this is the one radiance guard (the per-pass W clamp is the other), the 0.05
    // factor keeps persistent fireflies below visibility while leaving normal diffuse gi untouched
    return soft_saturate_radiance(gi, get_restir_w_clamp() * 0.05f);
}

#endif // SPARTAN_RESTIR_RESERVOIR
