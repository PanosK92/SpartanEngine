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

// gt7 tonemapper - port of siggraph 2025 presentation
static const float gt7_reference_white = 80.0f; // sdr reference white (bt.1886)

float gt7_smooth_step(float x, float edge0, float edge1)
{
    float t = saturate((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

// pq curve: linear (nits) -> perceptual (0-1)
float gt7_linear_to_pq(float v)
{
    const float m1 = 0.1593017578125f, m2 = 78.84375f;
    const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;

    float y  = (v * gt7_reference_white) / 10000.0f;
    float ym = pow(max(y, 0.0f), m1);
    return exp2(m2 * (log2(c1 + c2 * ym) - log2(1.0f + c3 * ym)));
}

// pq curve: perceptual (0-1) -> linear (nits)
float gt7_pq_to_linear(float n)
{
    const float m1 = 0.1593017578125f, m2 = 78.84375f;
    const float c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;

    float np = pow(max(n, 0.0f), 1.0f / m2);
    float l  = max(np - c1, 0.0f) / (c2 - c3 * np);
    return (pow(l, 1.0f / m1) * 10000.0f) / gt7_reference_white;
}

// ictcp color space - separates intensity from chroma to prevent hue shifts
void gt7_rgb_to_ictcp(float3 rgb, out float3 ictcp)
{
    // rgb -> lms
    float l = (rgb.r * 1688.0f + rgb.g * 2146.0f + rgb.b * 262.0f)  / 4096.0f;
    float m = (rgb.r * 683.0f  + rgb.g * 2951.0f + rgb.b * 462.0f)  / 4096.0f;
    float s = (rgb.r * 99.0f   + rgb.g * 309.0f  + rgb.b * 3688.0f) / 4096.0f;

    // lms -> ictcp (with pq)
    float lpq = gt7_linear_to_pq(l);
    float mpq = gt7_linear_to_pq(m);
    float spq = gt7_linear_to_pq(s);
    ictcp.x = (2048.0f  * lpq + 2048.0f  * mpq)                    / 4096.0f;
    ictcp.y = (6610.0f  * lpq - 13613.0f * mpq + 7003.0f  * spq)   / 4096.0f;
    ictcp.z = (17933.0f * lpq - 17390.0f * mpq - 543.0f   * spq)   / 4096.0f;
}

void gt7_ictcp_to_rgb(float3 ictcp, out float3 rgb)
{
    // ictcp -> lms
    float l = ictcp.x + 0.00860904f * ictcp.y + 0.11103f   * ictcp.z;
    float m = ictcp.x - 0.00860904f * ictcp.y - 0.11103f   * ictcp.z;
    float s = ictcp.x + 0.560031f   * ictcp.y - 0.320627f  * ictcp.z;

    // lms -> rgb (with inverse pq)
    float l_lin = gt7_pq_to_linear(l);
    float m_lin = gt7_pq_to_linear(m);
    float s_lin = gt7_pq_to_linear(s);
    rgb.r = max(3.43661f   * l_lin - 2.50645f   * m_lin + 0.0698454f * s_lin, 0.0f);
    rgb.g = max(-0.79133f  * l_lin + 1.9836f    * m_lin - 0.192271f  * s_lin, 0.0f);
    rgb.b = max(-0.0259499f* l_lin - 0.0989137f * m_lin + 1.12486f   * s_lin, 0.0f);
}

// s-curve with exponential shoulder
float gt7_curve(float x, float peak, float mid_point, float toe_strength)
{
    const float alpha          = 0.25f;
    const float linear_section = 0.538f;

    if (x < 0.0f)
        return 0.0f;

    float k  = (linear_section - 1.0f) / (alpha - 1.0f);
    float ka = peak * linear_section + peak * k;
    float kb = -peak * k * exp(linear_section / k);
    float kc = -1.0f / (k * peak);

    float weight_linear = gt7_smooth_step(x, 0.0f, mid_point);

    if (x < linear_section * peak)
    {
        float toe = mid_point * pow(max(x / mid_point, 0.0001f), toe_strength);
        return (1.0f - weight_linear) * toe + weight_linear * x;
    }

    return ka + kb * exp(x * kc);
}

float3 gran_turismo_7(float3 rgb, float max_display_nits, bool is_hdr)
{
    float target_nits = is_hdr ? max_display_nits : gt7_reference_white;
    float peak        = target_nits / gt7_reference_white;
    float mid_point   = 0.444f;
    float toe         = 1.280f;

    // analyze input brightness in ictcp space
    float3 input_ictcp;
    gt7_rgb_to_ictcp(rgb, input_ictcp);

    // path 1: per-channel curve (preserves saturation, shifts hue)
    float3 skewed_rgb = float3(
        gt7_curve(rgb.x, peak, mid_point, toe),
        gt7_curve(rgb.y, peak, mid_point, toe),
        gt7_curve(rgb.z, peak, mid_point, toe)
    );
    float3 skewed_ictcp;
    gt7_rgb_to_ictcp(skewed_rgb, skewed_ictcp);

    // path 2: luminance curve with chroma fade (preserves hue, desaturates highlights)
    float3 target_ictcp;
    gt7_rgb_to_ictcp(float3(peak, peak, peak), target_ictcp);
    float chroma_scale = 1.0f - gt7_smooth_step(input_ictcp.x / target_ictcp.x, 0.98f, 1.16f);
    float3 scaled_rgb;
    gt7_ictcp_to_rgb(float3(skewed_ictcp.x, input_ictcp.y * chroma_scale, input_ictcp.z * chroma_scale), scaled_rgb);

    // blend both paths and clamp to display max
    float3 out_rgb = min(lerp(skewed_rgb, scaled_rgb, 0.6f), peak);

    // normalize sdr output to 0-1
    if (!is_hdr)
        out_rgb /= peak;
    
    return out_rgb;
}

// aces fitted curve
float3 aces(float3 color)
{
    static const float3x3 aces_input = {
        { 0.59719f, 0.35458f, 0.04823f },
        { 0.07600f, 0.90834f, 0.01566f },
        { 0.02840f, 0.13383f, 0.83777f }
    };
    static const float3x3 aces_output = {
        {  1.60475f, -0.53108f, -0.07367f },
        { -0.10208f,  1.10813f, -0.00605f },
        { -0.00327f, -0.07276f,  1.07602f }
    };

    color = mul(aces_input, color);
    float3 a = color * (color + 0.0245786f) - 0.000090537f;
    float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
    return saturate(mul(aces_output, a / b));
}

// agx with log encoding and polynomial curve
float3 agx(float3 color)
{
    static const float3x3 agx_inset = {
        { 0.8566271533159880f, 0.1373185106779920f, 0.1118982129999500f },
        { 0.0951212405381588f, 0.7612419900249430f, 0.0767997842235547f },
        { 0.0482516061458523f, 0.1014394992970650f, 0.8113020027764950f }
    };
    static const float3x3 agx_outset = {
        {  1.1271467782272380f, -0.1468813165635330f, -0.1255038609319300f },
        { -0.0496500000000000f,  1.1084784877776500f, -0.0524964871144260f },
        { -0.0774967775043101f, -0.1468813165635330f,  1.2244001486462500f }
    };

    color = mul(agx_inset, color);
    color = saturate((log2(max(color, 1e-10f)) + 10.0f) / 16.5f);

    float3 x2 = color * color;
    float3 x4 = x2 * x2;
    color = 15.5f * x4 * x2 - 40.14f * x4 * color + 31.96f * x4 - 6.868f * x2 * color + 0.4298f * x2 + 0.1191f * color - 0.00232f;

    return saturate(mul(agx_outset, color));
}

float3 reinhard(float3 color)
{
    return color / (color + 1.0f);
}

// aces nautilus fit
float3 aces_nautilus(float3 color)
{
    return saturate((color * (2.51f * color + 0.03f)) / (color * (2.43f * color + 0.59f) + 0.14f));
}

// hdr10 encoding: rec.709 -> rec.2020 -> pq curve
float3 linear_to_hdr10(float3 color)
{
    static const float3x3 rec709_to_rec2020 = {
        { 0.6274040f, 0.3292820f, 0.0433136f },
        { 0.0690970f, 0.9195400f, 0.0113612f },
        { 0.0163916f, 0.0880132f, 0.8955950f }
    };
    static const float m1 = 2610.0f / 4096.0f / 4.0f;
    static const float m2 = 2523.0f / 4096.0f * 128.0f;
    static const float c1 = 3424.0f / 4096.0f;
    static const float c2 = 2413.0f / 4096.0f * 32.0f;
    static const float c3 = 2392.0f / 4096.0f * 32.0f;

    color = mul(rec709_to_rec2020, color);
    float3 cp = pow(abs(color), m1);
    return pow((c1 + c2 * cp) / (1.0f + c3 * cp), m2);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float3 f3_value = pass_get_f3_value();
    uint tonemapper = (uint)f3_value.x;
    float4 color    = tex[thread_id.xy];

    // convert radiometric (watts) to photometric (nits) and apply exposure
    color.rgb *= 683.0f;
    float exposure = f3_value.y >= 0.0f ? tex2.Load(int3(0, 0, 0)).r : 1.0f;
    color.rgb *= buffer_frame.camera_exposure * exposure;
    
    bool is_hdr    = buffer_frame.hdr_enabled != 0.0f;
    float max_nits = buffer_frame.hdr_max_nits;

    // tone mapping
    switch (tonemapper)
    {
        case 0: color.rgb = aces(color.rgb);                              break;
        case 1: color.rgb = agx(color.rgb);                               break;
        case 2: color.rgb = reinhard(color.rgb);                          break;
        case 3: color.rgb = aces_nautilus(color.rgb);                     break;
        case 4: color.rgb = gran_turismo_7(color.rgb, max_nits, is_hdr);  break;
        default: if (!is_hdr) color.rgb = saturate(color.rgb);            break;
    }

    // output encoding
    if (is_hdr)
    {
        // gt7 uses 80 nits reference, boost to rec.2100 reference (203 nits) for brightness parity
        float nits_scale = (tonemapper == 4) ? 203.0f : max_nits;
        color.rgb = linear_to_hdr10(color.rgb * nits_scale / 10000.0f);
    }
    else
    {
        color.rgb = linear_to_srgb(color.rgb);
    }

    color.a = 1.0f;
    tex_uav[thread_id.xy] = color;
}
