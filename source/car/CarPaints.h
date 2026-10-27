/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======================
#include <cstdint>
#include <cmath>
#include "../rendering/Material.h"
//=================================

namespace spartan::car_paints
{
    enum class family : uint8_t
    {
        rosso,
        giallo,
        blu,
        verde,
        grigio,
        bianco_nero,
        speciale,
        count
    };

    inline const char* family_name(family f)
    {
        switch (f)
        {
            case family::rosso:       return "RED";
            case family::giallo:      return "YELLOW";
            case family::blu:         return "BLUE";
            case family::verde:       return "GREEN";
            case family::grigio:      return "GREY";
            case family::bianco_nero: return "WHITE BLACK";
            case family::speciale:    return "SPECIAL";
            default:                  return "";
        }
    }

    // a factory colour, names and codes are ferrari's own, ferrari does not publish colour values
    // so the swatches are srgb approximations of the paint under daylight
    struct paint
    {
        const char* name;
        const char* code;       // factory code, empty where it is not public
        const char* finish;     // how ferrari sells it
        family group;
        MaterialPaintPreset preset;
        uint8_t r, g, b;        // srgb swatch
        uint8_t pr = 0, pg = 0, pb = 0; // srgb pearl, black keeps the preset's pearl
        float iridescence           = 0.0f;
        float iridescence_thickness = 400.0f;
    };

    inline constexpr paint palette[] =
    {
        { "Rosso Corsa",          "322",  "Solid",         family::rosso,       MaterialPaintPreset::GlossSolid, 196,  12,  14 },
        { "Rosso Scuderia",       "323",  "Solid",         family::rosso,       MaterialPaintPreset::GlossSolid, 226,  38,  16 },
        { "Rosso Fuoco",          "",     "Metallic",      family::rosso,       MaterialPaintPreset::Metallic,   170,  20,  14 },
        { "Rosso Fiorano",        "321",  "Metallic",      family::rosso,       MaterialPaintPreset::Metallic,   118,  12,  20 },
        { "Rosso Mugello",        "",     "Metallic",      family::rosso,       MaterialPaintPreset::Metallic,    92,   9,  18 },
        { "Rosso Trionfale",      "",     "Tri-coat",      family::rosso,       MaterialPaintPreset::Candy,      172,   8,  22 },
        { "Rosso Magma",          "",     "Metallic",      family::rosso,       MaterialPaintPreset::Metallic,   146,  44,  22 },

        { "Giallo Modena",        "4305", "Solid",         family::giallo,      MaterialPaintPreset::GlossSolid, 248, 176,   0 },
        { "Giallo Triplo Strato", "",     "Tri-coat",      family::giallo,      MaterialPaintPreset::Pearl,      246, 168,   6, 255, 228, 140 },
        { "Giallo Fly",           "",     "Solid",         family::giallo,      MaterialPaintPreset::GlossSolid, 238, 214,  10 },

        { "Blu Pozzi",            "520",  "Metallic",      family::blu,         MaterialPaintPreset::Metallic,    24,  38,  84 },
        { "Blu Tour de France",   "522",  "Metallic",      family::blu,         MaterialPaintPreset::Metallic,    20,  32,  66 },
        { "Blu Abu Dhabi",        "526",  "Metallic",      family::blu,         MaterialPaintPreset::Metallic,    62,  92, 134 },
        { "Blu Corsa",            "",     "Metallic",      family::blu,         MaterialPaintPreset::Metallic,    30,  70, 160 },
        { "Blu Elettrico",        "",     "Metallic",      family::blu,         MaterialPaintPreset::Metallic,    24,  58, 188 },
        { "Azzurro California",   "524",  "Metallic",      family::blu,         MaterialPaintPreset::Metallic,   110, 152, 196 },
        { "Azzurro Dino",         "",     "Solid",         family::blu,         MaterialPaintPreset::GlossSolid, 122, 168, 202 },

        { "Verde British Racing", "611",  "Solid",         family::verde,       MaterialPaintPreset::GlossSolid,  14,  56,  36 },
        { "Verde Pino",           "",     "Pearl",         family::verde,       MaterialPaintPreset::Pearl,       20,  52,  38, 130, 210, 170 },

        { "Argento Nurburgring",  "101",  "Metallic",      family::grigio,      MaterialPaintPreset::Metallic,   180, 184, 186 },
        { "Grigio Alloy",         "750",  "Metallic",      family::grigio,      MaterialPaintPreset::Metallic,   158, 160, 160 },
        { "Grigio Titanio",       "",     "Metallic",      family::grigio,      MaterialPaintPreset::Metallic,   120, 124, 128 },
        { "Grigio Silverstone",   "740",  "Metallic",      family::grigio,      MaterialPaintPreset::Metallic,   100, 104, 106 },
        { "Grigio Ferro",         "790",  "Metallic",      family::grigio,      MaterialPaintPreset::Metallic,    78,  82,  86 },
        { "Canna di Fucile",      "703",  "Metallic",      family::grigio,      MaterialPaintPreset::Metallic,    58,  61,  64 },

        { "Bianco Avus",          "100",  "Solid",         family::bianco_nero, MaterialPaintPreset::GlossSolid, 236, 236, 232 },
        { "Bianco Italia",        "",     "Tri-coat",      family::bianco_nero, MaterialPaintPreset::Pearl,      240, 238, 228, 255, 236, 214 },
        { "Nero",                 "1250", "Solid",         family::bianco_nero, MaterialPaintPreset::GlossSolid,  10,  10,  11 },
        { "Nero Daytona",         "910",  "Metallic",      family::bianco_nero, MaterialPaintPreset::Metallic,    18,  19,  22 },

        { "Azzurro Iridescente",  "",     "Iridescent",    family::speciale,    MaterialPaintPreset::Pearl,       34,  86, 150, 120, 200, 255, 1.0f, 420.0f },
        { "Rosso Corsa Opaco",    "",     "Matte",         family::speciale,    MaterialPaintPreset::Matte,      176,  12,  14 },
        { "Nero DS Opaco",        "",     "Matte",         family::speciale,    MaterialPaintPreset::Matte,       16,  16,  17 },
        { "Bronzo Opaco",         "",     "Matte",         family::speciale,    MaterialPaintPreset::Satin,      112,  80,  48 },
    };

