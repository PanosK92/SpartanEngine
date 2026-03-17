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
#include "../GeneralWindows.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
#include "../Widgets/Viewport.h"
#include "Core/ProgressTracker.h"
#include "Game/Game.h"
#include "RHI/RHI_Device.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor = nullptr;

    // default worlds (built-in, created programmatically via Game.cpp)
    struct DefaultWorldEntry
    {
        const char* name;
        const char* description;
        const char* status;      // wip, prototype, complete
        const char* performance; // light, moderate, demanding
        uint32_t vram;           // min vram requirement in megabytes
    };

    const DefaultWorldEntry default_worlds[] =
    {
        { "Car Showroom",      "Showcase world for YouTubers/Press. Does not use experimental tech", "Complete" , "Light",          2100 },
        { "Open World Forest", "256 million of Ghost of Tsushima grass blades",                      "Prototype", "Very demanding", 5600 },
        { "Liminal Space",     "Shifts your frequency to a nearby reality",                          "Prototype", "Light",          2100 },
        { "Sponza 4K",         "High-resolution textures & meshes",                                  "Complete" , "Demanding",      2600 },
        { "Cornell Box",       "Classic ray tracing test scene",                                     "Complete" , "Light",          2100 },
        { "San Miguel",        "Detailed courtyard scene with complex geometry and lighting",        "Complete" , "Demanding",      2600 },
        { "Basic",             "Light, camera, floor",                                               "Complete" , "Light",          2100 }
    };
    constexpr int default_world_count = sizeof(default_worlds) / sizeof(default_worlds[0]);

    // discovered world files from disk
    vector<spartan::WorldMetadata> world_files;
    int selected_index            = 0;
    bool is_default_world_selected = true;

    // visibility states
    bool visible_download_prompt  = false;
    bool visible_update_prompt    = false;
    bool visible_world_list       = false;
    bool downloaded_and_extracted = false;

    // card grid layout
    const float card_rounding    = 6.0f;
    const float card_padding     = 10.0f;
    const float card_spacing     = 8.0f;
    const float card_width_base  = 155.0f;
    const float card_height_base = 150.0f;
    const float badge_rounding   = 4.0f;
    const float section_spacing  = 12.0f;

    // card colors (initialized once per frame from style)
    ImU32 col_card_bg            = 0;
    ImU32 col_card_bg_hover      = 0;
    ImU32 col_card_bg_selected   = 0;
    ImU32 col_card_border        = 0;
    ImU32 col_card_border_accent = 0;
    ImU32 col_shadow             = 0;

    // search filter
    ImGuiTextFilter search_filter;

    // double-click timing
    float last_click_time = -1.0f;
    int last_click_index  = -1;
    bool last_click_was_default = true;

    // asset download configuration
    const char* assets_url          = "https://www.dropbox.com/scl/fi/lo0fiz7q4qggn07v2p7r7/project.7z?rlkey=twtmlivihh1rgz640ir85o4pk&st=2qbdrpxi&dl=1";
    const char* assets_destination  = "project/project.7z";
    const char* assets_extract_dir  = "project/";
    const char* assets_expected_sha = "ffa63b138a7867d1fb84687533368337f374d91e78330f74542e5c751b45eee5";

    void scan_directory_recursive(const string& directory)
    {
        // make sure the directory exists before trying to iterate
        if (!spartan::FileSystem::Exists(directory) || !spartan::FileSystem::IsDirectory(directory))
            return;

        // scan files in this directory
        vector<string> files = spartan::FileSystem::GetFilesInDirectory(directory);
        for (const string& file : files)
        {
            if (spartan::FileSystem::IsEngineWorldFile(file))
            {
                // normalize path to use forward slashes consistently
                string normalized_path = file;
                replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

                spartan::WorldMetadata metadata;
                if (spartan::World::ReadMetadata(normalized_path, metadata))
                {
                    world_files.push_back(metadata);
                }
            }
        }

        // recursively scan subdirectories
        vector<string> subdirectories = spartan::FileSystem::GetDirectoriesInDirectory(directory);
        for (const string& subdir : subdirectories)
        {
            scan_directory_recursive(subdir);
        }
    }

    void scan_for_world_files()
    {
        world_files.clear();

        // scan the project directory recursively (for exported/imported worlds with assets)
        string project_dir = spartan::ResourceCache::GetProjectDirectory();
        scan_directory_recursive(project_dir);

        // scan the worlds folder for git-tracked world files
        // check multiple possible locations since working directory may vary
        vector<string> worlds_dirs = { "worlds", "../worlds" };
        for (const string& worlds_dir : worlds_dirs)
        {
            if (spartan::FileSystem::Exists(worlds_dir))
            {
                scan_directory_recursive(worlds_dir);
                break; // only scan from one location to avoid duplicates
            }
        }
    }

    void check_assets_outdated_async()
    {
        // run hash check in background so ui doesn't freeze
        spartan::ThreadPool::AddTask([]()
        {
            if (!spartan::FileSystem::Exists(assets_destination))
                return;

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

        // run download and extract in background
        spartan::ThreadPool::AddTask([]()
        {
            // start progress tracking in continuous mode (job_count = 0)
            spartan::Progress& progress = spartan::ProgressTracker::GetProgress(spartan::ProgressType::Download);
            progress.Start(0, "Downloading projects...");
            spartan::ProgressTracker::SetGlobalLoadingState(true);

            // download with real-time progress callback
            bool success = spartan::FileSystem::DownloadFile(
                assets_url,
                assets_destination,
                [&progress](float download_progress)
                {
                    // download is 0-90%, extraction is 90-100%
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
                scan_for_world_files();
                visible_world_list = true;
            }
        });
    }

    void window_download_prompt()
    {
        if (!visible_download_prompt)
            return;

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Default worlds", &visible_download_prompt,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("No default worlds are present. Would you like to download some out of the box projects?");
            ImGui::Separator();

            float button_width = ImGui::CalcTextSize("Download Projects").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;
            float offset_x     = (ImGui::GetContentRegionAvail().x - button_width) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

            ImGui::BeginGroup();
            {
                if (ImGui::Button("Download Projects"))
                {
                    download_and_extract();
                }

                ImGui::SameLine();
                if (ImGui::Button("Cancel"))
                {
                    visible_download_prompt = false;
                }
            }
            ImGui::EndGroup();
        }
        ImGui::End();
    }

    void window_update_prompt()
    {
        if (!visible_update_prompt)
            return;

        // close world list when update prompt appears
        visible_world_list = false;

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Update available", &visible_update_prompt,
            ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("A newer version of the projects is available. Would you like to update?");
            ImGui::Separator();

            ImGui::BeginGroup();
            {
                if (ImGui::Button("Update"))
                {
                    visible_update_prompt = false;
                    // delete old assets.7z so it downloads fresh
                    if (spartan::FileSystem::Exists(assets_destination))
                    {
                        spartan::FileSystem::Delete(assets_destination);
                    }
                    download_and_extract();
                }

                ImGui::SameLine();
                if (ImGui::Button("Skip"))
                {
                    visible_update_prompt = false;
                    visible_world_list = true;
                }
            }
            ImGui::EndGroup();
        }
        ImGui::End();
    }

    void load_selected_world()
    {
        if (is_default_world_selected)
        {
            spartan::Game::Load(static_cast<spartan::DefaultWorld>(selected_index));
        }
        else if (selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
        {
            spartan::World::LoadFromFile(world_files[selected_index].file_path);
        }
        visible_world_list = false;
    }

    ImU32 get_status_color(const char* status)
    {
        if (strcmp(status, "Complete") == 0)  return ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_ok);
        if (strcmp(status, "Prototype") == 0) return ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_warning);
        return ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_info);
    }

    ImU32 get_performance_color(const char* performance)
    {
        if (strcmp(performance, "Light") == 0)          return ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_ok);
        if (strcmp(performance, "Demanding") == 0)      return ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_warning);
        if (strcmp(performance, "Very demanding") == 0) return ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_error);
        return ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_info);
    }

    int render_card_grid_default_worlds(float card_w, float card_h, float content_width, int col)
    {
        int columns = max(1, static_cast<int>((content_width + card_spacing) / (card_w + card_spacing)));

        for (int i = 0; i < default_world_count; i++)
        {
            if (!search_filter.PassFilter(default_worlds[i].name))
                continue;

            if (col > 0)
            {
                ImGui::SameLine(0, card_spacing);
            }

            ImGui::PushID(i);

            ImVec2 screen_pos = ImGui::GetCursorScreenPos();
            ImVec2 card_min   = screen_pos;
            ImVec2 card_max   = ImVec2(screen_pos.x + card_w, screen_pos.y + card_h);

            ImGui::InvisibleButton("##card", ImVec2(card_w, card_h));
            bool is_hovered  = ImGui::IsItemHovered();
            bool is_selected = is_default_world_selected && (selected_index == i);

            // selection and double-click
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                float now = static_cast<float>(ImGui::GetTime());
                if (last_click_index == i && last_click_was_default && (now - last_click_time) < 0.4f)
                {
                    selected_index            = i;
                    is_default_world_selected = true;
                    load_selected_world();
                    ImGui::PopID();
                    return col;
                }
                last_click_time           = now;
                last_click_index          = i;
                last_click_was_default    = true;
                selected_index            = i;
                is_default_world_selected = true;
            }

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            // shadow on hover/selection
            if (is_hovered || is_selected)
            {
                draw_list->AddRectFilled(
                    ImVec2(card_min.x + 2, card_min.y + 2),
                    ImVec2(card_max.x + 2, card_max.y + 2),
                    col_shadow, card_rounding
                );
            }

            // card background
            ImU32 bg = is_selected ? col_card_bg_selected : (is_hovered ? col_card_bg_hover : col_card_bg);
            draw_list->AddRectFilled(card_min, card_max, bg, card_rounding);

            // card border
            if (is_selected)
            {
                draw_list->AddRect(card_min, card_max, col_card_border_accent, card_rounding, 0, 2.0f);
            }
            else if (is_hovered)
            {
                draw_list->AddRect(card_min, card_max, col_card_border, card_rounding, 0, 1.0f);
            }

            // world icon centered in the upper portion
            float icon_area_h = card_h * 0.45f;
            float icon_size   = icon_area_h * 0.6f;
            if (spartan::RHI_Texture* icon_tex = spartan::ResourceCache::GetIcon(spartan::IconType::World))
            {
                float icon_x = card_min.x + (card_w - icon_size) * 0.5f;
                float icon_y = card_min.y + card_padding + (icon_area_h - icon_size) * 0.5f;

                // vram warning: tint the icon red if system vram is insufficient
                uint64_t system_vram = spartan::RHI_Device::MemoryGetTotalMb();
                ImU32 icon_tint = (system_vram < default_worlds[i].vram)
                    ? IM_COL32(255, 100, 100, 220)
                    : IM_COL32(255, 255, 255, 200);

                draw_list->AddImage(
                    reinterpret_cast<ImTextureID>(icon_tex),
                    ImVec2(icon_x, icon_y),
                    ImVec2(icon_x + icon_size, icon_y + icon_size),
                    ImVec2(0, 0), ImVec2(1, 1), icon_tint
                );
            }

            // world name centered below icon area (smaller font)
            float small_font_size = ImGui::GetFontSize() * 0.85f;
            float text_y = card_min.y + card_padding + icon_area_h + 4.0f;
            {
                const char* name  = default_worlds[i].name;
                ImVec2 name_size  = ImGui::GetFont()->CalcTextSizeA(small_font_size, FLT_MAX, 0.0f, name);
                float label_max_w = card_w - card_padding * 2;
                float label_x     = card_min.x + (card_w - min(name_size.x, label_max_w)) * 0.5f;

                draw_list->AddText(ImGui::GetFont(), small_font_size, ImVec2(label_x, text_y), ImGui::GetColorU32(ImGuiCol_Text), name);
            }

            // performance indicator below name (smaller font)
            {
                const char* perf = default_worlds[i].performance;
                ImVec2 perf_size = ImGui::GetFont()->CalcTextSizeA(small_font_size, FLT_MAX, 0.0f, perf);
                float perf_y     = text_y + small_font_size + 3.0f;
                float perf_x     = card_min.x + (card_w - perf_size.x) * 0.5f;
                ImU32 perf_col   = get_performance_color(perf);

                draw_list->AddText(ImGui::GetFont(), small_font_size, ImVec2(perf_x, perf_y), perf_col, perf);
            }

            // status strip at the bottom of the card (full width)
            {
                const char* status      = default_worlds[i].status;
                float strip_font_size   = ImGui::GetFontSize() * 0.8f;
                ImVec2 status_text_size = ImGui::GetFont()->CalcTextSizeA(strip_font_size, FLT_MAX, 0.0f, status);
                float strip_h           = status_text_size.y + 6.0f;
                float strip_y           = card_max.y - strip_h;

                ImU32 strip_col = get_status_color(status);
                draw_list->AddRectFilled(
                    ImVec2(card_min.x, strip_y),
                    card_max,
                    strip_col,
                    card_rounding,
                    ImDrawFlags_RoundCornersBottom
                );

                float text_x = card_min.x + (card_w - status_text_size.x) * 0.5f;
                float text_y_strip = strip_y + (strip_h - status_text_size.y) * 0.5f;
                draw_list->AddText(ImGui::GetFont(), strip_font_size, ImVec2(text_x, text_y_strip), IM_COL32(0, 0, 0, 255), status);
            }


            ImGui::PopID();

            col++;
            if (col >= columns)
            {
                col = 0;
            }
        }

        return col;
    }

    void render_card_grid_world_files(float card_w, float card_h, float content_width, int col)
    {
        int columns = max(1, static_cast<int>((content_width + card_spacing) / (card_w + card_spacing)));

        for (int i = 0; i < static_cast<int>(world_files.size()); i++)
        {
            if (!search_filter.PassFilter(world_files[i].name.c_str()))
                continue;

            if (col > 0)
            {
                ImGui::SameLine(0, card_spacing);
            }

            ImGui::PushID(default_world_count + i);

            ImVec2 screen_pos = ImGui::GetCursorScreenPos();
            ImVec2 card_min   = screen_pos;
            ImVec2 card_max   = ImVec2(screen_pos.x + card_w, screen_pos.y + card_h);

            ImGui::InvisibleButton("##card", ImVec2(card_w, card_h));
            bool is_hovered  = ImGui::IsItemHovered();
            bool is_selected = !is_default_world_selected && (selected_index == i);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            {
                float now = static_cast<float>(ImGui::GetTime());
                if (last_click_index == i && !last_click_was_default && (now - last_click_time) < 0.4f)
                {
                    selected_index            = i;
                    is_default_world_selected = false;
                    load_selected_world();
                    ImGui::PopID();
                    return;
                }
                last_click_time        = now;
                last_click_index       = i;
                last_click_was_default = false;
                selected_index            = i;
                is_default_world_selected = false;
            }

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            if (is_hovered || is_selected)
            {
                draw_list->AddRectFilled(
                    ImVec2(card_min.x + 2, card_min.y + 2),
                    ImVec2(card_max.x + 2, card_max.y + 2),
                    col_shadow, card_rounding
                );
            }

            ImU32 bg = is_selected ? col_card_bg_selected : (is_hovered ? col_card_bg_hover : col_card_bg);
            draw_list->AddRectFilled(card_min, card_max, bg, card_rounding);

            if (is_selected)
            {
                draw_list->AddRect(card_min, card_max, col_card_border_accent, card_rounding, 0, 2.0f);
            }
            else if (is_hovered)
            {
                draw_list->AddRect(card_min, card_max, col_card_border, card_rounding, 0, 1.0f);
            }

            // world icon
            float icon_area_h = card_h * 0.5f;
            float icon_size   = icon_area_h * 0.55f;
            if (spartan::RHI_Texture* icon_tex = spartan::ResourceCache::GetIcon(spartan::IconType::World))
            {
                float icon_x = card_min.x + (card_w - icon_size) * 0.5f;
                float icon_y = card_min.y + card_padding + (icon_area_h - icon_size) * 0.5f;
                draw_list->AddImage(
                    reinterpret_cast<ImTextureID>(icon_tex),
                    ImVec2(icon_x, icon_y),
                    ImVec2(icon_x + icon_size, icon_y + icon_size),
                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 200)
                );
            }

            // clip all text to the card bounds
            draw_list->PushClipRect(card_min, card_max, true);

            // world name (smaller font)
            float small_font_size = ImGui::GetFontSize() * 0.85f;
            float text_y = card_min.y + card_padding + icon_area_h + 4.0f;
            {
                const char* name  = world_files[i].name.c_str();
                float clip_right  = card_max.x - card_padding;

                ImGui::RenderTextEllipsis(
                    draw_list,
                    ImVec2(card_min.x + card_padding, text_y),
                    ImVec2(clip_right, card_max.y),
                    clip_right, clip_right,
                    name, nullptr, nullptr
                );
            }

            // description snippet below name (smaller font, clipped)
            if (!world_files[i].description.empty())
            {
                const char* desc = world_files[i].description.c_str();
                float desc_y     = text_y + ImGui::GetTextLineHeightWithSpacing();
                float clip_right = card_max.x - card_padding;

                ImGui::RenderTextEllipsis(
                    draw_list,
                    ImVec2(card_min.x + card_padding, desc_y),
                    ImVec2(clip_right, card_max.y - 4.0f),
                    clip_right, clip_right,
                    desc, nullptr, nullptr
                );
            }

            // status strip at the bottom (full width)
            {
                const char* status      = "XML";
                float strip_font_size   = ImGui::GetFontSize() * 0.8f;
                ImVec2 status_text_size = ImGui::GetFont()->CalcTextSizeA(strip_font_size, FLT_MAX, 0.0f, status);
                float strip_h           = status_text_size.y + 6.0f;
                float strip_y           = card_max.y - strip_h;

                ImU32 strip_col = ImGui::ColorConvertFloat4ToU32(ImGui::Style::color_accent_2);
                draw_list->AddRectFilled(
                    ImVec2(card_min.x, strip_y),
                    card_max,
                    strip_col,
                    card_rounding,
                    ImDrawFlags_RoundCornersBottom
                );

                float text_x       = card_min.x + (card_w - status_text_size.x) * 0.5f;
                float text_y_strip = strip_y + (strip_h - status_text_size.y) * 0.5f;
                draw_list->AddText(ImGui::GetFont(), strip_font_size, ImVec2(text_x, text_y_strip), IM_COL32(0, 0, 0, 255), status);
            }

            draw_list->PopClipRect();

            // tooltip for full name on hover
            if (is_hovered)
            {
                ImGui::SetTooltip("%s", world_files[i].name.c_str());
            }

            ImGui::PopID();

            col++;
            if (col >= columns)
            {
                col = 0;
            }
        }
    }

    void window_world_list()
    {
        if (!visible_world_list)
            return;

        float dpi = spartan::Window::GetDpiScale();

        // update card colors from current style
        col_card_bg            = ImGui::ColorConvertFloat4ToU32(ImVec4(0.12f, 0.12f, 0.13f, 1.0f));
        col_card_bg_hover      = ImGui::ColorConvertFloat4ToU32(ImVec4(0.18f, 0.18f, 0.20f, 1.0f));
        col_card_bg_selected   = ImGui::ColorConvertFloat4ToU32(ImVec4(0.15f, 0.25f, 0.35f, 1.0f));
        col_card_border        = ImGui::ColorConvertFloat4ToU32(ImVec4(0.25f, 0.25f, 0.28f, 1.0f));
        col_card_border_accent = ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        col_shadow             = IM_COL32(0, 0, 0, 50);

        float window_w = 1455.0f;
        float window_h = 776.0f;
        ImGui::SetNextWindowSize(ImVec2(window_w, window_h), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSizeConstraints(ImVec2(700.0f, 480.0f), ImVec2(2000.0f, 1200.0f));
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

        ImGuiWindowFlags flags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
        if (ImGui::Begin("World Selection", &visible_world_list, flags))
        {
            if (spartan::FileSystem::IsDirectoryEmpty("Project"))
            {
                visible_world_list     = false;
                visible_download_prompt = true;
                ImGui::End();
                return;
            }

            float card_w       = card_width_base * dpi;
            float card_h       = card_height_base * dpi;
            float content_w    = ImGui::GetContentRegionAvail().x;

            // search bar
            ImGui::SetNextItemWidth(content_w);
            search_filter.Draw("##search");
            if (!search_filter.IsActive())
            {
                ImVec2 input_min = ImGui::GetItemRectMin();
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(input_min.x + ImGui::GetStyle().FramePadding.x, input_min.y + ImGui::GetStyle().FramePadding.y),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    "Search worlds..."
                );
            }

            ImGui::Spacing();

            // reserve fixed space at the bottom for details, warning, and buttons
            float line_h        = ImGui::GetTextLineHeightWithSpacing();
            float detail_height = line_h * 3.0f + ImGui::GetStyle().ItemSpacing.y * 2.0f;
            float bottom_height = detail_height + line_h * 2.0f + ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 4.0f;
            float grid_height   = ImGui::GetContentRegionAvail().y - bottom_height;
            if (grid_height < 80.0f)
                grid_height = 80.0f;

            ImGui::BeginChild("##card_grid", ImVec2(0, grid_height), false);
            {
                float grid_w = ImGui::GetContentRegionAvail().x;
                int col = 0;

                if (default_world_count > 0)
                {
                    col = render_card_grid_default_worlds(card_w, card_h, grid_w, 0);
                }

                if (!world_files.empty())
                {
                    render_card_grid_world_files(card_w, card_h, grid_w, col);
                }
            }
            ImGui::EndChild();

            // detail strip for selected world (rendered inline, no child window)
            ImGui::Separator();
            {
                ImGui::PushTextWrapPos(0.0f);

                if (is_default_world_selected && selected_index >= 0 && selected_index < default_world_count)
                {
                    const DefaultWorldEntry& w = default_worlds[selected_index];

                    ImGui::Text("%s", w.name);
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(get_status_color(w.status)), "%s", w.status);
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();
                    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(get_performance_color(w.performance)), "%s", w.performance);
                    ImGui::SameLine();
                    ImGui::TextDisabled("|");
                    ImGui::SameLine();

                    uint64_t system_vram_mb = spartan::RHI_Device::MemoryGetTotalMb();
                    bool vram_ok            = system_vram_mb >= w.vram;
                    ImVec4 vram_color       = vram_ok ? ImGui::Style::color_info : ImGui::Style::color_error;
                    ImGui::TextColored(vram_color, "%u MB VRAM (System: %llu MB)", w.vram, system_vram_mb);

                    ImGui::TextWrapped("%s", w.description);
                }
                else if (!is_default_world_selected && selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
                {
                    const spartan::WorldMetadata& w = world_files[selected_index];

                    ImGui::Text("%s", w.name.c_str());

                    if (!w.description.empty())
                    {
                        ImGui::TextWrapped("%s", w.description.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("No description available.");
                    }

                    ImGui::TextDisabled("%s", w.file_path.c_str());
                }
                else
                {
                    ImGui::TextDisabled("Select a world to see details.");
                }

                ImGui::PopTextWrapPos();
            }

            // warning and buttons
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "Note: This is a developer build. It is experimental and not guaranteed to behave.");

            // vertically center the buttons in the remaining space
            float remaining_h  = ImGui::GetContentRegionAvail().y;
            float button_h     = ImGui::GetFrameHeight();
            float pad_y        = (remaining_h - button_h) * 0.5f;
            if (pad_y > 0.0f)
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + pad_y);

            float button_w  = 100.0f * dpi;
            float total_w   = button_w * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
            float offset_x  = (ImGui::GetContentRegionAvail().x - total_w) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

            if (ImGui::Button("Load", ImVec2(button_w, 0)))
            {
                load_selected_world();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(button_w, 0)))
            {
                visible_world_list = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Controls", ImVec2(button_w, 0)))
            {
                *GeneralWindows::GetVisiblityWindowControls() = true;
            }
        }
        ImGui::End();
    }
}

void WorldSelector::Initialize(Editor* editor_in)
{
    editor = editor_in;

    // check if assets are downloaded
    size_t file_count  = spartan::FileSystem::GetFilesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size();
    file_count        += spartan::FileSystem::GetDirectoriesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size();
    downloaded_and_extracted = file_count > 1; // assets.7z + extracted folders

    if (downloaded_and_extracted)
    {
        scan_for_world_files();
        check_assets_outdated_async();
        // don't show yet - GeneralWindows will show us after the welcome window is dismissed
    }
    else
    {
        visible_download_prompt = true;
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
    return visible_world_list;
}

void WorldSelector::SetVisible(bool visibility)
{
    visible_world_list = visibility;
    
    // rescan when becoming visible to pick up any new world files
    if (visibility)
    {
        scan_for_world_files();
        selected_index = 0;
        is_default_world_selected = true;
    }
}
