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
#include "WorldSelector.h"
#include "../WorldPreviews.h"
#include "../GeneralWindows.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
#include "../Widgets/Viewport.h"
#include "Core/ProgressTracker.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor = nullptr;

    vector<spartan::WorldMetadata> world_files;
    vector<int> visible_indices;
    int selected_index         = 0;
    int selected_visible_index = 0;
    int visible_columns        = 1;

    bool visible_download_prompt  = false;
    bool visible_update_prompt    = false;
    bool visible_world_list       = false;
    bool downloaded_and_extracted = false;
    bool update_check_started     = false;

    const float card_rounding      = 8.0f;
    const float panel_rounding     = 0.0f;
    const float card_padding       = 12.0f;
    const float card_spacing       = 12.0f;
    const float card_width_base    = 230.0f;
    const float card_height_base   = 252.0f;
    const float section_spacing    = 14.0f;
    const float details_width_base = 370.0f;

    struct LauncherColors
    {
        ImU32 card_bg            = 0;
        ImU32 card_bg_hover      = 0;
        ImU32 card_bg_selected   = 0;
        ImU32 card_border        = 0;
        ImU32 card_border_accent = 0;
        ImU32 panel_bg           = 0;
        ImU32 panel_border       = 0;
        ImU32 surface_bg         = 0;
        ImU32 chip_bg            = 0;
        ImU32 shadow             = 0;
        ImU32 preview_bg         = 0;
        ImU32 text_primary       = 0;
        ImU32 text_muted         = 0;
        ImU32 accent             = 0;
        ImU32 warning            = 0;
        ImVec4 button            = {};
        ImVec4 button_hover      = {};
        ImVec4 button_active     = {};
        ImVec4 primary           = {};
        ImVec4 primary_hover     = {};
        ImVec4 primary_active    = {};
    };

    LauncherColors colors;
    ImGuiTextFilter search_filter;

    float last_click_time = -1.0f;
    int last_click_index  = -1;

    const char* assets_url          = "https://www.dropbox.com/scl/fi/kydbsf9pzzlfuskfdmf6v/project.7z?rlkey=8uyp0ps2wmnf93fq3priooxo2&dl=1";
    const char* assets_destination  = "project/project.7z";
    const char* assets_extract_dir  = "project/";
    const char* assets_expected_sha = "cc9a40b3339068f19fcc44dfbbefcc131031245fb2b2e1404b4f0dc15698aad8";

    float dpi()
    {
        return spartan::Window::GetDpiScale();
    }

    float scaled(float value)
    {
        return value * dpi();
    }

    ImVec2 scaled_vec(float x, float y)
    {
        return ImVec2(scaled(x), scaled(y));
    }

    ImVec4 with_alpha(const ImVec4& color, float alpha)
    {
        return ImVec4(color.x, color.y, color.z, alpha);
    }

    ImU32 to_u32(const ImVec4& color)
    {
        return ImGui::ColorConvertFloat4ToU32(color);
    }

    void update_colors()
    {
        const ImVec4 bg_1    = ImGui::Style::bg_color_1;
        const ImVec4 bg_2    = ImGui::Style::bg_color_2;
        const ImVec4 accent  = ImGui::Style::color_accent_1;
        const ImVec4 warning = ImGui::Style::color_warning;
        const ImVec4 text    = ImGui::GetStyle().Colors[ImGuiCol_Text];
        const ImVec4 muted   = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];

        colors.card_bg            = to_u32(ImGui::Style::lerp(bg_1, bg_2, 0.28f));
        colors.card_bg_hover      = to_u32(ImGui::Style::lerp(bg_1, bg_2, 0.42f));
        colors.card_bg_selected   = to_u32(ImGui::Style::lerp(bg_1, accent, 0.20f));
        colors.card_border        = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.75f), 0.90f));
        colors.card_border_accent = to_u32(accent);
        colors.panel_bg           = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.18f), 0.96f));
        colors.panel_border       = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.70f), 0.78f));
        colors.surface_bg         = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.10f), 0.96f));
        colors.chip_bg            = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.55f), 0.78f));
        colors.shadow             = IM_COL32(0, 0, 0, 75);
        colors.preview_bg         = to_u32(with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.10f), 0.92f));
        colors.text_primary       = to_u32(text);
        colors.text_muted         = to_u32(muted);
        colors.accent             = to_u32(accent);
        colors.warning            = to_u32(warning);
        colors.button             = with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.50f), 0.80f);
        colors.button_hover       = with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.70f), 0.95f);
        colors.button_active      = with_alpha(ImGui::Style::lerp(bg_1, bg_2, 0.85f), 1.00f);
        colors.primary            = with_alpha(accent, 0.32f);
        colors.primary_hover      = with_alpha(accent, 0.46f);
        colors.primary_active     = with_alpha(accent, 0.62f);
    }

    void push_button_style(bool primary)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, scaled_vec(12.0f, 6.0f));

        if (primary)
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        colors.primary);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.primary_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  colors.primary_active);
        }
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Button,        colors.button);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors.button_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  colors.button_active);
        }
    }

    void pop_button_style()
    {
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(2);
    }

    bool launcher_button(const char* label, const ImVec2& size, bool primary = false)
    {
        push_button_style(primary);
        bool pressed = ImGui::Button(label, size);
        pop_button_style();
        return pressed;
    }

    void draw_panel_background(const ImVec2& min_pos, const ImVec2& max_pos)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(min_pos, max_pos, colors.panel_bg, scaled(panel_rounding));
        draw_list->AddRect(min_pos, max_pos, colors.panel_border, scaled(panel_rounding));
    }

    void begin_panel(const char* id, const ImVec2& size)
    {
        ImGui::BeginChild(id, size, false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        draw_panel_background(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
        ImGui::SetCursorPos(scaled_vec(14.0f, 14.0f));
    }

    void end_panel()
    {
        ImGui::EndChild();
    }

    void draw_chip(const char* text, ImU32 bg_col, ImU32 text_col)
    {
        ImVec2 text_size = ImGui::CalcTextSize(text);
        ImVec2 pos       = ImGui::GetCursorScreenPos();
        ImVec2 size      = ImVec2(text_size.x + scaled(16.0f), text_size.y + scaled(6.0f));
        ImDrawList* draw = ImGui::GetWindowDrawList();

        draw->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg_col, 0.0f);
        draw->AddText(ImVec2(pos.x + scaled(8.0f), pos.y + scaled(3.0f)), text_col, text);
        ImGui::Dummy(size);
    }

    string lowercase_copy(string value)
    {
        transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        return value;
    }

    bool is_work_in_progress(const spartan::WorldMetadata& world)
    {
        return lowercase_copy(world.name).find("forest") != string::npos;
    }

    string source_label(const string& path)
    {
        string lowercase_path = lowercase_copy(path);
        if (lowercase_path.find("project/") != string::npos)
        {
            return "project";
        }

        return "bundled";
    }

    bool world_matches_filter(const spartan::WorldMetadata& world)
    {
        if (!search_filter.IsActive())
        {
            return true;
        }

        return search_filter.PassFilter(world.name.c_str()) ||
               search_filter.PassFilter(world.description.c_str()) ||
               search_filter.PassFilter(world.file_path.c_str());
    }

    int visible_position_from_world_index(int world_index)
    {
        for (int i = 0; i < static_cast<int>(visible_indices.size()); i++)
        {
            if (visible_indices[i] == world_index)
            {
                return i;
            }
        }

        return -1;
    }

    void rebuild_visible_indices()
    {
        visible_indices.clear();

        for (int i = 0; i < static_cast<int>(world_files.size()); i++)
        {
            if (world_matches_filter(world_files[i]))
            {
                visible_indices.push_back(i);
            }
        }

        if (visible_indices.empty())
        {
            selected_index         = -1;
            selected_visible_index = -1;
            return;
        }

        selected_visible_index = visible_position_from_world_index(selected_index);
        if (selected_visible_index == -1)
        {
            selected_visible_index = 0;
            selected_index         = visible_indices[0];
        }
    }

    void select_visible_index(int visible_index)
    {
        if (visible_indices.empty())
        {
            selected_index         = -1;
            selected_visible_index = -1;
            return;
        }

        visible_index           = clamp(visible_index, 0, static_cast<int>(visible_indices.size()) - 1);
        selected_visible_index  = visible_index;
        selected_index          = visible_indices[visible_index];
    }

    const spartan::WorldMetadata* selected_world()
    {
        if (selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
        {
            return &world_files[selected_index];
        }

        return nullptr;
    }

    void scan_directory_recursive(const string& directory)
    {
        if (!spartan::FileSystem::Exists(directory) || !spartan::FileSystem::IsDirectory(directory))
        {
            return;
        }

        vector<string> files = spartan::FileSystem::GetFilesInDirectory(directory);
        for (const string& file : files)
        {
            if (spartan::FileSystem::IsEngineWorldFile(file))
            {
                string normalized_path = file;
                replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

                spartan::WorldMetadata metadata;
                if (spartan::World::ReadMetadata(normalized_path, metadata))
                {
                    world_files.push_back(metadata);
                }
            }
        }

        vector<string> subdirectories = spartan::FileSystem::GetDirectoriesInDirectory(directory);
        for (const string& subdir : subdirectories)
        {
            scan_directory_recursive(subdir);
        }
    }

    void scan_for_world_files()
    {
        world_files.clear();

        string project_dir = spartan::ResourceCache::GetProjectDirectory();
        scan_directory_recursive(project_dir);

        vector<string> worlds_dirs = { "worlds", "../worlds" };
        for (const string& worlds_dir : worlds_dirs)
        {
            if (spartan::FileSystem::Exists(worlds_dir))
            {
                scan_directory_recursive(worlds_dir);
                break;
            }
        }

        if (selected_index >= static_cast<int>(world_files.size()))
        {
            selected_index = world_files.empty() ? -1 : 0;
        }
    }

    void check_assets_outdated_async()
    {
        spartan::ThreadPool::AddTask([]()
        {
            if (!spartan::FileSystem::Exists(assets_destination))
            {
                return;
            }

            string local_hash = spartan::FileSystem::ComputeFileSha256(assets_destination);
            if (!local_hash.empty() && local_hash != assets_expected_sha)
            {
                visible_update_prompt = true;
            }
        });
    }

    void download_and_extract()
    {
        visible_download_prompt = false;

        spartan::ThreadPool::AddTask([]()
        {
            spartan::Progress& progress = spartan::ProgressTracker::GetProgress(spartan::ProgressType::Download);
            progress.Start(0, "Downloading projects...");
            spartan::ProgressTracker::SetGlobalLoadingState(true);

            bool success = spartan::FileSystem::DownloadFile(
                assets_url,
                assets_destination,
                [&progress](float download_progress)
                {
                    progress.SetFraction(download_progress * 0.9f);
                }
            );

            if (success)
            {
                progress.SetText("Extracting projects...");
                progress.SetFraction(0.9f);
                success = spartan::FileSystem::ExtractArchive(assets_destination, assets_extract_dir);
                progress.SetFraction(1.0f);
            }

            spartan::ProgressTracker::SetGlobalLoadingState(false);
            if (success)
            {
                downloaded_and_extracted = true;
                scan_for_world_files();
                visible_world_list = true;
            }
        });
    }

    void prompt_text_centered(const char* text)
    {
        float text_w = ImGui::CalcTextSize(text).x;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - text_w) * 0.5f);
        ImGui::TextDisabled("%s", text);
    }

    void text_centered_in_width(const char* text, float width, ImU32 color)
    {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float text_w = ImGui::CalcTextSize(text).x;
        float text_x = pos.x + (width - text_w) * 0.5f;
        ImGui::GetWindowDrawList()->PushClipRect(pos, ImVec2(pos.x + width, pos.y + ImGui::GetTextLineHeight()), true);
        ImGui::GetWindowDrawList()->AddText(ImVec2(text_x, pos.y), color, text);
        ImGui::GetWindowDrawList()->PopClipRect();
        ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
    }

    void text_wrapped_centered(const char* text, float width, ImU32 color)
    {
        string value = text ? text : "";
        const char* cursor = value.c_str();
        while (*cursor)
        {
            const char* line_start = cursor;
            const char* line_end   = cursor;
            const char* best_end   = cursor;
            float best_width       = 0.0f;

            while (*line_end)
            {
                const char* next = line_end;
                while (*next && *next != ' ')
                {
                    next++;
                }

                float candidate_width = ImGui::CalcTextSize(line_start, next).x;
                if (candidate_width > width && best_end != line_start)
                {
                    break;
                }

                best_end   = next;
                best_width = candidate_width;
                line_end   = next;

                while (*line_end == ' ')
                {
                    line_end++;
                }

                if (!*next)
                {
                    break;
                }
            }

            if (best_end == line_start)
            {
                while (*best_end && *best_end != ' ')
                {
                    best_end++;
                }
                best_width = ImGui::CalcTextSize(line_start, best_end).x;
                line_end   = best_end;
            }

            ImVec2 pos = ImGui::GetCursorScreenPos();
            float text_x = pos.x + (width - best_width) * 0.5f;
            string line(line_start, best_end);
            ImGui::GetWindowDrawList()->PushClipRect(pos, ImVec2(pos.x + width, pos.y + ImGui::GetTextLineHeight()), true);
            ImGui::GetWindowDrawList()->AddText(ImVec2(text_x, pos.y), color, line.c_str());
            ImGui::GetWindowDrawList()->PopClipRect();
            ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));

            cursor = line_end;
            while (*cursor == ' ')
            {
                cursor++;
            }
        }
    }

    void render_prompt_body(const char* body_0, const char* body_1, const char* primary_label, const char* secondary_label, void (*primary_action)(), void (*secondary_action)())
    {
        ImGui::SetCursorPosY(scaled(38.0f));
        prompt_text_centered(body_0);
        prompt_text_centered(body_1);

        ImGui::Dummy(ImVec2(0.0f, scaled(18.0f)));

        float button_w = scaled(148.0f);
        float total_w  = button_w * 2.0f + scaled(10.0f);
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - total_w) * 0.5f);

        if (launcher_button(primary_label, ImVec2(button_w, 0.0f), true))
        {
            primary_action();
        }

        ImGui::SameLine(0.0f, scaled(10.0f));
        if (launcher_button(secondary_label, ImVec2(button_w, 0.0f)))
        {
            secondary_action();
        }
    }

    void skip_download_prompt()
    {
        visible_download_prompt = false;
    }

    void update_assets()
    {
        visible_update_prompt = false;
        if (spartan::FileSystem::Exists(assets_destination))
        {
            spartan::FileSystem::Delete(assets_destination);
        }
        download_and_extract();
    }

    void skip_update_prompt()
    {
        visible_update_prompt = false;
        visible_world_list    = true;
    }

    void window_download_prompt()
    {
        if (!visible_download_prompt)
        {
            return;
        }

        update_colors();
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(scaled_vec(500.0f, 150.0f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(panel_rounding));
        if (ImGui::Begin("Download starter worlds", &visible_download_prompt,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
        {
            draw_panel_background(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
            render_prompt_body("No default worlds are present.", "Download the curated project package to populate the launcher.", "Download", "Not now", download_and_extract, skip_download_prompt);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void window_update_prompt()
    {
        if (!visible_update_prompt)
        {
            return;
        }

        visible_world_list = false;

        update_colors();
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(scaled_vec(500.0f, 150.0f), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(panel_rounding));
        if (ImGui::Begin("Update available", &visible_update_prompt,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoResize))
        {
            draw_panel_background(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x, ImGui::GetWindowPos().y + ImGui::GetWindowSize().y));
            render_prompt_body("A newer starter project package is available.", "Update now, or keep browsing the worlds already on disk.", "Update", "Skip", update_assets, skip_update_prompt);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }

    void load_selected_world()
    {
        if (selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
        {
            WorldPreviews::RequestGeneration(world_files[selected_index].file_path);
            spartan::World::LoadFromFile(world_files[selected_index].file_path);
        }
        visible_world_list = false;
    }

    void draw_world_preview(const spartan::WorldMetadata& world, const ImVec2& min_pos, const ImVec2& max_pos, float rounding)
    {
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        spartan::RHI_Texture* preview_tex = WorldPreviews::GetTexture(world.file_path);
        const spartan::Icon& world_icon   = spartan::ResourceCache::GetIcon(spartan::IconType::World);

        draw_list->AddRectFilled(min_pos, max_pos, colors.preview_bg, rounding);

        if (preview_tex)
        {
            draw_list->AddImageRounded(
                reinterpret_cast<ImTextureID>(preview_tex),
                min_pos,
                max_pos,
                ImVec2(0.0f, 0.0f),
                ImVec2(1.0f, 1.0f),
                IM_COL32(255, 255, 255, 255),
                rounding
            );
            return;
        }

        if (!world_icon.texture)
        {
            return;
        }

        float icon_size = min(max_pos.x - min_pos.x, max_pos.y - min_pos.y) * 0.38f;
        ImVec2 icon_min = ImVec2(min_pos.x + (max_pos.x - min_pos.x - icon_size) * 0.5f, min_pos.y + (max_pos.y - min_pos.y - icon_size) * 0.5f);
        ImVec2 icon_max = ImVec2(icon_min.x + icon_size, icon_min.y + icon_size);
        draw_list->AddImage(
            reinterpret_cast<ImTextureID>(world_icon.texture),
            icon_min,
            icon_max,
            ImVec2(world_icon.uv_min.x, world_icon.uv_min.y),
            ImVec2(world_icon.uv_max.x, world_icon.uv_max.y),
            IM_COL32(255, 255, 255, 190)
        );
    }

    bool draw_world_card(int world_index, float card_w, float card_h)
    {
        const spartan::WorldMetadata& world = world_files[world_index];
        ImGui::PushID(world_index);

        ImVec2 card_min = ImGui::GetCursorScreenPos();
        ImVec2 card_max = ImVec2(card_min.x + card_w, card_min.y + card_h);
        ImGui::InvisibleButton("##world_card", ImVec2(card_w, card_h));

        bool is_hovered  = ImGui::IsItemHovered();
        bool is_selected = selected_index == world_index;
        bool loaded      = false;

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            float now = static_cast<float>(ImGui::GetTime());
            if (last_click_index == world_index && (now - last_click_time) < 0.4f)
            {
                selected_index = world_index;
                load_selected_world();
                loaded = true;
            }
            else
            {
                last_click_time = now;
                last_click_index = world_index;
                select_visible_index(visible_position_from_world_index(world_index));
            }
        }

        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        float rounding        = scaled(card_rounding);

        if (is_hovered || is_selected)
        {
            draw_list->AddRectFilled(ImVec2(card_min.x + scaled(2.0f), card_min.y + scaled(3.0f)), ImVec2(card_max.x + scaled(2.0f), card_max.y + scaled(3.0f)), colors.shadow, rounding);
        }

        ImU32 bg = is_selected ? colors.card_bg_selected : (is_hovered ? colors.card_bg_hover : colors.card_bg);
        draw_list->AddRectFilled(card_min, card_max, bg, rounding);
        draw_list->AddRect(card_min, card_max, is_selected ? colors.card_border_accent : colors.card_border, rounding, is_selected ? scaled(2.0f) : scaled(1.0f));

        float pad       = scaled(card_padding);
        ImVec2 image_min = ImVec2(card_min.x + pad, card_min.y + pad);
        ImVec2 image_max = ImVec2(card_max.x - pad, card_min.y + card_h * 0.58f);
        draw_world_preview(world, image_min, image_max, scaled(6.0f));

        ImVec2 text_min = ImVec2(card_min.x + pad, image_max.y + scaled(12.0f));
        ImVec2 text_max = ImVec2(card_max.x - pad, card_max.y - pad);
        draw_list->PushClipRect(text_min, text_max, true);

        ImFont* title_font = Editor::font_bold ? Editor::font_bold : ImGui::GetFont();
        float title_size   = ImGui::GetFontSize();
        float body_size    = ImGui::GetFontSize() * 0.82f;
        float path_size    = ImGui::GetFontSize() * 0.72f;
        float title_y      = text_min.y;

        draw_list->AddText(title_font, title_size, ImVec2(text_min.x, title_y), colors.text_primary, world.name.c_str());

        float desc_y = title_y + ImGui::GetTextLineHeightWithSpacing();
        const char* description = world.description.empty() ? "No description available." : world.description.c_str();
        draw_list->AddText(ImGui::GetFont(), body_size, ImVec2(text_min.x, desc_y), colors.text_muted, description);

        string source = source_label(world.file_path);
        float path_y  = text_max.y - ImGui::GetFontSize() * 0.78f;
        draw_list->AddText(ImGui::GetFont(), path_size, ImVec2(text_min.x, path_y), colors.text_muted, source.c_str());

        draw_list->PopClipRect();

        if (is_work_in_progress(world))
        {
            const char* status = "wip";
            ImVec2 status_size = ImGui::CalcTextSize(status);
            ImVec2 chip_min    = ImVec2(image_min.x + scaled(8.0f), image_min.y + scaled(8.0f));
            ImVec2 chip_max    = ImVec2(chip_min.x + status_size.x + scaled(14.0f), chip_min.y + status_size.y + scaled(6.0f));

            draw_list->AddRectFilled(chip_min, chip_max, colors.warning, 0.0f);
            draw_list->AddText(ImVec2(chip_min.x + scaled(7.0f), chip_min.y + scaled(3.0f)), IM_COL32(0, 0, 0, 255), status);
        }

        if (is_hovered)
        {
            ImGui::SetTooltip("%s\n%s", world.name.c_str(), world.file_path.c_str());
        }

        ImGui::PopID();
        return loaded;
    }

    void render_card_grid_world_files(float content_width)
    {
        if (visible_indices.empty())
        {
            ImVec2 region = ImGui::GetContentRegionAvail();
            ImVec2 pos    = ImGui::GetCursorScreenPos();
            ImVec2 center = ImVec2(pos.x + region.x * 0.5f, pos.y + region.y * 0.42f);
            ImDrawList* draw = ImGui::GetWindowDrawList();

            float marker_half = scaled(38.0f);
            draw->AddRectFilled(ImVec2(center.x - marker_half, center.y - marker_half), ImVec2(center.x + marker_half, center.y + marker_half), colors.preview_bg, 0.0f);
            draw->AddText(ImVec2(center.x - scaled(58.0f), center.y + scaled(52.0f)), colors.text_muted, search_filter.IsActive() ? "No worlds match your search." : "No worlds found on disk.");
            return;
        }

        float spacing = scaled(card_spacing);
        float card_w  = scaled(card_width_base);
        float card_h  = scaled(card_height_base);
        visible_columns = max(1, static_cast<int>((content_width + spacing) / (card_w + spacing)));
        card_w = floorf((content_width - spacing * static_cast<float>(visible_columns - 1)) / static_cast<float>(visible_columns));

        int col = 0;
        for (int world_index : visible_indices)
        {
            if (col > 0)
            {
                ImGui::SameLine(0.0f, spacing);
            }

            if (draw_world_card(world_index, card_w, card_h))
            {
                return;
            }

            col++;
            if (col >= visible_columns)
            {
                col = 0;
            }
        }
    }

    void draw_toolbar(float content_w)
    {
        string count = to_string(world_files.size()) + " worlds";
        float count_w = ImGui::CalcTextSize(count.c_str()).x + scaled(16.0f);
        float button_w = scaled(92.0f);
        float search_w = content_w - count_w - button_w - scaled(28.0f);
        float start_x = ImGui::GetCursorPosX();

        if (search_w < scaled(220.0f))
        {
            search_w = scaled(220.0f);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, scaled_vec(12.0f, 8.0f));
        ImGui::SetNextItemWidth(search_w);
        search_filter.Draw("##world_search");
        if (!search_filter.IsActive())
        {
            ImVec2 input_min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(input_min.x + scaled(12.0f), input_min.y + scaled(8.0f)), colors.text_muted, "Search worlds, descriptions, or paths...");
        }
        ImGui::PopStyleVar(2);

        ImGui::SameLine(0.0f, scaled(8.0f));
        if (launcher_button("Refresh", ImVec2(button_w, 0.0f)))
        {
            scan_for_world_files();
            rebuild_visible_indices();
        }

        ImGui::SameLine(0.0f, scaled(8.0f));
        ImGui::SetCursorPosX(start_x + content_w - count_w);
        draw_chip(count.c_str(), colors.chip_bg, colors.text_muted);
    }

    void draw_detail_panel(float width, float height)
    {
        begin_panel("##world_details", ImVec2(width, height));

        const spartan::WorldMetadata* world = selected_world();
        if (!world)
        {
            ImGui::TextDisabled("Select a world to see details.");
            float footer_y = height - scaled(54.0f);
            if (ImGui::GetCursorPosY() < footer_y)
            {
                ImGui::SetCursorPosY(footer_y);
            }

            float width_available = width - scaled(28.0f);
            float button_w = (width_available - scaled(8.0f)) * 0.5f;
            if (launcher_button("Cancel", ImVec2(button_w, 0.0f)))
            {
                visible_world_list = false;
            }
            ImGui::SameLine(0.0f, scaled(8.0f));
            if (launcher_button("Controls", ImVec2(button_w, 0.0f)))
            {
                *GeneralWindows::GetVisiblityWindowControls() = true;
            }
            end_panel();
            return;
        }

        float content_height = max(scaled(120.0f), height - scaled(132.0f));
        ImGui::BeginChild("##world_details_content", ImVec2(width - scaled(28.0f), content_height), false);

        ImVec2 preview_min = ImGui::GetCursorScreenPos();
        ImVec2 preview_max = ImVec2(preview_min.x + width - scaled(28.0f), preview_min.y + scaled(170.0f));
        draw_world_preview(*world, preview_min, preview_max, 0.0f);
        ImGui::Dummy(ImVec2(width - scaled(28.0f), scaled(170.0f)));

        ImGui::Dummy(ImVec2(0.0f, scaled(10.0f)));
        if (Editor::font_bold)
        {
            ImGui::PushFont(Editor::font_bold, 0.0f);
        }
        text_centered_in_width(world->name.c_str(), width - scaled(28.0f), colors.text_primary);
        if (Editor::font_bold)
        {
            ImGui::PopFont();
        }

        float text_width = width - scaled(28.0f);
        ImGui::SetCursorPosX(scaled(14.0f));
        if (!world->description.empty())
        {
            text_wrapped_centered(world->description.c_str(), text_width, colors.text_muted);
        }
        else
        {
            text_centered_in_width("No description available.", text_width, colors.text_muted);
        }

        ImGui::Dummy(ImVec2(0.0f, scaled(8.0f)));
        string source = source_label(world->file_path);
        float source_chip_w = ImGui::CalcTextSize(source.c_str()).x + scaled(16.0f);
        float wip_chip_w = is_work_in_progress(*world) ? ImGui::CalcTextSize("work in progress").x + scaled(16.0f) : 0.0f;
        float chip_gap = wip_chip_w > 0.0f ? scaled(8.0f) : 0.0f;
        float chip_total_w = source_chip_w + wip_chip_w + chip_gap;
        ImGui::SetCursorPosX((width - chip_total_w) * 0.5f);
        draw_chip(source.c_str(), colors.chip_bg, colors.text_muted);
        if (is_work_in_progress(*world))
        {
            ImGui::SameLine(0.0f, chip_gap);
            draw_chip("work in progress", colors.warning, IM_COL32(0, 0, 0, 255));
        }

        ImGui::Dummy(ImVec2(0.0f, scaled(8.0f)));
        text_centered_in_width(world->file_path.c_str(), width - scaled(28.0f), colors.text_muted);

        ImGui::EndChild();

        float footer_w = width - scaled(28.0f);
        ImGui::SetCursorPosX(scaled(14.0f));
        text_centered_in_width("developer build", footer_w, colors.warning);
        ImGui::SetCursorPosX(scaled(14.0f));
        text_centered_in_width("experimental worlds may change", footer_w, colors.warning);
        ImGui::Dummy(ImVec2(0.0f, scaled(6.0f)));

        float load_w = min(scaled(220.0f), footer_w);
        ImGui::SetCursorPosX((width - load_w) * 0.5f);
        if (launcher_button("Load World", ImVec2(load_w, 0.0f), true))
        {
            load_selected_world();
        }

        float button_w = scaled(118.0f);
        float button_group_w = button_w * 2.0f + scaled(8.0f);
        ImGui::SetCursorPosX((width - button_group_w) * 0.5f);
        if (launcher_button("Cancel", ImVec2(button_w, 0.0f)))
        {
            visible_world_list = false;
        }
        ImGui::SameLine(0.0f, scaled(8.0f));
        if (launcher_button("Controls", ImVec2(button_w, 0.0f)))
        {
            *GeneralWindows::GetVisiblityWindowControls() = true;
        }

        end_panel();
    }

    void handle_keyboard()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows))
        {
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            visible_world_list = false;
            return;
        }

        if (ImGui::GetIO().WantTextInput)
        {
            return;
        }

        if (visible_indices.empty())
        {
            return;
        }

        int target = selected_visible_index;
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false))
        {
            target++;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false))
        {
            target--;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false))
        {
            target += visible_columns;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false))
        {
            target -= visible_columns;
        }

        if (target != selected_visible_index)
        {
            select_visible_index(target);
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
        {
            load_selected_world();
        }
    }

    void window_world_list()
    {
        if (!visible_world_list)
        {
            return;
        }

        update_colors();

        ImGui::SetNextWindowSize(ImVec2(1600.0f, 900.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(ImVec2(960.0f, 540.0f), ImVec2(2400.0f, 1350.0f));
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, scaled_vec(16.0f, 16.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, scaled(panel_rounding));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::Begin("World Launcher", &visible_world_list, flags))
        {
            if (spartan::FileSystem::IsDirectoryEmpty(spartan::ResourceCache::GetProjectDirectory()))
            {
                visible_world_list      = false;
                visible_download_prompt = true;
                ImGui::End();
                ImGui::PopStyleVar(2);
                return;
            }

            float content_w = ImGui::GetContentRegionAvail().x;
            rebuild_visible_indices();
            handle_keyboard();

            ImVec2 remaining = ImGui::GetContentRegionAvail();
            float bottom_bar_h = ImGui::GetFrameHeightWithSpacing() + scaled(8.0f);
            float panels_h = max(scaled(260.0f), remaining.y - bottom_bar_h);
            float details_w  = min(scaled(details_width_base), max(scaled(320.0f), remaining.x * 0.32f));
            float grid_w     = remaining.x - details_w - scaled(section_spacing);
            if (grid_w < scaled(300.0f))
            {
                grid_w    = remaining.x;
                details_w = remaining.x;
            }

            ImVec2 panels_pos = ImGui::GetCursorPos();
            begin_panel("##world_grid_panel", ImVec2(grid_w, panels_h));
            {
                ImGui::BeginChild("##world_grid_scroll", ImVec2(grid_w - scaled(28.0f), panels_h - scaled(28.0f)), false);
                render_card_grid_world_files(ImGui::GetContentRegionAvail().x);
                ImGui::EndChild();
            }
            end_panel();

            if (details_w != remaining.x)
            {
                ImGui::SameLine(0.0f, scaled(section_spacing));
                draw_detail_panel(details_w, panels_h);
            }
            else
            {
                ImGui::Dummy(ImVec2(0.0f, scaled(section_spacing)));
                draw_detail_panel(details_w, scaled(430.0f));
            }

            ImGui::SetCursorPos(ImVec2(panels_pos.x, panels_pos.y + panels_h + scaled(8.0f)));
            draw_toolbar(content_w);
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

void WorldSelector::Initialize(Editor* editor_in)
{
    editor = editor_in;

    size_t file_count  = spartan::FileSystem::GetFilesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size();
    file_count        += spartan::FileSystem::GetDirectoriesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size();
    downloaded_and_extracted = file_count > 1;

    if (downloaded_and_extracted)
    {
        scan_for_world_files();
    }
}

void WorldSelector::Tick()
{
    window_download_prompt();
    window_update_prompt();
    window_world_list();
}

bool WorldSelector::GetVisible()
{
    return visible_world_list || visible_download_prompt || visible_update_prompt;
}

void WorldSelector::SetVisible(bool visibility)
{
    visible_world_list      = false;
    visible_download_prompt = false;
    visible_update_prompt   = false;

    if (!visibility)
    {
        return;
    }

    if (!downloaded_and_extracted)
    {
        visible_download_prompt = true;
        return;
    }

    visible_world_list = true;
    scan_for_world_files();
    selected_index         = world_files.empty() ? -1 : 0;
    selected_visible_index = 0;
    last_click_index       = -1;
    last_click_time        = -1.0f;

    if (!update_check_started)
    {
        update_check_started = true;
        check_assets_outdated_async();
    }
}
