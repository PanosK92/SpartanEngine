/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= includes =========
#include "common.hlsl"
//====================

// ==============================================================================================
// gran turismo 7 tone mapper implementation
// port of the c++ siggraph 2025 presentation code
// ==============================================================================================

// ----------------------------------------------------------------------------------------------
// theory: "physical" units
// most tone mappers (reinhard, aces) operate on "normalized" data (0.0 to 1.0).
// the gt7 mapper operates on "physical" data. it treats the input pixel value as a specific
// light intensity in nits (cd/m^2).
//
// - input 1.0 = 100 nits (reference)
// - input 2.5 = 250 nits (paper white)
// - input 100.0 = 10,000 nits (max hdr sun)
// ----------------------------------------------------------------------------------------------

// defines the sdr reference white level.
// in gt7, they target 250 nits for "paper white" (the brightness of a white ui element).
static const float gt7_sdr_paper_white = 250.0f;

// the "unit definition".
// this defines that a pixel value of 1.0f in the frame buffer equals 100 nits.
// if your scene looks dark, it's because your lighting intensity is too low for this standard!
static const float gt7_ref_luminance = 100.0f;

// helper: converts engine units (1.0) -> real nits (100)
float gt7_fb_to_nits(float fb_value)
{
    return fb_value * gt7_ref_luminance;
}

// helper: converts real nits (100) -> engine units (1.0)
float gt7_nits_to_fb(float physical)
{
    return physical / gt7_ref_luminance;
}

// standard smoothstep (hermite interpolation)
// used for blending transitions between linear and shoulder sections of the curve.
float gt7_smooth_step(float x, float edge0, float edge1)
{
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// ----------------------------------------------------------------------------------------------
// color space helpers: st-2084 (pq)
// the ictcp color space relies on the pq (perceptual quantizer) curve instead of gamma.
// pq is designed to match human vision sensitivity to brightness, which is crucial for hdr.
// ----------------------------------------------------------------------------------------------

// inverse eotf: converts linear light (nits) -> perceptual signal (0-1)
float gt7_inverse_eotf(float v)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    const float pqc = 10000.0f; // pq is defined up to 10,000 nits

    float y = gt7_fb_to_nits(v) / pqc;
    float ym = pow(max(y, 0.0f), m1);
    
    return exp2(m2 * (log2(c1 + c2 * ym) - log2(1.0f + c3 * ym)));
}

// eotf: converts perceptual signal (0-1) -> linear light (nits)
float gt7_eotf(float n)
{
    const float m1 = 0.1593017578125f;
    const float m2 = 78.84375f;
    const float c1 = 0.8359375f;
    const float c2 = 18.8515625f;
    const float c3 = 18.6875f;
    const float pqc = 10000.0f;

    float np = pow(max(n, 0.0f), 1.0f / m2);
    float l = max(np - c1, 0.0f);
    l = l / (c2 - c3 * np);
    l = pow(l, 1.0f / m1);

    return gt7_nits_to_fb(l * pqc);
}

// ----------------------------------------------------------------------------------------------
// color space: ictcp (rec.2020)
// why use this instead of rgb?
// 1. rgb tone mapping causes hue shifts (red turns pink/white).
// 2. ictcp strictly separates intensity (i) from color/chroma (ct, cp).
// 3. this allows us to compress the brightness without changing the "red-ness".
// ----------------------------------------------------------------------------------------------

void gt7_rgb_to_ictcp(float3 rgb, out float3 ictcp)
{
    // rgb to lms (cone response)
    float l = (rgb.r * 1688.0f + rgb.g * 2146.0f + rgb.b * 262.0f) / 4096.0f;
    float m = (rgb.r * 683.0f + rgb.g * 2951.0f + rgb.b * 462.0f) / 4096.0f;
    float s = (rgb.r * 99.0f + rgb.g * 309.0f + rgb.b * 3688.0f) / 4096.0f;

    // apply pq non-linearity
    float lpq = gt7_inverse_eotf(l);
    float mpq = gt7_inverse_eotf(m);
    float spq = gt7_inverse_eotf(s);

    // lms to ictcp
    ictcp.x = (2048.0f * lpq + 2048.0f * mpq) / 4096.0f; // intensity (i)
    ictcp.y = (6610.0f * lpq - 13613.0f * mpq + 7003.0f * spq) / 4096.0f; // chroma (ct)
    ictcp.z = (17933.0f * lpq - 17390.0f * mpq - 543.0f * spq) / 4096.0f; // chroma (cp)
}

