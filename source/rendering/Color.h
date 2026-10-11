/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    class Color
    {
    public:
        Color() = default;
        Color(const Color& color) = default;
        Color(const float temperature_kelvin, const float a = 1.0f);
        Color(const float r, const float g, const float b, const float a = 1.0f);
        ~Color() = default;

        bool operator==(const Color& rhs) const
        {
            return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a;
        }

        bool operator!=(const Color& rhs) const
        {
            return !(*this == rhs);
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 0.0f;

        // standard
        static const Color standard_black;
        static const Color standard_white;
        static const Color standard_transparent;
        static const Color standard_yellow;
        static const Color standard_renderer_lines;

        // lights
        static const Color light_sky_clear;
        static const Color light_sky_daylight_overcast;
        static const Color light_sky_moonlight;
        static const Color light_sky_sunrise;
        static const Color light_candle_flame;
        static const Color light_digital_display;
        static const Color light_direct_sunlight;
        static const Color light_fluorescent_tube_light;
        static const Color light_kerosene_lamp;
        static const Color light_light_bulb;
        static const Color light_photo_flash;
    };
}
