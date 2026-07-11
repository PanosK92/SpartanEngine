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

//= includes =========
#include "common.hlsl"
//====================

// ==============================================================================================
// gran turismo 7 tone mapper implementation
// port of the c++ siggraph 2025 presentation code
// ==============================================================================================

// the input to this tonemapper is scene-linear where ~1.0 = white
// in gran turismo, 1.0 in linear frame-buffer space = REFERENCE_LUMINANCE cd/m^2 (100 nits)
// sdr paper white defines the target luminance for sdr tone mapping (250 nits)
static const float gt7_sdr_paper_white = 250.0f; // cd/m^2
static const float gt7_ref_luminance   = 100.0f; // cd/m^2 <-> 1.0f

static const float3x3 gt7_rec709_to_rec2020 =
{
    { 0.6274040f, 0.3292820f, 0.0433136f },
    { 0.0690970f, 0.9195400f, 0.0113612f },
    { 0.0163916f, 0.0880132f, 0.8955950f }
};

static const float3x3 gt7_rec2020_to_rec709 =
{
    { 1.6604910f, -0.5876411f, -0.0728499f },
    { -0.1245505f, 1.1328999f, -0.0083494f },
    { -0.0181508f, -0.1005789f, 1.1187297f }
};

// helper: scene-linear (1.0 = white) -> nits for pq math
float gt7_fb_to_nits(float fb_value)
{
    return fb_value * gt7_ref_luminance;
}

// helper: nits -> scene-linear
float gt7_nits_to_fb(float physical)
{
    return physical / gt7_ref_luminance;
}

float3 gt7_to_rec2020(float3 color)
{
    return mul(gt7_rec709_to_rec2020, color);
}

float3 gt7_to_rec709(float3 color)
{
    return mul(gt7_rec2020_to_rec709, color);
}

