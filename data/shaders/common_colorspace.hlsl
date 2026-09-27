/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// for details, read my blog post: https://panoskarabelas.com/blog/posts/hdr_in_under_10_minutes/

float get_gamma()
{
    return 2.4f;
}

float3 srgb_to_linear(float3 color)
{
    float3 linear_low  = color / 12.92;
    float3 linear_high = pow(max((color + 0.055) / 1.055, 0.0f), get_gamma());
    float3 is_high     = step(0.0404482362771082, color);
    return lerp(linear_low, linear_high, is_high);
}

float3 linear_to_srgb(float3 color)
{
    float3 srgb_low  = color * 12.92;
    float3 srgb_high = 1.055 * pow(max(color, 0.0f), 1.0 / get_gamma()) - 0.055;
    float3 is_high   = step(0.00313066844250063, color);
    return lerp(srgb_low, srgb_high, is_high);
}

// rec.709 (srgb primaries) linear values to hdr10 (rec.2020 + st.2084 pq curve)
float3 linear_to_hdr10(float3 color, float white_point)
{
    {
        static const float3x3 from709to2020 =
        {
            { 0.6274040f, 0.3292820f, 0.0433136f },
            { 0.0690970f, 0.9195400f, 0.0113612f },
            { 0.0163916f, 0.0880132f, 0.8955950f }
        };
        color = mul(from709to2020, color);
    }

    // normalize hdr scene values to the st.2084 [0..1] domain where 1.0 = 10000 nits
    const float st2084_max = 10000.0f;
    color *= white_point / st2084_max;

    {
        static const float m1 = 2610.0 / 4096.0 / 4;
        static const float m2 = 2523.0 / 4096.0 * 128;
        static const float c1 = 3424.0 / 4096.0;
        static const float c2 = 2413.0 / 4096.0 * 32;
        static const float c3 = 2392.0 / 4096.0 * 32;
        float3 cp = pow(abs(color), m1);
        color = pow((c1 + c2 * cp) / (1 + c3 * cp), m2);
    }

    return color;
}

// sdr ui colors to whatever the swapchain expects, hdr_mode: 0 = sdr, 1 = hdr10 pq, 2 = scrgb linear (1.0 = 80 nits)
float3 ui_to_display(float3 color_srgb, float hdr_mode, float ui_nits)
{
    if (hdr_mode == 0.0f)
    {
        return color_srgb;
    }

    float3 color_linear = srgb_to_linear(color_srgb);
    return hdr_mode > 1.5f ? color_linear * (ui_nits / 80.0f) : linear_to_hdr10(color_linear, ui_nits);
}
