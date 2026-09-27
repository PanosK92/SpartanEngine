/*
Copyright(c) 2015-2026 Panos Karabelas

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

#include "ImGui_Style.h"
#include "source/imgui_internal.h"
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
                Style::color_accent_1,
                0.10f
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
            ? Style::color_accent_line
            : hovered
                ? Style::color_border_strong
                : Style::color_border;
        draw_list->AddRect(
            min,
            max,
            color(border),
            scaled(rounding),
            scaled(1.0f),
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
            scaled(4.0f)
        );
        draw_list->AddRect(min, max, color(alpha(foreground, 0.22f)), scaled(4.0f), scaled(1.0f));
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
            Style::color_accent_1
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonHovered,
            Style::color_accent_hi
        );
        ImGui::PushStyleColor(
            ImGuiCol_ButtonActive,
            Style::lerp(
                Style::color_accent_1,
                Style::color_accent_2,
                0.20f
            )
        );
        // bright accents need dark labels, darker presets keep white ones
        const ImVec4 accent = Style::color_accent_1;
        const float brightness = accent.x * 0.2126f + accent.y * 0.7152f + accent.z * 0.0722f;
        ImGui::PushStyleColor(ImGuiCol_Text, brightness > 0.55f
            ? ImVec4(0.039f, 0.059f, 0.078f, 1.0f)
            : ImVec4(1, 1, 1, 1));
    }

    inline void pop_primary_button()
    {
        ImGui::PopStyleColor(4);
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
            ImVec4(0, 0, 0, 0)
        );
        ImGui::PushStyleColor(
            ImGuiCol_TableRowBgAlt,
            alpha(Style::color_text, 0.025f)
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

    // soft halo around a rect, rings fading outward, cheap enough for every frame
    inline void draw_glow(
        ImDrawList* draw_list,
        const ImVec2& min,
        const ImVec2& max,
        const ImVec4& tint,
        const float rounding,
        const float radius,
        const float strength = 1.0f
    )
    {
        constexpr int rings = 6;
        const float step    = radius / static_cast<float>(rings);
        for (int i = 0; i < rings; i++)
        {
            const float t       = (static_cast<float>(i) + 0.5f) / static_cast<float>(rings);
            const float expand  = step * (static_cast<float>(i) + 0.5f);
            const float falloff = (1.0f - t) * (1.0f - t);
            draw_list->AddRect(
                ImVec2(min.x - expand, min.y - expand),
                ImVec2(max.x + expand, max.y + expand),
                color(alpha(tint, tint.w * falloff * 0.30f * strength)),
                rounding + expand,
                step + 0.5f
            );
        }
    }

    // recolours the vertices added since vtx_start with a vertical gradient, anti-aliasing fringes keep their fade
    inline void shade_vertical(ImDrawList* draw_list, const int vtx_start, const float y0, const float y1, const ImVec4& top, const ImVec4& bottom)
    {
        const float height = ImMax(y1 - y0, 1.0f);
        for (int i = vtx_start; i < draw_list->VtxBuffer.Size; i++)
        {
            ImDrawVert& vertex = draw_list->VtxBuffer[i];
            ImVec4 value       = ImGui::Style::lerp(top, bottom, ImSaturate((vertex.pos.y - y0) / height));
            value.w           *= static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            vertex.col         = color(value);
        }
    }

    // an edge catching light from above: bright along the top, dissolved by fade_height below it
    inline void draw_lit_rim(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, const float rounding, const ImVec4& top, const ImVec4& bottom, const float fade_height)
    {
        const int vtx_start = draw_list->VtxBuffer.Size;
        draw_list->AddRect(ImVec2(min.x + 0.5f, min.y + 0.5f), ImVec2(max.x - 0.5f, max.y - 0.5f), IM_COL32_WHITE, rounding, ImMax(1.0f, scaled(1.0f)));
        shade_vertical(draw_list, vtx_start, min.y, min.y + fade_height, top, bottom);
    }

    // call right after a frame widget: it lifts on hover and takes a lit accent rim while it is being edited
    inline void decorate_field(const float rounding = -1.0f)
    {
        const ImGuiID id = ImGui::GetItemID();
        if (id == 0)
        {
            return;
        }

        const float hover = animate(id ^ 0x4a11c0deu, ImGui::IsItemHovered() ? 1.0f : 0.0f, 16.0f);
        const float focus = animate(id ^ 0x7f1e1d00u, ImGui::IsItemActive() ? 1.0f : 0.0f, 16.0f);
        if (hover < 0.01f && focus < 0.01f)
        {
            return;
        }

        const ImVec2 min      = ImGui::GetItemRectMin();
        const ImVec2 max      = ImGui::GetItemRectMax();
        const float radius    = rounding >= 0.0f ? rounding : ImGui::GetStyle().FrameRounding;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        if (focus > 0.01f)
        {
            draw_glow(draw_list, min, max, Style::color_accent_1, radius, scaled(6.0f), 0.8f * focus);
        }
        const ImVec4 rim = Style::lerp(alpha(Style::color_text, 0.14f * hover), alpha(Style::color_accent_1, 0.90f), focus);
        draw_list->AddRect(min, max, color(rim), radius, ImMax(1.0f, scaled(1.0f)));
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

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        if (!selected)
        {
            draw_list->AddRectFilled(min, max, color(alpha(Style::color_text, 0.045f)));
            return;
        }

        // a beam: a lit bar on the edge whose light fades out across the row
        const ImU32 strong = color(alpha(Style::color_accent_1, 0.26f));
        const ImU32 weak   = color(alpha(Style::color_accent_1, 0.06f));
        draw_list->AddRectFilledMultiColor(min, max, strong, weak, weak, strong);
        const float bar    = ImMax(2.0f, IM_ROUND(scaled(2.0f)));
        const ImVec2 b_min = ImVec2(min.x, min.y);
        const ImVec2 b_max = ImVec2(min.x + bar, max.y);
        draw_glow(draw_list, b_min, b_max, Style::color_accent_1, 0.0f, scaled(6.0f), 0.9f);
        draw_list->AddRectFilled(b_min, b_max, color(Style::color_accent_hi));
    }

    // letter spacing is in pixels, the uppercase micro labels use roughly a tenth of the font size
    inline float calc_text_tracked(const char* text, const float tracking, ImFont* font = nullptr, float font_size = 0.0f)
    {
        font      = font ? font : ImGui::GetFont();
        font_size = font_size > 0.0f ? font_size : ImGui::GetFontSize();

        float width = 0.0f;
        int glyphs  = 0;
        for (const char* c = text; *c;)
        {
            unsigned int codepoint = 0;
            const int length = ImMax(ImTextCharFromUtf8(&codepoint, c, nullptr), 1);
            width += font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, c, c + length).x;
            c += length;
            glyphs++;
        }

        return width + tracking * static_cast<float>(ImMax(glyphs - 1, 0));
    }

    inline void draw_text_tracked(
        ImDrawList* draw_list,
        ImVec2 position,
        const ImU32 tint,
        const char* text,
        const float tracking,
        ImFont* font = nullptr,
        float font_size = 0.0f
    )
    {
        font      = font ? font : ImGui::GetFont();
        font_size = font_size > 0.0f ? font_size : ImGui::GetFontSize();

        for (const char* c = text; *c;)
        {
            unsigned int codepoint = 0;
            const int length = ImMax(ImTextCharFromUtf8(&codepoint, c, nullptr), 1);
            draw_list->AddText(font, font_size, position, tint, c, c + length);
            position.x += font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, c, c + length).x + tracking;
            c += length;
        }
    }

    inline void to_upper(const char* text, char* buffer, const size_t buffer_size)
    {
        size_t i = 0;
        for (; text[i] && i + 1 < buffer_size; i++)
        {
            buffer[i] = (text[i] >= 'a' && text[i] <= 'z') ? static_cast<char>(text[i] - 32) : text[i];
        }
        buffer[i] = '\0';
    }

    inline float micro_label_size()
    {
        return ImGui::GetFontSize() * 0.80f;
    }

    inline float micro_label_width(const char* text, ImFont* font = nullptr)
    {
        char upper[128];
        to_upper(text, upper, sizeof(upper));
        const float size = micro_label_size();
        return calc_text_tracked(upper, size * 0.12f, font, size);
    }

    // uppercase, tracked, drawn at the given position and vertically centered on a line of the given height
    inline float draw_micro_label(ImDrawList* draw_list, const ImVec2& position, const float line_height, const char* text, const ImVec4& tint, ImFont* font = nullptr)
    {
        char upper[128];
        to_upper(text, upper, sizeof(upper));
        const float size     = micro_label_size();
        const float tracking = size * 0.12f;
        const float y        = position.y + (line_height - size) * 0.5f;
        draw_text_tracked(draw_list, ImVec2(position.x, IM_ROUND(y)), color(tint), upper, tracking, font, size);
        return calc_text_tracked(upper, tracking, font, size);
    }

    inline void micro_label(const char* text, const ImVec4& tint, ImFont* font = nullptr)
    {
        const ImVec2 position   = ImGui::GetCursorScreenPos();
        const float line_height = ImGui::GetTextLineHeight();
        const float width       = draw_micro_label(ImGui::GetWindowDrawList(), position, line_height, text, tint, font);
        ImGui::Dummy(ImVec2(width, line_height));
    }

    // "LABEL ——————" a tracked label followed by a hairline running to the edge of the content region
    inline void section_rule(const char* text, ImFont* font = nullptr)
    {
        ImDrawList* draw_list   = ImGui::GetWindowDrawList();
        const ImVec2 position   = ImGui::GetCursorScreenPos();
        const float line_height = ImGui::GetTextLineHeight();
        const float width       = draw_micro_label(draw_list, position, line_height, text, Style::color_text_muted, font);
        const float right       = position.x + ImGui::GetContentRegionAvail().x;
        const float y           = IM_ROUND(position.y + line_height * 0.5f);
        const float x           = position.x + width + scaled(10.0f);
        if (right > x)
        {
            draw_list->AddLine(ImVec2(x, y), ImVec2(right, y), color(Style::color_border), 1.0f);
        }
        ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, line_height));
    }

    inline void status_dot(ImDrawList* draw_list, const ImVec2& center, const float radius, const ImVec4& tint, const bool pulse = false)
    {
        if (pulse)
        {
            const float t = static_cast<float>(fmod(ImGui::GetTime(), 1.8)) / 1.8f;
            draw_list->AddCircleFilled(center, radius * (1.0f + t * 1.6f), color(alpha(tint, 0.35f * (1.0f - t))), 20);
        }
        draw_list->AddCircleFilled(center, radius * 2.2f, color(alpha(tint, 0.14f)), 20);
        draw_list->AddCircleFilled(center, radius, color(tint), 16);
    }

    inline void corner_brackets(ImDrawList* draw_list, const ImVec2& min, const ImVec2& max, const float length, const float thickness, const ImU32 tint)
    {
        const float h = thickness * 0.5f;
        draw_list->AddLine(ImVec2(min.x, min.y + h), ImVec2(min.x + length, min.y + h), tint, thickness);
        draw_list->AddLine(ImVec2(min.x + h, min.y), ImVec2(min.x + h, min.y + length), tint, thickness);
        draw_list->AddLine(ImVec2(max.x - length, min.y + h), ImVec2(max.x, min.y + h), tint, thickness);
        draw_list->AddLine(ImVec2(max.x - h, min.y), ImVec2(max.x - h, min.y + length), tint, thickness);
        draw_list->AddLine(ImVec2(min.x, max.y - h), ImVec2(min.x + length, max.y - h), tint, thickness);
        draw_list->AddLine(ImVec2(min.x + h, max.y - length), ImVec2(min.x + h, max.y), tint, thickness);
        draw_list->AddLine(ImVec2(max.x - length, max.y - h), ImVec2(max.x, max.y - h), tint, thickness);
        draw_list->AddLine(ImVec2(max.x - h, max.y - length), ImVec2(max.x - h, max.y), tint, thickness);
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