void gt7_ictcp_to_rgb(float3 ictcp, out float3 rgb)
{
    // inverse ictcp to lms
    float l = ictcp.x + 0.00860904f * ictcp.y + 0.11103f * ictcp.z;
    float m = ictcp.x - 0.00860904f * ictcp.y - 0.11103f * ictcp.z;
    float s = ictcp.x + 0.560031f * ictcp.y - 0.320627f * ictcp.z;

    // inverse pq
    float l_lin = gt7_eotf(l);
    float m_lin = gt7_eotf(m);
    float s_lin = gt7_eotf(s);

    // lms to linear rgb
    rgb.r = max(3.43661f * l_lin - 2.50645f * m_lin + 0.0698454f * s_lin, 0.0f);
    rgb.g = max(-0.79133f * l_lin + 1.9836f * m_lin - 0.192271f * s_lin, 0.0f);
    rgb.b = max(-0.0259499f * l_lin - 0.0989137f * m_lin + 1.12486f * s_lin, 0.0f);
}

// ----------------------------------------------------------------------------------------------
// the curve
// a custom s-curve designed to behave like the human eye.
// - toe: shadows are compressed slightly (gamma-like).
// - linear: mid-tones are untouched (contrast remains faithful).
// - shoulder: highlights are compressed smoothly to fit the specific display (1000/4000 nits).
// ----------------------------------------------------------------------------------------------
float gt7_evaluate_curve(float x, float peak_intensity, float mid_point, float toe_strength)
{
    const float alpha = 0.25f;
    const float linear_section = 0.538f;

    // pre-compute constants for the exponential shoulder (mathematical fit)
    float k = (linear_section - 1.0f) / (alpha - 1.0f);
    float ka = peak_intensity * linear_section + peak_intensity * k;
    float kb = -peak_intensity * k * exp(linear_section / k);
    float kc = -1.0f / (k * peak_intensity);

    if (x < 0.0f)
        return 0.0f;

    float weight_linear = gt7_smooth_step(x, 0.0f, mid_point);
    float weight_toe = 1.0f - weight_linear;

    // the shoulder (highlights)
    float shoulder = ka + kb * exp(x * kc);

    // the toe (shadows/mids)
    if (x < linear_section * peak_intensity)
    {
        float toe_mapped = mid_point * pow(max(x / mid_point, 0.0001f), toe_strength);
        return weight_toe * toe_mapped + weight_linear * x;
    }

    return shoulder;
}

