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

#pragma once


//= INCLUDES ===================
#include "../Core/Definitions.h"
//==============================

namespace Spartan
{
    class SP_CLASS Color
    {
    public:
        Color() = default;
        Color(const Color& color) = default;
        Color(const float temperature_kelvin);
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

        const float* Data() const { return &r; }

        // standard
        static const Color standard_black;
        static const Color standard_white;
        static const Color standard_transparent;
        static const Color standard_red;
        static const Color standard_green;
        static const Color standard_blue;
        static const Color standard_cornflower_blue;
        static const Color standard_renderer_lines;

        // materials
        static const Color material_aluminum;
        static const Color material_blood;
        static const Color material_bone;
        static const Color material_brass;
        static const Color material_brick;
        static const Color material_charcoal;
        static const Color material_chocolate;
        static const Color material_chromium;
        static const Color material_cobalt;
        static const Color material_concrete;
        static const Color material_cooking_oil;
        static const Color material_copper;
        static const Color material_diamond;
        static const Color material_egg_shell;
        static const Color material_eye_cornea;
        static const Color material_eye_lens;
        static const Color material_eye_sclera;
        static const Color material_glass;
        static const Color material_gold;
        static const Color material_gray_card;
        static const Color material_honey;
        static const Color material_ice;
        static const Color material_iron;
        static const Color material_ketchup;
        static const Color material_lead;
        static const Color material_mercury;
        static const Color material_milk;
        static const Color material_nickel;
        static const Color material_office_paper;
        static const Color material_plastic_pc;
        static const Color material_plastic_pet;
        static const Color material_plastic_acrylic;
        static const Color material_plastic_pp;
        static const Color material_plastic_pvc;
        static const Color material_platinum;
        static const Color material_salt;
        static const Color material_sand;
        static const Color material_sapphire;
        static const Color material_silver;
        static const Color material_skin_1;
        static const Color material_skin_2;
        static const Color material_skin_3;
        static const Color material_skin_4;
        static const Color material_skin_5;
        static const Color material_skin_6;
        static const Color material_snow;
        static const Color material_tire;
        static const Color material_titanium;
        static const Color material_tungsten;
        static const Color material_vanadium;
        static const Color material_water;
        static const Color material_zinc;

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
