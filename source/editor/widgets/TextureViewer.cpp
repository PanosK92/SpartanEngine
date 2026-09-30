/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "TextureViewer.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Properties.h"
#include "rendering/Material.h"
#include "rendering/Renderer.h"
#include "resource/ResourceCache.h"
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
        return t->GetRhiResource() ? t->GetObjectSize() : 0;
    }

    // RHI_Format_R16G16B16A16_Float reads as R16G16B16A16_Float, the prefix is the same on every texture
    const char* format_name(const spartan::RHI_Format format)
    {
        const char* name   = rhi_format_to_string(format);
        const char* prefix = "RHI_Format_";
        return strncmp(name, prefix, strlen(prefix)) == 0 ? name + strlen(prefix) : name;
    }

    // one tint per material slot, the type pills and the row captions share it
    ImVec4 material_type_tint(const spartan::MaterialTextureType t)
    {
        switch (t)
        {
            case spartan::MaterialTextureType::Color:     return ImVec4(1.00f, 0.52f, 0.42f, 1.0f);
            case spartan::MaterialTextureType::Roughness: return ImVec4(0.75f, 0.75f, 0.80f, 1.0f);
            case spartan::MaterialTextureType::Metalness: return ImVec4(0.55f, 0.75f, 0.95f, 1.0f);
            case spartan::MaterialTextureType::Normal:    return ImVec4(0.58f, 0.58f, 1.00f, 1.0f);
            case spartan::MaterialTextureType::Occlusion: return ImVec4(0.60f, 0.60f, 0.60f, 1.0f);
            case spartan::MaterialTextureType::Emission:  return ImVec4(1.00f, 0.78f, 0.30f, 1.0f);
            case spartan::MaterialTextureType::Height:    return ImVec4(0.55f, 0.85f, 0.35f, 1.0f);
            case spartan::MaterialTextureType::AlphaMask: return ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
            case spartan::MaterialTextureType::Packed:    return ImVec4(0.66f, 0.52f, 1.00f, 1.0f);
            default:                                      return ImGui::Style::color_accent_1;
        }
    }

    void select_texture(const texture_entry& entry)
    {
        if (!entry.tex || s.selected_object_id == entry.tex->GetObjectId())
        {
            return;
        }

        s.selected_object_id = entry.tex->GetObjectId();
        s.mip_level          = 0;
        s.array_level        = 0;
        s.request_fit        = true;
    }

    float splitter_thickness()
    {
        return ImGui::EditorUi::scaled(10.0f);
    }

    void draw_h_splitter(const char* id, float* size_left, float min_left, float max_left, float total_height, float sign)
    {
        const float thickness = splitter_thickness();
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

        // a hairline until it is grabbed, the gap between panels is enough to show where it is
        if (hovered || active)
        {
            const float x = IM_ROUND(cursor.x + thickness * 0.5f);
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, cursor.y), ImVec2(x, cursor.y + total_height), ImGui::EditorUi::color(active ? ImGui::Style::color_accent_1 : ImGui::Style::color_border), 2.0f);
        }
    }

    void draw_checkerboard(ImDrawList* dl, ImVec2 mn, ImVec2 mx, float tile)
    {
        const ImU32 c0 = IM_COL32(36, 37, 41, 255);
        const ImU32 c1 = IM_COL32(50, 51, 56, 255);
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

    uint32_t count_render_targets()
    {
        uint32_t count = 0;
        for (const std::shared_ptr<spartan::RHI_Texture>& tex : spartan::Renderer::GetRenderTargets())
        {
            count += tex ? 1 : 0;
        }
        return count;
    }

    uint32_t count_material_textures()
    {
        uint32_t count = 0;
        for (spartan::RHI_Texture* tex : spartan::Renderer::GetBindlessMaterialTextures())
        {
            count += tex ? 1 : 0;
        }
        return count;
    }

    void draw_toolbar()
    {
        namespace toolbar = editor_ui::toolbar;
        const float gap   = ImGui::EditorUi::scaled(6.0f);

        // the two sources are exclusive pills with their counts, the same control every tool window uses for a mode
        char label[64];
        snprintf(label, sizeof(label), "Render targets %u###source_rt", count_render_targets());
        if (toolbar::pill(label, s.source == viewer_state::source_kind::render_targets, ImGui::Style::color_accent_1, "Textures produced by renderer passes") && s.source != viewer_state::source_kind::render_targets)
        {
            s.source = viewer_state::source_kind::render_targets;
            refresh_entries();
        }
        ImGui::SameLine(0, gap);
        snprintf(label, sizeof(label), "Materials %u###source_materials", count_material_textures());
        if (toolbar::pill(label, s.source == viewer_state::source_kind::bindless_materials, ImGui::Style::color_accent_1, "Textures currently bound for materials") && s.source != viewer_state::source_kind::bindless_materials)
        {
            s.source = viewer_state::source_kind::bindless_materials;
            refresh_entries();
        }

        toolbar::divider();
        if (toolbar::pill("List", !s.view_grid, ImGui::Style::color_accent_1, "Rows with size and format"))
        {
            s.view_grid = false;
        }
        ImGui::SameLine(0, gap);
        if (toolbar::pill("Grid", s.view_grid, ImGui::Style::color_accent_1, "Thumbnails only, more at once"))
        {
            s.view_grid = true;
        }

        // zoom cluster, right aligned, secondary actions so they stay quiet until hovered
        char zoom_text[16];
        snprintf(zoom_text, sizeof(zoom_text), "%.0f%%", s.zoom * 100.0f);
        const float zoom_w    = ImGui::CalcTextSize("6400%").x;
        const float cluster_w = toolbar::ghost_button_width("Fit") + toolbar::ghost_button_width("1:1") + toolbar::ghost_button_width("-") * 2.0f + zoom_w + toolbar::ghost_button_width("Reset") + gap * 5.0f;

        toolbar::divider();
        char count[32];
        snprintf(count, sizeof(count), "%zu of %zu", entries_filtered.size(), entries.size());
        const float search_w = std::max(ImGui::EditorUi::scaled(120.0f), ImGui::GetContentRegionAvail().x - cluster_w - ImGui::EditorUi::scaled(16.0f));
        toolbar::search("##texture_search", "Search textures", s.search_filter, std::min(search_w, ImGui::EditorUi::scaled(420.0f)), count, entries_filtered.empty() && !entries.empty());

        toolbar::align_right(cluster_w);
        if (toolbar::ghost_button("Fit", "Fit to the preview (F)"))
        {
            s.request_fit = true;
        }
        ImGui::SameLine(0, gap);
        if (toolbar::ghost_button("1:1", "One texel to one pixel (1)"))
        {
            s.request_one_to_one = true;
        }
        ImGui::SameLine(0, gap);
        if (toolbar::ghost_button("-", "Zoom out (-)"))
        {
            s.request_zoom_mul = 0.9f;
        }
        ImGui::SameLine(0, gap);
        const float zoom_x = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::SetCursorPosX(zoom_x + (zoom_w - ImGui::CalcTextSize(zoom_text).x) * 0.5f);
        ImGui::TextUnformatted(zoom_text);
        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosX(zoom_x + zoom_w + gap);
        if (toolbar::ghost_button("+", "Zoom in (+)"))
        {
            s.request_zoom_mul = 1.1f;
        }
        ImGui::SameLine(0, gap);
        if (toolbar::ghost_button("Reset", "Reset pan and zoom (R)"))
        {
            s.request_reset = true;
        }
    }

    // material slots as wrapping pills with counts, clicking one isolates it and clicking it again shows every slot
    void draw_type_filter()
    {
        if (s.source != viewer_state::source_kind::bindless_materials)
        {
            return;
        }

        const uint32_t types = static_cast<uint32_t>(spartan::MaterialTextureType::Max);
        uint32_t counts[32]  = {};
        for (const texture_entry& e : entries)
        {
            counts[static_cast<uint32_t>(bindless_type_from_index(e.bindless_index))]++;
        }

        const uint32_t all_mask = (1u << types) - 1u;
        const bool all_types    = (s.type_filter_mask & all_mask) == all_mask;
        const float gap         = ImGui::EditorUi::scaled(4.0f);
        const float left        = ImGui::GetCursorScreenPos().x;
        const float right       = left + ImGui::GetContentRegionAvail().x;
        bool first              = true;
        for (uint32_t i = 0; i < types; i++)
        {
            if (counts[i] == 0)
            {
                continue;
            }

            const spartan::MaterialTextureType type = static_cast<spartan::MaterialTextureType>(i);
            char label[48];
            snprintf(label, sizeof(label), "%s %u###type_%u", material_texture_type_label(type), counts[i], i);
            if (!first)
            {
                ImGui::SameLine(0, gap);
                if (ImGui::GetCursorScreenPos().x + editor_ui::toolbar::pill_width(label, true) > right)
                {
                    ImGui::NewLine();
                }
            }
            first = false;

            const uint32_t bit = 1u << i;
            const bool active  = !all_types && (s.type_filter_mask & bit) != 0;
            if (editor_ui::toolbar::pill(label, active, material_type_tint(type), active ? "Click again to show every slot" : "Show only this material slot", true))
            {
                s.type_filter_mask = active ? 0xffffffffu : bit;
            }
        }
        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));
    }

    void draw_entry_row(int idx)
    {
        const texture_entry& e = entries_filtered[idx];
        if (!e.tex)
        {
            return;
        }

        const float row_height = ImGui::EditorUi::scaled(44.0f);
        const float thumb_size = row_height - ImGui::EditorUi::scaled(8.0f);
        const bool  selected   = (s.selected_object_id == e.tex->GetObjectId());

        ImGui::PushID(idx);

        // selectable owns the full row layout and click handling
        ImVec2 row_min = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##row", selected, 0, ImVec2(0.0f, row_height)))
        {
            select_texture(e);
        }

        // overlays are pure draws so we never disturb the cursor
        ImDrawList* dl       = ImGui::GetWindowDrawList();
        const float rounding = ImGui::EditorUi::scaled(4.0f);
        ImVec2 thumb_min     = ImVec2(row_min.x + ImGui::EditorUi::scaled(4.0f), row_min.y + (row_height - thumb_size) * 0.5f);
        ImVec2 thumb_max     = ImVec2(thumb_min.x + thumb_size, thumb_min.y + thumb_size);
        draw_checkerboard(dl, thumb_min, thumb_max, ImGui::EditorUi::scaled(6.0f));
        dl->AddImageRounded(reinterpret_cast<ImTextureID>(e.tex), thumb_min, thumb_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
        dl->AddRect(thumb_min, thumb_max, ImGui::EditorUi::color(ImGui::Style::color_border), rounding);

        // name first, then the facts that tell two similar names apart, the material slot leads in its tint
        const float text_x   = thumb_max.x + ImGui::EditorUi::scaled(10.0f);
        const float line_h   = ImGui::GetTextLineHeight();
        const float top      = row_min.y + (row_height - line_h * 2.0f - ImGui::EditorUi::scaled(2.0f)) * 0.5f;
        const float right    = row_min.x + ImGui::GetContentRegionAvail().x;
        dl->PushClipRect(ImVec2(text_x, row_min.y), ImVec2(right, row_min.y + row_height), true);
        dl->AddText(ImVec2(text_x, top), ImGui::EditorUi::color(ImGui::Style::color_text), e.tex->GetObjectName().c_str());

        float caption_x = text_x;
        const float caption_y = top + line_h + ImGui::EditorUi::scaled(2.0f);
        if (s.source == viewer_state::source_kind::bindless_materials)
        {
            const spartan::MaterialTextureType type = bindless_type_from_index(e.bindless_index);
            const char* type_label = material_texture_type_label(type);
            dl->AddText(ImVec2(caption_x, caption_y), ImGui::EditorUi::color(material_type_tint(type)), type_label);
            caption_x += ImGui::CalcTextSize(type_label).x + ImGui::EditorUi::scaled(8.0f);
        }
        char info[96];
        snprintf(info, sizeof(info), "%u \xC3\x97 %u   %s", e.tex->GetWidth(), e.tex->GetHeight(), format_name(e.tex->GetFormat()));
        dl->AddText(ImVec2(caption_x, caption_y), ImGui::EditorUi::color(ImGui::Style::color_text_muted), info);
        dl->PopClipRect();

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip("%s\n%u \xC3\x97 %u   %s", e.display_name.c_str(), e.tex->GetWidth(), e.tex->GetHeight(), format_name(e.tex->GetFormat()));
        }

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
        const float total_h = card_size + ImGui::GetTextLineHeight() + ImGui::EditorUi::scaled(6.0f);

        ImGui::PushID(idx);
        ImVec2 card_min = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##card", selected, 0, ImVec2(card_size, total_h)))
        {
            select_texture(e);
        }

        // overlays via draw list so we leave the layout cursor exactly where Selectable put it
        ImDrawList* dl       = ImGui::GetWindowDrawList();
        const float rounding = ImGui::EditorUi::scaled(5.0f);
        const float inset    = ImGui::EditorUi::scaled(3.0f);
        ImVec2 thumb_min     = ImVec2(card_min.x + inset, card_min.y + inset);
        ImVec2 thumb_max     = ImVec2(card_min.x + card_size - inset, card_min.y + card_size - inset);
        draw_checkerboard(dl, thumb_min, thumb_max, ImGui::EditorUi::scaled(8.0f));
        dl->AddImageRounded(reinterpret_cast<ImTextureID>(e.tex), thumb_min, thumb_max, ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, rounding);
        dl->AddRect(thumb_min, thumb_max, ImGui::EditorUi::color(selected ? ImGui::Style::color_accent_1 : ImGui::Style::color_border), rounding, selected ? 2.0f : 1.0f);

        // single line label, clipped to the card width
        ImVec4 clip(card_min.x, card_min.y + card_size, card_min.x + card_size - inset, card_min.y + total_h);
        dl->AddText(nullptr, 0.0f, ImVec2(card_min.x + inset, card_min.y + card_size), ImGui::EditorUi::color(selected ? ImGui::Style::color_text : ImGui::Style::color_text_muted), e.tex->GetObjectName().c_str(), nullptr, 0.0f, &clip);

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        {
            ImGui::SetTooltip("%s\n%u \xC3\x97 %u   %s", e.display_name.c_str(), e.tex->GetWidth(), e.tex->GetHeight(), format_name(e.tex->GetFormat()));
        }

        ImGui::PopID();
    }

    void draw_browser_panel(float width, float height)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
        ImGui::BeginChild("##browser_panel", ImVec2(width, height), ImGuiChildFlags_Borders);
        ImGui::PopStyleVar();

        draw_type_filter();

        ImGui::BeginChild("##entries_scroll", ImVec2(0, 0), false);

        if (entries_filtered.empty())
        {
            const bool filtered = !entries.empty();
            if (editor_ui::empty_state(filtered ? "No textures match" : "No textures yet", filtered ? "Nothing passes the search and slot filters together." : (s.source == viewer_state::source_kind::render_targets ? "Render targets appear once the renderer has created them." : "Material textures appear once a material binds them."), filtered ? "Clear filters" : nullptr))
            {
                s.search_filter.Clear();
                s.type_filter_mask = 0xffffffffu;
            }
        }
        else if (s.view_grid)
        {
            const float card     = ImGui::EditorUi::scaled(96.0f);
            const float padding  = ImGui::GetStyle().ItemSpacing.x;
            const float row_h    = card + ImGui::GetTextLineHeight() + ImGui::EditorUi::scaled(6.0f) + padding;
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
            clipper.Begin(static_cast<int>(entries_filtered.size()), ImGui::EditorUi::scaled(44.0f) + ImGui::GetStyle().ItemSpacing.y);
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
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
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

            // hud, one pill in the corner with what the image is showing, only the levels that exist
            char hud[128];
            int written = snprintf(hud, sizeof(hud), "%.0f%%", s.zoom * 100.0f);
            if (tex->GetResidentMipCount() > 1)
            {
                written += snprintf(hud + written, sizeof(hud) - written, "   mip %d of %u", s.mip_level, tex->GetResidentMipCount() - 1);
            }
            if (tex->GetArrayLength() > 1)
            {
                written += snprintf(hud + written, sizeof(hud) - written, "   slice %d of %u", s.array_level, tex->GetArrayLength() - 1);
            }
            const ImVec2 hud_size = ImGui::CalcTextSize(hud);
            const ImVec2 hud_pad  = ImGui::EditorUi::scaled(ImVec2(10.0f, 4.0f));
            const ImVec2 hud_min  = ImVec2(child_pos.x + ImGui::EditorUi::scaled(10.0f), child_max.y - ImGui::EditorUi::scaled(10.0f) - hud_size.y - hud_pad.y * 2.0f);
            const ImVec2 hud_max  = ImVec2(hud_min.x + hud_size.x + hud_pad.x * 2.0f, hud_min.y + hud_size.y + hud_pad.y * 2.0f);
            dl->AddRectFilled(hud_min, hud_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_canvas_deep, 0.85f)), (hud_max.y - hud_min.y) * 0.5f);
            dl->AddRect(hud_min, hud_max, ImGui::EditorUi::color(ImGui::Style::color_border), (hud_max.y - hud_min.y) * 0.5f);
            dl->AddText(ImVec2(hud_min.x + hud_pad.x, hud_min.y + hud_pad.y), ImGui::EditorUi::color(ImGui::Style::color_text), hud);
        }
        else
        {
            const char* msg     = "Select a texture to preview";
            ImVec2 text_size    = ImGui::CalcTextSize(msg);
            ImVec2 text_pos     = ImVec2(
                child_pos.x + (child_size.x - text_size.x) * 0.5f,
                child_pos.y + (child_size.y - text_size.y) * 0.5f
            );
            dl->AddText(text_pos, ImGui::EditorUi::color(ImGui::Style::color_text_muted), msg);
        }

        dl->AddRect(child_pos, child_max, ImGui::EditorUi::color(ImGui::Style::color_border), ImGui::EditorUi::scaled(6.0f), 1.0f);
        ImGui::EndChild();
    }

    float inspector_natural_width()
    {
        // the value column has to hold the longest format name, the label column is a fixed share of the panel
        const ImGuiStyle& style = ImGui::GetStyle();
        const float widest_value = ImGui::CalcTextSize("R16G16B16A16_Float").x + ImGui::EditorUi::scaled(12.0f);
        const float content      = widest_value / (1.0f - editor_ui::design::label_width);
        return std::max(ImGui::EditorUi::scaled(320.0f), content + style.WindowPadding.x * 2.0f + style.ScrollbarSize);
    }

    void draw_debug_writer()
    {
        // controls which pass writes the debug_output render target, only one writer runs at a time
        static const std::vector<std::string> debug_texture_modes =
        {
            "Off",
            "Meshlets: color by meshlet id",
            "Meshlets: wireframe by meshlet id",
            "Meshlets: color by post-cull draw id",
            "Meshlets: wireframe by post-cull draw id",
            "Clusters: lights per cluster (heatmap)",
            "Clusters: z slice"
        };

        uint32_t meshlet_mode = spartan::cvar_meshlet_visualize.GetValueAs<uint32_t>();
        uint32_t cluster_mode = spartan::cvar_cluster_visualize.GetValueAs<uint32_t>();
        uint32_t combined_mode =
            meshlet_mode > 0 ? meshlet_mode :
            cluster_mode > 0 ? (4u + cluster_mode) :
            0u;

        if (editor_ui::property_combo("Writer", debug_texture_modes, &combined_mode, "Picks which pass writes this texture, only one is active at a time"))
        {
            uint32_t new_meshlet = (combined_mode >= 1u && combined_mode <= 4u) ? combined_mode      : 0u;
            uint32_t new_cluster = (combined_mode >= 5u && combined_mode <= 6u) ? combined_mode - 4u : 0u;
            spartan::ConsoleRegistry::Get().SetValueFromString("r.meshlet_visualize", std::to_string(static_cast<float>(new_meshlet)));
            spartan::ConsoleRegistry::Get().SetValueFromString("r.cluster_visualize", std::to_string(static_cast<float>(new_cluster)));
        }

        // cluster overflow telemetry, surfaces the gpu side overflow counter so bad worlds are visible
        const uint32_t overflow = spartan::Renderer::GetClusterOverflowCount();
        char text[32];
        snprintf(text, sizeof(text), "%u", overflow);
        editor_ui::property_text("Cluster overflow", text, "Lights that did not fit in their cluster this frame, anything above zero is dropped lighting");
        if (overflow > 0)
        {
            editor_ui::layout::note("Some clusters overflowed, lights are being dropped", ImGui::Style::color_warning);
        }
    }

    // r g b a as four equal toggles in their channel colors, lit while the channel is shown
    void draw_channel_toggles()
    {
        bool* values[4]      = { &s.channel_r, &s.channel_g, &s.channel_b, &s.channel_a };
        const char* names[4] = { "R", "G", "B", "A" };
        const ImVec4 tints[4] =
        {
            ImVec4(1.00f, 0.38f, 0.38f, 1.0f),
            ImVec4(0.40f, 0.86f, 0.46f, 1.0f),
            ImVec4(0.36f, 0.60f, 1.00f, 1.0f),
            ImVec4(0.86f, 0.86f, 0.86f, 1.0f)
        };
        const float gap       = ImGui::EditorUi::scaled(4.0f);
        const float width     = (ImGui::GetContentRegionAvail().x - gap * 3.0f) / 4.0f;
        const float height    = ImGui::GetFrameHeight();
        const float rounding  = ImGui::GetStyle().FrameRounding;
        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        for (int i = 0; i < 4; i++)
        {
            if (i > 0)
            {
                ImGui::SameLine(0.0f, gap);
            }

            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##channel", ImVec2(width, height)))
            {
                *values[i] = !*values[i];
            }
            const bool hovered = ImGui::IsItemHovered();
            ImGui::PopID();

            const ImVec2 min  = ImGui::GetItemRectMin();
            const ImVec2 max  = ImGui::GetItemRectMax();
            const bool on     = *values[i];
            const ImVec4 fill = on ? ImGui::EditorUi::alpha(tints[i], 0.28f) : (hovered ? ImGui::Style::color_surface_hover : ImGui::Style::color_canvas_deep);
            draw_list->AddRectFilled(min, max, ImGui::EditorUi::color(fill), rounding);
            draw_list->AddRect(min, max, ImGui::EditorUi::color(on ? ImGui::EditorUi::alpha(tints[i], 0.85f) : ImGui::Style::color_border), rounding, 1.0f);

            const ImVec2 size = ImGui::CalcTextSize(names[i]);
            const ImVec4 text = on ? ImGui::Style::lerp(tints[i], ImVec4(1, 1, 1, 1), 0.45f) : ImGui::Style::color_text_faint;
            draw_list->AddText(ImVec2(IM_ROUND((min.x + max.x - size.x) * 0.5f), IM_ROUND((min.y + max.y - size.y) * 0.5f)), ImGui::EditorUi::color(text), names[i]);
            editor_ui::toolbar::tooltip(on ? "Shown, click to hide this channel" : "Hidden, click to show this channel");
        }
    }

    // a mip or slice picker, disabled with the reason when the texture only has one
    void draw_level_slider(const char* label, int* value, const int max_value, const char* single_reason)
    {
        editor_ui::layout::begin_property(label, max_value <= 0 ? single_reason : nullptr);
        char format[32];
        snprintf(format, sizeof(format), "%%d of %d", std::max(0, max_value));
        ImGui::BeginDisabled(max_value <= 0);
        ImGui::SliderInt((std::string("##") + label).c_str(), value, 0, std::max(0, max_value), format, ImGuiSliderFlags_AlwaysClamp);
        ImGui::EditorUi::decorate_field();
        ImGui::EndDisabled();
    }

    void draw_inspector_panel(float width, float height)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, ImGui::EditorUi::scaled(6.0f));
        ImGui::BeginChild("##inspector_panel", ImVec2(width, height), ImGuiChildFlags_Borders);
        ImGui::PopStyleVar();

        if (!s.texture_current)
        {
            editor_ui::empty_state("Nothing selected", "Pick a texture on the left to read its format and change how it is displayed.");
            ImGui::EndChild();
            return;
        }

        spartan::RHI_Texture* tex = s.texture_current;

        // the name, then what the gpu may do with it, as chips so the eye catches a render target at once
        if (Editor::font_bold)
        {
            ImGui::PushFont(Editor::font_bold, 0.0f);
        }
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(tex->GetObjectName().c_str());
        ImGui::PopTextWrapPos();
        if (Editor::font_bold)
        {
            ImGui::PopFont();
        }

        struct usage
        {
            const char* label;
            bool on;
            ImVec4 tint;
            const char* meaning;
        };
        const usage usages[] =
        {
            { "Sampled",       tex->IsSrv(), ImVec4(0.40f, 0.86f, 0.60f, 1.0f), "Shaders can sample it (SRV)" },
            { "Storage",       tex->IsUav(), ImVec4(1.00f, 0.72f, 0.36f, 1.0f), "Compute shaders can write it (UAV)" },
            { "Render target", tex->IsRtv(), ImVec4(0.36f, 0.66f, 1.00f, 1.0f), "Passes render into it (RTV)" },
            { "Depth",         tex->IsDsv(), ImVec4(1.00f, 0.42f, 0.66f, 1.0f), "Used as a depth stencil (DSV)" },
            { "Shading rate",  tex->IsVrs(), ImVec4(0.66f, 0.52f, 1.00f, 1.0f), "Drives variable rate shading (VRS)" }
        };
        bool first_chip = true;
        for (const usage& u : usages)
        {
            if (!u.on)
            {
                continue;
            }
            if (!first_chip)
            {
                ImGui::SameLine(0, ImGui::EditorUi::scaled(4.0f));
            }
            first_chip = false;
            editor_ui::chip(u.label, u.tint);
            editor_ui::toolbar::tooltip(u.meaning);
        }
        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));

        char size_text[48];
        snprintf(size_text, sizeof(size_text), "%u \xC3\x97 %u", tex->GetWidth(), tex->GetHeight());
        if (editor_ui::layout::fold("Info", true, size_text))
        {
            char text[96];
            editor_ui::property_text("Size", size_text);
            editor_ui::property_text("Format", format_name(tex->GetFormat()));
            snprintf(text, sizeof(text), "%s, %u channel%s", texture_type_label(tex->GetType()), tex->GetChannelCount(), tex->GetChannelCount() == 1 ? "" : "s");
            editor_ui::property_text("Type", text);
            snprintf(text, sizeof(text), "%u of %u resident", tex->GetResidentMipCount(), tex->GetMipCount());
            editor_ui::property_text("Mips", text, "Streaming keeps only the mips the camera needs in memory");
            if (tex->GetResidentMip() > 0)
            {
                snprintf(text, sizeof(text), "%u \xC3\x97 %u from mip %u", tex->GetResidentWidth(), tex->GetResidentHeight(), tex->GetResidentMip());
                editor_ui::property_text("Resident", text, "The largest mip currently in memory");
            }
            if (tex->GetArrayLength() > 1)
            {
                snprintf(text, sizeof(text), "%u", tex->GetArrayLength());
                editor_ui::property_text("Slices", text);
            }
            const uint64_t bytes = texture_byte_estimate(tex);
            editor_ui::property_text("Memory", bytes > 0 ? editor_ui::format::bytes(static_cast<double>(bytes)) : std::string("not resident"));
        }

        const int max_mip   = static_cast<int>(tex->GetResidentMipCount()) - 1;
        const int max_slice = static_cast<int>(tex->GetArrayLength()) - 1;
        if (max_mip > 0 || max_slice > 0)
        {
            char summary[48];
            snprintf(summary, sizeof(summary), "mip %d, slice %d", s.mip_level, s.array_level);
            if (editor_ui::layout::fold("Level", true, summary))
            {
                draw_level_slider("Mip", &s.mip_level, max_mip, "This texture has a single mip");
                draw_level_slider("Slice", &s.array_level, max_slice, "This texture has a single slice");
            }
        }

        const uint32_t shown = (s.channel_r ? 1 : 0) + (s.channel_g ? 1 : 0) + (s.channel_b ? 1 : 0) + (s.channel_a ? 1 : 0);
        char channel_summary[24];
        snprintf(channel_summary, sizeof(channel_summary), "%s", shown == 4 ? "all shown" : (shown == 0 ? "none shown" : "filtered"));
        if (editor_ui::layout::fold("Channels", true, channel_summary))
        {
            draw_channel_toggles();
        }

        if (editor_ui::layout::fold("Display"))
        {
            editor_ui::property_toggle("Gamma correct",  &s.gamma_correct,  "Apply the sRGB curve when displaying, turn off for data textures such as normals or masks");
            editor_ui::property_toggle("Remap signed",   &s.pack,           "Map -1..1 to 0..1, useful for normal or velocity textures");
            editor_ui::property_toggle("Boost",          &s.boost,          "Multiply the visible value to inspect dim hdr content");
            editor_ui::property_toggle("Absolute value", &s.abs_value,      "Take the absolute value before display, negative values become visible");
            editor_ui::property_toggle("Point sampling", &s.point_sampling, "Nearest neighbour sampling, shows individual texels when zoomed in");
        }

        // debug writer control lives here since it drives what this specific render target shows
        if (tex == spartan::Renderer::GetRenderTarget(spartan::Renderer_RenderTarget::debug_output))
        {
            if (editor_ui::layout::fold("Debug output"))
            {
                draw_debug_writer();
            }
        }

        ImGui::EndChild();
    }

    void draw_status_bar()
    {
        ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));
        spartan::RHI_Texture* tex = s.texture_current;
        if (!tex)
        {
            ImGui::TextColored(ImGui::Style::color_text_faint, "No texture selected");
            return;
        }

        // where the cursor is on the left, what is being shown on the right
        if (s.hovered_uv.x >= 0.0f)
        {
            const int px = static_cast<int>(s.hovered_uv.x * tex->GetWidth());
            const int py = static_cast<int>(s.hovered_uv.y * tex->GetHeight());
            ImGui::TextColored(ImGui::Style::color_text, "Pixel %d, %d", px, py);
            ImGui::SameLine(0, ImGui::EditorUi::scaled(16.0f));
            ImGui::TextColored(ImGui::Style::color_text_muted, "UV %.3f, %.3f", s.hovered_uv.x, s.hovered_uv.y);
        }
        else
        {
            ImGui::TextColored(ImGui::Style::color_text_faint, "Hover the preview to read a position, wheel zooms, middle drag pans");
        }

        char right[160];
        const uint64_t bytes = texture_byte_estimate(tex);
        snprintf(right, sizeof(right), "%s     mip %d of %u     slice %d of %u     %s", format_name(tex->GetFormat()), s.mip_level, std::max<uint32_t>(1u, tex->GetResidentMipCount()) - 1, s.array_level, std::max<uint32_t>(1u, tex->GetArrayLength()) - 1, bytes > 0 ? editor_ui::format::bytes(static_cast<double>(bytes)).c_str() : "not resident");
        editor_ui::toolbar::align_right(ImGui::CalcTextSize(right).x);
        ImGui::TextColored(ImGui::Style::color_text_muted, "%s", right);
    }
}

