/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============
#include "pch.h"
#include <sol/sol.hpp>
#include "ScriptingInternal.h"
#include "../ui/Hud.h"
//=========================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    void register_hud(sol::state_view state)
    {
        sol::table table = state.create_named_table("Hud");

        table["GetSize"] = []() -> tuple<float, float>
        {
            const Vector2 size = hud::get_size();
            return { size.x, size.y };
        };

        table["GetMousePosition"] = []() -> tuple<float, float>
        {
            const Vector2 position = hud::get_mouse_position();
            return { position.x, position.y };
        };

        table["LoadFont"] = [](const string& name, const string& path) -> bool
        {
            return hud::load_font(name, path);
        };

        table["Rect"] = [](float x, float y, float w, float h, const Vector4& color, sol::optional<float> rounding)
        {
            hud::rect(x, y, w, h, color, rounding.value_or(0.0f));
        };

        table["RectOutline"] = [](float x, float y, float w, float h, const Vector4& color, sol::optional<float> thickness, sol::optional<float> rounding)
        {
            hud::rect_outline(x, y, w, h, color, thickness.value_or(1.0f), rounding.value_or(0.0f));
        };

        table["RectGradient"] = [](float x, float y, float w, float h, const Vector4& top_left, const Vector4& top_right, const Vector4& bottom_right, const Vector4& bottom_left)
        {
            hud::rect_gradient(x, y, w, h, top_left, top_right, bottom_right, bottom_left);
        };

        table["Line"] = [](float x1, float y1, float x2, float y2, const Vector4& color, sol::optional<float> thickness)
        {
            hud::line(x1, y1, x2, y2, color, thickness.value_or(1.0f));
        };

        table["Circle"] = [](float x, float y, float radius, const Vector4& color, sol::optional<float> thickness)
        {
            hud::circle(x, y, radius, color, thickness.value_or(0.0f));
        };

        table["Triangle"] = [](float x1, float y1, float x2, float y2, float x3, float y3, const Vector4& color)
        {
            hud::triangle(x1, y1, x2, y2, x3, y3, color);
        };

        table["TextWidth"] = [](const string& text, float size, sol::optional<string> font, sol::optional<float> spacing) -> float
        {
            return hud::text_width(text, size, font.value_or("regular"), spacing.value_or(0.0f));
        };

        table["Text"] = [](const string& text, float x, float y, float size, const Vector4& color, sol::optional<string> font, sol::optional<float> align, sol::optional<float> spacing) -> float
        {
            return hud::text(text, x, y, size, color, font.value_or("regular"), align.value_or(0.0f), spacing.value_or(0.0f));
        };
    }
}
