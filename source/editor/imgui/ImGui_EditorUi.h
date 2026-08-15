/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files(the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

#include "ImGui_Style.h"
#include "Source/imgui_internal.h"
#include "Window.h"

namespace ImGui::EditorUi
{
    inline float scaled(const float value)
    {
        return value * spartan::Window::GetDpiScale();
    }

    inline ImVec2 scaled(const ImVec2& value)
    {
        return ImVec2(
            scaled(value.x),
            scaled(value.y)
        );
    }

    inline ImVec4 alpha(ImVec4 color, const float value)
    {
        color.w = value;
        return color;
    }

    inline ImU32 color(const ImVec4& value)
    {
        return ImGui::ColorConvertFloat4ToU32(value);
    }

    inline ImVec4 axis_color(const uint32_t index)
    {
        constexpr ImVec4 colors[] =
        {
            ImVec4(0.851f, 0.365f, 0.365f, 1.0f),
            ImVec4(0.400f, 0.702f, 0.416f, 1.0f),
            ImVec4(0.333f, 0.580f, 0.843f, 1.0f)
        };
        return colors[index < 3 ? index : 0];
    }

    inline float animate(
        const ImGuiID id,
        const float target,
        const float speed = 10.0f
    )
    {
        ImGuiStorage* storage = ImGui::GetStateStorage();
        float value = storage->GetFloat(id, 0.0f);
        const float amount = ImClamp(
            ImGui::GetIO().DeltaTime * speed,
            0.0f,
            1.0f
        );
        value = ImLerp(value, target, amount);
        storage->SetFloat(id, value);
        return value;
    }

    inline void draw_card(
        const ImVec2& min,
        const ImVec2& max,
        const bool hovered,
        const bool selected,
        const float rounding = 6.0f,
        ImGuiID animation_id = 0
    )
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        if (animation_id == 0)
        {
            animation_id = ImHashData(
                &min,
                sizeof(min),
                ImGui::GetCurrentWindow()->ID
            );
        }
        const float hover = animate(
            animation_id,
            hovered || selected ? 1.0f : 0.0f
        );
        ImVec4 background = Style::color_panel;
        if (selected)
        {
            background = Style::lerp(
                Style::color_panel,
                Style::color_accent_2,
                0.30f
            );
        }
        else if (hovered)
        {
            background = Style::color_surface_hover;
        }

        if (hover > 0.01f)
        {
            const ImVec2 shadow_min(
                min.x,
                min.y + scaled(3.0f)
            );
            const ImVec2 shadow_max(
                max.x,
                max.y + scaled(3.0f)
            );
            draw_list->AddRectFilled(
                shadow_min,
                shadow_max,
                IM_COL32(0, 0, 0, static_cast<int>(42.0f * hover)),
                scaled(rounding)
            );
        }

        draw_list->AddRectFilled(
            min,
            max,
            color(background),
            scaled(rounding)
        );

        const ImVec4 border = selected
            ? Style::color_accent_1
            : hovered
                ? Style::color_border_strong
                : Style::color_border;
        draw_list->AddRect(
            min,
            max,
            color(border),
            scaled(rounding),
            scaled(selected ? 1.5f : 1.0f),
            0
        );
    }

    inline void draw_chip(
        const char* text,
        const ImVec4& background,
        const ImVec4& foreground
    )
    {
        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const ImVec2 padding = scaled(ImVec2(7.0f, 3.0f));
        const ImVec2 min = ImGui::GetCursorScreenPos();
        const ImVec2 max(
            min.x + text_size.x + padding.x * 2.0f,
            min.y + text_size.y + padding.y * 2.0f
        );

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(
            min,
            max,
            color(background),
            scaled(8.0f)
        );
        draw_list->AddText(
            ImVec2(min.x + padding.x, min.y + padding.y),
            color(foreground),
            text
        );
        ImGui::Dummy(ImVec2(
            max.x - min.x,
            max.y - min.y
        ));
    }

    inline void push_primary_button()
    {
        ImGui::PushStyleColor(
            ImGuiCol_Button,
            Style::color_accent_2
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            Style::color_accent_1
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            Style::lerp(
                Style::color_accent_2,
                Style::color_canvas,
                0.24f
            )
        );
    }

    inline void pop_primary_button()
    {
        ImGui::PopStyleColor(3);
    }

    inline void panel_header(
        const char* label,
        const char* description = nullptr,
        ImFont* font = nullptr
    )
    {
        if (font)
        {
            ImGui::PushFont(font, 0.0f);
        }
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            Style::color_text
        );
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        if (font)
        {
            ImGui::PopFont();
        }

        if (description)
        {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                Style::color_text_muted
            );
            ImGui::TextWrapped("%s", description);
            ImGui::PopStyleColor();
        }

        ImGui::Dummy(ImVec2(0.0f, scaled(2.0f)));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, scaled(2.0f)));
    }

    inline void push_table_style()
    {
        ImGui::PushStyleColor(
            ImGuiCol_TableHeaderBg,
            Style::color_panel
        );
        ImGui::PushStyleColor(
            ImGuiCol_TableRowBg,
            Style::color_canvas
        );
        ImGui::PushStyleColor(
            ImGuiCol_TableRowBgAlt,
            Style::lerp(
                Style::color_canvas,
                Style::color_panel,
                0.32f
            )
        );
        ImGui::PushStyleColor(
            ImGuiCol_TableBorderStrong,
            Style::color_border
        );
        ImGui::PushStyleColor(
            ImGuiCol_TableBorderLight,
            alpha(Style::color_border, 0.55f)
        );
    }

    inline void pop_table_style()
    {
        ImGui::PopStyleColor(5);
    }

    inline void draw_row_highlight(
        const ImVec2& min,
        const ImVec2& max,
        const bool hovered,
        const bool selected
    )
    {
        if (!hovered && !selected)
        {
            return;
        }

        ImVec4 fill = selected
            ? Style::color_accent_2
            : Style::color_surface_hover;
        fill.w = selected ? 0.34f : 0.58f;
        ImGui::GetWindowDrawList()->AddRectFilled(
            min,
            max,
            color(fill),
            scaled(3.0f)
        );
    }

    inline float toolbar_icon_size()
    {
        return scaled(18.0f);
    }

    inline ImVec2 toolbar_button_size()
    {
        const float size = scaled(30.0f);
        return ImVec2(size, size);
    }
}
