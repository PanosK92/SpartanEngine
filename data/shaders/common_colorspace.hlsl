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

// for details, read my blog post: https://panoskarabelas.com/blog/posts/hdr_in_under_10_minutes/

float get_gamma()
{
    float gamma = 2.2f; // around 2.2 for SDR
    return buffer_frame.hdr_enabled ? 2.4f : gamma;
}

float3 srgb_to_linear(float3 color)
{
    float3 linear_low  = color / 12.92;
    float3 linear_high = pow((color + 0.055) / 1.055, get_gamma());
    float3 is_high     = step(0.0404482362771082, color);
    return lerp(linear_low, linear_high, is_high);
}

float3 linear_to_srgb(float3 color)
{
    float3 srgb_low  = color * 12.92;
    float3 srgb_high = 1.055 * pow(color, 1.0 / get_gamma()) - 0.055;
    float3 is_high   = step(0.00313066844250063, color);
    return lerp(srgb_low, srgb_high, is_high);
}

float3 linear_to_hdr10(float3 color, float white_point)
{
    // convert Rec.709 (similar to srgb) to Rec.2020 color space
    {
        static const float3x3 from709to2020 =
        {
            { 0.6274040f, 0.3292820f, 0.0433136f },
            { 0.0690970f, 0.9195400f, 0.0113612f },
            { 0.0163916f, 0.0880132f, 0.8955950f }
        };
        
        color = mul(from709to2020, color);
    }

    // normalize HDR scene values ([0..>1] to [0..1]) for the ST.2084 curve
    const float st2084_max = 10000.0f;
    color *= white_point / st2084_max;

    // apply ST.2084 (PQ curve) for HDR10 standard
    {
        static const float m1 = 2610.0 / 4096.0 / 4;
        static const float m2 = 2523.0 / 4096.0 * 128;
        static const float c1 = 3424.0 / 4096.0;
        static const float c2 = 2413.0 / 4096.0 * 32;
        static const float c3 = 2392.0 / 4096.0 * 32;
        float3 cp             = pow(abs(color), m1);
        color                 = pow((c1 + c2 * cp) / (1 + c3 * cp), m2);
    }

    return color;
}

float3 tone_map_to_display(float3 color_linear, float white_point, float display_max_nits)
{
    // Convert linear scene (0-∞) to real nits
    float3 nits = color_linear * white_point;

    // If the scene's reference white exceeds the display's peak, compress highlights
    const float shoulder_strength = 0.85; // 0 = hard clip, 1 = very soft
    nits = nits / (nits + display_max_nits * shoulder_strength);
    nits *= display_max_nits;

    // Back to normalized linear [0-1] for PQ encoding
    return nits / white_point;
}