    inline constexpr int palette_count = static_cast<int>(sizeof(palette) / sizeof(palette[0]));

    // how the colour is laid down, any colour takes any finish, original keeps the finish the colour is sold in
    enum class finish : uint8_t
    {
        original,
        solid,
        metallic,
        pearl,
        tricoat,
        satin,
        matte,
        iridescent,
        chameleon,
        chrome,
        carbon,
        count
    };

    struct finish_info
    {
        const char* name;
        const char* description;
    };

    inline constexpr finish_info finishes[] =
    {
        { "ORIGINAL",     "The finish the colour is sold in." },
        { "SOLID",        "Solid colour under a deep gloss clear coat." },
        { "METALLIC",     "Aluminium flakes in the base coat sparkle as the light moves." },
        { "PEARL",        "Mica pearl that glows toward a lighter tint at glancing angles." },
        { "TRI-COAT",     "Three layer candy, a tinted coat over a bright base for depth." },
        { "SATIN",        "Low sheen, the reflections soften to a silky glow." },
        { "MATTE",        "Flat matte, no clear coat gloss at all." },
        { "IRIDESCENT",   "Thin film interference, the colour travels through hues with the angle." },
        { "CHAMELEON",    "Colour shift pigment over a thick film, it flips hue across the body." },
        { "CHROME",       "Mirror chrome tinted with the colour." },
        { "CARBON",       "Bare carbon weave under clear coat, tinted with the colour." },
    };
    static_assert(sizeof(finishes) / sizeof(finishes[0]) == static_cast<size_t>(finish::count));

    inline constexpr int finish_count = static_cast<int>(finish::count);

    inline float srgb_to_linear(uint8_t value)
    {
        const float c = value / 255.0f;
        return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
}