// ----------------------------------------------------------------------------------------------
// main algorithm
// ----------------------------------------------------------------------------------------------
float3 gran_turismo_7(float3 rgb, float max_display_nits, bool is_hdr)
{
    // step a: target determination
    // if hdr, we target the actual tv capabilities (e.g. 1000 nits).
    // if sdr, we target the internal "paper white" standard (250 nits).
    float target_nits = is_hdr ? max_display_nits : gt7_sdr_paper_white;
    float fb_target = gt7_nits_to_fb(target_nits);

    // curve tunings (derived from gt sport/gt7 data)
    float mid_point = 0.444f;
    float toe_strength = 1.280f;

    // step b: analysis
    // convert current pixel to ictcp to understand its true brightness and color.
    float3 ucs;
    gt7_rgb_to_ictcp(rgb, ucs);

    // step c: path 1 - "skewed" rgb tone mapping
    // apply the tone curve to r, g, and b individually.
    // pro: preserves saturation in highlights.
    // con: shifts hues (blue becomes magenta, red becomes yellow/white).
    float3 skewed_rgb;
    skewed_rgb.x = gt7_evaluate_curve(rgb.x, fb_target, mid_point, toe_strength);
    skewed_rgb.y = gt7_evaluate_curve(rgb.y, fb_target, mid_point, toe_strength);
    skewed_rgb.z = gt7_evaluate_curve(rgb.z, fb_target, mid_point, toe_strength);

    // get the ictcp version of this "skewed" result for later blending
    float3 skewed_ucs;
    gt7_rgb_to_ictcp(skewed_rgb, skewed_ucs);

    // step d: path 2 - hue preserving tone mapping
    // calculate how bright the pixel *should* be according to the curve.
    float3 target_rgb = float3(fb_target, fb_target, fb_target);
    float3 target_ucs;
    gt7_rgb_to_ictcp(target_rgb, target_ucs);
    float ucs_target_lum = target_ucs.x; // the intensity of white at max nits

    // calculate a scaling factor for the chroma (color) based on brightness.
    // as we get closer to the peak brightness, we fade out the color.
    float fade_start = 0.98f;
    float fade_end = 1.16f;
    float chroma_scale = 1.0f - gt7_smooth_step(ucs.x / ucs_target_lum, fade_start, fade_end);

    // create a new color using:
    // 1. luminance (i) from the "skewed" rgb calculation (to match contrast).
    // 2. chroma (ct, cp) from the original pixel, scaled down.
    // this effectively changes brightness without changing the hue.
    float3 scaled_ucs = float3(skewed_ucs.x, ucs.y * chroma_scale, ucs.z * chroma_scale);

    float3 scaled_rgb;
    gt7_ictcp_to_rgb(scaled_ucs, scaled_rgb);

    // step e: the hybrid blend
    // we blend path 1 (skewed) and path 2 (scaled).
    // 60% scaled (correct hue) / 40% skewed (saturated highlights).
    // this blend prevents highlights from looking *too* pale/desaturated (path 2)
    // while preventing the "skittles" hue-shift effect (path 1).
    float blend_ratio = 0.6f;
    float3 out_rgb = lerp(skewed_rgb, scaled_rgb, blend_ratio);
    
    // safety clamp to ensure we never exceed the display's capabilities
    out_rgb = min(out_rgb, fb_target);

    // step f: sdr correction
    // if outputting to sdr, the rest of the pipeline expects 0.0-1.0.
    // since our calculations were in "physical" units (reaching 2.5 for 250 nits),
    // we must scale it back down so that 250 nits becomes 1.0.
    if (!is_hdr)
    {
        float sdr_correction = 1.0f / gt7_nits_to_fb(gt7_sdr_paper_white);
        out_rgb *= sdr_correction;
    }
    
    return out_rgb;
}

// ==============================================================================================
// standard tone mappers
// ==============================================================================================

float3 aces(float3 color)
{
    // baking lab by mjp and david neubelt
    // srgb => xyz => d65_2_d60 => ap1 => rrt_sat
    static const float3x3 aces_mat_input =
    {
        { 0.59719, 0.35458, 0.04823 },
        { 0.07600, 0.90834, 0.01566 },
        { 0.02840, 0.13383, 0.83777 }
    };
    color = mul(aces_mat_input, color);
    
    // rrtandodtfit
    float3 a = color * (color + 0.0245786f) - 0.000090537f;
    float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
    color = a / b;

    // odt_sat => xyz => d60_2_d65 => srgb
    static const float3x3 aces_mat_output =
    {
        { 1.60475, -0.53108, -0.07367 },
        { -0.10208, 1.10813, -0.00605 },
        { -0.00327, -0.07276, 1.07602 }
    };
    color = mul(aces_mat_output, color);

    return saturate(color);
}

float3 agx(float3 color)
{
    static const float3x3 agx_mat_inset =
    {
        { 0.8566271533159880, 0.1373185106779920, 0.1118982129999500 },
        { 0.0951212405381588, 0.7612419900249430, 0.0767997842235547 },
        { 0.0482516061458523, 0.1014394992970650, 0.8113020027764950 }
    };

    static const float3x3 agx_mat_outset =
    {
        { 1.1271467782272380, -0.1468813165635330, -0.1255038609319300 },
        { -0.0496500000000000, 1.1084784877776500, -0.0524964871144260 },
        { -0.0774967775043101, -0.1468813165635330, 1.2244001486462500 }
    };

    const float min_ev = -10.0f;
    const float max_ev = 6.5f;

    color = mul(agx_mat_inset, color);
    color = max(color, 1e-10f); // avoid log(0) and negative values
    color = log2(color);
    color = (color - min_ev) / (max_ev - min_ev);
    color = saturate(color);

    // s-curve approximation
    float3 x = color;
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    color = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;

    color = mul(agx_mat_outset, color);

    return saturate(color);
}

