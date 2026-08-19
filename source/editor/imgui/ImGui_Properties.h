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

//= INCLUDES ====================
#include <string>
#include <vector>
#include "ImGui_Extension.h"
#include "source/imgui_stdlib.h"
//===============================

// the shared inspector widget kit, a label column on the left and a value column on the right
// the properties panel and the terrain editor both draw through this so a row looks the same
// wherever it lives
namespace editor_ui
{
    //----------------------------------------------------------
    // design system - consistent spacing, colors, and dimensions
    //----------------------------------------------------------

    namespace design
    {
        // spacing
        constexpr float spacing_xs     = 2.0f;
        constexpr float spacing_sm     = 4.0f;
        constexpr float spacing_md     = 8.0f;
        constexpr float spacing_lg     = 12.0f;
        constexpr float spacing_xl     = 16.0f;
        constexpr float spacing_xxl    = 24.0f;

        // layout
        constexpr float label_width    = 0.38f;  // percentage of available width
        constexpr float row_height     = 26.0f;
        constexpr float section_gap    = 6.0f;

        // component accent colors (subtle, professional)
        inline ImVec4 accent_entity()     { return ImVec4(0.45f, 0.55f, 0.70f, 1.0f); }
        inline ImVec4 accent_light()      { return ImVec4(0.85f, 0.75f, 0.35f, 1.0f); }
        inline ImVec4 accent_camera()     { return ImVec4(0.50f, 0.70f, 0.55f, 1.0f); }
        inline ImVec4 accent_render()     { return ImVec4(0.60f, 0.50f, 0.70f, 1.0f); }
        inline ImVec4 accent_material()   { return ImVec4(0.70f, 0.55f, 0.50f, 1.0f); }
        inline ImVec4 accent_physics()    { return ImVec4(0.55f, 0.65f, 0.80f, 1.0f); }
        inline ImVec4 accent_audio()      { return ImVec4(0.70f, 0.45f, 0.55f, 1.0f); }
        inline ImVec4 accent_terrain()    { return ImVec4(0.50f, 0.70f, 0.45f, 1.0f); }
        inline ImVec4 accent_volume()     { return ImVec4(0.55f, 0.55f, 0.75f, 1.0f); }
        inline ImVec4 accent_spline()          { return ImVec4(0.30f, 0.75f, 0.70f, 1.0f); }
        inline ImVec4 accent_spline_follower() { return ImVec4(0.35f, 0.80f, 0.65f, 1.0f); }
        inline ImVec4 accent_script()          { return ImVec4(0.60f, 0.70f, 0.50f, 1.0f); }
        inline ImVec4 accent_particles() { return ImVec4(0.90f, 0.55f, 0.30f, 1.0f); }
        inline ImVec4 accent_water()     { return ImVec4(0.30f, 0.60f, 0.80f, 1.0f); }
        inline ImVec4 accent_text_3d()   { return ImVec4(0.75f, 0.55f, 0.85f, 1.0f); }

        // states
        inline ImVec4 warning() { return ImGui::Style::color_warning; }
        inline ImVec4 ok()      { return ImGui::Style::color_ok; }

        // helper to get dimmed version for backgrounds
        inline ImVec4 dimmed(const ImVec4& color, float factor = 0.15f)
        {
            return ImVec4(color.x * factor, color.y * factor, color.z * factor, 0.4f);
        }
    }

    //----------------------------------------------------------
    // layout helpers - consistent property row rendering
    //----------------------------------------------------------

    namespace layout
    {
        // get label column width
        inline float label_width()
        {
            return ImGui::GetContentRegionAvail().x * design::label_width;
        }

        // get value column width
        inline float value_width()
        {
            return ImGui::GetContentRegionAvail().x * (1.0f - design::label_width) - design::spacing_sm;
        }

        // start a property row with label
        inline void begin_property(const char* label, const char* tooltip = nullptr)
        {
            ImGui::AlignTextToFramePadding();

            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImGui::Style::color_text_muted
            );
            ImGui::TextUnformatted(label);
            ImGui::PopStyleColor();

