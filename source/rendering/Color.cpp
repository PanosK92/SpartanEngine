/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====
#include "pch.h"
#include "Color.h"
//================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
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
        SP_ASSERT(temperature_kelvin >= 0.0f);

        // the helland fit is expressed in hundreds of kelvin
        const float kelvin_100s = temperature_kelvin / 100.0f;

        r = 0.0f;
        g = 0.0f;
        b = 0.0f;

        if (kelvin_100s <= 66)
        {
            r = 255;
            g = kelvin_100s;
            g = 99.4708025861f * log(g) - 161.1195681661f;

            if (kelvin_100s <= 19)
            {
                b = 0;
            }
            else
            {
                b = kelvin_100s - 10.0f;
                b = 138.5177312231f * log(b) - 305.0447927307f;
            }
        }
        else
        {
            r = kelvin_100s - 60.0f;
            r = A_R * pow(r, B_R);
            g = kelvin_100s - 60.0f;
            g = A_G * pow(g, B_G);
            b = 255;
        }

        // clamp rgb values to [0, 1]
        r = clamp(r / 255.0f, 0.0f, 1.0f);
        g = clamp(g / 255.0f, 0.0f, 1.0f);
        b = clamp(b / 255.0f, 0.0f, 1.0f);

        // the helland fit outputs srgb encoded values, convert to linear light
        auto srgb_to_linear = [](const float c)
        {
            return c <= 0.04045f ? c / 12.92f : pow((c + 0.055f) / 1.055f, 2.4f);
        };
        r = srgb_to_linear(r);
        g = srgb_to_linear(g);
        b = srgb_to_linear(b);

        // normalize to unit luminance so the authored lux/lumens are delivered exactly
        const float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
        if (lum > 0.0f)
        {
            r /= lum;
            g /= lum;
            b /= lum;
        }
    }

    Color::Color(const float r, const float g, const float b, const float a /*= 1.0f*/)
    {
        this->r = r;
        this->g = g;
        this->b = b;
        this->a = a;
    }

    Color::Color(const float temperature_kelvin, const float a /*= 1.0f*/)
    {
        temperature_to_color(temperature_kelvin, r, g, b);
        this->a = a;
    }

    // standard
    const Color Color::standard_black           = Color(0.0f, 0.0f, 0.0f, 1.0f);
    const Color Color::standard_white           = Color(1.0f, 1.0f, 1.0f, 1.0f);
    const Color Color::standard_transparent     = Color(0.0f, 0.0f, 0.0f, 0.0f);
    const Color Color::standard_yellow          = Color(1.0f, 1.0f, 0.0f, 1.0f);
    const Color Color::standard_renderer_lines  = Color(0.41f, 0.86f, 1.0f, 1.0f);

    // lights
    const Color Color::light_sky_clear              = Color(15000); // intensity: 20000  lx
    const Color Color::light_sky_daylight_overcast  = Color(6500);  // intensity: 2000   lx
    const Color Color::light_sky_moonlight          = Color(4000);  // intensity: 0.1    lx
    const Color Color::light_sky_sunrise            = Color(2000); 
    const Color Color::light_candle_flame           = Color(1850);  // intensity: 13     lm
    const Color Color::light_direct_sunlight        = Color(5778);  // intensity: 120000 lx
    const Color Color::light_digital_display        = Color(6500);  // intensity: 200    cd/m2
    const Color Color::light_fluorescent_tube_light = Color(5000);  // intensity: 1000   lm 
    const Color Color::light_kerosene_lamp          = Color(1850);  // intensity: 50     lm
    const Color Color::light_light_bulb             = Color(2700);  // intensity: 800    lm
    const Color Color::light_photo_flash            = Color(5500);  // intensity: 20000  lm
}
