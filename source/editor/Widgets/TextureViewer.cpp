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

//= INCLUDES ========================
#include "pch.h"
#include "TextureViewer.h"
#include "../ImGui/ImGui_Extension.h"
#include "Rendering/Material.h"
//===================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    struct viewer_state
    {
        // selection by stable id, survives list rebuilds
        uint64_t selected_object_id            = 0;
        spartan::RHI_Texture* texture_current  = nullptr;

        enum class source_kind { render_targets, bindless_materials };
        source_kind source = source_kind::render_targets;

        // search and filtering
        ImGuiTextFilter search_filter;
        uint32_t type_filter_mask = 0xffffffffu;
        bool view_grid            = false;

        // splitter widths, right panel sized to fit the longest inspector content (format string in info table)
        float left_panel_width  = 280.0f;
        float right_panel_width = 380.0f;

        // visualisation state, mirrored into m_visualisation_flags getter
        int  mip_level         = 0;
        int  array_level       = 0;
        bool channel_r         = true;
        bool channel_g         = true;
        bool channel_b         = true;
        bool channel_a         = true;
        bool gamma_correct     = true;
        bool pack              = false;
        bool boost             = false;
        bool abs_value         = false;
        bool point_sampling    = false;
        uint32_t visualisation_flags = 0;

        // canvas
        float  zoom            = 1.0f;
        ImVec2 pan             = ImVec2(0.0f, 0.0f);
        ImVec2 hovered_uv      = ImVec2(-1.0f, -1.0f);
        bool   hovering_canvas = false;

        // bindless table fills over a couple of frames, so refresh periodically
        int frames_since_refresh = 0;

        // zoom requests come from toolbar buttons or keyboard shortcuts
        bool  request_fit         = false;
        bool  request_one_to_one  = false;
        bool  request_reset       = false;
        float request_zoom_mul    = 0.0f;
    };
    viewer_state s;

    struct texture_entry
    {
        spartan::RHI_Texture* tex = nullptr;
        std::string display_name;
        uint32_t bindless_index   = 0;
    };
    std::vector<texture_entry> entries;
    std::vector<texture_entry> entries_filtered;

    spartan::MaterialTextureType bindless_type_from_index(uint32_t i)
    {
        // bindless layout is texture_type major within each material, see refresh_entries
        const uint32_t slots = spartan::Material::slots_per_texture;
        const uint32_t types = static_cast<uint32_t>(spartan::MaterialTextureType::Max);
        return static_cast<spartan::MaterialTextureType>((i / slots) % types);
    }

    const char* material_texture_type_label(spartan::MaterialTextureType t)
    {
        switch (t)
        {
            case spartan::MaterialTextureType::Color:     return "Color";
            case spartan::MaterialTextureType::Roughness: return "Roughness";
            case spartan::MaterialTextureType::Metalness: return "Metalness";
            case spartan::MaterialTextureType::Normal:    return "Normal";
            case spartan::MaterialTextureType::Occlusion: return "Occlusion";
            case spartan::MaterialTextureType::Emission:  return "Emission";
            case spartan::MaterialTextureType::Height:    return "Height";
            case spartan::MaterialTextureType::AlphaMask: return "AlphaMask";
            case spartan::MaterialTextureType::Packed:    return "Packed";
            default:                                      return "Unknown";
        }
    }

    const char* texture_type_label(spartan::RHI_Texture_Type t)
    {
        switch (t)
        {
            case spartan::RHI_Texture_Type::Type2D:      return "2D";
            case spartan::RHI_Texture_Type::Type2DArray: return "2D Array";
            case spartan::RHI_Texture_Type::Type3D:      return "3D";
            case spartan::RHI_Texture_Type::TypeCube:    return "Cube";
            default:                                     return "?";
        }
    }

    void refresh_entries()
    {
        entries.clear();

        if (s.source == viewer_state::source_kind::render_targets)
        {
            for (const std::shared_ptr<spartan::RHI_Texture>& tex : spartan::Renderer::GetRenderTargets())
            {
                if (tex)
                {
                    texture_entry e;
                    e.tex          = tex.get();
                    e.display_name = tex->GetObjectName();
                    entries.push_back(std::move(e));
                }
            }
            std::sort(entries.begin(), entries.end(), [](const texture_entry& a, const texture_entry& b)
            {
                return a.display_name < b.display_name;
            });
        }
        else
        {
            const auto& bindless = spartan::Renderer::GetBindlessMaterialTextures();
            for (size_t i = 0; i < bindless.size(); ++i)
            {
                spartan::RHI_Texture* tex = bindless[i];
                if (!tex)
                {
                    continue;
                }
                texture_entry e;
                e.tex             = tex;
                e.bindless_index  = static_cast<uint32_t>(i);
                e.display_name    = "[" + std::to_string(i) + "] " + tex->GetObjectName();
                entries.push_back(std::move(e));
            }
        }

        s.frames_since_refresh = 0;
    }

    void apply_filters()
    {
        entries_filtered.clear();
        entries_filtered.reserve(entries.size());

        for (const texture_entry& e : entries)
        {
            if (s.search_filter.IsActive() && !s.search_filter.PassFilter(e.display_name.c_str()))
            {
                continue;
            }
            if (s.source == viewer_state::source_kind::bindless_materials)
            {
                spartan::MaterialTextureType t = bindless_type_from_index(e.bindless_index);
                uint32_t bit = 1u << static_cast<uint32_t>(t);
                if ((s.type_filter_mask & bit) == 0)
                {
                    continue;
                }
            }
            entries_filtered.push_back(e);
        }
    }

    int find_filtered_index_by_id(uint64_t id)
    {
        if (id == 0)
        {
            return -1;
        }
        for (size_t i = 0; i < entries_filtered.size(); ++i)
        {
            if (entries_filtered[i].tex && entries_filtered[i].tex->GetObjectId() == id)
            {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    uint64_t texture_byte_estimate(spartan::RHI_Texture* t)
    {
        if (!t)
        {
            return 0;
        }
        const uint64_t slices = std::max<uint32_t>(1u, t->GetArrayLength());
        const uint64_t bpp    = std::max<uint32_t>(1u, t->GetBytesPerPixel());
        uint64_t total = 0;
        for (uint32_t m = 0; m < t->GetMipCount(); ++m)
        {
            const uint64_t w = std::max<uint64_t>(1, static_cast<uint64_t>(t->GetWidth())  >> m);
            const uint64_t h = std::max<uint64_t>(1, static_cast<uint64_t>(t->GetHeight()) >> m);
            total += w * h * bpp;
        }
        return total * slices;
    }

    bool toggle_button(const char* label, bool active, const ImVec2& size = ImVec2(0, 0))
    {
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        bool clicked = ImGui::Button(label, size);
        if (active)
        {
            ImGui::PopStyleColor();
        }
        return clicked;
    }

    void draw_h_splitter(const char* id, float* size_left, float min_left, float max_left, float total_height, float sign)
    {
        const float thickness = 4.0f;
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(thickness, total_height));
        bool active  = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        if (hovered || active)
        {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        }
        if (active)
        {
            *size_left += sign * ImGui::GetIO().MouseDelta.x;
            *size_left = std::clamp(*size_left, min_left, max_left);
        }
        ImU32 col = active  ? ImGui::GetColorU32(ImGuiCol_SeparatorActive) :
                    hovered ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered) :
                              ImGui::GetColorU32(ImGuiCol_Separator);
        ImGui::GetWindowDrawList()->AddRectFilled(cursor, ImVec2(cursor.x + thickness, cursor.y + total_height), col);
    }

    void draw_checkerboard(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float tile)
    {
        const ImU32 c0 = IM_COL32(60, 60, 60, 255);
        const ImU32 c1 = IM_COL32(85, 85, 85, 255);
        dl->AddRectFilled(mn, mx, c0);
        for (float y = mn.y; y < mx.y; y += tile)
        {
            for (float x = mn.x; x < mx.x; x += tile)
            {
                int ix = static_cast<int>((x - mn.x) / tile);
                int iy = static_cast<int>((y - mn.y) / tile);
                if (((ix + iy) & 1) == 0)
                {
                    continue;
                }
                ImVec2 a(x, y);
                ImVec2 b(std::min(x + tile, mx.x), std::min(y + tile, mx.y));
                dl->AddRectFilled(a, b, c1);
            }
        }
    }

    void draw_toolbar()
    {
        // source tabs
        if (ImGui::BeginTabBar("##source_tabs", ImGuiTabBarFlags_None))
        {
            if (ImGui::BeginTabItem("Render Targets"))
            {
                if (s.source != viewer_state::source_kind::render_targets)
                {
                    s.source = viewer_state::source_kind::render_targets;
                    refresh_entries();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bindless Material Textures"))
            {
                if (s.source != viewer_state::source_kind::bindless_materials)
                {
                    s.source = viewer_state::source_kind::bindless_materials;
                    refresh_entries();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        // search
        ImGui::SetNextItemWidth(260.0f);
        s.search_filter.Draw("##texture_search", 260.0f);
        ImGuiSp::tooltip("Filter by name, supports inc and -exc patterns");
        ImGui::SameLine();
        if (toggle_button(s.view_grid ? "Grid" : "List", s.view_grid))
        {
            s.view_grid = !s.view_grid;
        }
        ImGuiSp::tooltip(s.view_grid ? "Switch to list mode" : "Switch to grid mode");

        // zoom cluster, right aligned
        char zoom_text[16];
        snprintf(zoom_text, sizeof(zoom_text), "%.0f%%", s.zoom * 100.0f);
        const float spacing  = ImGui::GetStyle().ItemSpacing.x;
        const float pad      = ImGui::GetStyle().FramePadding.x * 2.0f;
        float cluster_w = 0.0f;
        cluster_w += ImGui::CalcTextSize("Fit").x   + pad + spacing;
        cluster_w += ImGui::CalcTextSize("1:1").x   + pad + spacing;
        cluster_w += ImGui::CalcTextSize("-").x     + pad + spacing;
        cluster_w += ImGui::CalcTextSize(zoom_text).x       + spacing;
        cluster_w += ImGui::CalcTextSize("+").x     + pad + spacing;
        cluster_w += ImGui::CalcTextSize("Reset").x + pad;
        ImGui::SameLine();
        float avail = ImGui::GetContentRegionAvail().x;
        if (avail > cluster_w)
        {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - cluster_w);
        }
        if (ImGui::Button("Fit"))
        {
            s.request_fit = true;
        }
        ImGuiSp::tooltip("Fit to window (F)");
        ImGui::SameLine();
        if (ImGui::Button("1:1"))
        {
            s.request_one_to_one = true;
        }
        ImGuiSp::tooltip("One texel to one pixel (1)");
        ImGui::SameLine();
        if (ImGui::Button("-"))
        {
            s.request_zoom_mul = 0.9f;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(zoom_text);
        ImGui::SameLine();
        if (ImGui::Button("+"))
        {
            s.request_zoom_mul = 1.1f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
        {
            s.request_reset = true;
        }
        ImGuiSp::tooltip("Reset pan and zoom (R)");
    }

    void draw_type_chips()
    {
        if (s.source != viewer_state::source_kind::bindless_materials)
        {
            return;
        }
        ImGui::TextDisabled("filter:");
        ImGui::SameLine();
        if (ImGui::SmallButton("All"))
        {
            s.type_filter_mask = 0xffffffffu;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("None"))
        {
            s.type_filter_mask = 0u;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");

        const uint32_t types = static_cast<uint32_t>(spartan::MaterialTextureType::Max);
        for (uint32_t i = 0; i < types; ++i)
        {
            const uint32_t bit = 1u << i;
            const bool active  = (s.type_filter_mask & bit) != 0;
            ImGui::SameLine();
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton(material_texture_type_label(static_cast<spartan::MaterialTextureType>(i))))
            {
                s.type_filter_mask ^= bit;
            }
            ImGui::PopID();
            if (active)
            {
                ImGui::PopStyleColor();
            }
        }
    }

    void draw_entry_row(int idx)
    {
        const texture_entry& e = entries_filtered[idx];
        if (!e.tex)
        {
            return;
        }

        const float row_height = 44.0f;
        const float thumb_size = row_height - 4.0f;
        const bool  selected   = (s.selected_object_id == e.tex->GetObjectId());

        ImGui::PushID(idx);

        // selectable owns the full row layout and click handling
        ImVec2 row_min = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", selected, 0, ImVec2(0.0f, row_height)))
        {
            s.selected_object_id = e.tex->GetObjectId();
        }

        // overlays are pure draws so we never disturb the cursor
        ImDrawList* dl    = ImGui::GetWindowDrawList();
        const ImU32 col_t = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 col_d = ImGui::GetColorU32(ImGuiCol_TextDisabled);

        ImVec2 thumb_min = ImVec2(row_min.x + 4.0f, row_min.y + 2.0f);
        ImVec2 thumb_max = ImVec2(thumb_min.x + thumb_size, thumb_min.y + thumb_size);
        dl->AddImage(reinterpret_cast<ImTextureID>(e.tex), thumb_min, thumb_max);
        dl->AddRect(thumb_min, thumb_max, IM_COL32(20, 20, 20, 255));

        const float text_x = row_min.x + thumb_size + 12.0f;
        dl->AddText(ImVec2(text_x, row_min.y + 4.0f), col_t, e.display_name.c_str());

        char info[96];
        snprintf(info, sizeof(info), "%ux%u  %s", e.tex->GetWidth(), e.tex->GetHeight(), rhi_format_to_string(e.tex->GetFormat()));
        dl->AddText(ImVec2(text_x, row_min.y + 4.0f + ImGui::GetTextLineHeight() + 2.0f), col_d, info);

        ImGui::PopID();
    }

    void draw_entry_card(int idx, float card_size)
    {
        const texture_entry& e = entries_filtered[idx];
        if (!e.tex)
        {
            return;
        }
        const bool selected = (s.selected_object_id == e.tex->GetObjectId());
        const float total_h = card_size + ImGui::GetTextLineHeight() + 6.0f;

        ImGui::PushID(idx);
        ImVec2 card_min = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##card", selected, 0, ImVec2(card_size, total_h)))
        {
            s.selected_object_id = e.tex->GetObjectId();
        }

        // overlays via draw list so we leave the layout cursor exactly where Selectable put it
        ImDrawList* dl   = ImGui::GetWindowDrawList();
        ImVec2 thumb_min = ImVec2(card_min.x + 2.0f, card_min.y + 2.0f);
        ImVec2 thumb_max = ImVec2(card_min.x + card_size - 2.0f, card_min.y + card_size - 2.0f);
        dl->AddImage(reinterpret_cast<ImTextureID>(e.tex), thumb_min, thumb_max);
        dl->AddRect(thumb_min, thumb_max, IM_COL32(20, 20, 20, 255));

        // single line label, clipped to the card width
        const ImU32 col_t = ImGui::GetColorU32(ImGuiCol_Text);
        ImVec4 clip(card_min.x, card_min.y + card_size, card_min.x + card_size, card_min.y + total_h);
        dl->AddText(nullptr, 0.0f, ImVec2(card_min.x + 2.0f, card_min.y + card_size), col_t, e.display_name.c_str(), nullptr, 0.0f, &clip);

        ImGui::PopID();
    }

    void draw_browser_panel(float width, float height)
    {
        ImGui::BeginChild("##browser_panel", ImVec2(width, height), true);

        draw_type_chips();
        if (s.source == viewer_state::source_kind::bindless_materials)
        {
            ImGui::Separator();
        }
        ImGui::Text("%zu of %zu", entries_filtered.size(), entries.size());

        ImGui::BeginChild("##entries_scroll", ImVec2(0, 0), false);

        if (s.view_grid)
        {
            const float card     = 96.0f;
            const float padding  = ImGui::GetStyle().ItemSpacing.x;
            const float row_h    = card + ImGui::GetTextLineHeight() + 6.0f + padding;
            int columns          = std::max(1, static_cast<int>((ImGui::GetContentRegionAvail().x + padding) / (card + padding)));
            int total            = static_cast<int>(entries_filtered.size());
            int rows             = (total + columns - 1) / columns;

            ImGuiListClipper clipper;
            clipper.Begin(rows, row_h);
            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
                {
                    for (int col = 0; col < columns; ++col)
                    {
                        int i = row * columns + col;
                        if (i >= total)
                        {
                            break;
                        }
                        if (col > 0)
                        {
                            ImGui::SameLine();
                        }
                        draw_entry_card(i, card);
                    }
                }
            }
            clipper.End();
        }
        else
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(entries_filtered.size()), 44.0f);
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    draw_entry_row(i);
                }
            }
            clipper.End();
        }

        ImGui::EndChild();
        ImGui::EndChild();
    }

    void draw_preview_panel(float width, float height)
    {
        ImGui::BeginChild("##preview_panel", ImVec2(width, height), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 child_pos  = ImGui::GetCursorScreenPos();
        ImVec2 child_size = ImGui::GetContentRegionAvail();
        ImVec2 child_max  = ImVec2(child_pos.x + child_size.x, child_pos.y + child_size.y);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        draw_checkerboard(dl, child_pos, child_max, 16.0f);

        spartan::RHI_Texture* tex = s.texture_current;

        if (tex)
        {
            float tex_w  = static_cast<float>(tex->GetWidth());
            float tex_h  = static_cast<float>(tex->GetHeight());
            float aspect = tex_w / std::max(1.0f, tex_h);

            float fit_w = child_size.x;
            float fit_h = child_size.x / std::max(0.0001f, aspect);
            if (fit_h > child_size.y)
            {
                fit_h = child_size.y;
                fit_w = child_size.y * aspect;
            }

            // toolbar requests
            if (s.request_fit)
            {
                s.zoom = 1.0f;
                s.pan  = ImVec2(0.0f, 0.0f);
                s.request_fit = false;
            }
            if (s.request_one_to_one)
            {
                s.zoom = std::clamp(tex_w / std::max(1.0f, fit_w), 0.05f, 64.0f);
                s.pan  = ImVec2(0.0f, 0.0f);
                s.request_one_to_one = false;
            }
            if (s.request_reset)
            {
                s.zoom = 1.0f;
                s.pan  = ImVec2(0.0f, 0.0f);
                s.request_reset = false;
            }

            float draw_w = fit_w * s.zoom;
            float draw_h = fit_h * s.zoom;

            ImVec2 base_pos = ImVec2(
                child_pos.x + (child_size.x - draw_w) * 0.5f,
                child_pos.y + (child_size.y - draw_h) * 0.5f
            );
            ImVec2 image_pos = ImVec2(base_pos.x + s.pan.x, base_pos.y + s.pan.y);

            ImGui::SetCursorScreenPos(image_pos);
            ImGuiSp::image(tex, math::Vector2(draw_w, draw_h), ImColor(255, 255, 255, 255), ImColor(40, 40, 40, 255));

            // drag drop only for textures backed by a real path
            if (!tex->GetResourceFilePath().empty() && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
            {
                ImGuiSp::DragDropPayload payload;
                payload.type = ImGuiSp::DragPayloadType::Texture;
                payload.set_paths(tex->GetResourceFilePath().c_str(), tex->GetResourceFilePath().c_str());
                ImGuiSp::create_drag_drop_payload(payload);
                ImGui::TextUnformatted(tex->GetObjectName().c_str());
                ImGui::EndDragDropSource();
            }

            ImGuiIO& io       = ImGui::GetIO();
            s.hovering_canvas = ImGui::IsWindowHovered();

            if (s.hovering_canvas)
            {
                ImVec2 mouse_pos = io.MousePos;
                ImVec2 rel       = ImVec2(mouse_pos.x - image_pos.x, mouse_pos.y - image_pos.y);
                if (rel.x >= 0.0f && rel.y >= 0.0f && rel.x <= draw_w && rel.y <= draw_h)
                {
                    s.hovered_uv = ImVec2(rel.x / std::max(1.0f, draw_w), rel.y / std::max(1.0f, draw_h));
                }
                else
                {
                    s.hovered_uv = ImVec2(-1.0f, -1.0f);
                }

                if (io.MouseWheel != 0.0f)
                {
                    float prev_zoom = s.zoom;
                    s.zoom *= (io.MouseWheel > 0.0f) ? 1.1f : 0.9f;
                    s.zoom = std::clamp(s.zoom, 0.05f, 64.0f);
                    s.pan.x -= rel.x * (s.zoom / prev_zoom - 1.0f);
                    s.pan.y -= rel.y * (s.zoom / prev_zoom - 1.0f);
                }

                // pan with middle button only, left button conflicts with window drag and selection
                if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                {
                    s.pan.x += io.MouseDelta.x;
                    s.pan.y += io.MouseDelta.y;
                }

                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Middle))
                {
                    s.zoom = 1.0f;
                    s.pan  = ImVec2(0.0f, 0.0f);
                }

                // keyboard shortcuts active when hovering the canvas
                if (ImGui::IsKeyPressed(ImGuiKey_F, false))
                {
                    s.request_fit = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_1, false))
                {
                    s.request_one_to_one = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_R, false))
                {
                    s.request_reset = true;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))
                {
                    s.request_zoom_mul = 1.1f;
                }
                if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false))
                {
                    s.request_zoom_mul = 0.9f;
                }
            }
            else
            {
                s.hovered_uv = ImVec2(-1.0f, -1.0f);
            }

            // toolbar zoom multiplier always honoured, centred on canvas
            if (s.request_zoom_mul != 0.0f)
            {
                float prev_zoom = s.zoom;
                s.zoom = std::clamp(s.zoom * s.request_zoom_mul, 0.05f, 64.0f);
                ImVec2 center_rel = ImVec2(child_size.x * 0.5f, child_size.y * 0.5f);
                s.pan.x -= center_rel.x * (s.zoom / prev_zoom - 1.0f);
                s.pan.y -= center_rel.y * (s.zoom / prev_zoom - 1.0f);
                s.request_zoom_mul = 0.0f;
            }

            // hud
            char hud[256];
            int written = 0;
            written += snprintf(hud + written, sizeof(hud) - written, "zoom %.0f%%", s.zoom * 100.0f);
            if (tex->GetMipCount() > 1)
            {
                written += snprintf(hud + written, sizeof(hud) - written, "\nmip %d / %u", s.mip_level, tex->GetMipCount() - 1);
            }
            if (tex->GetArrayLength() > 1)
            {
                written += snprintf(hud + written, sizeof(hud) - written, "\nslice %d / %u", s.array_level, tex->GetArrayLength() - 1);
            }
            ImVec2 hud_size = ImGui::CalcTextSize(hud);
            ImVec2 hud_pos  = ImVec2(child_pos.x + 8.0f, child_pos.y + 8.0f);
            dl->AddRectFilled(
                ImVec2(hud_pos.x - 4.0f, hud_pos.y - 2.0f),
                ImVec2(hud_pos.x + hud_size.x + 4.0f, hud_pos.y + hud_size.y + 2.0f),
                IM_COL32(0, 0, 0, 160), 4.0f
            );
            dl->AddText(hud_pos, IM_COL32(255, 255, 255, 240), hud);
        }
        else
        {
            const char* msg     = "no texture selected";
            ImVec2 text_size    = ImGui::CalcTextSize(msg);
            ImVec2 text_pos     = ImVec2(
                child_pos.x + (child_size.x - text_size.x) * 0.5f,
                child_pos.y + (child_size.y - text_size.y) * 0.5f
            );
            dl->AddText(text_pos, IM_COL32(180, 180, 180, 255), msg);
        }

        dl->AddRect(child_pos, child_max, IM_COL32(0, 0, 0, 255), 0.0f, 2.0f, 0);
        ImGui::EndChild();
    }

    float inspector_natural_width()
    {
        // measure the widest content the inspector ever renders so the right panel can grow to fit it
        const ImGuiStyle& style = ImGui::GetStyle();
        float widest_value = 0.0f;
        widest_value = std::max(widest_value, ImGui::CalcTextSize("R10G10B10A2_Unorm").x);
        widest_value = std::max(widest_value, ImGui::CalcTextSize("99999 x 99999").x);
        float widest_label = ImGui::CalcTextSize("Channels").x;
        float info_table   = widest_label + widest_value + style.ItemSpacing.x * 2.0f + style.CellPadding.x * 4.0f;
        float visu         = ImGui::CalcTextSize("Pack -1..1 to 0..1").x + ImGui::GetFrameHeight() + style.ItemInnerSpacing.x;
        float content      = std::max({ info_table, visu, ImGui::CalcTextSize("Slice").x + 200.0f });
        return content + style.WindowPadding.x * 2.0f + style.ScrollbarSize + 4.0f;
    }

    void draw_inspector_panel(float width, float height)
    {
        ImGui::BeginChild("##inspector_panel", ImVec2(width, height), true);

        if (!s.texture_current)
        {
            ImGui::TextDisabled("Select a texture to inspect");
            ImGui::EndChild();
            return;
        }

        spartan::RHI_Texture* tex = s.texture_current;

        if (ImGui::CollapsingHeader("Info", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("##info_table", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg))
            {
                auto row = [](const char* k, const char* v)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", k);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(v);
                };
                auto rowf = [](const char* k, const char* fmt, ...)
                {
                    va_list args;
                    va_start(args, fmt);
                    char buf[160];
                    vsnprintf(buf, sizeof(buf), fmt, args);
                    va_end(args);
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("%s", k);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(buf);
                };

                row("Name",     tex->GetObjectName().c_str());
                rowf("Size",    "%u x %u", tex->GetWidth(), tex->GetHeight());
                rowf("Channels","%u", tex->GetChannelCount());
                row("Format",   rhi_format_to_string(tex->GetFormat()));
                rowf("Mips",    "%u", tex->GetMipCount());
                rowf("Slices",  "%u", tex->GetArrayLength());
                row("Type",     texture_type_label(tex->GetType()));
                rowf("Memory",  "%.2f MB", static_cast<double>(texture_byte_estimate(tex)) / (1024.0 * 1024.0));
                ImGui::EndTable();
            }

            // usage badges
            ImGui::Spacing();
            ImGui::TextDisabled("usage:");
            auto badge = [](const char* label, bool on, ImU32 color_on)
            {
                if (!on)
                {
                    return;
                }
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, color_on);
                ImGui::SmallButton(label);
                ImGui::PopStyleColor();
            };
            badge("SRV", tex->IsSrv(), IM_COL32(60, 110, 60, 255));
            badge("UAV", tex->IsUav(), IM_COL32(110, 90, 60, 255));
            badge("RTV", tex->IsRtv(), IM_COL32(60, 90, 130, 255));
            badge("DSV", tex->IsDsv(), IM_COL32(130, 60, 90, 255));
            badge("VRS", tex->IsVrs(), IM_COL32(90, 60, 130, 255));
        }

        if (ImGui::CollapsingHeader("View", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const int max_mip = static_cast<int>(tex->GetMipCount()) - 1;
            ImGui::BeginDisabled(max_mip <= 0);
            ImGui::SliderInt("Mip", &s.mip_level, 0, std::max(0, max_mip));
            ImGui::EndDisabled();
            if (max_mip <= 0)
            {
                ImGuiSp::tooltip("Single mip");
            }

            const int max_slice = static_cast<int>(tex->GetArrayLength()) - 1;
            ImGui::BeginDisabled(max_slice <= 0);
            ImGui::SliderInt("Slice", &s.array_level, 0, std::max(0, max_slice));
            ImGui::EndDisabled();
            if (max_slice <= 0)
            {
                ImGuiSp::tooltip("Single slice");
            }
        }

        if (ImGui::CollapsingHeader("Channels", ImGuiTreeNodeFlags_DefaultOpen))
        {
            const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f;
            if (toggle_button("R", s.channel_r, ImVec2(w, 0)))
            {
                s.channel_r = !s.channel_r;
            }
            ImGui::SameLine();
            if (toggle_button("G", s.channel_g, ImVec2(w, 0)))
            {
                s.channel_g = !s.channel_g;
            }
            ImGui::SameLine();
            if (toggle_button("B", s.channel_b, ImVec2(w, 0)))
            {
                s.channel_b = !s.channel_b;
            }
            ImGui::SameLine();
            if (toggle_button("A", s.channel_a, ImVec2(w, 0)))
            {
                s.channel_a = !s.channel_a;
            }
        }

        if (ImGui::CollapsingHeader("Visualisation", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Gamma correct",      &s.gamma_correct);
            ImGuiSp::tooltip("Apply sRGB gamma curve when displaying the texture");
            ImGui::Checkbox("Pack -1..1 to 0..1", &s.pack);
            ImGuiSp::tooltip("Remap signed values to unsigned, useful for normal or velocity textures");
            ImGui::Checkbox("Boost",              &s.boost);
            ImGuiSp::tooltip("Multiply the visible value to inspect dim hdr content");
            ImGui::Checkbox("Abs",                &s.abs_value);
            ImGuiSp::tooltip("Take the absolute value before display");
            ImGui::Checkbox("Point sampling",     &s.point_sampling);
            ImGuiSp::tooltip("Use nearest neighbour sampling, useful when zoomed in");
        }

        ImGui::EndChild();
    }

    void draw_status_bar()
    {
        ImGui::Separator();
        spartan::RHI_Texture* tex = s.texture_current;
        if (!tex)
        {
            ImGui::TextDisabled("no texture");
            return;
        }

        if (s.hovered_uv.x >= 0.0f)
        {
            int px = static_cast<int>(s.hovered_uv.x * tex->GetWidth());
            int py = static_cast<int>(s.hovered_uv.y * tex->GetHeight());
            ImGui::Text("pixel %4d, %4d", px, py);
            ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
            ImGui::Text("uv %.3f, %.3f", s.hovered_uv.x, s.hovered_uv.y);
        }
        else
        {
            ImGui::TextDisabled("hover preview for pixel info");
        }

        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("%s", rhi_format_to_string(tex->GetFormat()));
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("mip %d / %u", s.mip_level, std::max<uint32_t>(1u, tex->GetMipCount()) - 1);
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("slice %d / %u", s.array_level, std::max<uint32_t>(1u, tex->GetArrayLength()) - 1);
        ImGui::SameLine(); ImGui::TextDisabled("|"); ImGui::SameLine();
        ImGui::Text("~%.1f MB", static_cast<double>(texture_byte_estimate(tex)) / (1024.0 * 1024.0));
    }
}

