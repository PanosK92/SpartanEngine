/*
Copyright(c) 2016-2020 Panos Karabelas

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

float3 Reinhard(float3 hdr, float k = 1.0f)
{
    return hdr / (hdr + k);
}

float3 ReinhardInverse(float3 sdr, float k = 1.0)
{
    return k * sdr / (k - sdr);
}

float3 Uncharted2(float3 x)
{
    float A = 0.15;
    float B = 0.50;
    float C = 0.10;
    float D = 0.20;
    float E = 0.02;
    float F = 0.30;
    float W = 11.2;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

//== ACESFitted ===========================
//  Baking Lab
//  by MJP and David Neubelt
//  http://mynameismjp.wordpress.com/
//  All code licensed under the MIT license
//=========================================

// sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
static const float3x3 ACESInputMat =
{
    {0.59719, 0.35458, 0.04823},
    {0.07600, 0.90834, 0.01566},
    {0.02840, 0.13383, 0.83777}
};

// ODT_SAT => XYZ => D60_2_D65 => sRGB
static const float3x3 ACESOutputMat =
{
    { 1.60475, -0.53108, -0.07367},
    {-0.10208,  1.10813, -0.00605},
    {-0.00327, -0.07276,  1.07602}
};

float3 RRTAndODTFit(float3 v)
{
    float3 a = v * (v + 0.0245786f) - 0.000090537f;
    float3 b = v * (0.983729f * v + 0.4329510f) + 0.238081f;
    return a / b;
}

float3 ACESFitted(float3 color)
{
    color = mul(ACESInputMat, color);

    // Apply RRT and ODT
    color = RRTAndODTFit(color);

    color = mul(ACESOutputMat, color);

    // Clamp to [0, 1]
    color = saturate(color);

    return color;
}

// Compute EV100 from the camera settings. Aperture in f-stops, shutterSpeed in seconds, sensitivity in ISO.
float camera_settings_to_ev100(float aperture, float shutterSpeed, float sensitivity)
{
    return log2((aperture * aperture) / shutterSpeed * 100.0 / sensitivity);
}

// Computes the exposure normalization factor from the camera's EV100
float ev100_to_exposure(float ev100)
{
    return 1.0 / (pow(2.0, ev100) * 1.2);
}

float3 ToneMap(float3 color)
{
    float ev100     = camera_settings_to_ev100(g_camera_aperture, g_camera_shutter_speed, g_camera_iso);
    float exposure  = ev100_to_exposure(ev100);
    
    color *= exposure;
    
    [branch]
    if (g_toneMapping == 0) // OFF
    {
        // Do nothing
    }
    else if (g_toneMapping == 1) // ACES
    {
        // attempting to match contrast levels
        color = pow(abs(color), 0.833f);
        color *= 1.07f;

        color = ACESFitted(color);
    }
    else if (g_toneMapping == 2) // REINHARD
    {
        color = Reinhard(color);
    }
    else if (g_toneMapping == 3) // UNCHARTED 2
    {
        color = Uncharted2(color);
    }
    
    return color;
}
