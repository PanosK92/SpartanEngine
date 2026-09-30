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
#include <cstring>
#include <unordered_map>
#include "Definitions.h"
#include "logging/Log.h"
#include "Window.h"
#include "Engine.h"
#include "rhi/RHI_Texture.h"
#include "rendering/Renderer.h"
#include "world/World.h"
#include "world/components/Camera.h"
#include "resource/ResourceCache.h"
#include "core/ThreadPool.h"
#include "display/Display.h"
#include "source/imgui_internal.h"
#include "ImGui_EditorUi.h"
#include "../Editor.h"
#include <resource/ResourceCache.h>
//=================================

namespace ImGuiSp
{
    constexpr std::string_view GDragDropTypes[] = {
        "Texture",
        "Entity",
        "Model",
        "Audio",
        "Material",
        "Lua",
        "Prefab",
        "Undefined",
    };

    enum class DragPayloadType
    {
        Texture,
        Entity,
        Model,
        Audio,
        Material,
        Lua,
        Prefab,
        Undefined
    };

    enum class ButtonPress
    {
        Yes,
        No,
        Undefined
    };

    static const ImVec4 default_tint(1, 1, 1, 1);

    // true while typing, flying the camera or playing, game input owns the keyboard so editor shortcuts must not fire
    static bool editor_shortcuts_blocked()
    {
        if (ImGui::GetIO().WantTextInput)
        {
            return true;
        }

        if (spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
        {
            return true;
        }

        if (spartan::Camera* camera = spartan::World::GetCamera())
        {
            return camera->GetFlag(spartan::CameraFlags::IsControlled);
        }

        return false;
    }

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

    // atlas icon variant, binds the shared atlas texture and samples the icon's uv sub rect
    static bool image_button(const spartan::IconType icon, const spartan::math::Vector2& size, bool border, ImVec4 tint = {1,1,1,1})
    {
        const spartan::Icon& entry = spartan::ResourceCache::GetIcon(icon);

        if (!border)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        }

        // use the icon type as a stable id, every icon now shares one atlas texture pointer
        // so an id derived from the texture would collide across different icon buttons
        ImGui::PushID(static_cast<int>(icon));
        bool result = ImGui::ImageButton
        (
            "",
            reinterpret_cast<ImTextureID>(entry.texture),
            size,
            ImVec2(entry.uv_min.x, entry.uv_min.y),
            ImVec2(entry.uv_max.x, entry.uv_max.y),
            ImColor(0, 0, 0, 0),
            tint
        );
        ImGui::PopID();

        if (!border)
        {
            ImGui::PopStyleVar();
        }

        return result;
    }

    static bool icon_button(
        const spartan::IconType icon,
        const ImVec4& tint = ImVec4(1, 1, 1, 1)
    )
    {
        const float icon_size = ImGui::EditorUi::toolbar_icon_size();
        const float padding = (
            ImGui::EditorUi::toolbar_button_size().x - icon_size
        ) * 0.5f;
        ImGui::PushStyleVar(
            ImGuiStyleVar_FramePadding,
            ImVec2(padding, padding)
        );
        const bool pressed = image_button(
            icon,
            spartan::math::Vector2(icon_size, icon_size),
            false,
            tint
        );
        ImGui::PopStyleVar();
        return pressed;
    }

    static void image(spartan::RHI_Texture* texture, const spartan::math::Vector2& size, bool border = false)
    {
        if (!border)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        }

        ImGui::ImageWithBg(
            reinterpret_cast<ImTextureID>(texture),
            size,
            ImVec2(0, 0),
            ImVec2(1, 1),
            ImColor(0, 0, 0, 0), // bg
            default_tint         // tint
        );

