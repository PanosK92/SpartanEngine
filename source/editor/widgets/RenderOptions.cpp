/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "RenderOptions.h"
#include "core/Timer.h"
#include "rhi/RHI_Device.h"
#include "rendering/Renderer.h"
#include "world/World.h"
#include "../imgui/ImGui_Extension.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    namespace ui    = ImGui::EditorUi;
    namespace style = ImGui::Style;

    vector<DisplayMode> display_modes;
    ImGuiTextFilter filter;

    // one page, many sections: while filtering a section draws its header only once a row survives,
    // so sections without a match disappear instead of leaving empty headers behind
    struct Section
    {
        const char* title   = nullptr;
        string summary;
        bool default_open   = true;
        bool title_matches  = false;
        bool header_drawn   = false;
        bool table_open     = false;
    };
    Section section;
    const char* row_cvar       = nullptr;
    uint32_t visible_row_count = 0;

    const float fps_unlocked = 10000.0f;
    const char* separator    = "  \xC2\xB7  "; // middle dot

    //= CVARS ==================================================================
    float cvar_get(const char* name)
    {
        return ConsoleRegistry::Get().GetAs<float>(name);
    }

    // only write on user interaction, otherwise an async cvar update (eg world load) can be reverted
    void cvar_write(const char* name, const float value)
    {
        ConsoleRegistry::Get().SetValueFromString(name, to_string(value));
    }

    float cvar_default(const char* name)
    {
        // these register during static init, before the device can report ray tracing support, so their stored default always reads as off
        if (strcmp(name, "r.ray_traced_reflections") == 0 || strcmp(name, "r.ray_traced_shadows") == 0)
        {
            return RHI_Device::IsSupportedRayTracing() ? 1.0f : 0.0f;
        }

        const ConsoleVariable* variable = ConsoleRegistry::Get().Find(name);
        const float* value              = variable ? get_if<float>(&variable->m_default_value) : nullptr;
        return value ? *value : 0.0f;
    }

    bool cvar_modified(const char* name)
    {
        return fabsf(cvar_get(name) - cvar_default(name)) > 1e-4f;
    }

    string format_resolution(const uint32_t width, const uint32_t height)
    {
        return to_string(width) + " \xC3\x97 " + to_string(height); // multiplication sign
    }

    string format_resolution(const Vector2& resolution)
    {
        return format_resolution(static_cast<uint32_t>(resolution.x), static_cast<uint32_t>(resolution.y));
    }

    Renderer_AntiAliasing_Upsampling upscaler()
    {
        return cvar_antialiasing_upsampling.GetValueAs<Renderer_AntiAliasing_Upsampling>();
    }

    bool upscaler_is_temporal()
    {
        const Renderer_AntiAliasing_Upsampling method = upscaler();
        return method == Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau ||
               method == Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess ||
               method == Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss;
    }

    const char* upscaler_short_name()
    {
        switch (upscaler())
        {
            case Renderer_AntiAliasing_Upsampling::AA_Fxaa_Upscale_Linear: return "FXAA";
            case Renderer_AntiAliasing_Upsampling::AA_Taau_Upscale_Taau:   return "TAAU";
            case Renderer_AntiAliasing_Upsampling::AA_Xess_Upscale_Xess:   return "XeSS";
            case Renderer_AntiAliasing_Upsampling::AA_Dlss_Upscale_Dlss:   return "DLSS";
            default:                                                       return "Bilinear";
        }
    }

    //= SECTIONS ===============================================================
    void draw_section_header()
    {
        section.header_drawn = true;

        // a filtered view forces every section open under its own id, so clearing the
        // filter brings back exactly the sections the user had open before
        bool open = true;
        if (filter.IsActive())
        {
            ImGui::PushID("filtered");
            ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            open = ImGuiSp::collapsing_header(section.title, ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::PopID();
        }
        else
        {
            open = ImGuiSp::collapsing_header(section.title, section.default_open ? ImGuiTreeNodeFlags_DefaultOpen : 0);
        }

        // a quiet summary on the right edge, so a collapsed section still reports its state
        if (!section.summary.empty())
        {
            const ImVec2 min        = ImGui::GetItemRectMin();
            const ImVec2 max        = ImGui::GetItemRectMax();
            const ImVec2 size       = ImGui::CalcTextSize(section.summary.c_str());
            const float padding     = ImGui::GetStyle().FramePadding.x * 2.0f;
            const float title_right = min.x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(section.title).x + padding;
            const float x           = max.x - padding - size.x;
            if (x > title_right)
            {
                const float y = min.y + (max.y - min.y - size.y) * 0.5f;
                ImGui::GetWindowDrawList()->AddText(ImVec2(x, y), ui::color(style::color_text_muted), section.summary.c_str());
            }
        }

        section.table_open = false;
        if (!open)
        {
            section.title = nullptr;
        }
    }

    // returns false when the section is collapsed, rows are then skipped entirely
    bool section_begin(const char* title, const string& summary = "", const bool default_open = true)
    {
        section               = Section();
        section.title         = title;
        section.summary       = summary;
        section.default_open  = default_open;
        section.title_matches = filter.IsActive() && filter.PassFilter(title);

        if (filter.IsActive())
        {
            return true;
        }

        draw_section_header();
        return section.title != nullptr;
    }

    // true when the section shows everything, custom content (like the resolution card) only draws then
    bool section_unfiltered()
    {
        return !filter.IsActive() && section.title != nullptr;
    }

    void section_end()
    {
        if (section.table_open)
        {
            ImGui::EndTable();
            ui::pop_table_style();
            section.table_open = false;
        }

        if (section.header_drawn)
        {
            ImGui::Dummy(ImVec2(0.0f, ui::scaled(6.0f)));
        }
    }

    bool ensure_rows()
    {
        if (!section.header_drawn)
        {
            draw_section_header();
        }

        if (!section.title)
        {
            return false;
        }

        if (!section.table_open)
        {
            ui::push_table_style();
            // every section uses the same table id so the label/value split stays aligned down the page
            const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg;
            section.table_open = ImGui::BeginTable("##render_option_rows", 2, flags);
            if (!section.table_open)
            {
                ui::pop_table_style();
                return false;
            }
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.46f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 0.54f);
        }

        return true;
    }

    //= ROWS ===================================================================
    void draw_row_tooltip(const char* label, const char* tooltip, const char* cvar)
    {
        if (!ImGui::BeginTooltip())
        {
            return;
        }

        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::PushFont(Editor::font_bold, 0.0f);
        ImGui::TextUnformatted(label);
        ImGui::PopFont();
        if (tooltip)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, style::color_text_muted);
            ImGui::TextWrapped("%s", tooltip);
            ImGui::PopStyleColor();
        }
        if (cvar)
        {
            ImGui::Dummy(ImVec2(0.0f, ui::scaled(2.0f)));
            ImGui::PushStyleColor(ImGuiCol_Text, style::color_text_faint);
            ImGui::PushFont(Editor::font_mono, 0.0f);
            ImGui::Text("%s  default %g", cvar, cvar_default(cvar));
            ImGui::PopFont();
            ImGui::TextUnformatted(cvar_modified(cvar) ? "Right-click to reset" : "Right-click for options");
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }

    // label cell: a modified dot in the gutter, indented children hang off a hairline elbow
    bool row_begin(const char* label, const char* tooltip = nullptr, const char* cvar = nullptr, const int depth = 0)
    {
        const bool visible = !filter.IsActive() || section.title_matches || filter.PassFilter(label) || (cvar && filter.PassFilter(cvar));
        if (!visible || !ensure_rows())
        {
            return false;
        }

        visible_row_count++;
        row_cvar = cvar;

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID(label);

        const float gutter       = ui::scaled(12.0f);
        const float indent       = ui::scaled(14.0f);
        const float frame_height = ImGui::GetFrameHeight();
        const ImVec2 cell        = ImGui::GetCursorScreenPos();
        ImDrawList* draw_list    = ImGui::GetWindowDrawList();

        if (depth > 0)
        {
            const float x      = cell.x + gutter + indent * (static_cast<float>(depth) - 0.6f);
            const float y_mid  = IM_ROUND(cell.y + frame_height * 0.5f);
            const ImU32 line   = ui::color(style::color_border_strong);
            draw_list->AddLine(ImVec2(x, cell.y - ImGui::GetStyle().CellPadding.y), ImVec2(x, y_mid), line, 1.0f);
            draw_list->AddLine(ImVec2(x, y_mid), ImVec2(x + indent * 0.4f, y_mid), line, 1.0f);
        }

        if (cvar && cvar_modified(cvar))
        {
            ui::status_dot(draw_list, ImVec2(cell.x + gutter * 0.4f, cell.y + frame_height * 0.5f), ui::scaled(2.5f), style::color_accent_1);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + gutter + indent * static_cast<float>(depth));
        ImGui::PushStyleColor(ImGuiCol_Text, depth > 0 ? style::color_text_muted : style::color_text);
        ImGui::TextUnformatted(label);
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            draw_row_tooltip(label, tooltip, cvar);
        }
        if (cvar && ImGui::IsItemClicked(ImGuiMouseButton_Right))
        {
            ImGui::OpenPopup("##row_menu");
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::PushItemWidth(-FLT_MIN);
        return true;
    }

    void row_end()
    {
        if (row_cvar)
        {
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                ImGui::OpenPopup("##row_menu");
            }

            if (ImGui::BeginPopup("##row_menu"))
            {
                char reset_label[64];
                snprintf(reset_label, sizeof(reset_label), "Reset to default (%g)", cvar_default(row_cvar));
                if (ImGui::MenuItem(reset_label, nullptr, false, cvar_modified(row_cvar)))
                {
                    cvar_write(row_cvar, cvar_default(row_cvar));
                }
                if (ImGui::MenuItem("Copy console variable name"))
                {
                    ImGui::SetClipboardText(row_cvar);
                }
                ImGui::EndPopup();
            }
        }

        ImGui::PopItemWidth();
        ImGui::PopID();
        row_cvar = nullptr;
    }

    // a wrapped line under the value of the row above, for context that changes with the settings
    void row_note(const char* text, const ImVec4& color)
    {
        if (!section.table_open)
        {
            return;
        }

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", text);
        ImGui::PopStyleColor();
    }

    void push_combo_style()
    {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered));
        ImGui::PushStyleColor(ImGuiCol_Text, style::color_text);
    }

    //= WIDGETS ================================================================
    void cvar_toggle(const char* label, const char* cvar, const char* tooltip = nullptr, const int depth = 0)
    {
        if (!row_begin(label, tooltip, cvar, depth))
        {
            return;
        }

        bool value = cvar_get(cvar) != 0.0f;
        if (ImGuiSp::toggle_switch("##toggle", &value))
        {
            cvar_write(cvar, value ? 1.0f : 0.0f);
        }
        row_end();
    }

    // the slider covers the useful range, ctrl+click can type up to hard_max when it is larger
    void cvar_slider(const char* label, const char* cvar, const char* tooltip, const float min, const float max, const char* format,
                     const float display_scale = 1.0f, const float hard_max = 0.0f, const int depth = 0, const ImGuiSliderFlags extra_flags = 0)
    {
        if (!row_begin(label, tooltip, cvar, depth))
        {
            return;
        }

        const float limit = hard_max > max ? hard_max : max;
        ImGuiSliderFlags flags = extra_flags | (hard_max > max ? ImGuiSliderFlags_None : ImGuiSliderFlags_AlwaysClamp);
        float value = cvar_get(cvar) * display_scale;
        if (ImGui::SliderFloat("##slider", &value, min * display_scale, max * display_scale, format, flags))
        {
            cvar_write(cvar, clamp(value / display_scale, min, limit));
        }
        ui::decorate_field();
        row_end();
    }

    void cvar_combo(const char* label, const char* cvar, const vector<string>& options, const char* tooltip = nullptr)
    {
        if (!row_begin(label, tooltip, cvar))
        {
            return;
        }

        uint32_t index = static_cast<uint32_t>(cvar_get(cvar));
        if (ImGuiSp::combo_box("##combo", options, &index))
        {
            cvar_write(cvar, static_cast<float>(index));
        }
        row_end();
    }

    // a display mode picker whose first entry follows another size (the viewport or the output)
    void resolution_combo(const Vector2& current, void (*set_resolution)(uint32_t, uint32_t, bool), const char* follow_label, const Vector2& follow)
    {
        const string preview = format_resolution(current);
        push_combo_style();
        const bool open = ImGui::BeginCombo("##resolution", preview.c_str());
        ImGui::PopStyleColor(3);
        if (!open)
        {
            ui::decorate_field();
            return;
        }

        const uint32_t follow_w = static_cast<uint32_t>(follow.x);
        const uint32_t follow_h = static_cast<uint32_t>(follow.y);
        const string follow_text = string(follow_label) + "  (" + format_resolution(follow_w, follow_h) + ")";
        if (ImGui::Selectable(follow_text.c_str(), current == follow))
        {
            set_resolution(follow_w, follow_h, true);
        }
        ImGui::Separator();

        for (const DisplayMode& mode : display_modes)
        {
            const bool selected = mode.width == current.x && mode.height == current.y;
            if (ImGui::Selectable(format_resolution(mode.width, mode.height).c_str(), selected))
            {
                set_resolution(mode.width, mode.height, true);
            }
            if (selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    //= RESOLUTION CARD ========================================================
    // render -> upscaler -> output at a glance, the one place that answers "what am I actually rendering"
    void draw_resolution_card()
    {
        const Vector2& output   = Renderer::GetResolutionOutput();
        const Vector2& render   = Renderer::GetResolutionRender();
        const uint32_t active_w = Renderer::GetScaledDimension(static_cast<uint32_t>(render.x));
        const uint32_t active_h = Renderer::GetScaledDimension(static_cast<uint32_t>(render.y));
        const float percent     = output.x > 0.0f ? 100.0f * static_cast<float>(active_w) / output.x : 100.0f;
        const bool native       = active_w == static_cast<uint32_t>(output.x) && active_h == static_cast<uint32_t>(output.y);
        const bool stretched    = !native && !upscaler_is_temporal() && percent < 100.0f;

        const string render_text = format_resolution(active_w, active_h);
        const string output_text = format_resolution(output);
        char render_caption[64];
        snprintf(render_caption, sizeof(render_caption), "%s%.0f%% of output", cvar_dynamic_resolution.GetValueAs<bool>() ? "auto, " : "", percent);
        const RHI_Viewport& viewport = Renderer::GetViewport();
        const bool viewport_mismatch = fabsf(viewport.width - output.x) > 1.0f || fabsf(viewport.height - output.y) > 1.0f;
        const char* output_caption   = viewport_mismatch ? "stretched to viewport" : "final image";

        const char* chip_text = upscaler_short_name();
        ImVec4 chip_color     = style::color_accent_1;
        if (native)
        {
            chip_text  = upscaler_is_temporal() ? upscaler_short_name() : "1:1";
            chip_color = style::color_ok;
        }
        else if (stretched)
        {
            chip_color = style::color_warning;
        }
        else if (percent > 100.0f)
        {
            chip_text = "Downsample";
        }

        ImDrawList* draw_list  = ImGui::GetWindowDrawList();
        ImFont* font_big       = Editor::font_bold;
        const float big_size   = ImGui::GetFontSize() * 1.2f;
        const float small_size = ImGui::GetFontSize();
        const float micro      = ui::micro_label_size();
        const float padding    = ui::scaled(12.0f);
        const float gap        = ui::scaled(4.0f);
        const float width      = ImGui::GetContentRegionAvail().x;
        const float height     = padding * 2.0f + micro + big_size + small_size + gap * 2.0f;
        const ImVec2 min       = ImGui::GetCursorScreenPos();
        const ImVec2 max       = ImVec2(min.x + width, min.y + height);
        ui::draw_card(min, max, false, false, 6.0f, ImGui::GetID("##resolution_card"));

        const float y_micro   = min.y + padding;
        const float y_big     = y_micro + micro + gap;
        const float y_caption = y_big + big_size + gap;
        const float left      = min.x + padding;
        const float right     = max.x - padding;

        // left block, render
        ui::draw_text_tracked(draw_list, ImVec2(left, y_micro), ui::color(style::color_text_muted), "RENDER", micro * 0.12f, nullptr, micro);
        draw_list->AddText(font_big, big_size, ImVec2(left, y_big), ui::color(style::color_text), render_text.c_str());
        draw_list->AddText(ImVec2(left, y_caption), ui::color(style::color_text_faint), render_caption);
        const float left_end = left + ImMax(font_big->CalcTextSizeA(big_size, FLT_MAX, 0.0f, render_text.c_str()).x, ImGui::CalcTextSize(render_caption).x);

        // right block, output, right aligned
        const float output_w  = font_big->CalcTextSizeA(big_size, FLT_MAX, 0.0f, output_text.c_str()).x;
        const float caption_w = ImGui::CalcTextSize(output_caption).x;
        const float label_w   = ui::calc_text_tracked("OUTPUT", micro * 0.12f, nullptr, micro);
        ui::draw_text_tracked(draw_list, ImVec2(right - label_w, y_micro), ui::color(style::color_text_muted), "OUTPUT", micro * 0.12f, nullptr, micro);
        draw_list->AddText(font_big, big_size, ImVec2(right - output_w, y_big), ui::color(style::color_text), output_text.c_str());
        draw_list->AddText(ImVec2(right - caption_w, y_caption), ui::color(viewport_mismatch ? style::color_warning : style::color_text_faint), output_caption);
        const float right_start = right - ImMax(output_w, caption_w);

        // middle, an arrow carrying the upscaler, dropped when the panel is too narrow for it
        const ImVec2 chip_size = ImGui::CalcTextSize(chip_text);
        const ImVec2 chip_pad  = ui::scaled(ImVec2(7.0f, 3.0f));
        const float span_left  = left_end + ui::scaled(10.0f);
        const float span_right = right_start - ui::scaled(10.0f);
        if (span_right - span_left > chip_size.x + chip_pad.x * 2.0f + ui::scaled(16.0f))
        {
            const float y       = IM_ROUND(y_big + big_size * 0.5f);
            const float center  = (span_left + span_right) * 0.5f;
            const ImU32 line    = ui::color(ui::alpha(chip_color, 0.45f));
            const float head    = ui::scaled(4.0f);
            draw_list->AddLine(ImVec2(span_left, y), ImVec2(span_right, y), line, ImMax(1.0f, ui::scaled(1.0f)));
            draw_list->AddTriangleFilled(ImVec2(span_right, y), ImVec2(span_right - head * 1.5f, y - head), ImVec2(span_right - head * 1.5f, y + head), line);

            const ImVec2 chip_min = ImVec2(IM_ROUND(center - chip_size.x * 0.5f - chip_pad.x), IM_ROUND(y - chip_size.y * 0.5f - chip_pad.y));
            const ImVec2 chip_max = ImVec2(chip_min.x + chip_size.x + chip_pad.x * 2.0f, chip_min.y + chip_size.y + chip_pad.y * 2.0f);
            draw_list->AddRectFilled(chip_min, chip_max, ui::color(style::color_panel), ui::scaled(4.0f));
            draw_list->AddRectFilled(chip_min, chip_max, ui::color(ui::alpha(chip_color, 0.14f)), ui::scaled(4.0f));
            draw_list->AddRect(chip_min, chip_max, ui::color(ui::alpha(chip_color, 0.40f)), ui::scaled(4.0f), 1.0f);
            draw_list->AddText(ImVec2(chip_min.x + chip_pad.x, chip_min.y + chip_pad.y), ui::color(chip_color), chip_text);
        }

        ImGui::Dummy(ImVec2(width, height));
        ImGui::Dummy(ImVec2(0.0f, ui::scaled(2.0f)));
    }

    //= SECTION BODIES =========================================================
    void section_resolution()
    {
        const Vector2& output   = Renderer::GetResolutionOutput();
        const Vector2& render   = Renderer::GetResolutionRender();
        const uint32_t active_w = Renderer::GetScaledDimension(static_cast<uint32_t>(render.x));
        const float percent     = output.x > 0.0f ? 100.0f * static_cast<float>(active_w) / output.x : 100.0f;
        char summary[64];
        snprintf(summary, sizeof(summary), "%s%s%.0f%%", upscaler_short_name(), separator, percent);

        if (section_begin("Resolution & Upscaling", summary))
        {
            if (section_unfiltered())
            {
                draw_resolution_card();
            }

            if (row_begin("Output resolution", "Size of the final image, after upscaling. In the editor this should match the viewport, otherwise the image is stretched to fit it."))
            {
                const RHI_Viewport& viewport = Renderer::GetViewport();
                resolution_combo(output, Renderer::SetResolutionOutput, "Match viewport", Vector2(viewport.width, viewport.height));
                row_end();
            }

            if (row_begin("Render resolution", "Size of the frame the renderer draws before upscaling. Changing it reallocates render targets, use render scale for quick changes."))
            {
                resolution_combo(render, Renderer::SetResolutionRender, "Match output", output);
                row_end();
            }

            const bool dynamic = cvar_dynamic_resolution.GetValueAs<bool>();
            if (row_begin("Render scale", "Share of the render resolution that is shaded each frame. Cheap to change, nothing is reallocated. Auto lowers it whenever the GPU misses 60 fps.", "r.resolution_scale"))
            {
                const float toggle_width = ImGui::GetFrameHeight() * 0.78f * 1.85f + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize("Auto").x;
                ImGui::SetNextItemWidth(ImMax(ImGui::GetContentRegionAvail().x - toggle_width - ImGui::GetStyle().ItemSpacing.x, ui::scaled(40.0f)));
                ImGui::BeginDisabled(dynamic);
                float value = (dynamic ? Renderer::GetResolutionScale() : cvar_resolution_scale.GetValue()) * 100.0f;
                if (ImGui::SliderFloat("##scale", &value, 25.0f, 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))
                {
                    cvar_write("r.resolution_scale", value / 100.0f);
                }
                ui::decorate_field();
                ImGui::EndDisabled();
                ImGui::SameLine();
                bool automatic = dynamic;
                if (ImGuiSp::toggle_switch("Auto", &automatic))
                {
                    cvar_write("r.dynamic_resolution", automatic ? 1.0f : 0.0f);
                }
                ImGuiSp::tooltip("Adjust the render scale automatically to hold 60 fps");
                row_end();
            }

            const float scale_requested = cvar_resolution_scale.GetValue();
            const float scale_effective = Renderer::GetResolutionScale();
            if (!filter.IsActive() && !dynamic && scale_effective > scale_requested + 0.005f)
            {
                char note[160];
                snprintf(note, sizeof(note), "Held at %.0f%%: %s needs at least %.0f%% of the output resolution as input.", scale_effective * 100.0f, upscaler_short_name(), percent);
                row_note(note, style::color_text_faint);
            }

            vector<string> upscalers = { "None (bilinear)", "FXAA (bilinear)", "TAAU", "Intel XeSS 3" };
            if (RHI_Device::IsSupportedDlss())
            {
                upscalers.emplace_back("NVIDIA DLSS 4.5");
            }
            cvar_combo("Upscaler", "r.antialiasing_upsampling", upscalers, "Anti-aliasing and upscaling from render to output resolution. TAAU, XeSS and DLSS reconstruct detail over time, the others stretch the image.");

            if (!filter.IsActive() && !upscaler_is_temporal() && percent < 99.5f)
            {
                row_note("Stretched without reconstruction, expect a soft image. TAAU, XeSS or DLSS rebuild the missing detail.", style::color_warning);
            }

            if (upscaler_is_temporal())
            {
                cvar_slider("Reactivity", "r.dlss_reactivity", "How quickly the temporal upscaler drops history where things move or appear. Higher means less ghosting but more shimmer.", 0.0f, 1.0f, "%.2f", 1.0f, 0.0f, 1);
            }

            cvar_slider("Sharpening", "r.sharpness", "AMD FidelityFX Contrast Adaptive Sharpening, applied after upscaling. 0% is off.", 0.0f, 1.0f, "%.0f%%", 100.0f);
            cvar_toggle("Variable rate shading", "r.variable_rate_shading", "Shades low-detail parts of the image at a coarser rate to save GPU time.");
        }
        section_end();
    }

    void section_lighting()
    {
        const bool rt_supported = RHI_Device::IsSupportedRayTracing();
        string summary = "Raster";
        if (rt_supported && (cvar_get("r.ray_traced_reflections") != 0.0f || cvar_get("r.ray_traced_shadows") != 0.0f))
        {
            summary = "Ray traced";
        }
        if (rt_supported && cvar_restir_pt.GetValueAs<bool>())
        {
            summary += " + ReSTIR";
        }

        if (section_begin("Lighting", summary))
        {
            if (!rt_supported && section_unfiltered() && ensure_rows())
            {
                row_note("This GPU does not support ray tracing, ray traced effects are unavailable.", style::color_warning);
            }

            ImGui::BeginDisabled(!rt_supported);
            cvar_toggle("Ray traced reflections", "r.ray_traced_reflections", "Accurate reflections of off-screen geometry, blended with screen space and image based lighting by roughness.");
            cvar_toggle("Ray traced shadows", "r.ray_traced_shadows", "Contact-hardening shadows for all lights, traced instead of shadow maps.");
            cvar_toggle("Path traced GI (ReSTIR)", "r.restir_pt", "Work in progress. Global illumination via ReSTIR path tracing.");
            if (cvar_restir_pt.GetValueAs<bool>())
            {
                cvar_slider("Resolution", "r.restir_pt_scale", "Share of the render resolution that is path traced. Lower is faster and noisier.", 0.1f, 1.0f, "%.0f%%", 100.0f, 0.0f, 1);
            }
            ImGui::EndDisabled();

            cvar_toggle("Ambient occlusion", "r.ssao", "Screen space ambient occlusion, darkens creases and contact areas.");
        }
        section_end();
    }

    void section_post_processing()
    {
        static const vector<string> tonemappers = { "ACES", "AgX", "Reinhard", "ACES Nautilus", "Gran Turismo 7", "Off" };
        const uint32_t tonemapper = cvar_tonemapping.GetValueAs<uint32_t>();
        const string summary      = tonemapper < tonemappers.size() ? tonemappers[tonemapper] : "";

        if (section_begin("Post-processing", summary))
        {
            cvar_combo("Tone mapping", "r.tonemapping", tonemappers, "Maps scene brightness to the display range and sets the overall look.");
            cvar_slider("Bloom", "r.bloom", "Light scattered into a glow around bright areas. 1 is subtle, 0 disables bloom.", 0.0f, 4.0f, "%.2f", 1.0f, 10.0f);
            if (cvar_get("r.bloom") > 0.0f)
            {
                cvar_slider("Spread", "r.bloom_scatter", "Halo width, higher values reach further.", 0.05f, 0.95f, "%.2f", 1.0f, 0.0f, 1);
            }
            cvar_toggle("Motion blur", "r.motion_blur", "Strength follows the camera shutter speed.");
            cvar_toggle("Depth of field", "r.depth_of_field", "Cinematic auto focus, strength follows the camera aperture.");
            cvar_toggle("Chromatic aberration", "r.chromatic_aberration", "Lens colour fringing towards the edges of the frame.");
            cvar_toggle("Film grain", "r.film_grain", "Simulated film camera noise.");
            cvar_toggle("Dithering", "r.dithering", "Breaks up colour banding in smooth gradients.");
            cvar_toggle("VHS playback", "r.vhs", "NTSC tape look: colour bleed, line jitter, dropouts and head switching.");
        }
        section_end();
    }

    void section_display()
    {
        const bool vsync        = cvar_vsync.GetValueAs<bool>();
        const float monitor_hz  = Display::GetRefreshRate();
        const FpsLimitType type = Timer::GetFpsLimitType();

        string limit_text;
        char buffer[64];
        if (vsync)
        {
            snprintf(buffer, sizeof(buffer), "VSync (%.0f Hz)", monitor_hz);
            limit_text = buffer;
        }
        else if (type == FpsLimitType::FixedToMonitor)
        {
            snprintf(buffer, sizeof(buffer), "Match monitor (%.0f Hz)", monitor_hz);
            limit_text = buffer;
        }
        else if (type == FpsLimitType::Unlocked)
        {
            limit_text = "Unlocked";
        }
        else
        {
            snprintf(buffer, sizeof(buffer), "%.0f fps", Timer::GetFpsLimit());
            limit_text = buffer;
        }

        char rate[32];
        if (vsync || type == FpsLimitType::FixedToMonitor)
        {
            snprintf(rate, sizeof(rate), "%s%.0f Hz", vsync ? "VSync " : "", monitor_hz);
        }
        else
        {
            snprintf(rate, sizeof(rate), type == FpsLimitType::Unlocked ? "unlocked" : "%.0f fps", Timer::GetFpsLimit());
        }
        const string summary = string(cvar_hdr.GetValueAs<bool>() ? "HDR" : "SDR") + separator + rate;
        if (section_begin("Display", summary))
        {
            cvar_toggle("HDR", "r.hdr", "High dynamic range output on HDR displays. When off, output is SDR with the sRGB transfer.");
            cvar_toggle("VSync", "r.vsync", "Synchronise presentation with the monitor refresh, removes tearing and caps the frame rate to it.");

            if (row_begin("Frame rate limit", "Caps how often frames are produced. Follows the monitor while VSync is on."))
            {
                ImGui::BeginDisabled(vsync);
                push_combo_style();
                const bool open = ImGui::BeginCombo("##fps_limit", limit_text.c_str());
                ImGui::PopStyleColor(3);
                if (open)
                {
                    snprintf(buffer, sizeof(buffer), "Match monitor (%.0f Hz)", monitor_hz);
                    if (ImGui::Selectable(buffer, type == FpsLimitType::FixedToMonitor))
                    {
                        Timer::SetFpsLimit(-1.0f);
                    }
                    for (const float fps : { 30.0f, 60.0f, 120.0f, 144.0f, 240.0f })
                    {
                        snprintf(buffer, sizeof(buffer), "%.0f fps", fps);
                        if (ImGui::Selectable(buffer, type == FpsLimitType::Fixed && Timer::GetFpsLimit() == fps))
                        {
                            Timer::SetFpsLimit(fps);
                        }
                    }
                    if (ImGui::Selectable("Unlocked", type == FpsLimitType::Unlocked))
                    {
                        Timer::SetFpsLimit(fps_unlocked);
                    }
                    ImGui::Separator();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("Custom");
                    ImGui::SameLine();
                    float custom = Timer::GetFpsLimit();
                    ImGui::SetNextItemWidth(ui::scaled(90.0f));
                    if (ImGui::InputFloat("##custom_fps", &custom, 0.0f, 0.0f, "%.0f fps", ImGuiInputTextFlags_EnterReturnsTrue))
                    {
                        Timer::SetFpsLimit(custom);
                    }
                    ImGui::EndCombo();
                }
                else
                {
                    ui::decorate_field();
                }
                ImGui::EndDisabled();
                row_end();
            }

            cvar_combo("Performance overlay", "r.performance_metrics", { "Off", "Full", "Compact" }, "Frame time, GPU and memory statistics over the viewport.");
        }
        section_end();
    }

    void section_world()
    {
        Vector3 wind    = World::GetWind();
        float strength  = wind.Length();
        float direction = atan2f(wind.x, wind.z) * (180.0f / math::pi);
        if (direction < 0.0f)
        {
            direction += 360.0f;
        }

        char summary[64];
        snprintf(summary, sizeof(summary), "wind %.1f m/s", strength);
        if (section_begin("Atmosphere & Wind", summary))
        {
            cvar_slider("Mist", "r.atmosphere.mist_density", "Extra haze and mist. 0 is clear air, the baseline atmosphere and light scattering always remain.", 0.0f, 5.0f, "%.2f", 1.0f, 100.0f);
            cvar_slider("Height", "r.atmosphere.mist_height", "Altitude scale of the extra haze.", 1.0f, 5000.0f, "%.0f m", 1.0f, 0.0f, 1, ImGuiSliderFlags_Logarithmic);
            cvar_slider("Ground mist", "r.atmosphere.ground_mist", "Mist hugging the ground relative to the mist amount, strongest in sheltered valleys.", 0.0f, 3.0f, "%.2f", 1.0f, 10.0f, 1);
            cvar_slider("Variation", "r.atmosphere.mist_variation", "Wind driven patchiness of the mist.", 0.0f, 1.0f, "%.2f", 1.0f, 0.0f, 1);

            bool changed = false;
            if (row_begin("Wind strength", "Drives vegetation, cloth and mist movement."))
            {
                changed |= ImGui::SliderFloat("##wind_strength", &strength, 0.1f, 10.0f, "%.1f m/s", ImGuiSliderFlags_AlwaysClamp);
                ui::decorate_field();
                row_end();
            }
            if (row_begin("Wind direction", "Compass heading the wind blows towards, 0 is +Z."))
            {
                changed |= ImGui::SliderFloat("##wind_direction", &direction, 0.0f, 360.0f, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
                ui::decorate_field();
                row_end();
            }
            if (changed)
            {
                const float radians = direction * (math::pi / 180.0f);
                wind.x = sinf(radians) * strength;
                wind.z = cosf(radians) * strength;
                World::SetWind(wind);
            }
        }
        section_end();
    }

    void section_editor()
    {
        if (section_begin("Editor", "", false))
        {
            cvar_toggle("Grid", "r.grid");
            cvar_toggle("Selection outline", "r.selection_outline");
            cvar_toggle("Entity icons", "r.entity_icons", "Icons for lights, cameras, audio sources and other entities without a mesh.");
            cvar_slider("Move snap", "r.transform_snap_translate", "Step of the move gizmo while snapping.", 0.001f, 10.0f, "%.3f m", 1.0f, 100.0f, 0, ImGuiSliderFlags_Logarithmic);
            cvar_slider("Rotate snap", "r.transform_snap_rotate", "Step of the rotate gizmo while snapping.", 0.1f, 90.0f, "%.1f deg", 1.0f, 180.0f);
            cvar_slider("Scale snap", "r.transform_snap_scale", "Step of the scale gizmo while snapping.", 0.001f, 1.0f, "%.3f", 1.0f, 10.0f, 0, ImGuiSliderFlags_Logarithmic);
        }
        section_end();
    }

    void section_debug()
    {
        uint32_t overlays = 0;
        for (const char* cvar : { "r.physics", "r.ragdoll", "r.aabb", "r.volumes", "r.picking_ray", "r.wireframe" })
        {
            overlays += cvar_get(cvar) != 0.0f ? 1 : 0;
        }
        const string summary = overlays ? to_string(overlays) + " on" : "";

        if (section_begin("Debug", summary, false))
        {
            cvar_toggle("Physics", "r.physics", "Collision shapes and contacts.");
            cvar_toggle("Ragdoll", "r.ragdoll", "Capsules and joints, works while playing.");
            cvar_toggle("Bounding boxes", "r.aabb", "Render and light bounding boxes.");
            cvar_toggle("Volumes", "r.volumes", "Volume component bounds and audio polygons (sound and reverb regions).");
            cvar_toggle("Picking ray", "r.picking_ray");
            cvar_toggle("Wireframe", "r.wireframe");
            cvar_toggle("Occlusion culling", "r.hiz_occlusion", "Hi-Z occlusion culling. Turn off only to debug missing geometry.");
            cvar_toggle("Mesh shaders", "r.mesh_shaders", "Primary opaque path when supported, otherwise vertex-shader pull.");
        }
        section_end();
    }
}

RenderOptions::RenderOptions(Editor* editor) : Widget(editor)
{
    m_title         = "Renderer Options";
    m_visible       = false;
    m_toolbar_order = 4;
    m_toolbar_icon  = static_cast<int>(spartan::IconType::Gear);
    m_alpha         = 1.0f;
    m_size_initial  = Vector2(Display::GetWidth() * 0.25f, Display::GetHeight() * 0.6f);
}

void RenderOptions::OnVisible()
{
    display_modes.clear();
    for (const DisplayMode& display_mode : Display::GetDisplayModes())
    {
        // these select texture dimensions, not monitor refresh modes, keep each size once
        // even when it is only advertised at another refresh rate
        const bool duplicate = any_of(display_modes.begin(), display_modes.end(), [&](const DisplayMode& mode)
        {
            return mode.width == display_mode.width && mode.height == display_mode.height;
        });
        if (!duplicate)
        {
            display_modes.emplace_back(display_mode);
        }
    }
}

void RenderOptions::OnTickVisible()
{
    // search stays pinned above the scrolling page and covers every section
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, ui::scaled(5.0f));
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
    if (ImGui::InputTextWithHint("##render_options_search", "Search settings or console variables", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf), ImGuiInputTextFlags_EscapeClearsAll))
    {
        filter.Build();
    }
    ui::decorate_field();
    ImGui::PopStyleVar();
    ImGui::Dummy(ImVec2(0.0f, ui::scaled(4.0f)));

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ui::scaled(ImVec2(6.0f, 4.0f)));
    if (ImGui::BeginChild("##render_options_page", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None, ImGuiWindowFlags_None))
    {
        visible_row_count = 0;

        section_resolution();
        section_lighting();
        section_post_processing();
        section_display();
        section_world();
        section_editor();
        section_debug();

        if (filter.IsActive() && visible_row_count == 0)
        {
            ImGui::Dummy(ImVec2(0.0f, ui::scaled(12.0f)));
            ImGui::PushStyleColor(ImGuiCol_Text, style::color_text_muted);
            ImGui::TextWrapped("No settings match \"%s\".", filter.InputBuf);
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
