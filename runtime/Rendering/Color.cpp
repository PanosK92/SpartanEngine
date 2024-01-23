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

//= INCLUDES =====
#include "pch.h"
#include "Color.h"
//================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    // Most of the color/temperature values are derived from: https://physicallybased.info/ 
    // Might get inaccurate over 40000 K (which is really the limit that you should be using)
    static void temperature_to_color(const float temperature_kelvin, float& r, float& g, float& b)
    {
        // Constants for color temperature to RGB conversion
        const float A_R = 329.698727446f;
        const float B_R = -0.1332047592f;
        const float A_G = 288.1221695283f;
        const float B_G = -0.0755148492f;

        // Ensure temperature is above absolute zero
        if (temperature_kelvin < 0) {
            // Handle error
        }

        float temp = temperature_kelvin / 100.0f;

        r = 0.0f;
        g = 0.0f;
        b = 0.0f;

        if (temp <= 66)
        {
            r = 255;
            g = temp;
            g = 99.4708025861f * Math::Helper::Log(g) - 161.1195681661f;

            if (temp <= 19)
            {
                b = 0;
            }
            else
            {
                b = temp - 10.0f;
                b = 138.5177312231f * Math::Helper::Log(b) - 305.0447927307f;
            }
        }
        else
        {
            r = temp - 60.0f;
            r = A_R * Math::Helper::Pow(r, B_R);
            g = temp - 60.0f;
            g = A_G * Math::Helper::Pow(g, B_G);
            b = 255;
        }

        // Clamp RGB values to [0, 1]
        r = Math::Helper::Clamp(r / 255.0f, 0.0f, 1.0f);
        g = Math::Helper::Clamp(g / 255.0f, 0.0f, 1.0f);
        b = Math::Helper::Clamp(b / 255.0f, 0.0f, 1.0f);
    }

    Color::Color(const float r, const float g, const float b, const float a /*= 1.0f*/)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }

    Color::Color(const float temperature_kelvin)
    {
        temperature_to_color(temperature_kelvin, r, g, b);
    }

    // standard
    const Color Color::standard_black           = Color(0.0f, 0.0f, 0.0f, 1.0f);
    const Color Color::standard_white           = Color(1.0f, 1.0f, 1.0f, 1.0f);
    const Color Color::standard_transparent     = Color(0.0f, 0.0f, 0.0f, 0.0f);
    const Color Color::standard_red             = Color(1.0f, 0.0f, 0.0f, 1.0f);
    const Color Color::standard_green           = Color(0.0f, 1.0f, 0.0f, 1.0f);
    const Color Color::standard_blue            = Color(0.0f, 0.0f, 1.0f, 1.0f);
    const Color Color::standard_cornflower_blue = Color(0.396f, 0.611f, 0.937f, 1.0f);
    const Color Color::standard_renderer_lines  = Color(0.41f, 0.86f, 1.0f, 1.0f);

    // materials
    const Color Color::material_aluminum        = Color(0.912f, 0.914f, 0.920f); // Metallic: 1.0
    const Color Color::material_blood           = Color(0.644f, 0.003f, 0.005f);
    const Color Color::material_bone            = Color(0.793f, 0.793f, 0.664f);
    const Color Color::material_brass           = Color(0.887f, 0.789f, 0.434f);
    const Color Color::material_brick           = Color(0.262f, 0.095f, 0.061f);
    const Color Color::material_charcoal        = Color(0.020f, 0.020f, 0.020f);
    const Color Color::material_chocolate       = Color(0.162f, 0.091f, 0.060f);
    const Color Color::material_chromium        = Color(0.550f, 0.556f, 0.554f); // Metallic: 1.0
    const Color Color::material_cobalt          = Color(0.662f, 0.655f, 0.634f);
    const Color Color::material_concrete        = Color(0.510f, 0.510f, 0.510f);
    const Color Color::material_cooking_oil     = Color(0.738f, 0.687f, 0.091f);
    const Color Color::material_copper          = Color(0.926f, 0.721f, 0.504f);
    const Color Color::material_diamond         = Color(1.000f, 1.000f, 1.000f);
    const Color Color::material_egg_shell       = Color(0.610f, 0.624f, 0.631f);
    const Color Color::material_eye_cornea      = Color(1.000f, 1.000f, 1.000f);
    const Color Color::material_eye_lens        = Color(1.000f, 1.000f, 1.000f);
    const Color Color::material_eye_sclera      = Color(0.680f, 0.490f, 0.370f);
    const Color Color::material_glass           = Color(1.000f, 1.000f, 1.000f);
    const Color Color::material_gold            = Color(0.944f, 0.776f, 0.373f);
    const Color Color::material_gray_card       = Color(0.180f, 0.180f, 0.180f);
    const Color Color::material_honey           = Color(0.831f, 0.397f, 0.038f);
    const Color Color::material_ice             = Color(1.000f, 1.000f, 1.000f);
    const Color Color::material_iron            = Color(0.531f, 0.512f, 0.496f); // Metallic: 1.0
    const Color Color::material_ketchup         = Color(0.164f, 0.006f, 0.002f);
    const Color Color::material_lead            = Color(0.632f, 0.626f, 0.641f);
    const Color Color::material_mercury         = Color(0.781f, 0.779f, 0.779f);
    const Color Color::material_milk            = Color(0.604f, 0.584f, 0.497f);
    const Color Color::material_nickel          = Color(0.649f, 0.610f, 0.541f);
    const Color Color::material_office_paper    = Color(0.738f, 0.768f, 1.000f);
    const Color Color::material_plastic_pc      = Color(1.000f, 1.000f, 1.000f); // Specular: 0.640
    const Color Color::material_plastic_pet     = Color(1.000f, 1.000f, 1.000f); // Specular: 0.623
    const Color Color::material_plastic_acrylic = Color(1.000f, 1.000f, 1.000f); // Specular: 0.462
    const Color Color::material_plastic_pp      = Color(1.000f, 1.000f, 1.000f); // Specular: 0.487
    const Color Color::material_plastic_pvc     = Color(1.000f, 1.000f, 1.000f); // Specular: 0.550
    const Color Color::material_platinum        = Color(0.679f, 0.642f, 0.588f);
    const Color Color::material_salt            = Color(0.800f, 0.800f, 0.800f);
    const Color Color::material_sand            = Color(0.440f, 0.386f, 0.231f);
    const Color Color::material_sapphire        = Color(0.670f, 0.764f, 0.855f);
    const Color Color::material_silver          = Color(0.962f, 0.949f, 0.922f);
    const Color Color::material_skin_1          = Color(0.847f, 0.638f, 0.552f);
    const Color Color::material_skin_2          = Color(0.799f, 0.485f, 0.347f);
    const Color Color::material_skin_3          = Color(0.600f, 0.310f, 0.220f);
    const Color Color::material_skin_4          = Color(0.430f, 0.200f, 0.130f);
    const Color Color::material_skin_5          = Color(0.360f, 0.160f, 0.080f);
    const Color Color::material_skin_6          = Color(0.090f, 0.050f, 0.020f);
    const Color Color::material_snow            = Color(0.810f, 0.810f, 0.810f);
    const Color Color::material_tire            = Color(0.023f, 0.023f, 0.023f); // Metallic: 0.0, Specular 0.5
    const Color Color::material_titanium        = Color(0.616f, 0.582f, 0.544f);
    const Color Color::material_tungsten        = Color(0.925f, 0.835f, 0.757f);
    const Color Color::material_vanadium        = Color(0.945f, 0.894f, 0.780f);
    const Color Color::material_water           = Color(1.000f, 1.000f, 1.000f);
    const Color Color::material_zinc            = Color(0.875f, 0.867f, 0.855f);

    //lights
    const Color Color::light_sky_clear              = Color(15000); // Intensity: 20000  lx
    const Color Color::light_sky_daylight_overcast  = Color(6500);  // Intensity: 2000   lx
    const Color Color::light_sky_moonlight          = Color(4000);  // Intensity: 0.1    lx
    const Color Color::light_sky_sunrise            = Color(2000); 
    const Color Color::light_candle_flame           = Color(1850);  // Intensity: 13     lm
    const Color Color::light_direct_sunlight        = Color(5778);  // Intensity: 120000 lx
    const Color Color::light_digital_display        = Color(6500);  // Intensity: 200    cd/m2
    const Color Color::light_fluorescent_tube_light = Color(5000);  // Intensity: 1000   lm 
    const Color Color::light_kerosene_lamp          = Color(1850);  // Intensity: 50     lm
    const Color Color::light_light_bulb             = Color(2700);  // Intensity: 800    lm
    const Color Color::light_photo_flash            = Color(5500);  // Intensity: 20000  lm
}
