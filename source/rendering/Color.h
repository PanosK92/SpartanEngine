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

        const float* Data() const { return &r; }

        // standard
        static const Color standard_black;
        static const Color standard_white;
        static const Color standard_transparent;
        static const Color standard_red;
        static const Color standard_green;
        static const Color standard_blue;
        static const Color standard_yellow;
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