float3 reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 aces_nautilus(float3 c)
{
    // nautilus fit of aces by nolram
    float a = 2.51f;
    float b = 0.03f;
    float y = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((c * (a * c + b)) / (c * (y * c + d) + e), 0.0, 1.0);
}

// ==============================================================================================
// helpers
// ==============================================================================================

// apply pq curve (st.2084)
// input: linear nits normalized to 10,000 (0.0 = 0 nits, 1.0 = 10,000 nits)
// note: this function handles color space conversion (709->2020) and curve encoding.
// it DOES NOT perform scaling (exposure/white point). that must be done before calling.
float3 linear_to_hdr10(float3 color)
{
    // convert Rec.709 (similar to srgb) to Rec.2020 color space
    static const float3x3 from709to2020 =
    {
        { 0.6274040f, 0.3292820f, 0.0433136f },
        { 0.0690970f, 0.9195400f, 0.0113612f },
        { 0.0163916f, 0.0880132f, 0.8955950f }
    };
    color = mul(from709to2020, color);

    // apply ST.2084 (PQ curve) for HDR10 standard
    static const float m1 = 2610.0 / 4096.0 / 4;
    static const float m2 = 2523.0 / 4096.0 * 128;
    static const float c1 = 3424.0 / 4096.0;
    static const float c2 = 2413.0 / 4096.0 * 32;
    static const float c3 = 2392.0 / 4096.0 * 32;
    float3 cp = pow(abs(color), m1);
    color = pow((c1 + c2 * cp) / (1 + c3 * cp), m2);

    return color;
}

// ==============================================================================================
// main
// ==============================================================================================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get input data
    float3 f3_value                = pass_get_f3_value();
    uint tone_mapping              = (uint)f3_value.x;
    float is_auto_exposure_enabled = f3_value.y >= 0.0f;
    float4 color                   = tex[thread_id.xy];

    // apply exposure (camera and auto-exposure)
    float exposure  = is_auto_exposure_enabled ? tex2.Load(int3(0, 0, 0)).r : 1.0f;
    color.rgb      *= buffer_frame.camera_exposure * exposure;
    
    // check hdr state
    bool is_hdr    = buffer_frame.hdr_enabled != 0.0f;
    float max_nits = buffer_frame.hdr_max_nits;

    switch (tone_mapping)
    {
        case 0: // aces
            color.rgb = aces(color.rgb);
            break;
        case 1: // agx
            color.rgb = agx(color.rgb);
            break;
        case 2: // reinhard
            color.rgb = reinhard(color.rgb);
            break;
        case 3: // aces nautilus
            color.rgb = aces_nautilus(color.rgb);
            break;
        case 4: // gran turismo 7
            // gt7 handles both sdr and hdr scaling internally
            color.rgb = gran_turismo_7(color.rgb, max_nits, is_hdr);
            break;
        default:
            // do nothing
            break;
    }

    if (is_hdr)
    {
        // 1. normalize to pq range (0.0 - 1.0 where 1.0 = 10,000 nits)
        const float pq_max_nits = 10000.0f;

        if (tone_mapping != 4) // aces, agx, reinhard, etc.
        {
            // these mappers output 0.0 to 1.0. 
            // we map 1.0 -> max display nits -> normalize to pq 10k
            color.rgb = (color.rgb * max_nits) / pq_max_nits;
        }
        else // gran turismo 7
        {
            // gt7 outputs in framebuffer units (1.0 = 100 nits)
            // we convert to nits -> normalize to pq 10k
            color.rgb = (color.rgb * 100.0f) / pq_max_nits;
        }

        // 2. encode (color space conversion + pq curve)
        color.rgb = linear_to_hdr10(color.rgb);
    }
    else
    {
        // sdr gamma correction
        // gt7 output for sdr is already normalized 0-1, so it fits right in here
        color.rgb = linear_to_srgb(color.rgb);
    }

    // at this point there is not reason to store an alpha value
    color.a = 1.0f;

    tex_uav[thread_id.xy] = color;
}