        if (!border)
        {
            ImGui::PopStyleVar();
        }
    }

    static void image(spartan::RHI_Texture* texture, const ImVec2& size, const ImVec4& tint = default_tint, const ImColor& bg = ImColor(0, 0, 0, 0))
    {
        ImGui::ImageWithBg(
            reinterpret_cast<ImTextureID>(texture),
            size,
            ImVec2(0, 0),
            ImVec2(1, 1),
            bg,
            tint
        );
    }

    // standalone texture variant with explicit uvs, used for atlas icons drawn through ImGuiSp::image
    static void image(spartan::RHI_Texture* texture, const ImVec2& size, const spartan::math::Vector2& uv0, const spartan::math::Vector2& uv1, const ImVec4& tint = default_tint)
    {
        ImGui::ImageWithBg(
            reinterpret_cast<ImTextureID>(texture),
            size,
            ImVec2(uv0.x, uv0.y),
            ImVec2(uv1.x, uv1.y),
            ImColor(0, 0, 0, 0), // bg
            tint
        );
    }

    static void image(const spartan::IconType icon, const float size, const ImVec4 tint = default_tint)
    {
        const spartan::Icon& entry = spartan::ResourceCache::GetIcon(icon);
        ImGui::ImageWithBg(
            reinterpret_cast<ImTextureID>(entry.texture),
            ImVec2(size, size),
            ImVec2(entry.uv_min.x, entry.uv_min.y),
            ImVec2(entry.uv_max.x, entry.uv_max.y),
            ImColor(0, 0, 0, 0), // bg
            tint                 // tint
        );
    }

    // self contained drag drop payload, paths are embedded directly so the
    // payload survives any reallocation or refresh of the source asset list
    struct DragDropPayload
    {
        static constexpr size_t max_path_length = 512;
        DragPayloadType type                    = DragPayloadType::Undefined;
        char path[max_path_length]              = {};
        char path_relative[max_path_length]     = {};

        void set_paths(const char* full, const char* relative)
        {
            path[0]          = '\0';
            path_relative[0] = '\0';
            if (full)
            {
                strncpy_s(path, max_path_length, full, _TRUNCATE);
            }
            if (relative)
            {
                strncpy_s(path_relative, max_path_length, relative, _TRUNCATE);
            }
        }
    };

    static void create_drag_drop_payload(const DragDropPayload& payload)
    {
        ImGui::SetDragDropPayload(GDragDropTypes[(int)payload.type].data(), &payload, sizeof(payload), ImGuiCond_Once);
    }

    static DragDropPayload* receive_drag_drop_payload(DragPayloadType type)
    {
        if (ImGui::BeginDragDropTarget())
        {
            const ImGuiPayload* payload_imgui = ImGui::AcceptDragDropPayload(GDragDropTypes[(int)type].data());
            ImGui::EndDragDropTarget();
            if (payload_imgui && payload_imgui->DataSize >= static_cast<int>(sizeof(DragDropPayload)))
            {
                return static_cast<DragDropPayload*>(payload_imgui->Data);
            }
        }

        return nullptr;
    }

    // drop target bound to an explicit screen rect, applies on mouse release to bypass imgui two frame delivery which can fail for custom targets
    static DragDropPayload* receive_drag_drop_payload_rect(DragPayloadType type, const ImVec2& rect_min, const ImVec2& rect_max, ImGuiID id)
    {
        DragDropPayload* result = nullptr;

        if (ImGui::BeginDragDropTargetCustom(ImRect(rect_min, rect_max), id))
        {
            const ImGuiPayload* payload_imgui = ImGui::AcceptDragDropPayload(GDragDropTypes[(int)type].data(),
                ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect);

            if (payload_imgui && payload_imgui->DataSize >= static_cast<int>(sizeof(DragDropPayload)) && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                result = static_cast<DragDropPayload*>(payload_imgui->Data);
            }

            ImGui::EndDragDropTarget();
        }

        return result;
    }

    // image slot - returns true if the user clicked on the slot (for browse functionality)
    static bool image_slot(spartan::RHI_Texture* texture_in, const std::function<void(spartan::RHI_Texture*)>& setter, const float size = 80.0f)
    {
        const ImVec2 slot_size  = ImVec2(size * spartan::Window::GetDpiScale());
        const float button_size = 15.0f * spartan::Window::GetDpiScale();
        bool clicked_for_browse = false;

        ImGui::BeginGroup();
        {
            spartan::RHI_Texture* texture   = texture_in;
            const ImVec2 pos_image          = ImGui::GetCursorPos();
            const ImVec2 screen_pos         = ImGui::GetCursorScreenPos();

            // x button position (top-right corner)
            const float x_btn_offset_x = slot_size.x - button_size - 4.0f;
            const float x_btn_offset_y = 4.0f;
            ImVec2 x_btn_screen_min    = ImVec2(screen_pos.x + x_btn_offset_x, screen_pos.y + x_btn_offset_y);
            ImVec2 x_btn_screen_max    = ImVec2(x_btn_screen_min.x + button_size, x_btn_screen_min.y + button_size);

            // check x button click FIRST using manual hit test
            bool x_clicked = false;
            if (texture != nullptr)
            {
                bool x_hovered = ImGui::IsMouseHoveringRect(x_btn_screen_min, x_btn_screen_max);
                if (x_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    setter(nullptr);
                    x_clicked = true;
                }
            }

            // main slot interaction (only if x wasn't clicked)
            ImGui::SetCursorPos(pos_image);
            ImGui::InvisibleButton("##slot_click", slot_size);
            bool is_hovered = ImGui::IsItemHovered();
            
            if (!x_clicked && ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                clicked_for_browse = true;
            }

            // drop target bound to the explicit slot rect, this does not depend on the last item hovered rect status which can be stale
            const ImVec2 drop_rect_min = screen_pos;
            const ImVec2 drop_rect_max = ImVec2(screen_pos.x + slot_size.x, screen_pos.y + slot_size.y);
            const ImGuiID drop_id      = ImGui::GetID("##slot_drop");

            if (auto payload = receive_drag_drop_payload_rect(DragPayloadType::Texture, drop_rect_min, drop_rect_max, drop_id))
            {
                if (payload->path[0] != '\0')
                {
                    if (const auto tex = spartan::ResourceCache::Load<spartan::RHI_Texture>(payload->path).get())
                    {
                        // load only produces a cpu texture, prepare it for the gpu so the slot and material can display it
                        tex->PrepareForGpu();
                        setter(tex);
                        SP_LOG_INFO("image_slot: assigned texture '%s'", payload->path);
                    }
                    else
                    {
                        SP_LOG_WARNING("image_slot: failed to load texture '%s'", payload->path);
                    }
                }
            }

            // draw the slot, an empty slot is a dark grey placeholder, a filled slot shows the texture
            ImVec2 rect_min     = screen_pos;
            ImVec2 rect_max     = ImVec2(screen_pos.x + slot_size.x, screen_pos.y + slot_size.y);
            const ImVec4 border = is_hovered
                ? ImGui::Style::color_accent_1
                : ImGui::Style::color_border;
            ImU32 border_col = ImGui::EditorUi::color(border);
            if (texture != nullptr)
            {
                ImGui::SetCursorPos(pos_image);
                image(
                    texture,
                    slot_size,
                    ImVec4(1, 1, 1, 1),
                    ImGui::Style::color_canvas_deep
                );
                ImGui::GetWindowDrawList()->AddRect(
                    rect_min,
                    rect_max,
                    border_col,
                    ImGui::EditorUi::scaled(6.0f)
                );

                // drag source
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::EndDragDropSource();
                }
            }
            else
            {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                draw_list->AddRectFilled(
                    rect_min,
                    rect_max,
                    ImGui::EditorUi::color(
                        ImGui::Style::color_canvas_deep
                    ),
                    ImGui::EditorUi::scaled(6.0f)
                );
                draw_list->AddRect(
                    rect_min,
                    rect_max,
                    border_col,
                    ImGui::EditorUi::scaled(6.0f)
                );
            }

            // draw x button (visual only - click handled above)
            if (texture != nullptr)
            {
                ImVec2 pos_button = ImVec2(pos_image.x + x_btn_offset_x, pos_image.y + x_btn_offset_y);
                ImGui::SetCursorPos(pos_button);
                
                // draw button background on hover
                bool x_hovered = ImGui::IsMouseHoveringRect(x_btn_screen_min, x_btn_screen_max);
                if (x_hovered)
                {
                    ImGui::GetWindowDrawList()->AddRectFilled(
                        x_btn_screen_min,
                        x_btn_screen_max,
                        ImGui::EditorUi::color(
                            ImGui::EditorUi::alpha(
                                ImGui::Style::color_error,
                                0.72f
                            )
                        ),
                        ImGui::EditorUi::scaled(3.0f)
                    );
                }
                
                // draw x icon
                image(spartan::IconType::X, button_size);
            }
        }
        ImGui::EndGroup();

        return clicked_for_browse;
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
        ImGui::EditorUi::decorate_field();
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

        // the arrow is a bare chevron on the well rather than a separate button block
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text);
        const bool open = ImGui::BeginCombo(label, preview);
        ImGui::PopStyleColor(3);
        if (!open)
        {
            ImGui::EditorUi::decorate_field();
        }
        if (open)
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
            {
                item_width = 1.0f;
            }
        }

        float* values[3]           = { &vector.x, &vector.y, &vector.z };
        const char* axis_labels[3] = { "X", "Y", "Z" };
        const ImU32 axis_colors[3] =
        {
            ImGui::EditorUi::color(ImGui::EditorUi::axis_color(0)),
            ImGui::EditorUi::color(ImGui::EditorUi::axis_color(1)),
            ImGui::EditorUi::color(ImGui::EditorUi::axis_color(2))
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
        {
            return false;
        }

        ImGuiContext& g         = *GImGui;
        const ImGuiStyle& style = g.Style;
        const ImGuiID id        = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, nullptr, true);

        // the switch is slimmer than a frame and centered on the row, so it lines up with the fields around it
        const float frame_height = ImGui::GetFrameHeight();
        const float height       = IM_ROUND(frame_height * 0.78f);
        const float width        = IM_ROUND(height * 1.85f);
        const float radius       = height * 0.5f;
        const float knob_radius  = radius - ImMax(2.0f, ImGui::EditorUi::scaled(2.0f));

        // layout: switch on the left, label on the right
        const ImVec2 pos          = window->DC.CursorPos;
        const float switch_y      = IM_ROUND(pos.y + (frame_height - height) * 0.5f);
        const ImRect switch_bb    = ImRect(ImVec2(pos.x, switch_y), ImVec2(pos.x + width, switch_y + height));
        const ImRect total_bb     = ImRect(pos, ImVec2(pos.x + width + (label_size.x > 0.0f ? style.ItemInnerSpacing.x + label_size.x : 0.0f), pos.y + frame_height));

        ImGui::ItemSize(total_bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(total_bb, id))
        {
            return false;
        }

        // input handling
        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(switch_bb, id, &hovered, &held);
        if (pressed)
        {
            *v = !(*v);
            ImGui::MarkItemEdited(id);
        }

        const float t     = ImGui::EditorUi::animate(id, *v ? 1.0f : 0.0f, 14.0f);
        const float hover = ImGui::EditorUi::animate(id ^ 0x4a11c0deu, hovered ? 1.0f : 0.0f, 16.0f);
        const ImVec4 accent = ImGui::Style::color_accent_1;

        // off, the track is sunk into the panel, on, it is lit with the signal and glows
        ImDrawList* draw_list = window->DrawList;
        if (t > 0.01f)
        {
            ImGui::EditorUi::draw_glow(draw_list, switch_bb.Min, switch_bb.Max, accent, radius, ImGui::EditorUi::scaled(7.0f), (0.55f + 0.35f * hover) * t);
        }
        const ImVec4 track_off = ImGui::Style::lerp(ImGui::Style::color_canvas_deep, ImGui::Style::color_surface, 0.25f * hover);
        const ImVec4 track_on  = ImGui::Style::lerp(ImGui::Style::lerp(ImGui::Style::color_accent_2, accent, 0.80f), accent, hover);
        draw_list->AddRectFilled(switch_bb.Min, switch_bb.Max, ImGui::EditorUi::color(ImGui::Style::lerp(track_off, track_on, t)), radius);
        draw_list->AddRect(switch_bb.Min, switch_bb.Max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.08f * (1.0f - t))), radius, ImMax(1.0f, ImGui::EditorUi::scaled(1.0f)));

        // the knob casts a soft shadow onto the track and gives slightly under the pointer while pressed
        const float knob_x_off = switch_bb.Min.x + radius;
        const float knob_x_on  = switch_bb.Max.x - radius;
        const float knob_x     = knob_x_off + (knob_x_on - knob_x_off) * t;
        const float knob_y     = switch_bb.Min.y + radius;
        const float press      = held ? 0.9f : 1.0f;
        draw_list->AddCircleFilled(ImVec2(knob_x, knob_y + ImGui::EditorUi::scaled(1.0f)), knob_radius * press + 1.0f, IM_COL32(0, 0, 0, 90), 32);
        const ImVec4 knob = ImGui::Style::lerp(ImGui::Style::color_text_muted, ImVec4(1.0f, 1.0f, 1.0f, 1.0f), t);
        draw_list->AddCircleFilled(ImVec2(knob_x, knob_y), knob_radius * press, ImGui::EditorUi::color(knob), 32);

        // draw label
        if (label_size.x > 0.0f)
        {
            ImGui::RenderText(ImVec2(switch_bb.Max.x + style.ItemInnerSpacing.x, pos.y + style.FramePadding.y), label);
        }

        return pressed;
    }

    struct CommandLabel
    {
        char text[128];
        ImFont* font;
        float size;
        float tracking;
        float width;
    };

    static CommandLabel command_label(const char* label)
    {
        CommandLabel result;
        const std::string visible(label, ImGui::FindRenderedTextEnd(label));
        ImGui::EditorUi::to_upper(visible.c_str(), result.text, sizeof(result.text));
        result.font     = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
        result.size     = ImGui::GetFontSize() * 0.86f;
        result.tracking = result.size * 0.14f;
        result.width    = ImGui::EditorUi::calc_text_tracked(result.text, result.tracking, result.font, result.size);
        return result;
    }

    // the width a command button needs to show its label with breathing room on both sides
    static float command_button_width(const char* label)
    {
        return IM_ROUND(command_label(label).width + ImGui::EditorUi::scaled(28.0f));
    }

    // a console key: a dark well with a lit rim and a tracked uppercase label, primary keys carry the signal color
    static bool command_button(const char* label, const ImVec2& size_arg, const bool primary = true, const ImVec4* tint_override = nullptr)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems)
        {
            return false;
        }

        const ImGuiID id           = window->GetID(label);
        const CommandLabel caption = command_label(label);
        const char* upper          = caption.text;
        ImFont* font               = caption.font;
        const float size_px        = caption.size;
        const float tracking       = caption.tracking;
        const float text_w         = caption.width;
        const float height         = size_arg.y > 0.0f ? size_arg.y : IM_ROUND(ImGui::GetFrameHeight() + ImGui::EditorUi::scaled(8.0f));
        const float width          = size_arg.x > 0.0f ? size_arg.x : (size_arg.x < 0.0f ? ImMax(ImGui::GetContentRegionAvail().x + size_arg.x + 1.0f, text_w) : command_button_width(label));

        const ImVec2 pos = window->DC.CursorPos;
        const ImRect bb(pos, ImVec2(IM_ROUND(pos.x + width), IM_ROUND(pos.y + height)));
        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, id))
        {
            return false;
        }

        bool hovered = false;
        bool held    = false;
        const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

        const float hover       = ImGui::EditorUi::animate(id ^ 0x5eed0001u, hovered ? 1.0f : 0.0f, 14.0f);
        const float press       = ImGui::EditorUi::animate(id ^ 0x5eed0002u, held ? 1.0f : 0.0f, 24.0f);
        const ImVec4 signal     = tint_override ? *tint_override : (primary ? ImGui::Style::color_accent_1 : ImGui::Style::color_text);
        const float rounding    = ImGui::EditorUi::scaled(4.0f);
        ImDrawList* draw_list   = window->DrawList;

        if (hover > 0.01f)
        {
            ImGui::EditorUi::draw_glow(draw_list, bb.Min, bb.Max, signal, rounding, ImGui::EditorUi::scaled(10.0f), (primary ? 0.75f : 0.35f) * hover);
        }

        // the well darkens under the finger and lifts toward the signal under the pointer
        const float lift        = (primary ? 0.10f : 0.05f) + (primary ? 0.10f : 0.06f) * hover - 0.06f * press;
        const ImVec4 well_top   = ImGui::Style::lerp(ImGui::Style::color_surface, signal, lift);
        const ImVec4 well_base  = ImGui::Style::lerp(ImGui::Style::color_canvas_deep, signal, lift * 0.5f);
        const int vtx_start     = draw_list->VtxBuffer.Size;
        draw_list->AddRectFilled(bb.Min, bb.Max, IM_COL32_WHITE, rounding);
        ImGui::EditorUi::shade_vertical(draw_list, vtx_start, bb.Min.y, bb.Max.y, well_top, well_base);

        const ImVec4 rim_top    = ImGui::EditorUi::alpha(signal, (primary ? 0.70f : 0.22f) + 0.30f * hover);
        const ImVec4 rim_bottom = ImGui::EditorUi::alpha(signal, (primary ? 0.18f : 0.06f) + 0.14f * hover);
        ImGui::EditorUi::draw_lit_rim(draw_list, bb.Min, bb.Max, rounding, rim_top, rim_bottom, bb.GetHeight());

        const ImVec4 label_tint = primary ? ImGui::Style::lerp(ImGui::Style::color_accent_hi, ImVec4(1, 1, 1, 1), 0.35f * hover) : ImGui::Style::lerp(ImGui::Style::color_text_muted, ImGui::Style::color_text, 0.4f + 0.6f * hover);
        const ImVec2 text_pos   = ImVec2(IM_ROUND(bb.Min.x + (width - text_w) * 0.5f), IM_ROUND(bb.Min.y + (height - size_px) * 0.5f + press));
        ImGui::EditorUi::draw_text_tracked(draw_list, text_pos, ImGui::EditorUi::color(label_tint), upper, tracking, font, size_px);

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
