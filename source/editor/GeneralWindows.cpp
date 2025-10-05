/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INLUCDES =====================
#include "pch.h"
#include "GeneralWindows.h"
#include "ImGui/Source/imgui.h"
#include "ImGui/ImGui_Extension.h"
#include "FileSystem/FileSystem.h"
#include "Settings.h"
#include "Widgets/Viewport.h"
#include "Input/Input.h"
#include "Game/Game.h"
#include "Core/ProgressTracker.h"
#include "RHI/RHI_Device.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor = nullptr;

    namespace sponsor
    {
        bool visible = true;

        void window()
        {
            if (!visible)
                return;

            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("Support Spartan Engine", &visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::PushItemWidth(500.0f * spartan::Window::GetDpiScale());
                ImGui::Text("I cover the costs for hosting and bandwidth of engine assets");
                ImGui::Text("If you enjoy the simplicity of running a single script, build, run and have everything just work, please consider sponsoring to help keep everything running smoothly!");
                ImGui::PopItemWidth();

                ImGui::Separator();

                if (ImGuiSp::button_centered_on_line("Sponsor"))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/sponsors/PanosK92");
                }
            }
            ImGui::End();
        }
    }

    namespace introduction
    {
        bool visible = true;

        void window()
        {
             if (!visible)
                return;

            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            if (ImGui::Begin("What should you expect", &visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::PushItemWidth(500.0f * spartan::Window::GetDpiScale());
                ImGui::Text("This isn't an engine for the average user, it's designed for advanced research and experimentation, ideal for industry veterans.");
                ImGui::PopItemWidth();

                ImGui::Separator();

                if (ImGuiSp::button_centered_on_line("Ok"))
                {
                    visible = false;
                }
             }

            ImGui::End();
        }
    }

    namespace about
    {
        bool visible = false;

        static const char* license_text =
            "MIT License"
            "\n\n"
            "Copyright(c) 2015-2025 Panos Karabelas"
             "\n\n"
            "Permission is hereby granted, free of charge, to any person obtaining a copy"
            "of this software and associated documentation files (the \"Software\"), to deal"
            "in the Software without restriction, including without limitation the rights"
            "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell"
            "copies of the Software, and to permit persons to whom the Software is"
            "furnished to do so, subject to the following conditions:"
            "\n\n"
            "The above copyright notice and this permission notice shall be included in all"
            "copies or substantial portions of the Software."
            "\n\n"
            "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR"
            "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
            "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE"
            "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER"
            "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
            "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.";

        struct Contributor
        {
            string role;
            string name;
            string country;
            string button_text;
            string button_url;
            string contribution;
            string steam_key;
        };
        
        static const vector<Contributor> contributors =
        {
            { "Spartan", "Iker Galardi",        "Basque Country", "LinkedIn",  "https://www.linkedin.com/in/iker-galardi/",               "Linux port (WIP)",                                                        "N/A" },
            { "Spartan", "Jesse Guerrero",      "United States",  "LinkedIn",  "https://www.linkedin.com/in/jguer",                       "UX updates",                                                              "N/A" },
            { "Spartan", "Konstantinos Benos",  "Greece",         "X",         "https://x.com/deg3x",                                     "Bug fixes & editor theme v2",                                             "N/A" },
            { "Spartan", "Nick Polyderopoulos", "Greece",         "LinkedIn",  "https://www.linkedin.com/in/nick-polyderopoulos-21742397","UX updates",                                                              "N/A" },
            { "Spartan", "Panos Kolyvakis",     "Greece",         "LinkedIn",  "https://www.linkedin.com/in/panos-kolyvakis-66863421a/",  "Water buoyancy improvements",                                             "N/A" },
            { "Spartan", "Tri Tran",            "Belgium",        "LinkedIn",  "https://www.linkedin.com/in/mtrantr/",                    "Screen space shadows (Days Gone)",                                        "Starfield" },
            { "Spartan", "Ege",                 "Turkey",         "X",         "https://x.com/egedq",                                     "Editor theme v3 + save/load themes",                                      "N/A" },
            { "Spartan", "Sandro Mtchedlidze",  "Georgia",        "Artstation","https://www.artstation.com/sandromch",                    "Tonemapper, perf/lighting finds, tubes lights in the car showroom world", "N/A" },
            { "Hoplite", "Apostolos Bouzalas",  "Greece",         "LinkedIn",  "https://www.linkedin.com/in/apostolos-bouzalas",          "A few performance reports",                                               "N/A" },
            { "Hoplite", "Nikolas Pattakos",    "Greece",         "LinkedIn",  "https://www.linkedin.com/in/nikolaspattakos/",            "GCC fixes",                                                               "N/A" },
            { "Hoplite", "Roman Koshchei",      "Ukraine",        "X",         "https://x.com/roman_koshchei",                            "Circular stack (undo/redo)",                                              "N/A" },
            { "Hoplite", "Kristi Kercyku",      "Albania",        "GitHub",    "https://github.com/kristiker",                            "G-buffer depth issue fix",                                                "N/A" },
            { "Hoplite", "Kinjal Kishor",       "India",          "X",         "https://x.com/kinjalkishor",                              "A few testing reports",                                                   "N/A" },
        };

        void personal_details()
        {
            ImGui::BeginGroup();
            {
                // shift text that the buttons and the text align
                static const float y_shift = 6.0f;
            
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                ImGui::Text("Creator");
            
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                if (ImGuiSp::button("Panos Karabelas"))
                {
                    spartan::FileSystem::OpenUrl("https://panoskarabelas.com/");
                }
            
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                if (ImGuiSp::button("GitHub"))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine");
                }
            
                ImGui::SameLine();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                if (ImGuiSp::button("X"))
                {
                    spartan::FileSystem::OpenUrl("https://twitter.com/panoskarabelas1");
                }
            }
            ImGui::EndGroup();
        }

        void contributors_table()
        {
            ImGui::Text("Contributors");
            if (ImGui::BeginTable("##contributors_table", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Title");
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Country");
                ImGui::TableSetupColumn("URL");
                ImGui::TableSetupColumn("Contribution");
                ImGui::TableSetupColumn("Steam Key");
                ImGui::TableHeadersRow();
        
                static const float y_shift = 8.0f;
        
                for (const auto& c : contributors)
                {
                    ImGui::TableNextRow();
        
                    // role
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                    ImGui::TextUnformatted(c.role.c_str());
        
                    // name
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                    ImGui::TextUnformatted(c.name.c_str());
        
                    // country
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                    ImGui::TextUnformatted(c.country.c_str());
        
                    // url button
                    ImGui::TableSetColumnIndex(3);
                    ImGui::PushID(&c); // unique id per row
                    if (ImGui::Button(c.button_text.c_str()))
                    {
                        spartan::FileSystem::OpenUrl(c.button_url);
                    }
                    ImGui::PopID();
        
                    // contribution
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(c.contribution.c_str());
        
                    // steam key
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(c.steam_key.c_str());
                }
                ImGui::EndTable();
            }
        }

        void third_party_libraries()
        {
            ImGui::BeginGroup();
            {
                ImGui::Text("Third party libraries");
                if (ImGui::BeginTable("##third_party_libs_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
                {
                    ImGui::TableSetupColumn("Name", 0, -1.0f);
                    ImGui::TableSetupColumn("Version", 0, -1.0f);
                    ImGui::TableSetupColumn("URL", 0, -1.0f);
                    ImGui::TableHeadersRow();
            
                    for (const spartan::third_party_lib& lib : spartan::Settings::GetThirdPartyLibs())
                    {
                        // switch row
                        ImGui::TableNextRow();
            
                        // shift text down so that it's on the same line with the button
                        static const float y_shift = 8.0f;
            
                        // name
                        ImGui::TableSetColumnIndex(0);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                        ImGui::Text(lib.name.c_str());
            
                        // version
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                        ImGui::Text(lib.version.c_str());
            
                        // url
                        ImGui::TableSetColumnIndex(2);
                        ImGui::PushID(lib.url.c_str());
                        if (ImGuiSp::button("URL"))
                        {
                            spartan::FileSystem::OpenUrl(lib.url);
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndGroup();
        }

        void window()
        {
             if (!visible)
                return;

            ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::Begin(spartan::version::c_str(), &visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
            {
                ImGui::BeginGroup();
                {
                    // my details
                    personal_details();

                    ImGui::Separator();

                    // license
                    float max_width = 500.0f * spartan::Window::GetDpiScale();
                    ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + max_width);
                    ImGui::TextWrapped(license_text);
                    ImGui::PopTextWrapPos();

                    ImGui::SameLine();

                    third_party_libraries();
                }
                ImGui::EndGroup();

                ImGui::Separator();

                contributors_table();
            }
            ImGui::End();
        }
    }

    namespace controls
    {
        bool visible = false;
    
        struct Shortcut
        {
            const char* shortcut;
            const char* description;
        };
    
        static const Shortcut editor_shortcuts[] =
        {
            { "Ctrl+P",       "Toggle this window"         },
            { "Ctrl+S",       "Save world"                 },
            { "Ctrl+L",       "Load world"                 },
            { "Ctrl+Z",       "Undo"                       },
            { "Ctrl+Shift+Z", "Redo"                       },
            { "Alt+Enter",    "Toggle fullscreen viewport" },
            { "F",            "Entity focus"               }
        };
    
        static const Shortcut camera_controls[] =
        {
            { "Left click while holding right click", "Fire physics cube"           },
            { "Right click",                          "Enable first-person control" },
            { "W, A, S, D",                           "Movement"                    },
            { "Q, E",                                 "Elevation"                   },
            { "F",                                    "Flashlight"                  },
            { "Ctrl",                                 "Crouch"                      }
        };

        void show_shortcut_table(const char* label, const Shortcut* shortcuts, size_t count)
        {
            if (ImGui::BeginTable(label, 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
            {
                ImGui::TableSetupColumn("Shortcut", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();
        
                for (size_t i = 0; i < count; i++)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(shortcuts[i].shortcut);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(shortcuts[i].description);
                }
        
                ImGui::EndTable();
            }
        }
        
        void window()
        {
            if (!visible)
                return;
        
            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowFocus();
            ImGui::Begin("Controls", &visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse);
            {
                float table_width = 400.0f;
                float spacing     = ImGui::GetStyle().ItemSpacing.x;
                float available   = ImGui::GetContentRegionAvail().x;
        
                bool side_by_side = available >= (table_width * 2.0f + spacing);
        
                ImGui::BeginGroup();
                ImGui::Text("Editor");
                show_shortcut_table("editor_shortcuts", editor_shortcuts, std::size(editor_shortcuts));
                ImGui::EndGroup();
        
                if (side_by_side)
                    ImGui::SameLine();
        
                ImGui::BeginGroup();
                ImGui::Text("Camera");
                show_shortcut_table("camera_controls", camera_controls, std::size(camera_controls));
                ImGui::EndGroup();
            }
            ImGui::End();
        }
    }

    namespace worlds
    {
        struct WorldEntry
        {
            const char* name;
            const char* description;
            const char* status;      // wip, prototype, complete
            const char* performance; // light, moderate, demanding
            uint32_t vram;           // min vram requirement in megabytes
        };

        const WorldEntry worlds[] =
        {
            { "Car Showroom",      "Showcase world for YouTubers/Press. Does not use experimental tech.", "Complete"      , "Light",          4000 },
            { "Open World Forest", "Millions of Ghost of Tsushima grass blades.",                         "Prototype"     , "Very demanding", 8000 },
            { "Liminal Space",     "Shifts your frequency to a nearby reality.",                          "Prototype"     , "Light",          2048 },
            { "Sponza 4K",         "High-resolution textures & meshes.",                                  "Complete"      , "Demanding",      5000 },
            { "Subway",            "GI test. No lights, only emissive textures.",                         "Complete"      , "Moderate",       5000 },
            { "Minecraft",         "Blocky aesthetic.",                                                   "Complete"      , "Light",          4000 },
            { "Basic",             "Light, camera, floor.",                                               "Complete"      , "Light",          4000 },
            { "Water",             "Light, camera, ocean.",                                               "In Progress...", "?"             , 4000 }
        };
        int world_index = 0;

        bool downloaded_and_extracted = false;
        bool visible_download_prompt  = false;
        bool visible_world_list       = false;

        void world_on_download_finished()
        {
            spartan::ProgressTracker::SetGlobalLoadingState(false);
            visible_world_list = true;
        }

        void download_and_extract()
        {
            spartan::FileSystem::Command("py download_assets.py", world_on_download_finished, false);
            spartan::ProgressTracker::SetGlobalLoadingState(true);
            visible_download_prompt = false;
        }

        void window()
        {
            if (visible_download_prompt)
            {
                ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                if (ImGui::Begin("Default worlds", &visible_download_prompt,
                    ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::TextWrapped("No default worlds are present. Would you like to download them?");

                    bool python_available =
                        spartan::FileSystem::IsExecutableInPath("py") ||
                        spartan::FileSystem::IsExecutableInPath("python") ||
                        spartan::FileSystem::IsExecutableInPath("python3");

                    if (!python_available)
                    {
                        ImGui::Spacing();
                        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f),
                            "Error: Python is not installed or not found in your PATH.\n"
                            "Please install it to enable downloading.");
                    }

                    ImGui::Separator();

                    float button_width = ImGui::CalcTextSize("Download Worlds").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;
                    float offset_x     = (ImGui::GetContentRegionAvail().x - button_width) * 0.5f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);

                    ImGui::BeginGroup();
                    {
                        ImGui::BeginDisabled(!python_available);
                        if (ImGui::Button("Download Worlds"))
                        {
                            download_and_extract();
                        }
                        ImGui::EndDisabled();

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

            if (visible_world_list)
            {
                ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
                if (ImGui::Begin("World Selection", &visible_world_list, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
                {
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
                            if (ImGui::Selectable(worlds[i].name, world_index == i))
                            {
                                world_index = i;
                            }
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
                        else
                        {
                            ImGui::TextWrapped("%u MB (System: %u MB)", w.vram, system_vram_mb);
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
                        spartan::Game::Load(static_cast<spartan::DefaultWorld>(world_index));
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
                        controls::visible = true;
                    }
                }
                ImGui::End();
            }
        }
    }
}

void GeneralWindows::Initialize(Editor* editor_in)
{
    editor = editor_in;

    // the sponsor window only shows up if the editor.ini file doesn't exist, which means that this is the first ever run
    sponsor::visible      = !spartan::FileSystem::Exists(ImGui::GetIO().IniFilename);
    introduction::visible = !spartan::FileSystem::Exists(ImGui::GetIO().IniFilename);

    // world download
    {
        size_t file_count                 = spartan::FileSystem::GetFilesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size();       // assets.7z
        file_count                       += spartan::FileSystem::GetDirectoriesInDirectory(spartan::ResourceCache::GetProjectDirectory()).size(); // extracted folders
        worlds::downloaded_and_extracted  = file_count > 1; // assets.7z + extracted folders

        if (worlds::downloaded_and_extracted)
        {
            worlds::visible_world_list = true;
        }
        else
        {
            if (file_count == 0)
            { 
                worlds::visible_download_prompt = true;
            }
            else // assets.7z is present but not extracted
            {
                worlds::download_and_extract();
            }
        }
    }
}

void GeneralWindows::Tick()
{
    // windows
    {
        worlds::window();
        introduction::window();
        sponsor::window();
        about::window();
        controls::window();
    }

    // shortcuts
    if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::P))
    {
        controls::visible = !controls::visible;
    }
}

bool GeneralWindows::GetVisibilityWorlds()
{
    return worlds::visible_world_list;
}

void GeneralWindows::SetVisibilityWorlds(const bool visibility)
{
    worlds::visible_world_list = visibility;
}

bool* GeneralWindows::GetVisiblityWindowAbout()
{
    return &about::visible;
}

bool* GeneralWindows::GetVisiblityWindowControls()
{
    return &controls::visible;
}