// standard smoothstep for blending
float gt7_smooth_step(float x, float edge0, float edge1)
{
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// ----------------------------------------------------------------------------------------------
// color space helpers: st-2084 (pq)
// ----------------------------------------------------------------------------------------------

// inverse eotf: linear light (nits) -> perceptual signal (0-1)
float gt7_inverse_eotf(float v)
{
    const float m1  = 0.1593017578125f;
    const float m2  = 78.84375f;
    const float c1  = 0.8359375f;
    const float c2  = 18.8515625f;
    const float c3  = 18.6875f;
    const float pqc = 10000.0f; // max pq nits

    float y = gt7_fb_to_nits(v) / pqc;
    float ym = pow(max(y, 0.0f), m1);
    
    return exp2(m2 * (log2(c1 + c2 * ym) - log2(1.0f + c3 * ym)));
}

// eotf: perceptual signal (0-1) -> linear light (nits)
float gt7_eotf(float n)
{
    const float m1  = 0.1593017578125f;
    const float m2  = 78.84375f;
    const float c1  = 0.8359375f;
    const float c2  = 18.8515625f;
    const float c3  = 18.6875f;
    const float pqc = 10000.0f;

    float np = pow(saturate(n), 1.0f / m2);
    float l = max(np - c1, 0.0f);
    l = l / (c2 - c3 * np);
    l = pow(l, 1.0f / m1);

    return gt7_nits_to_fb(l * pqc);
}

// ----------------------------------------------------------------------------------------------
// color space: ictcp (rec.2020)
// separates intensity (i) from chroma (ct, cp) to prevent hue shifts
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
    ictcp.x = (2048.0f * lpq + 2048.0f * mpq) / 4096.0f; // intensity
    ictcp.y = (6610.0f * lpq - 13613.0f * mpq + 7003.0f * spq) / 4096.0f; // chroma ct
    ictcp.z = (17933.0f * lpq - 17390.0f * mpq - 543.0f * spq) / 4096.0f; // chroma cp
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
// the s-curve
// ----------------------------------------------------------------------------------------------

// precomputed gt7 curve constants, built once per dispatch and reused per channel
struct gt7_curve
{
    float peak_intensity;
    float mid_point;
    float toe_strength;
    float ka;
    float kb;
    float kc;
    float shoulder_threshold;
};

gt7_curve gt7_make_curve(float peak_intensity, float mid_point, float toe_strength)
{
    const float alpha          = 0.25f;
    const float linear_section = 0.444f;

    float k = (linear_section - 1.0f) / (alpha - 1.0f);

    gt7_curve c;
    c.peak_intensity     = peak_intensity;
    c.mid_point          = mid_point;
    c.toe_strength       = toe_strength;
    c.ka                 = peak_intensity * linear_section + peak_intensity * k;
    c.kb                 = -peak_intensity * k * exp(linear_section / k);
    c.kc                 = -1.0f / (k * peak_intensity);
    c.shoulder_threshold = linear_section * peak_intensity;
    return c;
}

float gt7_evaluate_curve(float x, gt7_curve c)
{
    if (x < 0.0f)
    {
        return 0.0f;
    }

    // shoulder for highlights
    if (x >= c.shoulder_threshold)
    {
        return c.ka + c.kb * exp(x * c.kc);
    }

    // toe and linear blend for shadows and mids
    float weight_linear = gt7_smooth_step(x, 0.0f, c.mid_point);
    float weight_toe    = 1.0f - weight_linear;
    float toe_mapped    = c.mid_point * pow(max(x / c.mid_point, 0.0f), c.toe_strength);
    return weight_toe * toe_mapped + weight_linear * x;
}

// ----------------------------------------------------------------------------------------------
// main algorithm
// ----------------------------------------------------------------------------------------------
float3 gran_turismo_7(float3 rgb, float max_display_nits, bool is_hdr)
{
    float3 rgb_rec2020 = gt7_to_rec2020(rgb);

    // determine target peak luminance, the curve tunings assume sdr paper white = 250 nits
    // so peak below 250 is undefined per the polyphony digital reference
    float target_nits = is_hdr ? clamp(max_display_nits, 250.0f, 10000.0f) : gt7_sdr_paper_white;
    float fb_target   = gt7_nits_to_fb(target_nits);

    // official gt7 curve and blend tunings from polyphony digital
    gt7_curve curve         = gt7_make_curve(fb_target, 0.538f, 1.280f);
    const float blend_ratio = 0.6f;
    const float fade_start  = 0.98f;
    const float fade_end    = 1.16f;

    // pre-compute target luminance in ucs space
    float3 target_ucs;
    gt7_rgb_to_ictcp(float3(fb_target, fb_target, fb_target), target_ucs);
    float ucs_target_lum = target_ucs.x;

    // analyze brightness using ictcp
    float3 ucs;
    gt7_rgb_to_ictcp(rgb_rec2020, ucs);

    // path 1, per-channel mapping, preserves saturation but can shift hue
    float3 skewed_rgb = float3(
        gt7_evaluate_curve(rgb_rec2020.x, curve),
        gt7_evaluate_curve(rgb_rec2020.y, curve),
        gt7_evaluate_curve(rgb_rec2020.z, curve));

    float3 skewed_ucs;
    gt7_rgb_to_ictcp(skewed_rgb, skewed_ucs);

    // path 2, luminance mapping, preserves hue but desaturates highlights
    float chroma_scale = 1.0f - gt7_smooth_step(ucs.x / ucs_target_lum, fade_start, fade_end);
    float3 scaled_ucs  = float3(skewed_ucs.x, ucs.y * chroma_scale, ucs.z * chroma_scale);
    float3 scaled_rgb;
    gt7_ictcp_to_rgb(scaled_ucs, scaled_rgb);

    // blend paths, clamp to display peak, then apply sdr correction
    float3 out_rgb       = (1.0f - blend_ratio) * skewed_rgb + blend_ratio * scaled_rgb;
    float sdr_correction = is_hdr ? 1.0f : (1.0f / gt7_nits_to_fb(gt7_sdr_paper_white));
    out_rgb              = sdr_correction * min(out_rgb, fb_target);

    if (is_hdr)
    {
        return max(out_rgb, 0.0f);
    }

    return max(gt7_to_rec709(out_rgb), 0.0f);
}

// ==============================================================================================
// standard tone mappers
// ==============================================================================================

float3 aces(float3 color)
{
    // srgb => xyz => d65_2_d60 => ap1 => rrt_sat
    static const float3x3 aces_mat_input =
    {
        { 0.59719, 0.35458, 0.04823 },
        { 0.07600, 0.90834, 0.01566 },
        { 0.02840, 0.13383, 0.83777 }
    };
    color = mul(aces_mat_input, color);
    
    // rrt and odt fit
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

    // canonical agx log2 encoding range, places mid gray 0.18 at the reference position
    const float min_ev = -12.47393f;
    const float max_ev = 4.026069f;

    color = mul(agx_mat_inset, color);
    color = max(color, 1e-10f); // prevent log(0)
    color = log2(color);
    color = (color - min_ev) / (max_ev - min_ev);
    color = saturate(color);

    // s-curve approximation
    float3 x = color;
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    color = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;

    // agx look transform - restores saturation that log encoding removes
    // without this, the image appears washed out (blender applies this by default)
    float3 lw          = float3(0.2126f, 0.7152f, 0.0722f);
    float luma         = dot(color, lw);
    float3 offset      = color - luma;
    float saturation   = 1.35f; // base agx look saturation boost
    float contrast     = 1.10f; // subtle contrast enhancement
    float contrast_mid = 0.18f; // pivot point for contrast (mid-gray)
    color              = luma + offset * saturation;
    color              = contrast_mid + (color - contrast_mid) * contrast;
    color              = saturate(color);

    color = mul(agx_mat_outset, color);

    return saturate(color);
}

float3 reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 aces_nautilus(float3 c)
{
    // nautilus fit by nolram
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

// apply pq curve (st.2084) to rec.2020 linear values
// input: linear nits normalized to 10,000 (0.0 = 0 nits, 1.0 = 10,000 nits)
float3 linear_rec2020_to_hdr10(float3 color)
{
    // apply st.2084 (pq curve)
    static const float m1 = 2610.0 / 4096.0 / 4;
    static const float m2 = 2523.0 / 4096.0 * 128;
    static const float c1 = 3424.0 / 4096.0;
    static const float c2 = 2413.0 / 4096.0 * 32;
    static const float c3 = 2392.0 / 4096.0 * 32;
    float3 cp = pow(abs(color), m1);
    color = pow((c1 + c2 * cp) / (1 + c3 * cp), m2);

    return color;
}

float3 linear_rec709_to_hdr10(float3 color)
{
    return linear_rec2020_to_hdr10(gt7_to_rec2020(color));
}

// ==============================================================================================
// main
// ==============================================================================================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get input data
    float3 f3_value        = pass_get_f3_value();
    uint tone_mapping      = (uint)f3_value.x;
    bool is_auto_exposure  = f3_value.y > 0.0f;
    bool force_sdr         = f3_value.z > 0.0f;
    float4 color           = tex[thread_id.xy];

    // the scene buffer stays radiometric until the display path.
    // convert once here so exposure and tone mapping operate in photometric display space.
    color.rgb = radiometric_to_photometric(color.rgb);

    // apply exposure
    // auto exposure stores the resolved final scale, while manual mode falls back to the camera settings
    float exposure = is_auto_exposure ? tex2.Load(int3(0, 0, 0)).r : buffer_frame.camera_exposure;
    color.rgb     *= exposure;
    
    // check hdr state, 1 = hdr10 pq, 2 = scrgb linear (1.0 = 80 nits)
    bool is_hdr    = buffer_frame.hdr_enabled != 0.0f && !force_sdr;
    bool is_scrgb  = buffer_frame.hdr_enabled > 1.5f;
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
            color.rgb = gran_turismo_7(color.rgb, max_nits, is_hdr);
            break;
        default: // no tone mapping
            if (!is_hdr)
            {
                // simple clamp for sdr if no mapper is used
                color.rgb = saturate(color.rgb);
            }
            break;
    }

    if (is_hdr)
    {
        const float sdr_white_nits = buffer_frame.hdr_sdr_white_nits > 0.0f ? buffer_frame.hdr_sdr_white_nits : 203.0f;

        if (is_scrgb)
        {
            // scrgb linear, 1.0 = 80 nits, match the desktop sdr white level
            if (tone_mapping == 4)
            {
                // gt7 hdr returns rec.2020 fb units where 1.0 = 100 nits
                color.rgb = gt7_to_rec709(color.rgb) * (gt7_ref_luminance / 80.0f);
            }
            else
            {
                color.rgb = color.rgb * (sdr_white_nits / 80.0f);
            }
        }
        else
        {
            // normalize to pq range (0.0 - 1.0 where 1.0 = 10,000 nits)
            const float pq_max_nits = 10000.0f;

            if (tone_mapping == 4) // gran turismo 7
            {
                // gt7 outputs in fb units where 1.0 = 100 nits (gt7_ref_luminance)
                // for hdr, output is already in range [0, fb_target] where fb_target = max_nits / 100
                // convert to pq normalized range while staying in rec.2020
                color.rgb = (color.rgb * gt7_ref_luminance) / pq_max_nits;
                color.rgb = linear_rec2020_to_hdr10(color.rgb);
            }
            else
            {
                // sdr tonemappers output 0-1 where 1.0 = sdr white
                // no tonemapping also uses this since exposure normalizes values to ~1.0 = white
                color.rgb = (color.rgb * sdr_white_nits) / pq_max_nits;
                color.rgb = linear_rec709_to_hdr10(color.rgb);
            }
        }
    }
    else
    {
        // sdr gamma correction (linear -> srgb)
        color.rgb = linear_to_srgb(color.rgb);
    }

    color.a = 1.0f;
    tex_uav[thread_id.xy] = color;
}
