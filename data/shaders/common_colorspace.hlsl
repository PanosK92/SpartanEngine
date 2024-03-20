/*
Copyright(c) 2016-2024 Panos Karabelas

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

// These srgb/linear conversion functions surpass the simple pow(2.2) approach by accurately mirroring
// the srgb standard's piecewise linear curve for low values and non-linear curve for higher values. This
// ensures more precise color fidelity, particularly in gradients and low-light. The default engine gamma is 2.4.
// a good read: https://entropymine.com/imageworsener/srgbformula/

float3 srgb_to_linear(float3 color)
{
    float3 linear_low  = color / 12.92;
    float3 linear_high = pow((color + 0.055) / 1.055, buffer_frame.gamma);
    float3 is_high     = step(0.0404482362771082, color); // 1 if srgb > 0.04045, else 0
    return lerp(linear_low, linear_high, is_high);
}

float3 linear_to_srgb(float3 color)
{
    float3 srgb_low  = color * 12.92;
    float3 srgb_high = 1.055 * pow(color, 1.0 / buffer_frame.gamma) - 0.055;
    float3 is_high   = step(0.00313066844250063, color); // 1 if linear > 0.0031308, else 0
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

    // Adjust color values to match human perception of white under common lighting, like in living rooms or offices.
    // SDR's paper white (80 nits) appears grey in bright environments. This normalization aligns HDR visuals
    // with real-world white perception by factoring in ambient brightness, up to the ST.2084 spec limit of 10,000 nits.
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