TextureViewer::TextureViewer(Editor* editor) : Widget(editor)
{
    m_title         = "Texture Viewer";
    m_visible       = false;
    m_toolbar_order = 5;
    m_toolbar_icon  = static_cast<int>(spartan::IconType::Texture);
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
            select_texture(entries_filtered[0]);
        }
        s.texture_current = entries_filtered[idx].tex;
    }

    // clamp mip and slice to current texture
    if (s.texture_current)
    {
        s.mip_level   = std::clamp(s.mip_level,   0, std::max(0, static_cast<int>(s.texture_current->GetResidentMipCount())    - 1));
        s.array_level = std::clamp(s.array_level, 0, std::max(0, static_cast<int>(s.texture_current->GetArrayLength()) - 1));
    }

    draw_toolbar();

    // body sits above the status bar, panels separated by manual splitters
    ImGui::Dummy(ImVec2(0.0f, ImGui::EditorUi::scaled(2.0f)));
    const float status_h            = ImGui::GetTextLineHeightWithSpacing() + ImGui::EditorUi::scaled(8.0f);
    const float body_h              = std::max(80.0f, ImGui::GetContentRegionAvail().y - status_h);
    const float total_w             = ImGui::GetContentRegionAvail().x;
    const float min_left            = ImGui::EditorUi::scaled(240.0f);
    const float min_center          = ImGui::EditorUi::scaled(220.0f);
    const float inspector_fit       = inspector_natural_width();
    const float min_right           = inspector_fit;
    const float gutters             = splitter_thickness() * 2.0f;

    // right panel always at least as wide as its content, otherwise the format and channel a button get clipped
    s.right_panel_width = std::max(s.right_panel_width, min_right);
    s.left_panel_width  = std::clamp(s.left_panel_width,  min_left,  std::max(min_left,  total_w - min_center - s.right_panel_width - gutters));
    s.right_panel_width = std::clamp(s.right_panel_width, min_right, std::max(min_right, total_w - min_center - s.left_panel_width  - gutters));

    float center_w = std::max(min_center, total_w - s.left_panel_width - s.right_panel_width - gutters);

    draw_browser_panel(s.left_panel_width, body_h);
    ImGui::SameLine(0.0f, 0.0f);
    draw_h_splitter("##splitter_left",  &s.left_panel_width,  min_left,  std::max(min_left,  total_w - min_center - s.right_panel_width - gutters), body_h, +1.0f);
    ImGui::SameLine(0.0f, 0.0f);
    draw_preview_panel(center_w, body_h);
    ImGui::SameLine(0.0f, 0.0f);
    draw_h_splitter("##splitter_right", &s.right_panel_width, min_right, std::max(min_right, total_w - min_center - s.left_panel_width  - gutters), body_h, -1.0f);
    ImGui::SameLine(0.0f, 0.0f);
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
