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

float3 nautilus(float3 c)
{

    // Nautilus fit of ACES
    // By Nolram
    
    float a = 2.51f;
    float b = 0.03f;
    float y = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((c * (a * c + b)) / (c * (y * c + d) + e), 0.0, 1.0);
}
//==========================================================================================
// ENTRY
//==========================================================================================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    // get cpu data
    float3 f3_value    = pass_get_f3_value();
    float tone_mapping = f3_value.y;

    float4 color  = tex[thread_id.xy];

    if (buffer_frame.hdr_enabled == 0.0f) // SDR
    {
        switch (tone_mapping)
        {
            case 0:
                color.rgb = aces(color.rgb);
                break;
            case 1:
                color.rgb = nautilus(color.rgb);
                break;
            case 2:
                color.rgb = reinhard(color.rgb);
                break;
            case 3:
                color.rgb = uncharted_2(color.rgb);
                break;
            case 4:
                color.rgb = matrix_movie(color.rgb);
                break;
        }

        color.rgb = linear_to_srgb(color.rgb);
    }
    else // HDR
    {
        color.rgb = linear_to_hdr10(color.rgb, buffer_frame.hdr_white_point);
    }

    tex_uav[thread_id.xy] = color;
}
