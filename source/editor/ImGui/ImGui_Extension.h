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

//= INCLUDES ======================
#include <string>
#include <variant>
#include <unordered_map>
#include "Definitions.h"
#include "Logging/Log.h"
#include "Window.h"
#include "RHI/RHI_Texture.h"
#include "Rendering/Renderer.h"
#include "World/World.h"
#include "Resource/ResourceCache.h"
#include "Core/ThreadPool.h"
#include "Display/Display.h"
#include "Source/imgui_internal.h"
#include "../Editor.h"
#include <Resource/ResourceCache.h>
//=================================

namespace ImGuiSp
{
    enum class DragPayloadType
    {
        Texture,
        Entity,
        Model,
        Audio,
        Material,
        Undefined
    };

    enum class ButtonPress
    {
        Yes,
        No,
        Undefined
    };

    static const ImVec4 default_tint(1, 1, 1, 1);

    // Collapsing header
    static bool collapsing_header(const char* label, ImGuiTreeNodeFlags flags = 0)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        bool result = ImGui::CollapsingHeader(label, flags);
        ImGui::PopStyleVar();
        return result;
    }

    // Button
    static bool button(const char* label, const ImVec2& size = ImVec2(0, 0))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        // use label as the id - cursor position was causing id changes between
        // frames due to floating point precision
        bool result = ImGui::Button(label, size);
        ImGui::PopStyleVar();
        return result;
    }

    static bool button_centered_on_line(const char* label, float alignment = 0.5f)
    {
        ImGuiStyle& style = ImGui::GetStyle();

        float size  = ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f;
        float avail = ImGui::GetContentRegionAvail().x;

        float off = (avail - size) * alignment;
        if (off > 0.0f)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
        }

        return ImGui::Button(label);
    }

    static bool image_button(spartan::RHI_Texture* texture, const spartan::math::Vector2& size, bool border, ImVec4 tint = {1,1,1,1})
    {
        if (!border)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        }

        // use the texture pointer as a stable id - cursor position was causing
        // id changes between frames due to floating point precision, which caused
        // clicks to not register properly (requiring multiple clicks)
        ImGui::PushID(texture);
        bool result = ImGui::ImageButton
        (
            "",                                     // str_id
            reinterpret_cast<ImTextureID>(texture), // user_texture_id
            size,                                   // size
            ImVec2(0, 0),                           // uv0
            ImVec2(1, 1),                           // uv1
            ImColor(0, 0, 0, 0),                    // bg_col
            tint                                    // tint_col
        );
        ImGui::PopID();

        if (!border)
        {
            ImGui::PopStyleVar();
        }

        return result;
    }

    static void image(spartan::RHI_Texture* texture, const spartan::math::Vector2& size, bool border = false)
    {
        if (!border)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        }

        ImGui::Image(
            reinterpret_cast<ImTextureID>(texture),
            size,
            ImVec2(0, 0),
            ImVec2(1, 1),
            default_tint,       // tint
            ImColor(0, 0, 0, 0) // border
        );

        if (!border)
        {
            ImGui::PopStyleVar();
        }
    }

    static void image(spartan::RHI_Texture* texture, const ImVec2& size, const ImVec4& tint = default_tint, const ImColor& border = ImColor(0, 0, 0, 0))
    {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(texture),
            size,
            ImVec2(0, 0),
            ImVec2(1, 1),
            tint,
            border
        );
    }

    static void image(const spartan::IconType icon, const float size)
    {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(spartan::ResourceCache::GetIcon(icon)),
            ImVec2(size, size),
            ImVec2(0, 0),
            ImVec2(1, 1),
            default_tint,       // tint
            ImColor(0, 0, 0, 0) // border
        );
    }

    static void image(const spartan::IconType icon, const float size,const ImVec4 tint)
    {
        ImGui::Image(
            reinterpret_cast<ImTextureID>(spartan::ResourceCache::GetIcon(icon)),
            ImVec2(size, size),
            ImVec2(0, 0),
            ImVec2(1, 1),
            tint,       // tint
            ImColor(0, 0, 0, 0) // border
        );
    }

    struct DragDropPayload
    {
        using DataVariant = std::variant<const char*, uint64_t>;
        DragDropPayload(const DragPayloadType type = DragPayloadType::Undefined, const DataVariant data = nullptr)
        {
            this->type = type;
            this->data = data;
        }
        DragPayloadType type;
        DataVariant data;
    };

    static void create_drag_drop_paylod(const DragDropPayload& payload)
    {
        ImGui::SetDragDropPayload(reinterpret_cast<const char*>(&payload.type), reinterpret_cast<const void*>(&payload), sizeof(payload), ImGuiCond_Once);
    }

    static DragDropPayload* receive_drag_drop_payload(DragPayloadType type)
    {
        DragDropPayload* result = nullptr;
        if (ImGui::BeginDragDropTarget())
        {
            if (const auto payload_imgui = ImGui::AcceptDragDropPayload(reinterpret_cast<const char*>(&type)))
            {
                result = static_cast<DragDropPayload*>(payload_imgui->Data);
            }
            ImGui::EndDragDropTarget();
        }

        return result;
    }

    // image slot
    static void image_slot(spartan::RHI_Texture* texture_in, const std::function<void(spartan::RHI_Texture*)>& setter)
    {
        const ImVec2 slot_size  = ImVec2(80 * spartan::Window::GetDpiScale());
        const float button_size = 15.0f * spartan::Window::GetDpiScale();

        // Image
        ImGui::BeginGroup();
        {
            spartan::RHI_Texture* texture = texture_in;
            const ImVec2 pos_image        = ImGui::GetCursorPos();
            const ImVec2 pos_button       = ImVec2(ImGui::GetCursorPosX() + slot_size.x - button_size * 2.0f + 6.0f, ImGui::GetCursorPosY() + 1.0f);

            // image
            ImVec4 colro_tint   = (texture != nullptr) ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 0);
            ImVec4 color_border = ImVec4(1, 1, 1, 0.5f);
            ImGui::SetCursorPos(pos_image);
            image(texture, slot_size, colro_tint, color_border);

            // x (remove) button
            if (texture != nullptr)
            {
                ImGui::SetCursorPos(pos_button);
                if (image_button(spartan::ResourceCache::GetIcon(spartan::IconType::X), button_size, true))
                {
                    texture = nullptr;
                    setter(nullptr);
                }
            }
        }
        ImGui::EndGroup();

        // drop target
        if (auto payload = receive_drag_drop_payload(DragPayloadType::Texture))
        {
            try
            {
                if (const auto tex = spartan::ResourceCache::Load<spartan::RHI_Texture>(std::get<const char*>(payload->data)).get())
                {
                    setter(tex);
                }
            }
            catch (const std::bad_variant_access& e) { SP_LOG_ERROR("%s", e.what()); }
        }
    }

    static void tooltip(const char* text)
    {
        SP_ASSERT_MSG(text != nullptr, "Text is null");

        if (ImGui::IsItemHovered())
        {
            ImGui::BeginTooltip();
            ImGui::Text(text);
            ImGui::EndTooltip();
        }
    }

    // a drag float which will wrap the mouse cursor around the edges of the screen
    static bool draw_float_wrap(const char* label, float* v, float v_speed = 1.0f, float v_min = 0.0f, float v_max = 0.0f, const char* format = "%.3f", const ImGuiSliderFlags flags = 0)
    {
        static const uint32_t screen_edge_padding = 10;
        ImGuiIO& io = ImGui::GetIO();
        
        static ImVec2 last_mouse_pos = io.MousePos;
        
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left))
        {
            ImVec2 mouse_pos = io.MousePos;
            bool wrapped = false;
        
            float left  = static_cast<float>(screen_edge_padding);
            float right = static_cast<float>(spartan::Display::GetWidth() - screen_edge_padding);
        
            if (mouse_pos.x >= right)
            {
                mouse_pos.x = left + 1;
                wrapped = true;
            }
            else if (mouse_pos.x <= left)
            {
                mouse_pos.x = right - 1;
                wrapped = true;
            }
        
            if (wrapped)
            {
                io.MousePos        = mouse_pos;
                io.WantSetMousePos = true;
                io.MouseDelta.x    = 0.0f;
                io.MouseDelta.y    = 0.0f;
        
                // update last_mouse_pos to avoid delta spikes in the next frame
                last_mouse_pos = mouse_pos;
            }
            else
            {
                // update last position normally
                last_mouse_pos = mouse_pos;
            }
        }
        
        ImGui::PushID(static_cast<int>(ImGui::GetCursorPosX() + ImGui::GetCursorPosY()));
        bool changed = ImGui::DragFloat(label, v, v_speed, v_min, v_max, format, flags);
        ImGui::PopID();
    
        return changed;
    }

    static bool combo_box(const char* label, const std::vector<std::string>& options, uint32_t* selection_index)
    {
        const uint32_t option_count = static_cast<uint32_t>(options.size());
    
        // clamp index
        if (*selection_index >= option_count)
        {
            *selection_index = option_count ? option_count - 1 : 0;
        }
    
        bool selection_made = false;
    
        // preview: direct pointer into existing string buffer
        const char* preview = option_count ? options[*selection_index].data() : "";
    
        if (ImGui::BeginCombo(label, preview))
        {
            for (uint32_t i = 0; i < option_count; ++i)
            {
                const bool is_selected = (*selection_index == i);
                // direct data() — null-terminated, no copy
                if (ImGui::Selectable(options[i].data(), is_selected))
                {
                    *selection_index = i;
                    selection_made     = true;
                }
                if (is_selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return selection_made;
    }

    static void vector3(const char* label, spartan::math::Vector3& vector, bool vertical = true)
    {
        // configuration
        const float label_indent = 15.0f * spartan::Window::GetDpiScale();
        const float axis_spacing = 15.0f * spartan::Window::GetDpiScale();
        const float step         = 0.01f;
    
        ImGui::PushID(label);
        ImGui::BeginGroup();
    
        // label
        ImGui::Indent(label_indent);
        ImGui::TextUnformatted(label);
        ImGui::Unindent(label_indent);
    
        // layout calculation
        float item_width = 128.0f;
        if (!vertical)
        {
            float avail_x       = ImGui::GetContentRegionAvail().x;
            float spacing       = ImGui::GetStyle().ItemSpacing.x;
            float total_spacing = spacing * 2.0f;
            item_width          = (avail_x - total_spacing) / 3.0f;
            item_width          -= axis_spacing;
    
            if (item_width < 1.0f)
                item_width = 1.0f;
        }
    
        float* values[3]           = { &vector.x, &vector.y, &vector.z };
        const char* axis_labels[3] = { "X", "Y", "Z" };
        const ImU32 axis_colors[3] = {
            IM_COL32(168, 46, 2, 255),
            IM_COL32(112, 162, 22, 255),
            IM_COL32(51, 122, 210, 255)
        };
    
        // components
        for (int i = 0; i < 3; ++i)
        {
            ImGui::PushID(i);
    
            // horizontal layout
            if (!vertical && i > 0)
            {
                ImGui::SameLine();
            }
    
            // axis label
            ImGui::TextUnformatted(axis_labels[i]);
            ImGui::SameLine();
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + axis_spacing - ImGui::CalcTextSize(axis_labels[i]).x);
            spartan::math::Vector2 pos_post_label = ImGui::GetCursorScreenPos();
    
            // float input
            ImGui::PushItemWidth(item_width);
            ImGuiSp::draw_float_wrap("##v", values[i], step, std::numeric_limits<float>::lowest(), std::numeric_limits<float>::max(), "%.4f");
            ImGui::PopItemWidth();
    
            // color bar decoration
            static const spartan::math::Vector2 size   = spartan::math::Vector2(4.0f, 19.0f);
            static const spartan::math::Vector2 offset = spartan::math::Vector2(-7.0f, 4.0f);
            spartan::math::Vector2 draw_pos            = pos_post_label + offset;
            ImGui::GetWindowDrawList()->AddRectFilled(draw_pos, draw_pos + size, axis_colors[i]);
    
            ImGui::PopID();
        }
    
        ImGui::EndGroup();
        ImGui::PopID();
    }

    // toggle switch - ios style toggle that replaces checkbox
    static bool toggle_switch(const char* label, bool* v)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
            return false;

        ImGuiContext& g         = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id        = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

        // switch dimensions
        const float height       = ImGui::GetFrameHeight();
        const float width        = height * 1.75f;
        const float radius       = height * 0.5f;
        const float knob_radius  = radius * 0.8f;
        const float knob_padding = radius - knob_radius;

        // layout: switch on the left, label on the right
        const ImVec2 pos          = window->DC.CursorPos;
        const ImRect switch_bb    = ImRect(pos, ImVec2(pos.x + width, pos.y + height));
        const ImRect total_bb     = ImRect(pos, ImVec2(pos.x + width + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), pos.y + height));

        ImGui::ItemSize(total_bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(total_bb, id))
            return false;

        // input handling
        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(switch_bb, id, &hovered, &held);
        if (pressed)
        {
            *v = !(*v);
            ImGui::MarkItemEdited(id);
        }

        // animation state (stored per widget id)
        static std::unordered_map<ImGuiID, float> animation_state;
        float& t = animation_state[id];

        // target position: 0.0 = off (left), 1.0 = on (right)
        float target = *v ? 1.0f : 0.0f;

        // smooth interpolation
        float animation_speed = 12.0f;
        if (t < target)
            t = ImMin(t + g.IO.DeltaTime * animation_speed, target);
        else if (t > target)
            t = ImMax(t - g.IO.DeltaTime * animation_speed, target);

        // colors - use imgui style for the active color
        ImU32 col_bg_off      = ImGui::GetColorU32(ImGuiCol_FrameBg);
        ImU32 col_bg_on       = ImGui::GetColorU32(ImGuiCol_CheckMark);
        ImU32 col_knob        = IM_COL32(255, 255, 255, 255);
        ImU32 col_knob_shadow = IM_COL32(0, 0, 0, 40);

        // interpolate background color
        ImVec4 bg_off = ImGui::ColorConvertU32ToFloat4(col_bg_off);
        ImVec4 bg_on  = ImGui::ColorConvertU32ToFloat4(col_bg_on);
        ImVec4 bg_lerp;
        bg_lerp.x = bg_off.x + (bg_on.x - bg_off.x) * t;
        bg_lerp.y = bg_off.y + (bg_on.y - bg_off.y) * t;
        bg_lerp.z = bg_off.z + (bg_on.z - bg_off.z) * t;
        bg_lerp.w = bg_off.w + (bg_on.w - bg_off.w) * t;
        ImU32 col_bg = ImGui::ColorConvertFloat4ToU32(bg_lerp);

        // draw track (pill shape as a single rounded rectangle)
        ImDrawList* draw_list = window->DrawList;
        draw_list->AddRectFilled(switch_bb.Min, switch_bb.Max, col_bg, radius);

        // knob position (interpolated)
        float knob_x_off = switch_bb.Min.x + radius;
        float knob_x_on  = switch_bb.Max.x - radius;
        float knob_x     = knob_x_off + (knob_x_on - knob_x_off) * t;
        float knob_y     = switch_bb.Min.y + radius;

        // draw knob shadow (offset slightly down and right)
        draw_list->AddCircleFilled(ImVec2(knob_x + 1.0f, knob_y + 2.0f), knob_radius, col_knob_shadow, 24);

        // draw knob
        draw_list->AddCircleFilled(ImVec2(knob_x, knob_y), knob_radius, col_knob, 24);

        // draw label
        if (label_size.x > 0.0f)
        {
            ImGui::RenderText(ImVec2(switch_bb.Max.x + style.ItemInnerSpacing.x, switch_bb.Min.y + style.FramePadding.y), label);
        }

        return pressed;
    }

    inline ButtonPress window_yes_no(const char* title, const char* text)
    {
        // Set position
        ImVec2 position     = ImVec2(spartan::Display::GetWidth() * 0.5f, spartan::Display::GetHeight() * 0.5f);
        ImVec2 pivot_center = ImVec2(0.5f, 0.5f);
        ImGui::SetNextWindowPos(position, ImGuiCond_Always, pivot_center);

        // Window
        ButtonPress button_press = ButtonPress::Undefined;
        if (ImGui::Begin(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
        {
            ImGui::Text(text);

            if (ImGuiSp::button_centered_on_line("Yes", 0.4f))
            {
                button_press = ButtonPress::Yes;
            }

            ImGui::SameLine();

            if (ImGui::Button("No"))
            {
                button_press = ButtonPress::No;
            }
        }
        ImGui::End();

        return button_press;
    }
}
