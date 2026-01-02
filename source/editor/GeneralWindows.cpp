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

    static void center_next_window(Editor* editor)
    {
        ImGui::SetNextWindowPos(
            editor->GetWidget<Viewport>()->GetCenter(),
            ImGuiCond_Appearing, // use appearing so the user can move it if they want
            ImVec2(0.5f, 0.5f)
        );
    }

    namespace sponsor
    {
        bool visible = true;

        void window()
        {
            if (!visible) return;

            center_next_window(editor);

            // Added NoResize to keep the tighter layout control
            if (ImGui::Begin("Support Spartan Engine", &visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
            {
                // Use a slightly larger width for better reading flow
                float content_width = 550.0f * spartan::Window::GetDpiScale();

                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + content_width);
                ImGui::Text("I cover the costs for hosting and bandwidth of engine assets.");
                ImGui::Spacing(); // Breathing room
                ImGui::Text("If you enjoy the simplicity of running a single script, build, run and have everything just work, please consider sponsoring to help keep everything running smoothly!");
                ImGui::PopTextWrapPos();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Make the button prominent
                if (ImGuiSp::button_centered_on_line("Sponsor on GitHub"))
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
            if (!visible) return;

            center_next_window(editor);

            if (ImGui::Begin("Welcome", &visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
            {
                float content_width = 500.0f * spartan::Window::GetDpiScale();

                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + content_width);
                // Visual hierarchy: Icon or emphasis (simulated with text here)
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "[ Note ]");
                ImGui::SameLine();
                ImGui::Text("This isn't an engine for the average user.");

                ImGui::Spacing();
                ImGui::Text("It is designed for advanced research, ideal for game engine and rendering engineers.");
                ImGui::PopTextWrapPos();

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                if (ImGuiSp::button_centered_on_line("I Understand"))
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

        const char* license_text =
            "MIT License"
            "\n\n"
            "Copyright(c) 2015-2026 Panos Karabelas"
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

       const vector<Contributor> contributors =
        {
            { "Spartan", "Iker Galardi",        "Basque Country", "LinkedIn",  "https://www.linkedin.com/in/iker-galardi/",               "Linux port (WIP)",                                                        "N/A" },
            { "Spartan", "Jesse Guerrero",      "United States",  "LinkedIn",  "https://www.linkedin.com/in/jguer",                       "UX updates",                                                              "N/A" },
            { "Spartan", "Konstantinos Benos",  "Greece",         "X",         "https://x.com/deg3x",                                     "Bug fixes & editor theme v2",                                             "N/A" },
            { "Spartan", "Nick Polyderopoulos", "Greece",         "LinkedIn",  "https://www.linkedin.com/in/nick-polyderopoulos-21742397","UX updates",                                                              "N/A" },
            { "Spartan", "Panos Kolyvakis",     "Greece",         "LinkedIn",  "https://www.linkedin.com/in/panos-kolyvakis-66863421a/",  "Water buoyancy improvements",                                             "N/A" },
            { "Spartan", "Tri Tran",            "Belgium",        "LinkedIn",  "https://www.linkedin.com/in/mtrantr/",                    "Screen space shadows (Days Gone)",                                        "Starfield" },
            { "Spartan", "Ege",                 "Turkey",         "X",         "https://x.com/egedq",                                     "Editor theme v3 + save/load themes",                                      "N/A" },
            { "Spartan", "Sandro Mtchedlidze",  "Georgia",        "Artstation","https://www.artstation.com/sandromch",                    "Tonemapper, perf/lighting finds, tubes lights in the car showroom world", "N/A" },
            { "Spartan", "Dimitris Kalyvas",    "Greece",         "X",         "https://x.com/punctuator_",                               "Volumetric clouds, entity multi-select, grass performance improvement",   "BeamNG.drive" },
            { "Spartan", "Bryan Casagrande ",   "United States",  "X",         "https://x.com/mrdrelliot",                                "Implement console variable support",                                      "N/A" },
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
                    spartan::FileSystem::OpenUrl("https://twitter.com/panoskarabelas");
                }
            }
            ImGui::EndGroup();
        }

       void tab_general()
        {
            // --- Top Section: Creator & Links (Fixed Height) ---
            ImGui::Text("Creator");
            ImGui::SameLine();
            ImGui::Spacing();

            ImGui::AlignTextToFramePadding();

            if (ImGuiSp::button("Panos Karabelas"))
                spartan::FileSystem::OpenUrl("https://panoskarabelas.com/");

            ImGui::SameLine();
            if (ImGuiSp::button("GitHub"))
                spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine");

            ImGui::SameLine();
            if (ImGuiSp::button("X"))
                spartan::FileSystem::OpenUrl("https://twitter.com/panoskarabelas");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // --- Bottom Section: License (Dynamic Height) ---
            ImGui::Text("License");

            // Push a darker background color for the text area
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);

            // ImVec2(0.0f, -FLT_MIN) tells ImGui:
            // X = 0.0f      -> "Use all available width"
            // Y = -FLT_MIN  -> "Use all remaining vertical space"
            if (ImGui::BeginChild("license_scroll", ImVec2(0.0f, -FLT_MIN), true))
            {
                // Use GetContentRegionAvail().x to ensure text wraps *before* hitting the scrollbar
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + ImGui::GetContentRegionAvail().x);
                ImGui::TextUnformatted(license_text);
                ImGui::PopTextWrapPos();
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        void tab_contributors()
        {
            // Use StretchProp so columns resize nicely with the window
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;

            // Subtract a bit of height to account for tab bar
            if (ImGui::BeginTable("##contributors_table", 6, flags, ImVec2(0.0f, -FLT_MIN)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);

                // Defining weights allows for better default sizing
                ImGui::TableSetupColumn("Title", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Country");
                ImGui::TableSetupColumn("Link", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Contribution", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Steam Key");
                ImGui::TableHeadersRow();

                for (const auto& c : contributors)
                {
                    ImGui::TableNextRow();

                    // Column 0: Role
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding(); // Fix vertical alignment
                    ImGui::TextUnformatted(c.role.c_str());

                    // Column 1: Name
                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(c.name.c_str());

                    // Column 2: Country
                    ImGui::TableSetColumnIndex(2);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(c.country.c_str());

                    // Column 3: Button
                    ImGui::TableSetColumnIndex(3);
                    ImGui::PushID(&c);
                    // Use small button style if available, or standard
                    if (ImGui::Button(c.button_text.c_str()))
                    {
                        spartan::FileSystem::OpenUrl(c.button_url);
                    }
                    ImGui::PopID();

                    // Column 4: Contribution
                    ImGui::TableSetColumnIndex(4);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(c.contribution.c_str());

                    // Column 5: Key
                    ImGui::TableSetColumnIndex(5);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(c.steam_key.c_str());
                }
                ImGui::EndTable();
            }
        }

        void tab_libraries()
        {
            ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;

            if (ImGui::BeginTable("##third_party_libs_table", 3, flags, ImVec2(0.0f, -FLT_MIN)))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Version", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("Link", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                ImGui::TableHeadersRow();

                for (const spartan::third_party_lib& lib : spartan::Settings::GetThirdPartyLibs())
                {
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(lib.name.c_str());

                    ImGui::TableSetColumnIndex(1);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(lib.version.c_str());

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

        void window()
        {
            if (!visible) return;

            center_next_window(editor);

            // fixed size for the About window to prevent it jumping around when switching tabs
            ImGui::SetNextWindowSize(ImVec2(800.0f * spartan::Window::GetDpiScale(), 500.0f * spartan::Window::GetDpiScale()), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("About Spartan Engine", &visible, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse))
            {
                if (ImGui::BeginTabBar("##about_tabs"))
                {
                    if (ImGui::BeginTabItem("General"))
                    {
                        ImGui::Spacing();
                        tab_general();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Contributors"))
                    {
                        tab_contributors();
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Third Party"))
                    {
                        tab_libraries();
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
            }
            ImGui::End();
        }
    }

    namespace controls
    {
        bool visible = false;

        struct Shortcut
        {
            const char* keys;        // stored as "Ctrl+S"
            const char* description;
        };

        static const Shortcut editor_shortcuts[] =
        {
            { "Ctrl+P",       "Toggle this window"       },
            { "Ctrl+S",       "Save world"               },
            { "Ctrl+L",       "Load world"               },
            { "Ctrl+Z",       "Undo"                     },
            { "Ctrl+Shift+Z", "Redo"                     },
            { "Alt+Enter",    "Toggle fullscreen"        },
            { "F",            "Focus on entity"          }
        };

        static const Shortcut camera_controls[] =
        {
            { "Hold R-Click", "Enable First Person"      },
            { "W, A, S, D",   "Movement"                 },
            { "Q, E",         "Elevation (Up/Down)"      },
            { "Ctrl",         "Crouch"                   },
            { "Shift",        "Sprint / Fast Move"       },
            { "F",            "Toggle Flashlight"        },
            { "L-Click",      "Shoot physics cube"       }
        };

        // helper to render "Ctrl" + "S" as distinct visual styling elements
        void render_key_combo(const char* key_string)
        {
            std::string s = key_string;
            std::string delimiter = "+";
            size_t pos = 0;
            std::string token;

            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(2.0f, 0.0f));

            // We use a local index to differentiate keys within the same combo (just in case)
            int key_part_index = 0;
            bool first = true;

            while ((pos = s.find(delimiter)) != std::string::npos)
            {
                token = s.substr(0, pos);
                if (!first) { ImGui::SameLine(); ImGui::TextDisabled("+"); ImGui::SameLine(); }

                // PushID ensures that this "Ctrl" button is unique from others
                ImGui::PushID(key_part_index++);
                ImGui::SmallButton(token.c_str());
                ImGui::PopID();

                s.erase(0, pos + delimiter.length());
                first = false;
            }

            if (!first) { ImGui::SameLine(); ImGui::TextDisabled("+"); ImGui::SameLine(); }

            ImGui::PushID(key_part_index++);
            ImGui::SmallButton(s.c_str());
            ImGui::PopID();

            ImGui::PopStyleVar();
        }

        void show_shortcut_table(const char* str_id, const Shortcut* shortcuts, size_t count)
        {
            ImGuiTableFlags flags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY;

            if (ImGui::BeginTable(str_id, 2, flags, ImVec2(0.0f, -FLT_MIN)))
            {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Key Combination", ImGuiTableColumnFlags_WidthFixed, 180.0f * spartan::Window::GetDpiScale());

                for (size_t i = 0; i < count; i++)
                {
                    ImGui::TableNextRow();

                    // Push the row index as an ID
                    // This makes "Ctrl" in Row 0 distinct from "Ctrl" in Row 1
                    ImGui::PushID((int)i);

                    // Column 1: Description
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(shortcuts[i].description);

                    // Column 2: Keys
                    ImGui::TableSetColumnIndex(1);
                    render_key_combo(shortcuts[i].keys);

                    ImGui::PopID(); // Don't forget to Pop!
                }

                ImGui::EndTable();
            }
        }
        void window()
        {
            if (!visible)
                return;

            // Center the window on first use, but let user move it freely afterwards
            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

            // Set a reasonable default size
            ImGui::SetNextWindowSize(ImVec2(500.0f * spartan::Window::GetDpiScale(), 350.0f * spartan::Window::GetDpiScale()), ImGuiCond_FirstUseEver);

            if (ImGui::Begin("Controls & Shortcuts", &visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
            {
                if (ImGui::BeginTabBar("##controls_tabs"))
                {
                    if (ImGui::BeginTabItem("Editor Shortcuts"))
                    {
                        ImGui::Spacing();
                        show_shortcut_table("##editor_shortcuts_table", editor_shortcuts, std::size(editor_shortcuts));
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Camera Controls"))
                    {
                        ImGui::Spacing();
                        show_shortcut_table("##camera_controls_table", camera_controls, std::size(camera_controls));
                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }
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
            { "Car Showroom",      "Showcase world for YouTubers/Press. Does not use experimental tech", "Complete" , "Light",          2100 },
            { "Open World Forest", "256 million of Ghost of Tsushima grass blades",                      "Prototype", "Very demanding", 5600 },
            { "Liminal Space",     "Shifts your frequency to a nearby reality",                          "Prototype", "Light",          2100 },
            { "Sponza 4K",         "High-resolution textures & meshes",                                  "Complete" , "Demanding",      2600 },
            { "Subway",            "GI test. No lights, only emissive textures",                         "Complete" , "Moderate",       2600 },
            { "Minecraft",         "Blocky aesthetic",                                                   "Complete" , "Light",          2100 },
            { "Basic",             "Light, camera, floor",                                               "Complete" , "Light",          2100 }
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
