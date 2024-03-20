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

//= INCLUDES =========
#include "common.hlsl"
//====================

//==========================================================================================
// SDR TONEMAPPING
//==========================================================================================

float3 reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 uncharted_2(float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 matrix_movie(float3 keannu)
{
    static const float pow_a = 3.0f / 2.0f;
    static const float pow_b = 4.0f / 5.0f;

    return float3(pow(abs(keannu.r), pow_a), pow(abs(keannu.g), pow_b), pow(abs(keannu.b), pow_a));
}

float3 aces(float3 color)
{
    //  Baking Lab
    //  by MJP and David Neubelt
    //  http://mynameismjp.wordpress.com/
    //  All code licensed under the MIT license
    
    // sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
    static const float3x3 aces_mat_input =
    {
        {0.59719, 0.35458, 0.04823},
        {0.07600, 0.90834, 0.01566},
        {0.02840, 0.13383, 0.83777}
    };
    color = mul(aces_mat_input, color);
    
    // RRTAndODTFit
    float3 a = color * (color + 0.0245786f) - 0.000090537f;
    float3 b = color * (0.983729f * color + 0.4329510f) + 0.238081f;
    color = a / b;

    // ODT_SAT => XYZ => D60_2_D65 => sRGB
    static const float3x3 aces_mat_output =
    {
        { 1.60475, -0.53108, -0.07367},
        {-0.10208,  1.10813, -0.00605},
        {-0.00327, -0.07276,  1.07602}
    };
    color = mul(aces_mat_output, color);

    return saturate(color);
}

//==========================================================================================
// HDR
//==========================================================================================

float3 to_hdr10(float3 color, float white_point)
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

//==========================================================================================
// ENTRY
//==========================================================================================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    // get cpu data
    float3 f3_value    = pass_get_f3_value();
    float tone_mapping = f3_value.y;
    float exposure     = f3_value.z;
   
    float4 color  = tex[thread_id.xy];

    if (buffer_frame.hdr_enabled == 0.0f) // SDR
    {
        color.rgb *= exposure;

        switch (tone_mapping)
        {
            case 0:
                color.rgb = aces(color.rgb);
                break;
            case 1:
                color.rgb = reinhard(color.rgb);
                break;
            case 2:
                color.rgb = uncharted_2(color.rgb);
                break;
            case 3:
                color.rgb = matrix_movie(color.rgb);
                break;
        }

        color.rgb = linear_to_srgb(color.rgb);
    }
    else // HDR
    {
        color.rgb = to_hdr10(color.rgb, buffer_frame.hdr_white_point);
    }

    tex_uav[thread_id.xy] = color;
}


