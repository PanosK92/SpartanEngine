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

        // component accent colors, saturated and far apart so each component type is identifiable by color alone
        inline ImVec4 accent_entity()     { return ImVec4(0.62f, 0.78f, 1.00f, 1.0f); }
        inline ImVec4 accent_light()      { return ImVec4(1.00f, 0.78f, 0.30f, 1.0f); }
        inline ImVec4 accent_camera()     { return ImVec4(0.40f, 0.88f, 0.60f, 1.0f); }
        inline ImVec4 accent_render()     { return ImVec4(0.66f, 0.52f, 1.00f, 1.0f); }
        inline ImVec4 accent_material()   { return ImVec4(1.00f, 0.52f, 0.42f, 1.0f); }
        inline ImVec4 accent_physics()    { return ImVec4(0.36f, 0.62f, 1.00f, 1.0f); }
        inline ImVec4 accent_audio()      { return ImVec4(1.00f, 0.42f, 0.66f, 1.0f); }
        inline ImVec4 accent_terrain()    { return ImVec4(0.55f, 0.85f, 0.35f, 1.0f); }
        inline ImVec4 accent_volume()     { return ImVec4(0.58f, 0.58f, 1.00f, 1.0f); }
        inline ImVec4 accent_spline()          { return ImVec4(0.25f, 0.88f, 0.82f, 1.0f); }
        inline ImVec4 accent_spline_follower() { return ImVec4(0.30f, 0.92f, 0.62f, 1.0f); }
        inline ImVec4 accent_script()          { return ImVec4(0.80f, 0.90f, 0.40f, 1.0f); }
        inline ImVec4 accent_particles() { return ImVec4(1.00f, 0.60f, 0.25f, 1.0f); }
        inline ImVec4 accent_water()     { return ImVec4(0.25f, 0.70f, 1.00f, 1.0f); }
        inline ImVec4 accent_text_3d()   { return ImVec4(0.82f, 0.55f, 1.00f, 1.0f); }

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
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        }

        // property row without label (for multi-value rows)
        inline void begin_value()
        {
            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        }

        // position cursor at value column (alias for begin_value)
        inline void move_to_value_column()
        {
            ImGui::SameLine(label_width());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        }

        // add vertical spacing between groups
        inline void group_spacing()
        {
            ImGui::Dummy(ImVec2(0, design::section_gap));
        }

        // a soft rule that fades out toward both ends
        inline void separator()
        {
            ImGui::Dummy(ImVec2(0, design::spacing_sm));
            const ImVec2 p       = ImGui::GetCursorScreenPos();
            const float width    = ImGui::GetContentRegionAvail().x;
            const float center_x = IM_ROUND(p.x + width * 0.5f);
            const float y        = IM_ROUND(p.y);
            const ImU32 lit      = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.10f));
            const ImU32 clear    = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.0f));
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilledMultiColor(ImVec2(p.x, y), ImVec2(center_x, y + 1.0f), clear, lit, lit, clear);
            draw_list->AddRectFilledMultiColor(ImVec2(center_x, y), ImVec2(p.x + width, y + 1.0f), lit, clear, clear, lit);
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

        // 1 opens and 0 closes every fold drawn this frame, -1 leaves them to the user
        inline int fold_force = -1;

        // a collapsible group inside a component, a tracked title with a hairline and a summary
        // that keeps reporting what the group holds while it is closed
        inline bool fold(const char* title, const bool default_open = true, const char* summary = nullptr)
        {
            ImGui::PushID(title);

            ImGuiStorage* storage = ImGui::GetStateStorage();
            const ImGuiID id      = ImGui::GetID("##fold_state");
            if (fold_force >= 0)
            {
                storage->SetBool(id, fold_force == 1);
            }
            bool open = storage->GetBool(id, default_open);

            ImGui::Dummy(ImVec2(0, design::spacing_xs));
            const float width  = ImGui::GetContentRegionAvail().x;
            const float height = ImGui::GetTextLineHeight() + ImGui::EditorUi::scaled(8.0f);
            if (ImGui::InvisibleButton("##fold", ImVec2(width, height)))
            {
                open = !open;
                storage->SetBool(id, open);
            }
            const bool hovered = ImGui::IsItemHovered();
            const ImVec2 min   = ImGui::GetItemRectMin();
            const ImVec2 max   = ImGui::GetItemRectMax();

            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const float center_y  = IM_ROUND((min.y + max.y) * 0.5f);
            const float chevron   = ImGui::GetFontSize() * 0.45f;
            const float anim      = ImGui::EditorUi::animate(id + 1, open ? 1.0f : 0.0f, 16.0f);
            const ImVec4 tint     = hovered ? ImGui::Style::color_text : ImGui::Style::color_text_muted;
            ImGui::EditorUi::draw_chevron(draw_list, ImVec2(min.x + chevron * 0.5f, center_y), chevron, anim, tint);

            const float label_x = min.x + chevron + ImGui::EditorUi::scaled(8.0f);
            const float label_w = ImGui::EditorUi::draw_micro_label(draw_list, ImVec2(label_x, min.y), height, title, tint, Editor::font_bold);

            float rule_end = max.x;
            if (summary && summary[0] != '\0')
            {
                const ImVec2 size = ImGui::CalcTextSize(summary);
                rule_end          = max.x - size.x - ImGui::EditorUi::scaled(8.0f);
                draw_list->AddText(ImVec2(max.x - size.x, IM_ROUND(center_y - size.y * 0.5f)), ImGui::EditorUi::color(ImGui::Style::color_text_faint), summary);
            }

            const float rule_start = label_x + label_w + ImGui::EditorUi::scaled(10.0f);
            if (rule_end > rule_start)
            {
                draw_list->AddLine(ImVec2(rule_start, center_y), ImVec2(rule_end, center_y), ImGui::EditorUi::color(ImGui::Style::color_border), 1.0f);
            }

            ImGui::PopID();
            return open;
        }

        // one short sentence that explains the row above it, colored when it is a warning
        inline void note(const char* text, const ImVec4& tint)
        {
            const float radius = ImGui::EditorUi::scaled(2.5f);
            const ImVec2 pos   = ImGui::GetCursorScreenPos();
            const float line_h = ImGui::GetTextLineHeight();
            ImGui::EditorUi::status_dot(ImGui::GetWindowDrawList(), ImVec2(pos.x + radius * 2.0f, pos.y + line_h * 0.5f), radius, tint);
            ImGui::SetCursorScreenPos(ImVec2(pos.x + radius * 4.0f + ImGui::EditorUi::scaled(6.0f), pos.y));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::lerp(ImGui::Style::color_text_muted, tint, 0.55f));
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }
    }

    //----------------------------------------------------------
    // formatting - numbers people read at a glance
    //----------------------------------------------------------

    namespace format
    {
        // 8,500
        inline std::string grouped(const double value)
        {
            char digits[48];
            snprintf(digits, sizeof(digits), "%.0f", fabs(value));
            const size_t length = strlen(digits);
            std::string result;
            for (size_t i = 0; i < length; i++)
            {
                if (i > 0 && (length - i) % 3 == 0)
                {
                    result += ',';
                }
                result += digits[i];
            }
            return value <= -0.5 ? "-" + result : result;
        }

        // 950, 4.2k, 38k, 1.2M
        inline std::string compact(const double value)
        {
            const double magnitude = fabs(value);
            char buffer[32];
            if (magnitude >= 1e6)
            {
                snprintf(buffer, sizeof(buffer), "%.1fM", value / 1e6);
            }
            else if (magnitude >= 1e4)
            {
                snprintf(buffer, sizeof(buffer), "%.0fk", value / 1e3);
            }
            else if (magnitude >= 1e3)
            {
                snprintf(buffer, sizeof(buffer), "%.1fk", value / 1e3);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%.0f", value);
            }
            return buffer;
        }

        // photographers read shutter speeds as fractions, 1/250 s
        inline std::string shutter(const float seconds)
        {
            char buffer[32];
            if (seconds >= 0.3f)
            {
                snprintf(buffer, sizeof(buffer), "%.1f s", seconds);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "1/%.0f s", 1.0f / ImMax(seconds, 0.00001f));
            }
            return buffer;
        }

        // 3.44 GB, 128 MB, 9.8 KB, 512 B, binary units the way drivers and the os report them
        inline std::string bytes(const double value)
        {
            char buffer[32];
            const double magnitude = fabs(value);
            if (magnitude >= 1024.0 * 1024.0 * 1024.0)
            {
                snprintf(buffer, sizeof(buffer), "%.2f GB", value / (1024.0 * 1024.0 * 1024.0));
            }
            else if (magnitude >= 1024.0 * 1024.0 * 100.0)
            {
                snprintf(buffer, sizeof(buffer), "%.0f MB", value / (1024.0 * 1024.0));
            }
            else if (magnitude >= 1024.0 * 1024.0)
            {
                snprintf(buffer, sizeof(buffer), "%.1f MB", value / (1024.0 * 1024.0));
            }
            else if (magnitude >= 1024.0)
            {
                snprintf(buffer, sizeof(buffer), "%.1f KB", value / 1024.0);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%.0f B", value);
            }
            return buffer;
        }

        // 16.7 ms, 0.66 ms, 42 µs, three significant digits is all a frame budget needs
        inline std::string milliseconds(const float value)
        {
            char buffer[32];
            if (value >= 100.0f)
            {
                snprintf(buffer, sizeof(buffer), "%.0f ms", value);
            }
            else if (value >= 10.0f)
            {
                snprintf(buffer, sizeof(buffer), "%.1f ms", value);
            }
            else if (value >= 0.1f)
            {
                snprintf(buffer, sizeof(buffer), "%.2f ms", value);
            }
            else
            {
                snprintf(buffer, sizeof(buffer), "%.0f \xC2\xB5s", value * 1000.0f);
            }
            return buffer;
        }

        // doubles the percent signs so finished text can be handed to a slider as its format
        inline std::string literal(const std::string& text)
        {
            std::string result;
            for (const char c : text)
            {
                result += c;
                if (c == '%')
                {
                    result += '%';
                }
            }
            return result;
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
        ImGui::EditorUi::decorate_field();
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
        ImGui::EditorUi::decorate_field();
    }

    // read only path with a browse button, true when the user asked to browse
    inline bool property_path(const char* label, const std::string& path, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);

        const float browse_width = ImGui::EditorUi::scaled(28.0f);
        std::string shown = path;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - browse_width - design::spacing_sm);
        ImGui::InputText(("##" + std::string(label)).c_str(), &shown, ImGuiInputTextFlags_ReadOnly);

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

        // the label sits in the widest free stretch of track so it never lands on a handle
        const ImVec2 text_size = ImGui::CalcTextSize(text);
        const float clearance  = radius + ImGui::EditorUi::scaled(8.0f);
        const float free_left  = x_min - clearance - bb.Min.x;
        const float free_right = bb.Max.x - (x_max + clearance);
        float text_x           = bb.Min.x + (width - text_size.x) * 0.5f;
        bool clear             = false;
        if (free_left >= free_right && free_left >= text_size.x)
        {
            text_x = x_min - clearance - text_size.x;
            clear  = true;
        }
        else if (free_right >= text_size.x)
        {
            text_x = x_max + clearance;
            clear  = true;
        }
        const ImVec2 text_pos(IM_ROUND(text_x), IM_ROUND(bb.Min.y + (height - text_size.y) * 0.5f));
        draw_list->AddRectFilled(
            ImVec2(text_pos.x - ImGui::EditorUi::scaled(4.0f), text_pos.y),
            ImVec2(text_pos.x + text_size.x + ImGui::EditorUi::scaled(4.0f), text_pos.y + text_size.y),
            ImGui::EditorUi::color(clear ? ImGui::Style::color_canvas : ImGui::EditorUi::alpha(ImGui::Style::color_canvas, 0.96f)),
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

    // a bounded value, a slider says there is a range where a drag field hides it
    inline bool property_slider(
        const char* label,
        float* value,
        const float min,
        const float max,
        const char* format           = "%.2f",
        const char* tooltip          = nullptr,
        const ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp
    )
    {
        layout::begin_property(label, tooltip);
        const bool changed = ImGui::SliderFloat(("##" + std::string(label)).c_str(), value, min, max, format, flags);
        ImGui::EditorUi::decorate_field();
        return changed;
    }

    // a normalized quantity shown as a percentage, the stored value stays 0 to 1
    inline bool property_percent(const char* label, float* value, const char* tooltip = nullptr, const float max = 1.0f)
    {
        float percent = *value * 100.0f;
        if (property_slider(label, &percent, 0.0f, max * 100.0f, "%.0f%%", tooltip))
        {
            *value = percent / 100.0f;
            return true;
        }
        return false;
    }

    // exclusive choices that fit on one line, every option stays visible and one click away
    inline bool property_segmented(const char* label, const std::vector<std::string>& labels, uint32_t* index, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        return segmented_control(label, labels, index);
    }

    // x y z on/off pills in the gizmo's axis colors, so a locked axis maps straight onto the viewport
    inline bool property_axes(const char* label, bool* x, bool* y, bool* z, const char* tooltip = nullptr)
    {
        layout::begin_property(label, tooltip);
        ImGui::PushID(label);

        bool* values[3]       = { x, y, z };
        const char* names[3]  = { "X", "Y", "Z" };
        const float gap       = ImGui::EditorUi::scaled(4.0f);
        const float width     = (ImGui::GetContentRegionAvail().x - gap * 2.0f) / 3.0f;
        const float height    = ImGui::GetFrameHeight();
        const float rounding  = ImGui::GetStyle().FrameRounding;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        bool changed          = false;

        for (int i = 0; i < 3; i++)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, gap);
            }

            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##axis", ImVec2(width, height)))
            {
                *values[i] = !*values[i];
                changed    = true;
            }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            const ImVec2 min  = ImGui::GetItemRectMin();
            const ImVec2 max  = ImGui::GetItemRectMax();
            const ImVec4 tint = ImGui::EditorUi::axis_color(i);
            const bool on     = *values[i];
            const ImVec4 fill = on ? ImGui::EditorUi::alpha(tint, 0.32f) : (hovered ? ImGui::Style::color_surface_hover : ImGui::Style::color_canvas_deep);
            draw_list->AddRectFilled(min, max, ImGui::EditorUi::color(fill), rounding);
            draw_list->AddRect(min, max, ImGui::EditorUi::color(on ? ImGui::EditorUi::alpha(tint, 0.85f) : ImGui::Style::color_border), rounding, 1.0f);

            const ImVec2 size = ImGui::CalcTextSize(names[i]);
            const ImVec4 text = on ? ImGui::Style::lerp(tint, ImVec4(1, 1, 1, 1), 0.45f) : ImGui::Style::color_text_faint;
            draw_list->AddText(ImVec2(IM_ROUND((min.x + max.x - size.x) * 0.5f), IM_ROUND((min.y + max.y - size.y) * 0.5f)), ImGui::EditorUi::color(text), names[i]);
        }

        ImGui::PopID();
        return changed;
    }

    // a drawing surface as wide as the component, the visuals that explain a component live on these
    // the whole surface is one item so it can be clicked and dragged
    inline ImDrawList* canvas(const char* id, const float height, ImVec2* min, ImVec2* max, bool* hovered = nullptr, bool* held = nullptr)
    {
        const float width = ImGui::GetContentRegionAvail().x;
        *min              = ImGui::GetCursorScreenPos();
        *max              = ImVec2(min->x + width, min->y + height);
        ImGui::InvisibleButton(id, ImVec2(width, height));
        if (hovered)
        {
            *hovered = ImGui::IsItemHovered();
        }
        if (held)
        {
            *held = ImGui::IsItemActive();
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(*min, *max, ImGui::EditorUi::color(ImGui::Style::color_canvas_deep), ImGui::EditorUi::scaled(6.0f));
        draw_list->AddRect(*min, *max, ImGui::EditorUi::color(ImGui::Style::color_border), ImGui::EditorUi::scaled(6.0f), 1.0f);
        return draw_list;
    }

    struct Stat
    {
        std::string value;
        const char* label = "";
        ImVec4 tint       = ImVec4(0, 0, 0, 0);
    };

    // a row of numbers with tracked captions, the facts worth knowing before reading a single field
    inline void stat_strip(const char* id, const std::vector<Stat>& stats)
    {
        if (stats.empty())
        {
            return;
        }

        const float pad_x     = ImGui::EditorUi::scaled(10.0f);
        const float pad_y     = ImGui::EditorUi::scaled(7.0f);
        const float value_h   = ImGui::GetFontSize();
        const float label_h   = ImGui::EditorUi::micro_label_size();
        const float height    = pad_y * 2.0f + value_h + label_h + ImGui::EditorUi::scaled(3.0f);
        ImVec2 min, max;
        ImDrawList* draw_list = canvas(id, height, &min, &max);
        const float cell      = (max.x - min.x) / static_cast<float>(stats.size());
        ImFont* bold          = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();

        for (size_t i = 0; i < stats.size(); i++)
        {
            const float x = min.x + cell * static_cast<float>(i);
            if (i > 0)
            {
                draw_list->AddLine(ImVec2(x, min.y + pad_y), ImVec2(x, max.y - pad_y), ImGui::EditorUi::color(ImGui::Style::color_border), 1.0f);
            }

            const ImVec4 tint = stats[i].tint.w > 0.0f ? stats[i].tint : ImGui::Style::color_text;
            draw_list->PushClipRect(ImVec2(x, min.y), ImVec2(x + cell, max.y), true);
            draw_list->AddText(bold, value_h, ImVec2(IM_ROUND(x + pad_x), IM_ROUND(min.y + pad_y)), ImGui::EditorUi::color(tint), stats[i].value.c_str());
            ImGui::EditorUi::draw_micro_label(draw_list, ImVec2(IM_ROUND(x + pad_x), IM_ROUND(min.y + pad_y + value_h + ImGui::EditorUi::scaled(3.0f))), label_h, stats[i].label, ImGui::Style::color_text_faint);
            draw_list->PopClipRect();
        }
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

    //----------------------------------------------------------
    // tool windows - the toolbars, filters and readouts of the windows the toolbar opens
    //----------------------------------------------------------

    namespace toolbar
    {
        inline void tooltip(const char* text)
        {
            if (text && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            {
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
                ImGui::TextUnformatted(text);
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        inline float pill_width(const char* label, const bool dot = false)
        {
            const float text = ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x;
            return text + ImGui::EditorUi::scaled(10.0f) * 2.0f + (dot ? ImGui::EditorUi::scaled(12.0f) : 0.0f);
        }

        // an on/off pill, outlined while off and tinted while on, with an optional leading status dot
        // every toggle and filter of a tool window is one of these, so on reads the same everywhere
        inline bool pill(const char* label, const bool active, const ImVec4& tint = ImGui::Style::color_accent_1, const char* tooltip_text = nullptr, const bool dot = false, const bool pulse = false)
        {
            const float height     = ImGui::GetFrameHeight();
            const float pad        = ImGui::EditorUi::scaled(10.0f);
            const char* label_end  = ImGui::FindRenderedTextEnd(label);
            const float dot_space  = dot ? ImGui::EditorUi::scaled(12.0f) : 0.0f;
            const bool pressed     = ImGui::InvisibleButton(label, ImVec2(pill_width(label, dot), height));
            const bool hovered     = ImGui::IsItemHovered();
            const ImVec2 min       = ImGui::GetItemRectMin();
            const ImVec2 max       = ImGui::GetItemRectMax();
            const float rounding   = height * 0.5f;
            ImDrawList* draw_list  = ImGui::GetWindowDrawList();

            ImVec4 fill = active ? ImGui::EditorUi::alpha(tint, 0.16f) : ImVec4(0, 0, 0, 0);
            if (hovered)
            {
                fill = active ? ImGui::EditorUi::alpha(tint, 0.26f) : ImGui::Style::color_surface_hover;
            }
            const ImVec4 border = active ? ImGui::EditorUi::alpha(tint, 0.55f) : ImGui::Style::color_border;
            const ImVec4 text   = active ? ImGui::Style::lerp(tint, ImGui::Style::color_text, 0.35f) : (hovered ? ImGui::Style::color_text : ImGui::Style::color_text_muted);

            draw_list->AddRectFilled(min, max, ImGui::EditorUi::color(fill), rounding);
            draw_list->AddRect(min, max, ImGui::EditorUi::color(border), rounding);
            if (dot)
            {
                const float radius = ImGui::EditorUi::scaled(3.0f);
                ImGui::EditorUi::status_dot(draw_list, ImVec2(min.x + pad + radius, (min.y + max.y) * 0.5f), radius, active ? tint : ImGui::EditorUi::alpha(tint, 0.55f), pulse);
            }
            const float text_h = ImGui::GetFontSize();
            draw_list->AddText(ImVec2(IM_ROUND(min.x + pad + dot_space), IM_ROUND(min.y + (height - text_h) * 0.5f)), ImGui::EditorUi::color(text), label, label_end);

            tooltip(tooltip_text);
            return pressed;
        }

        // a secondary action, text only until hovered so it never competes with the primary one
        inline bool ghost_button(const char* label, const char* tooltip_text = nullptr, const ImVec4& hover_tint = ImGui::Style::color_text, const ImVec2& size = ImVec2(0, 0))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::EditorUi::alpha(hover_tint, 0.12f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::EditorUi::alpha(hover_tint, 0.22f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::EditorUi::scaled(4.0f));
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(4);
            tooltip(tooltip_text);
            return pressed;
        }

        inline float ghost_button_width(const char* label)
        {
            return ImGui::CalcTextSize(label, ImGui::FindRenderedTextEnd(label)).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        }

        // the search field of every tool window, while it filters the match count sits inside it, red when nothing matches
        inline bool search(const char* id, const char* hint, ImGuiTextFilter& filter, const float width, const char* count = nullptr, const bool nothing = false)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ImGui::EditorUi::scaled(4.0f));
            ImGui::SetNextItemWidth(width);
            ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
            const bool changed = ImGui::InputTextWithHint(id, hint, filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf), ImGuiInputTextFlags_EscapeClearsAll);
            if (changed)
            {
                filter.Build();
            }
            ImGui::EditorUi::decorate_field();
            ImGui::PopStyleVar();

            if (count && filter.IsActive())
            {
                const ImVec2 min  = ImGui::GetItemRectMin();
                const ImVec2 max  = ImGui::GetItemRectMax();
                const ImVec2 size = ImGui::CalcTextSize(count);
                const float x     = max.x - size.x - ImGui::EditorUi::scaled(8.0f);
                if (x > min.x + ImGui::CalcTextSize(filter.InputBuf).x + ImGui::EditorUi::scaled(24.0f))
                {
                    const ImVec4 tint = nothing ? ImGui::Style::color_error : ImGui::Style::color_text_muted;
                    ImGui::GetWindowDrawList()->AddText(ImVec2(x, IM_ROUND((min.y + max.y - size.y) * 0.5f)), ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.85f)), count);
                }
            }
            return changed;
        }

        // a bounded value shaped like a pill, the fill is the value and dragging anywhere on it sets it
        inline bool slider(const char* id, float* value, const float min_value, const float max_value, const char* text, const float width, const char* tooltip_text = nullptr, const ImVec4& tint = ImGui::Style::color_accent_1)
        {
            const float height = ImGui::GetFrameHeight();
            ImGui::InvisibleButton(id, ImVec2(width, height));
            const bool hovered = ImGui::IsItemHovered();
            const bool active  = ImGui::IsItemActive();
            const ImVec2 min   = ImGui::GetItemRectMin();
            const ImVec2 max   = ImGui::GetItemRectMax();

            bool changed = false;
            if (active)
            {
                const float t    = ImClamp((ImGui::GetIO().MousePos.x - min.x) / ImMax(1.0f, width), 0.0f, 1.0f);
                const float next = min_value + t * (max_value - min_value);
                if (next != *value)
                {
                    *value  = next;
                    changed = true;
                }
            }

            const float rounding  = height * 0.5f;
            const float fraction  = ImClamp((*value - min_value) / ImMax(1e-6f, max_value - min_value), 0.0f, 1.0f);
            const float fill_x    = min.x + ImMax(rounding * 2.0f, width * fraction);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            draw_list->AddRectFilled(min, max, ImGui::EditorUi::color(hovered || active ? ImGui::Style::color_surface_hover : ImGui::Style::color_canvas_deep), rounding);
            draw_list->AddRectFilled(min, ImVec2(fill_x, max.y), ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, active ? 0.30f : 0.18f)), rounding);
            draw_list->AddRect(min, max, ImGui::EditorUi::color(active ? ImGui::EditorUi::alpha(tint, 0.70f) : ImGui::Style::color_border), rounding);

            const ImVec2 size = ImGui::CalcTextSize(text);
            draw_list->AddText(ImVec2(IM_ROUND((min.x + max.x - size.x) * 0.5f), IM_ROUND((min.y + max.y - size.y) * 0.5f)), ImGui::EditorUi::color(hovered || active ? ImGui::Style::color_text : ImGui::Style::color_text_muted), text);

            if (!active)
            {
                tooltip(tooltip_text);
            }
            return changed;
        }

        // a hairline between groups of a toolbar row, call it between two items on the same line
        inline void divider()
        {
            ImGui::SameLine(0, ImGui::EditorUi::scaled(8.0f));
            const ImVec2 pos   = ImGui::GetCursorScreenPos();
            const float height = ImGui::GetFrameHeight();
            const float inset  = height * 0.22f;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(IM_ROUND(pos.x), pos.y + inset), ImVec2(IM_ROUND(pos.x), pos.y + height - inset), ImGui::EditorUi::color(ImGui::Style::color_border), 1.0f);
            ImGui::Dummy(ImVec2(1.0f, height));
            ImGui::SameLine(0, ImGui::EditorUi::scaled(8.0f));
        }

        // continues the row with items that need the given width flush against the right edge
        inline void align_right(const float width)
        {
            ImGui::SameLine();
            const float available = ImGui::GetContentRegionAvail().x;
            if (available > width)
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + available - width);
            }
        }
    }

    // one part of a whole, drawn by stacked_bar and legend
    struct Segment
    {
        std::string label;
        double value = 0.0;
        ImVec4 tint  = ImVec4(1, 1, 1, 1);
        std::string detail;
    };

    // one bar split into what fills it, the composition of a budget at a glance, hovering a part names it
    // returns the index of the hovered segment, or -1
    inline int stacked_bar(const char* id, const std::vector<Segment>& segments, const double total, const float height, const int highlighted = -1)
    {
        const float width     = ImGui::GetContentRegionAvail().x;
        const ImVec2 min      = ImGui::GetCursorScreenPos();
        const ImVec2 max      = ImVec2(min.x + width, min.y + height);
        ImGui::InvisibleButton(id, ImVec2(width, height));
        const bool hovered    = ImGui::IsItemHovered();
        const float mouse_x   = ImGui::GetIO().MousePos.x;
        const float rounding  = ImGui::EditorUi::scaled(4.0f);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        draw_list->AddRectFilled(min, max, ImGui::EditorUi::color(ImGui::Style::color_canvas_deep), rounding);
        if (total <= 0.0)
        {
            return -1;
        }

        int first = -1;
        int last  = -1;
        for (int i = 0; i < static_cast<int>(segments.size()); i++)
        {
            if (segments[i].value > 0.0)
            {
                first = first < 0 ? i : first;
                last  = i;
            }
        }

        int hovered_index = -1;
        double cursor     = 0.0;
        for (int i = 0; i < static_cast<int>(segments.size()); i++)
        {
            const Segment& segment = segments[i];
            if (segment.value <= 0.0)
            {
                continue;
            }

            const float x0 = min.x + static_cast<float>(cursor / total) * width;
            cursor        += segment.value;
            const float x1 = ImMax(x0 + 1.0f, min.x + static_cast<float>(ImMin(cursor / total, 1.0)) * width);
            const bool over = hovered && mouse_x >= x0 && mouse_x < x1;
            if (over)
            {
                hovered_index = i;
            }

            ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
            if (i == first && i == last)
            {
                corners = ImDrawFlags_RoundCornersAll;
            }
            else if (i == first)
            {
                corners = ImDrawFlags_RoundCornersLeft;
            }
            else if (i == last && cursor >= total * 0.999)
            {
                corners = ImDrawFlags_RoundCornersRight;
            }

            const bool lit     = over || highlighted == i;
            const bool dimmed  = (hovered_index >= 0 || highlighted >= 0) && !lit;
            const ImVec4 tint  = lit ? ImGui::Style::lerp(segment.tint, ImVec4(1, 1, 1, 1), 0.18f) : ImGui::EditorUi::alpha(segment.tint, dimmed ? 0.55f : 1.0f);
            draw_list->AddRectFilled(ImVec2(x0, min.y), ImVec2(x1, max.y), ImGui::EditorUi::color(tint), rounding, corners);
            if (i != first)
            {
                draw_list->AddLine(ImVec2(IM_ROUND(x0), min.y), ImVec2(IM_ROUND(x0), max.y), ImGui::EditorUi::color(ImGui::Style::color_canvas_deep), 1.0f);
            }
        }

        if (hovered_index >= 0)
        {
            const Segment& segment = segments[hovered_index];
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(segment.label.c_str());
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
            ImGui::Text("%s, %.1f%% of the whole", segment.detail.c_str(), segment.value / total * 100.0);
            ImGui::PopStyleColor();
            ImGui::EndTooltip();
        }
        return hovered_index;
    }

    // a swatch, a name and a value per segment, wrapping to the width of the window
    inline void legend(const std::vector<Segment>& segments, const int highlighted = -1)
    {
        const float swatch    = ImGui::EditorUi::scaled(9.0f);
        const float gap       = ImGui::EditorUi::scaled(6.0f);
        const float spacing   = ImGui::EditorUi::scaled(18.0f);
        const float line_h    = ImGui::GetTextLineHeightWithSpacing();
        const float left      = ImGui::GetCursorScreenPos().x;
        const float right     = left + ImGui::GetContentRegionAvail().x;
        ImVec2 pos            = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const float start_y   = pos.y;

        for (int i = 0; i < static_cast<int>(segments.size()); i++)
        {
            const Segment& segment = segments[i];
            if (segment.value <= 0.0)
            {
                continue;
            }

            const float label_w = ImGui::CalcTextSize(segment.label.c_str()).x;
            const float value_w = ImGui::CalcTextSize(segment.detail.c_str()).x;
            const float width   = swatch + gap + label_w + gap + value_w;
            if (pos.x > left && pos.x + width > right)
            {
                pos.x  = left;
                pos.y += line_h;
            }

            const bool dimmed   = highlighted >= 0 && highlighted != i;
            const float text_h  = ImGui::GetTextLineHeight();
            const float swatch_y = IM_ROUND(pos.y + (text_h - swatch) * 0.5f);
            draw_list->AddRectFilled(ImVec2(pos.x, swatch_y), ImVec2(pos.x + swatch, swatch_y + swatch), ImGui::EditorUi::color(ImGui::EditorUi::alpha(segment.tint, dimmed ? 0.45f : 1.0f)), ImGui::EditorUi::scaled(2.0f));
            draw_list->AddText(ImVec2(pos.x + swatch + gap, pos.y), ImGui::EditorUi::color(dimmed ? ImGui::Style::color_text_muted : ImGui::Style::color_text), segment.label.c_str());
            draw_list->AddText(ImVec2(pos.x + swatch + gap + label_w + gap, pos.y), ImGui::EditorUi::color(dimmed ? ImGui::Style::color_text_faint : ImGui::Style::color_text_muted), segment.detail.c_str());
            pos.x += width + spacing;
        }

        ImGui::Dummy(ImVec2(right - left, pos.y - start_y + ImGui::GetTextLineHeight()));
    }

    // a table cell value drawn right aligned over a faint bar of its share, a sorted column then reads as a curve
    inline void cell_bar(const char* text, const float fraction, const ImVec4& tint)
    {
        const ImVec2 pos   = ImGui::GetCursorScreenPos();
        const float width  = ImGui::GetContentRegionAvail().x;
        const float height = ImGui::GetTextLineHeight();
        const float filled = width * ImClamp(fraction, 0.0f, 1.0f);
        if (filled >= 1.0f)
        {
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(pos.x + width - filled, pos.y + 1.0f), ImVec2(pos.x + width, pos.y + height - 1.0f), ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.20f)), ImGui::EditorUi::scaled(2.0f));
        }
        const float text_w = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorScreenPos(ImVec2(pos.x + ImMax(0.0f, width - text_w - ImGui::EditorUi::scaled(4.0f)), pos.y));
        ImGui::TextUnformatted(text);
    }

    // right aligned text in the current table cell, numbers line up on their last digit
    inline void text_right(const char* text, const ImVec4& tint)
    {
        const float width  = ImGui::GetContentRegionAvail().x;
        const float text_w = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImMax(0.0f, width - text_w - ImGui::EditorUi::scaled(4.0f)));
        ImGui::TextColored(tint, "%s", text);
    }

    // what an empty list says instead of nothing, a title and one line of help centered in the space left
    // returns true when the action button under it was pressed
    inline bool empty_state(const char* title, const char* detail, const char* action = nullptr)
    {
        const float width    = ImGui::GetContentRegionAvail().x;
        const float wrap     = ImMin(width - ImGui::EditorUi::scaled(32.0f), ImGui::EditorUi::scaled(420.0f));
        const float origin_x = ImGui::GetCursorPosX();
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + ImMax(ImGui::EditorUi::scaled(12.0f), ImGui::GetContentRegionAvail().y * 0.3f));

        ImGui::SetCursorPosX(origin_x + ImMax(0.0f, (width - ImGui::CalcTextSize(title).x) * 0.5f));
        ImGui::TextColored(ImGui::Style::color_text, "%s", title);

        if (detail)
        {
            const ImVec2 size = ImGui::CalcTextSize(detail, nullptr, false, wrap);
            const float x     = origin_x + ImMax(0.0f, (width - size.x) * 0.5f);
            ImGui::SetCursorPosX(x);
            ImGui::PushTextWrapPos(x + wrap);
            ImGui::TextColored(ImGui::Style::color_text_muted, "%s", detail);
            ImGui::PopTextWrapPos();
        }

        if (!action)
        {
            return false;
        }
        ImGui::Dummy(ImVec2(0, ImGui::EditorUi::scaled(4.0f)));
        ImGui::SetCursorPosX(origin_x + ImMax(0.0f, (width - toolbar::ghost_button_width(action)) * 0.5f));
        return ImGuiSp::button(action);
    }
}