            if (tooltip && ImGui::IsItemHovered())
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(300.0f);
                ImGui::TextUnformatted(tooltip);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }

            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(value_width());
        }

        // property row without label (for multi-value rows)
        inline void begin_value()
        {
            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(value_width());
        }

        // position cursor at value column (alias for begin_value)
        inline void move_to_value_column()
        {
            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(value_width());
        }

        // add vertical spacing between groups
        inline void group_spacing()
        {
            ImGui::Dummy(ImVec2(0, design::section_gap));
        }

        // draw a subtle horizontal separator
        inline void separator()
        {
            ImGui::Dummy(ImVec2(0, design::spacing_sm));
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(p.x, p.y),
                ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y),
                ImGui::EditorUi::color(
                    ImGui::Style::color_border
                ),
                1.0f
            );
            ImGui::Dummy(ImVec2(0, design::spacing_md));
        }

        // section header within a component
        inline void section_header(const char* title)
        {
            ImGui::Dummy(ImVec2(0, design::spacing_sm));
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImGui::Style::color_text
            );
            ImGui::PushFont(Editor::font_bold, 0.0f);
            ImGui::TextUnformatted(title);
            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, design::spacing_xs));
        }

        // muted caption line, use it for the one sentence that explains a section
        inline void caption(const char* text)
        {
            ImGui::PushStyleColor(
                ImGuiCol_Text,
                ImGui::Style::color_text_muted
            );
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }

    //----------------------------------------------------------
    // property widgets
    //----------------------------------------------------------

    // styled combo box
    inline bool property_combo(const char* label, const std::vector<std::string>& options, uint32_t* index, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        return ImGuiSp::combo_box(("##" + std::string(label)).c_str(), options, index);
    }

    // styled float input with drag
    inline bool property_float(const char* label, float* value, float speed = 0.1f, float min = 0.0f, float max = 0.0f, const char* tooltip = nullptr, const char* format = "%.3f")
    {
        layout::begin_property(label, tooltip);
        return ImGuiSp::draw_float_wrap(("##" + std::string(label)).c_str(), value, speed, min, max, format);
    }

    // styled uint input with drag
    inline bool property_uint(
        const char* label,
        uint32_t* value,
        float speed = 1.0f,
        uint32_t min = 0,
        uint32_t max = 0,
        const char* tooltip = nullptr
    )
    {
        layout::begin_property(label, tooltip);
        int v = static_cast<int>(*value);
        const bool changed = ImGui::DragInt(
            ("##" + std::string(label)).c_str(),
            &v,
            speed,
            static_cast<int>(min),
            static_cast<int>(max)
        );
        if (changed)
        {
            *value = static_cast<uint32_t>(v < 0 ? 0 : v);
        }
        return changed;
    }

    // styled toggle switch
    inline bool property_toggle(const char* label, bool* value, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        return ImGuiSp::toggle_switch(("##" + std::string(label)).c_str(), value);
    }

    // styled text input (read-only display)
    inline void property_text(const char* label, const std::string& text, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::TextUnformatted(text.c_str());
        ImGui::PopStyleColor();
    }

    // styled text input field
    inline void property_input_text(const char* label, std::string* text, bool readonly = false, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AutoSelectAll;
        if (readonly)
        {
            flags |= ImGuiInputTextFlags_ReadOnly;
        }
        ImGui::InputText(("##" + std::string(label)).c_str(), text, flags);
    }

    // read only path with a browse button, true when the user asked to browse
    inline bool property_path(const char* label, const std::string& path, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);

        const float browse_width = ImGui::EditorUi::scaled(28.0f);
        const float input_width  = ImGui::CalcItemWidth() - browse_width - design::spacing_sm;

        std::string shown = path;
        ImGui::PushItemWidth(input_width);
        ImGui::InputText(("##" + std::string(label)).c_str(), &shown, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopItemWidth();

        ImGui::SameLine(0, design::spacing_sm);

        ImGui::PushID(label);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        const bool browse = ImGuiSp::button("...", ImVec2(browse_width, 0.0f));
        ImGui::PopStyleVar();
        ImGui::PopID();

        return browse;
    }

    // a band with two handles, one row instead of a min row and a max row
    // this is the widget every slope and altitude range should be authored with
    inline bool property_range(
        const char* label,
        float* value_min,
        float* value_max,
        float limit_min,
        float limit_max,
        const char* tooltip = nullptr,
        const char* format  = "%.0f"
    )
    {
        layout::begin_property(label, tooltip);

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
        {
            return false;
        }

        const float width  = ImGui::CalcItemWidth();
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos   = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(pos.x + width, pos.y + height));
        const ImGuiID id   = window->GetID(label);

        ImGui::ItemSize(bb, ImGui::GetStyle().FramePadding.y);
        if (!ImGui::ItemAdd(bb, id))
        {
            return false;
        }

        bool hovered = false;
        bool held    = false;
        ImGui::ButtonBehavior(bb, id, &hovered, &held);

        const float span     = limit_max - limit_min;
        const float radius   = height * 0.34f;
        const float track_x0 = bb.Min.x + radius;
        const float track_x1 = bb.Max.x - radius;
        const float track_w  = ImMax(track_x1 - track_x0, 1.0f);

        auto to_x = [&](const float value)
        {
            const float t = span > 0.0f ? ImClamp((value - limit_min) / span, 0.0f, 1.0f) : 0.0f;
            return track_x0 + t * track_w;
        };

        // which of the two handles the current drag owns, decided on press
        ImGuiStorage* storage      = ImGui::GetStateStorage();
        const ImGuiID key_handle   = id + 1;
        bool changed               = false;

        if (ImGui::IsItemActivated())
        {
            const float mouse_x = ImGui::GetIO().MousePos.x;
            const bool grab_min = fabsf(mouse_x - to_x(*value_min)) <= fabsf(mouse_x - to_x(*value_max));
            storage->SetInt(key_handle, grab_min ? 0 : 1);
        }

        if (held && span > 0.0f)
        {
            const float t     = ImClamp((ImGui::GetIO().MousePos.x - track_x0) / track_w, 0.0f, 1.0f);
            const float value = limit_min + t * span;

            if (storage->GetInt(key_handle, 0) == 0)
            {
                const float clamped = ImMin(value, *value_max);
                if (clamped != *value_min)
                {
                    *value_min = clamped;
                    changed    = true;
                }
            }
            else
            {
                const float clamped = ImMax(value, *value_min);
                if (clamped != *value_max)
                {
                    *value_max = clamped;
                    changed    = true;
                }
            }
        }

        // draw
        ImDrawList* draw_list = window->DrawList;
        const float track_y   = bb.Min.y + height * 0.5f;
        const float track_h   = ImGui::EditorUi::scaled(4.0f);
        const float x_min     = to_x(*value_min);
        const float x_max     = to_x(*value_max);

        draw_list->AddRectFilled(
            ImVec2(bb.Min.x, track_y - track_h * 0.5f),
            ImVec2(bb.Max.x, track_y + track_h * 0.5f),
            ImGui::EditorUi::color(ImGui::Style::color_surface),
            track_h
        );
        draw_list->AddRectFilled(
            ImVec2(x_min, track_y - track_h * 0.5f),
            ImVec2(x_max, track_y + track_h * 0.5f),
            ImGui::EditorUi::color(
                hovered || held ? ImGui::Style::color_accent_1 : ImGui::Style::color_accent_2
            ),
            track_h
        );

        const ImU32 knob = ImGui::EditorUi::color(ImGui::Style::color_text);
        draw_list->AddCircleFilled(ImVec2(x_min, track_y), radius, knob, 20);
        draw_list->AddCircleFilled(ImVec2(x_max, track_y), radius, knob, 20);

        // the numbers ride on top of the band, no extra row spent on them
        char text[96];
        char text_min[32];
        char text_max[32];
        snprintf(text_min, sizeof(text_min), format, *value_min);
        snprintf(text_max, sizeof(text_max), format, *value_max);
        snprintf(text, sizeof(text), "%s  to  %s", text_min, text_max);

        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const ImVec2 text_pos(
            bb.Min.x + (width - text_size.x) * 0.5f,
            bb.Min.y + (height - text_size.y) * 0.5f
        );
        draw_list->AddRectFilled(
            ImVec2(text_pos.x - ImGui::EditorUi::scaled(4.0f), text_pos.y),
            ImVec2(text_pos.x + text_size.x + ImGui::EditorUi::scaled(4.0f), text_pos.y + text_size.y),
            ImGui::EditorUi::color(
                ImGui::EditorUi::alpha(ImGui::Style::color_canvas, 0.82f)
            ),
            ImGui::EditorUi::scaled(3.0f)
        );
        draw_list->AddText(text_pos, ImGui::EditorUi::color(ImGui::Style::color_text), text);

        return changed;
    }

    // a signed influence row, the bar grows left of center for negative and right for positive
    // so a whole block of them reads as a shape rather than as a column of numbers
    inline bool property_influence(const char* label, float* value, const char* tooltip = nullptr)
    {
        const bool changed = property_float(label, value, 0.01f, -1.0f, 1.0f, tooltip, "%.2f");

        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        const float center    = (item_min.x + item_max.x) * 0.5f;
        const float half      = (item_max.x - item_min.x) * 0.5f;
        const float y         = item_max.y - ImGui::EditorUi::scaled(2.0f);
        const float amount    = ImClamp(*value, -1.0f, 1.0f);

        ImGui::GetWindowDrawList()->AddRectFilled(
            ImVec2(ImMin(center, center + half * amount), y),
            ImVec2(ImMax(center, center + half * amount), y + ImGui::EditorUi::scaled(2.0f)),
            ImGui::EditorUi::color(
                amount >= 0.0f ? ImGui::Style::color_accent_2 : ImGui::Style::color_accent_1
            )
        );

        return changed;
    }

    // read only bar with a caption, use it to show what a rule actually produced
    inline void property_meter(const char* label, float fraction, const char* text, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);

        const float width  = ImGui::CalcItemWidth();
        const float height = ImGui::GetFrameHeight();
        const ImVec2 pos   = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        const float bar_h = ImGui::EditorUi::scaled(6.0f);
        const float bar_y = pos.y + (height - bar_h) * 0.5f;

        draw_list->AddRectFilled(
            ImVec2(pos.x, bar_y),
            ImVec2(pos.x + width, bar_y + bar_h),
            ImGui::EditorUi::color(ImGui::Style::color_surface),
            bar_h
        );
        draw_list->AddRectFilled(
            ImVec2(pos.x, bar_y),
            ImVec2(pos.x + width * ImClamp(fraction, 0.0f, 1.0f), bar_y + bar_h),
            ImGui::EditorUi::color(ImGui::Style::color_accent_2),
            bar_h
        );

        if (text)
        {
            const ImVec2 text_size = ImGui::CalcTextSize(text);
            draw_list->AddText(
                ImVec2(pos.x + width - text_size.x, pos.y + (height - text_size.y) * 0.5f),
                ImGui::EditorUi::color(ImGui::Style::color_text),
                text
            );
        }

        ImGui::Dummy(ImVec2(width, height));
    }

    // one pill row of exclusive choices, this is the mode switch
    inline bool segmented_control(const char* id, const std::vector<std::string>& labels, uint32_t* index, float width = -1.0f)
    {
        if (labels.empty())
        {
            return false;
        }

        ImGui::PushID(id);

        const float height = ImGui::GetFrameHeight() + ImGui::EditorUi::scaled(4.0f);
        const float total  = width > 0.0f ? width : ImGui::GetContentRegionAvail().x;
        const float cell   = total / static_cast<float>(labels.size());
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(
            origin,
            ImVec2(origin.x + total, origin.y + height),
            ImGui::EditorUi::color(ImGui::Style::color_canvas_deep),
            ImGui::EditorUi::scaled(6.0f)
        );

        bool changed = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(labels.size()); i++)
        {
            const ImVec2 cell_min(origin.x + cell * static_cast<float>(i), origin.y);
            const ImVec2 cell_max(cell_min.x + cell, cell_min.y + height);

            ImGui::SetCursorScreenPos(cell_min);
            ImGui::PushID(static_cast<int>(i));
            const bool pressed = ImGui::InvisibleButton("##cell", ImVec2(cell, height));
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            if (pressed && *index != i)
            {
                *index  = i;
                changed = true;
            }

            const bool selected = (*index == i);
            if (selected || hovered)
            {
                draw_list->AddRectFilled(
                    ImVec2(cell_min.x + ImGui::EditorUi::scaled(2.0f), cell_min.y + ImGui::EditorUi::scaled(2.0f)),
                    ImVec2(cell_max.x - ImGui::EditorUi::scaled(2.0f), cell_max.y - ImGui::EditorUi::scaled(2.0f)),
                    ImGui::EditorUi::color(
                        selected ? ImGui::Style::color_accent_2 : ImGui::Style::color_surface_hover
                    ),
                    ImGui::EditorUi::scaled(5.0f)
                );
            }

            const ImVec2 text_size = ImGui::CalcTextSize(labels[i].c_str());
            draw_list->AddText(
                ImVec2(cell_min.x + (cell - text_size.x) * 0.5f, cell_min.y + (height - text_size.y) * 0.5f),
                ImGui::EditorUi::color(
                    selected ? ImGui::Style::color_text : ImGui::Style::color_text_muted
                ),
                labels[i].c_str()
            );
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height));
        ImGui::Dummy(ImVec2(total, ImGui::EditorUi::scaled(2.0f)));
        ImGui::PopID();

        return changed;
    }

    // a titled sub panel, cards are what stop a long form from reading as one wall
    inline void card_begin(const char* title, const char* caption = nullptr)
    {
        ImGui::PushID(title);

        const ImVec4 background = ImGui::Style::lerp(
            ImGui::Style::color_canvas,
            ImGui::Style::color_panel,
            0.55f
        );

        ImGui::PushStyleColor(ImGuiCol_ChildBg, background);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(design::spacing_lg, design::spacing_md));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(design::spacing_sm, design::spacing_sm));
        ImGui::BeginChild(
            "##card",
            ImVec2(0, 0),
            ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding
        );

        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();

        if (caption)
        {
            layout::caption(caption);
        }

        ImGui::Dummy(ImVec2(0, design::spacing_xs));
    }

    inline void card_end()
    {
        ImGui::EndChild();
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, design::spacing_md));
    }

    // small inline status pill
    inline void chip(const char* text, const ImVec4& color)
    {
        ImGui::EditorUi::draw_chip(
            text,
            ImGui::EditorUi::alpha(color, 0.22f),
            color
        );
    }

    // a button that reads as the primary action of the panel
    inline bool primary_button(const char* label, const ImVec2& size = ImVec2(0, 0))
    {
        ImGui::EditorUi::push_primary_button();
        const bool pressed = ImGuiSp::button(label, size);
        ImGui::EditorUi::pop_primary_button();
        return pressed;
    }

    // a button that reads as pending work, amber means the viewport is out of date
    inline bool attention_button(const char* label, const bool attention, const ImVec2& size = ImVec2(0, 0))
    {
        if (!attention)
        {
            return ImGuiSp::button(label, size);
        }

        const ImVec4 amber = design::warning();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::EditorUi::alpha(amber, 0.85f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, amber);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::EditorUi::alpha(amber, 0.70f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_canvas_deep);
        const bool pressed = ImGuiSp::button(label, size);
        ImGui::PopStyleColor(4);
        return pressed;
    }
}
