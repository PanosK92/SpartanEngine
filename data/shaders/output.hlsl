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

//= INCLUDES =========
#include "common.hlsl"
//====================

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
        { -0.0496500000000000,  1.1084784877776500, -0.0524964871144260 },
        { -0.0774967775043101, -0.1468813165635330,  1.2244001486462500 }
    };

    const float min_ev = -10.0f;
    const float max_ev = 6.5f;

    color = mul(agx_mat_inset, color);
    color = max(color, 1e-10f); // avoid log(0) and negative values
    color = log2(color);
    color = (color - min_ev) / (max_ev - min_ev);
    color = saturate(color);

    // s-curve approximation
    float3 x  = color;
    float3 x2 = x * x;
    float3 x4 = x2 * x2;
    color     = 15.5f * x4 * x2 - 40.14f * x4 * x + 31.96f * x4 - 6.868f * x2 * x + 0.4298f * x2 + 0.1191f * x - 0.00232f;

    color = mul(agx_mat_outset, color);

    return saturate(color);
}

float3 reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 aces_nautilus(float3 c)
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

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // get input data
    float3 f3_value    = pass_get_f3_value();
    float tone_mapping = f3_value.x;
    float4 color       = tex[thread_id.xy];

    // apply exposure (camera and auto-exposure)
    float exposure  = tex2.Load(int3(0,0,0)).r;
    color.rgb      *= buffer_frame.camera_exposure * exposure;

    // best used for sdr only
    switch (tone_mapping)
    {
        case 0:
            color.rgb = aces(color.rgb);
            break;
        case 1:
            color.rgb = agx(color.rgb);
            break;
        case 2:
            color.rgb = reinhard(color.rgb);
            break;
        case 3:
            color.rgb = aces_nautilus(color.rgb);
            break;
    }

    if (buffer_frame.hdr_enabled == 0.0f) // sdr
    {
        color.rgb  = linear_to_srgb(color.rgb);
    }
    else // hdr
    {
        // scale to max display nits
        float peak_scale = buffer_frame.hdr_max_nits / buffer_frame.hdr_white_point;
        float3 clamped   = color.rgb / peak_scale;
        clamped          = clamped / (1.0f + clamped);
        color.rgb        = clamped * peak_scale;

        // convert to hdr10
        color.rgb  = linear_to_hdr10(color.rgb, buffer_frame.hdr_white_point);
    }

    // at this point there is not reason to store an alpha value
    // and if the output image is displayed from within ImGui, it will have missing pixels
    color.a = 1.0f;

    tex_uav[thread_id.xy] = color;
}
