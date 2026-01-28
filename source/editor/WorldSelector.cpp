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

//= INCLUDES =======================
#include "pch.h"
#include "WorldSelector.h"
#include "GeneralWindows.h"
#include "ImGui/Source/imgui.h"
#include "ImGui/ImGui_Extension.h"
#include "FileSystem/FileSystem.h"
#include "Widgets/Viewport.h"
#include "Core/ProgressTracker.h"
#include "Core/ThreadPool.h"
#include "Resource/ResourceCache.h"
#include "World/World.h"
#include "Game/Game.h"
#include "RHI/RHI_Device.h"
//==================================

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
        { "Car Showroom",      "Showcase world for YouTubers/Press. Does not use experimental tech",                                                                  "Complete" ,  "Light",          2100 },
        { "Car Playground",    "Highly realistic vehicle physics with proper tire slip, thermals, aero, LSD, multi ray tire, and speed dependent steering geometry.", "Prototype",  "Light",          2100 },
        { "Open World Forest", "256 million of Ghost of Tsushima grass blades",                                                                                       "Prototype",  "Very demanding", 5600 },
        { "Liminal Space",     "Shifts your frequency to a nearby reality",                                                                                           "Prototype",  "Light",          2100 },
        { "Sponza 4K",         "High-resolution textures & meshes",                                                                                                   "Complete" ,  "Demanding",      2600 },
        { "Subway",            "GI test. No lights, only emissive textures",                                                                                          "Prototype" , "Moderate",       2600 },
        { "Minecraft",         "Blocky aesthetic",                                                                                                                    "Complete" ,  "Light",          2100 },
        { "Basic",             "Light, camera, floor",                                                                                                                "Complete" ,  "Light",          2100 }
    };
    constexpr int default_world_count = sizeof(default_worlds) / sizeof(default_worlds[0]);

    // discovered world files from disk
    vector<spartan::WorldMetadata> world_files;
    int selected_index = 0;
    bool is_default_world_selected = true; // true = default world, false = world file

    // visibility states
    bool visible_download_prompt  = false;
    bool visible_update_prompt    = false;
    bool visible_world_list       = false;
    bool downloaded_and_extracted = false;

    // asset download configuration
    const char* assets_url          = "https://www.dropbox.com/scl/fi/v78hpy6vb0kgu5ik5gqh5/assets.7z?rlkey=c53z3iblelyp2j0qcyumejday&st=m2itkz74&dl=1";
    const char* assets_destination  = "project/assets.7z";
    const char* assets_extract_dir  = "project/";
    const char* assets_expected_sha = "94f9e1ebe6ded2280e3bd56bcdfe87ed2f351260ea2495c96f4319333194226d";

    void scan_directory_recursive(const string& directory)
    {
        // scan files in this directory
        vector<string> files = spartan::FileSystem::GetFilesInDirectory(directory);
        for (const string& file : files)
        {
            if (spartan::FileSystem::IsEngineWorldFile(file))
            {
                spartan::WorldMetadata metadata;
                if (spartan::World::ReadMetadata(file, metadata))
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

        // recursively scan the project directory and all subdirectories for .world files
        string project_dir = spartan::ResourceCache::GetProjectDirectory();
        scan_directory_recursive(project_dir);
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
            progress.Start(0, "Downloading assets...");
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
                progress.SetText("Extracting assets...");
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
            ImGui::TextWrapped("No default worlds are present. Would you like to download them?");
            ImGui::Separator();

            float button_width = ImGui::CalcTextSize("Download Worlds").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;
            float offset_x     = (ImGui::GetContentRegionAvail().x - button_width) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

            ImGui::BeginGroup();
            {
                if (ImGui::Button("Download Worlds"))
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
            ImGui::TextWrapped("A newer version of the assets is available. Would you like to update?");
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

    void window_world_list()
    {
        if (!visible_world_list)
            return;

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("World Selection", &visible_world_list, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
        {
            // check if project directory is empty
            if (spartan::FileSystem::IsDirectoryEmpty("Project"))
            {
                visible_world_list = false;
                visible_download_prompt = true;
                ImGui::End();
                return;
            }

            const char* text_prompt  = "Select the world you would like to load.";
            const char* text_warning = "Note: This is a developer build. It is experimental and not guaranteed to behave.";

            ImGui::Text(text_prompt);
            ImGui::Separator();

            // calculate height to fit all entries without scrolling
            float row_height   = ImGui::GetTextLineHeightWithSpacing();
            int total_entries  = default_world_count + static_cast<int>(world_files.size());
            // add extra rows for section headers and separators
            int header_rows    = 2; // "Default Worlds" + separator
            if (!world_files.empty())
            {
                header_rows += 3; // spacing + "World Files" + separator
            }
            int visible_count  = min(total_entries + header_rows, 14);
            float list_height  = row_height * visible_count + ImGui::GetStyle().FramePadding.y * 2;

            // layout: left list, right details
            ImGui::BeginChild("left_panel", ImVec2(200, list_height), true);
            {
                // default worlds section
                if (default_world_count > 0)
                {
                    ImGui::TextDisabled("Default Worlds");
                    ImGui::Separator();
                    for (int i = 0; i < default_world_count; i++)
                    {
                        bool is_selected = is_default_world_selected && (selected_index == i);
                        if (ImGui::Selectable(default_worlds[i].name, is_selected))
                        {
                            selected_index = i;
                            is_default_world_selected = true;
                        }
                    }
                }

                // world files section (if any exist)
                if (!world_files.empty())
                {
                    ImGui::Spacing();
                    ImGui::TextDisabled("World Files");
                    ImGui::Separator();
                    for (int i = 0; i < static_cast<int>(world_files.size()); i++)
                    {
                        bool is_selected = !is_default_world_selected && (selected_index == i);
                        const char* display_name = world_files[i].title.empty() ? world_files[i].name.c_str() : world_files[i].title.c_str();
                        if (ImGui::Selectable(display_name, is_selected))
                        {
                            selected_index = i;
                            is_default_world_selected = false;
                        }
                    }
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("right_panel", ImVec2(800, list_height), true);
            {
                ImGui::PushTextWrapPos(0.0f);

                if (is_default_world_selected && selected_index >= 0 && selected_index < default_world_count)
                {
                    // show default world details
                    const DefaultWorldEntry& w = default_worlds[selected_index];

                    ImGui::TextWrapped("Description: %s", w.description);
                    ImGui::Separator();
                    ImGui::TextWrapped("Status: %s", w.status);
                    ImGui::Separator();
                    ImGui::TextWrapped("Performance: %s", w.performance);
                    ImGui::Separator();

                    uint64_t system_vram_mb = spartan::RHI_Device::MemoryGetTotalMb();
                    bool vram_sufficient    = system_vram_mb >= w.vram;
                    ImGui::TextWrapped("Minimum VRAM:");
                    ImGui::SameLine();
                    if (!vram_sufficient)
                    {
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%u MB (System: %llu MB)", w.vram, system_vram_mb);
                    }
                    else
                    {
                        ImGui::TextWrapped("%u MB (System: %llu MB)", w.vram, system_vram_mb);
                    }
                }
                else if (!is_default_world_selected && selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
                {
                    // show world file details
                    const spartan::WorldMetadata& w = world_files[selected_index];

                    ImGui::TextWrapped("Title: %s", w.title.empty() ? w.name.c_str() : w.title.c_str());
                    ImGui::Separator();

                    if (!w.description.empty())
                    {
                        ImGui::TextWrapped("Description: %s", w.description.c_str());
                    }
                    else
                    {
                        ImGui::TextDisabled("No description available.");
                    }
                    ImGui::Separator();

                    ImGui::TextWrapped("File: %s", w.file_path.c_str());
                }

                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();

            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), text_warning);

            // buttons
            ImGui::Spacing();
            float button_width = 100.0f;
            float total_width  = button_width * 3 + ImGui::GetStyle().ItemSpacing.x * 2;
            float offset_x     = (ImGui::GetContentRegionAvail().x - total_width) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

            if (ImGui::Button("Load", ImVec2(button_width, 0)))
            {
                if (is_default_world_selected)
                {
                    // load default world via Game::Load()
                    spartan::Game::Load(static_cast<spartan::DefaultWorld>(selected_index));
                }
                else if (selected_index >= 0 && selected_index < static_cast<int>(world_files.size()))
                {
                    // load world file from disk
                    spartan::World::LoadFromFile(world_files[selected_index].file_path);
                }
                visible_world_list = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(button_width, 0)))
            {
                visible_world_list = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Controls", ImVec2(button_width, 0)))
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
        // scan for world files and show list immediately
        scan_for_world_files();
        visible_world_list = true;
        check_assets_outdated_async();
    }
    else
    {
        // ask the user before downloading
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
