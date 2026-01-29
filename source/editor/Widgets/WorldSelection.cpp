/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

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

//= INCLUDES =====================
#include "pch.h"
#include "../GeneralWindows.h"
#include "../ImGui/Source/imgui.h"
#include "../ImGui/ImGui_Extension.h"
#include "FileSystem/FileSystem.h"
#include "Settings.h"
#include "Viewport.h"
#include "Input/Input.h"
#include "Game/Game.h"
#include "Core/ProgressTracker.h"
//================================

/*
//= NAMESPACES =====
using namespace std;
//==================

struct WorldEntry
{
    const char* name;
    const char* description;
    const char* status;       // wip, prototype, complete
    const char* performance;  // light, moderate, demanding
    uint32_t vram;            // min vram requirement in megabytes
};

/**
 * TODO: Ideally this should be loaded from a remote JSON file to allow adding new worlds without updating the source code and
 * making the code easier to read and cleaner.
 #1#
const WorldEntry worlds[] = {
    {.name        = "Car Showroom",
     .description = "Showcase world for YouTubers/Press. Does not use experimental tech",
     .status      = "Complete",
     .performance = "Light",
     .vram        = 2100},
    {.name        = "Car Playground",
     .description = "Highly realistic vehicle physics with proper tire slip, thermals, aero, LSD, multi ray tire, and "
                    "speed dependent steering geometry.",
     .status      = "Prototype",
     .performance = "Light",
     .vram        = 2100},
    {.name        = "Open World Forest",
     .description = "256 million of Ghost of Tsushima grass blades",
     .status      = "Prototype",
     .performance = "Very demanding",
     .vram        = 5600},
    {.name        = "Liminal Space",
     .description = "Shifts your frequency to a nearby reality",
     .status      = "Prototype",
     .performance = "Light",
     .vram        = 2100},
    {.name        = "Sponza 4K",
     .description = "High-resolution textures & meshes",
     .status      = "Complete",
     .performance = "Demanding",
     .vram        = 2600},
    {.name        = "Subway",
     .description = "GI test. No lights, only emissive textures",
     .status      = "Prototype",
     .performance = "Moderate",
     .vram        = 2600},
    {.name = "Minecraft", .description = "Blocky aesthetic", .status = "Complete", .performance = "Light", .vram = 2100},
    {.name = "Basic", .description = "Light, camera, floor", .status = "Complete", .performance = "Light", .vram = 2100}};

// ------------------------------------------

int world_index = 0;

bool downloaded_and_extracted = false;
bool visible_download_prompt  = false;
bool visible_update_prompt    = false;
bool visible_world_list       = false;

// asset download configuration
const char* assets_url = "https://www.dropbox.com/scl/fi/2dsh84c9hokjxv5xmmv4t/assets.7z?rlkey=a88etud443hqddsnkjzbvlwpu&st=rg4ptyos&dl=1";
const char* assets_destination  = "project/assets.7z";
const char* assets_extract_dir  = "project/";
const char* assets_expected_sha = "a11dd5ae80d9bc85541646670f3e69f1ab7e48e4b4430712038f8f4fb1300637";

void check_assets_outdated_async()
{
    // run hash check in background so UI doesn't freeze
    spartan::ThreadPool::AddTask(
        []()
        {
            if (!spartan::FileSystem::Exists(assets_destination)) return;

            std::string local_hash = spartan::FileSystem::ComputeFileSha256(assets_destination);
            if (!local_hash.empty() && local_hash != assets_expected_sha) { visible_update_prompt = true; }
        });
}

void download_and_extract()
{
    visible_download_prompt = false;

    // run download and extract in background
    spartan::ThreadPool::AddTask(
        []()
        {
            // start progress tracking in continuous mode (job_count = 0)
            spartan::Progress& progress = spartan::ProgressTracker::GetProgress(spartan::ProgressType::Download);
            progress.Start(0, "Downloading assets...");
            spartan::ProgressTracker::SetGlobalLoadingState(true);

            // download with real-time progress callback
            bool success = spartan::FileSystem::DownloadFile(
                assets_url, assets_destination,
                [&progress](float download_progress)
                {
                    // download is 0-90%, extraction is 90-100%
                    progress.SetFraction(download_progress * 0.9f);
                });

            if (success)
            {
                progress.SetText("Extracting assets...");
                progress.SetFraction(0.9f);
                success = spartan::FileSystem::ExtractArchive(assets_destination, assets_extract_dir);
                progress.SetFraction(1.0f);
            }

            spartan::ProgressTracker::SetGlobalLoadingState(false);
            if (success) { visible_world_list = true; }
        });
}

void window()
{
    // download prompt - assets don't exist
    if (visible_download_prompt)
    {
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(
                "Default worlds", &visible_download_prompt,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("No default worlds are present. Would you like to download them?");
            ImGui::Separator();

            float button_width = ImGui::CalcTextSize("Download Worlds").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;
            float offset_x     = (ImGui::GetContentRegionAvail().x - button_width) * 0.5f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

            ImGui::BeginGroup();
            {
                if (ImGui::Button("Download Worlds")) { download_and_extract(); }

                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { visible_download_prompt = false; }
            }
            ImGui::EndGroup();
        }
        ImGui::End();
    }

    // update prompt - assets exist but are outdated (checked async)
    if (visible_update_prompt)
    {
        // close world list when update prompt appears
        visible_world_list = false;

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(
                "Update available", &visible_update_prompt,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                    ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextWrapped("A newer version of the assets is available. Would you like to update?");
            ImGui::Separator();

            ImGui::BeginGroup();
            {
                if (ImGui::Button("Update"))
                {
                    visible_update_prompt = false;
                    // delete old assets.7z so it downloads fresh
                    if (spartan::FileSystem::Exists(assets_destination)) { spartan::FileSystem::Delete(assets_destination); }
                    download_and_extract();
                }

                ImGui::SameLine();
                if (ImGui::Button("Skip"))
                {
                    visible_update_prompt = false;
                    visible_world_list    = true;
                }
            }
            ImGui::EndGroup();
        }
        ImGui::End();
    }

    if (visible_world_list)
    {
        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin(
                "World Selection", &visible_world_list,
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (spartan::FileSystem::IsDirectoryEmpty("Project"))
            {
                visible_world_list      = false;
                visible_download_prompt = true;
                ImGui::End();
                return;
            }

            const char* text_prompt  = "Select the world you would like to load.";
            const char* text_warning = "Note: This is a developer build. It is experimental and not guaranteed to behave.";

            ImGui::Text(text_prompt);
            ImGui::Separator();

            // calculate height to fit all world names without scrolling
            float row_height  = ImGui::GetTextLineHeightWithSpacing();
            float list_height = row_height * IM_ARRAYSIZE(worlds) + ImGui::GetStyle().FramePadding.y * 2;

            // layout: left list, right details
            ImGui::BeginChild("left_panel", ImVec2(190, list_height), true);
            {
                for (int i = 0; i < IM_ARRAYSIZE(worlds); i++)
                {
                    if (ImGui::Selectable(worlds[i].name, world_index == i)) { world_index = i; }
                }
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("right_panel", ImVec2(800, list_height), true);
            {
                const WorldEntry& w = worlds[world_index];

                // push full window wrap
                ImGui::PushTextWrapPos(0.0f);
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
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%u MB (System: %u MB)", w.vram, system_vram_mb);
                }
                else { ImGui::TextWrapped("%u MB (System: %u MB)", w.vram, system_vram_mb); }
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
                spartan::Game::Load(static_cast<spartan::DefaultWorld>(world_index));
                visible_world_list = false;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(button_width, 0))) { visible_world_list = false; }
            ImGui::SameLine();
            if (ImGui::Button("Controls", ImVec2(button_width, 0))) { controls::visible = true; }
        }
        ImGui::End();
    }
}
*/