TextureViewer::TextureViewer(Editor* editor) : Widget(editor)
{
    m_title        = "Texture Viewer";
    m_visible      = false;
    m_size_initial = math::Vector2(1080.0f, 640.0f);
    m_size_min     = math::Vector2(880.0f, 440.0f);
}

void TextureViewer::OnTick()
{
    s.visualisation_flags = 0;
    s.texture_current     = nullptr;
}

void TextureViewer::OnVisible()
{
    refresh_entries();
}

void TextureViewer::OnTickVisible()
{
    // bindless table fills over a couple of frames, refresh periodically
    s.frames_since_refresh++;
    if (s.frames_since_refresh > 30)
    {
        refresh_entries();
    }

    apply_filters();

    // resolve current selection by stable id, fall back to first entry
    s.texture_current = nullptr;
    if (!entries_filtered.empty())
    {
        int idx = find_filtered_index_by_id(s.selected_object_id);
        if (idx < 0)
        {
            idx = 0;
            s.selected_object_id = entries_filtered[0].tex ? entries_filtered[0].tex->GetObjectId() : 0;
        }
        s.texture_current = entries_filtered[idx].tex;
    }

    // clamp mip and slice to current texture
    if (s.texture_current)
    {
        s.mip_level   = std::clamp(s.mip_level,   0, std::max(0, static_cast<int>(s.texture_current->GetMipCount())    - 1));
        s.array_level = std::clamp(s.array_level, 0, std::max(0, static_cast<int>(s.texture_current->GetArrayLength()) - 1));
    }

    draw_toolbar();

    // body sits above the status bar, panels separated by manual splitters
    const float status_h            = ImGui::GetTextLineHeightWithSpacing() + 8.0f;
    const float body_h              = std::max(80.0f, ImGui::GetContentRegionAvail().y - status_h);
    const float total_w             = ImGui::GetContentRegionAvail().x;
    const float min_left            = 200.0f;
    const float min_center          = 220.0f;
    const float inspector_fit       = inspector_natural_width();
    const float min_right           = inspector_fit;

    // right panel always at least as wide as its content, otherwise the format and channel a button get clipped
    s.right_panel_width = std::max(s.right_panel_width, min_right);
    s.left_panel_width  = std::clamp(s.left_panel_width,  min_left,  std::max(min_left,  total_w - min_center - s.right_panel_width - 16.0f));
    s.right_panel_width = std::clamp(s.right_panel_width, min_right, std::max(min_right, total_w - min_center - s.left_panel_width  - 16.0f));

    float center_w = std::max(min_center, total_w - s.left_panel_width - s.right_panel_width - 8.0f);

    draw_browser_panel(s.left_panel_width, body_h);
    ImGui::SameLine();
    draw_h_splitter("##splitter_left",  &s.left_panel_width,  min_left,  std::max(min_left,  total_w - min_center - s.right_panel_width - 16.0f), body_h, +1.0f);
    ImGui::SameLine();
    draw_preview_panel(center_w, body_h);
    ImGui::SameLine();
    draw_h_splitter("##splitter_right", &s.right_panel_width, min_right, std::max(min_right, total_w - min_center - s.left_panel_width  - 16.0f), body_h, -1.0f);
    ImGui::SameLine();
    draw_inspector_panel(s.right_panel_width, body_h);

    draw_status_bar();

    // pack flags consumed by ImGui_RHI when rendering the visualised texture
    s.visualisation_flags = 0;
    s.visualisation_flags |= s.channel_r      ? Visualise_Channel_R    : 0;
    s.visualisation_flags |= s.channel_g      ? Visualise_Channel_G    : 0;
    s.visualisation_flags |= s.channel_b      ? Visualise_Channel_B    : 0;
    s.visualisation_flags |= s.channel_a      ? Visualise_Channel_A    : 0;
    s.visualisation_flags |= s.gamma_correct  ? Visualise_GammaCorrect : 0;
    s.visualisation_flags |= s.pack           ? Visualise_Pack         : 0;
    s.visualisation_flags |= s.boost          ? Visualise_Boost        : 0;
    s.visualisation_flags |= s.abs_value      ? Visualise_Abs          : 0;
    s.visualisation_flags |= s.point_sampling ? Visualise_Sample_Point : 0;
}

uint32_t TextureViewer::GetVisualisationFlags()
{
    return s.visualisation_flags;
}

int TextureViewer::GetMipLevel()
{
    return s.mip_level;
}

int TextureViewer::GetArrayLevel()
{
    return s.array_level;
}

uint64_t TextureViewer::GetVisualisedTextureId()
{
    return s.texture_current ? s.texture_current->GetObjectId() : 0;
}
