/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "GeneralWindows.h"
#include "ImGui/Source/imgui.h"
#include "ImGui/ImGui_Extension.h"
#include "FileSystem/FileSystem.h"
#include "Settings.h"
#include "Widgets/Viewport.h"
#include "Input/Input.h"
#include "Game/Game.h"
#include "Core/ProgressTracker.h"
#include <sstream>
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor = nullptr;

    namespace sponsor
    {
        bool visible      = true;
        const float width = 512.0f;

        void window()
        {
            ImGui::SetNextWindowSize(ImVec2(width, width * 0.52f));
            if (ImGui::Begin("Support Spartan Engine", &visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking))
            {
                ImGui::SetNextItemWidth(width);
                ImGui::TextWrapped(
                    "I cover the costs for Dropbox hosting and a GitHub Pro subscription for benefits like assets and package bandwidth."
                    ""
                    "If you enjoy the simplicity of running a single script and have everything just work, please consider sponsoring to help keep everything running smoothly!"
                );
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
        bool visible      = true;
        const float width = 512.0f;

        void window()
        {
            ImGui::SetNextWindowSize(ImVec2(width, width * 0.36f));
            if (ImGui::Begin("What should you expect", &visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking))
            {
                ImGui::TextWrapped("This isn't an engine for the average user, it's designed for advanced research and experimentation, ideal for industry veterans.");
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

        vector<string> contributors =
        {
            // role,  name,                country,       button text,   button url,                                               contribution,                                                    steam key
            "Spartan, Iker Galardi,        Basque Country,   LinkedIn,   https://www.linkedin.com/in/iker-galardi/,                Linux port (WIP),                                                N/A",
            "Spartan, Jesse Guerrero,      United States,    LinkedIn,   https://www.linkedin.com/in/jguer,                        UX improvements,                                                 N/A",
            "Spartan, Konstantinos Benos,  Greece,           X,          https://twitter.com/deg3x,                                Bug fixes & editor theme improvements,                           N/A",
            "Spartan, Nick Polyderopoulos, Greece,           LinkedIn,   https://www.linkedin.com/in/nick-polyderopoulos-21742397, UX improvements,                                                 N/A",
            "Spartan, Panos Kolyvakis,     Greece,           LinkedIn,   https://www.linkedin.com/in/panos-kolyvakis-66863421a/,   Improved water buoyancy,                                         N/A",
            "Spartan, Tri Tran,            Belgium,          LinkedIn,   https://www.linkedin.com/in/mtrantr/,                     Days Gone screen space shadows,                                  Starfield",
            "Spartan, Ege,                 Turkey,           X,          https://x.com/egedq,                                      Editor theme & ability to save/load themes,                      N/A",
            "Hoplite, Apostolos Bouzalas,  Greece,           LinkedIn,   https://www.linkedin.com/in/apostolos-bouzalas,           Provided performance reports,                                    N/A",
            "Hoplite, Nikolas Pattakos,    Greece,           LinkedIn,   https://www.linkedin.com/in/nikolaspattakos/,             GCC compile fixes,                                               N/A",
            "Hoplite, Sandro Mtchedlidze,  Georgia,          Artstation, https://www.artstation.com/sandromch,                     Added Nautilus tonemapper & spotted lighting/performance issues, N/A",
            "Hoplite, Roman Koshchei,      Ukraine,          X,          https://x.com/roman_koshchei,                             Circular stack for the undo/redo system,                         N/A",
            "Hoplite, Kristi Kercyku,      Albania,          GitHub,     https://github.com/kristiker,                             Identified g-buffer depth testing issue,                         N/A",
            "Hoplite, Kinjal Kishor,       India,            X,          https://x.com/kinjalkishor,                               Supported with testing & technical issues,                       N/A",
            //"Patron,  Kiss Tibor,          Hungary,          GitHub,     https://github.com/kisstp2006,                            GitHub Sponsor,                                                  N/A"
        };

        vector<string> comma_seperate_contributors(const vector<string>& contributors)
        {
            vector<string> result;
        
            for (const auto& entry : contributors)
            {
                string processed_entry;
                bool space_allowed = true;
                for (char c : entry)
                {
                    if (c == ',')
                    {
                        processed_entry.push_back(c);
                        space_allowed = false;
                    }
                    else if (!space_allowed && c != ' ')
                    {
                        space_allowed = true;
                        processed_entry.push_back(c);
                    }
                    else if (space_allowed)
                    {
                        processed_entry.push_back(c);
                    }
                }
        
                istringstream ss(processed_entry);
                string item;
                while (getline(ss, item, ','))
                {
                    result.push_back(item);
                }
            }
        
            return result;
        }
    
        void window()
        {
            static vector<string> comma_seperated_contributors = comma_seperate_contributors(contributors);
        
            static const string window_title  = "Spartan " + to_string(sp_info::version_major) + "." + to_string(sp_info::version_minor) + "." + to_string(sp_info::version_revision);
            const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowFocus();
            ImGui::Begin(window_title.c_str(), &visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
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
        
                // group: license
                ImGui::BeginGroup();
                ImGui::Separator();
                ImGui::TextWrapped(license_text);
                ImGui::EndGroup();
        
                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));
        
                // group: contributors
                ImGui::SameLine();
                ImGui::BeginGroup();
                {
                ImGui::Text("Contributors");
                if (ImGui::BeginTable("##contributors_table", 6, table_flags, ImVec2(ImGui::GetContentRegionAvail().x * 0.74f, 0.0f)))
                {
                    ImGui::TableSetupColumn("Title");
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("Country");
                    ImGui::TableSetupColumn("URL");
                    ImGui::TableSetupColumn("Contribution");
                    ImGui::TableSetupColumn("Steam Key");
                    ImGui::TableHeadersRow();
    
                    uint32_t index = 0;
                    for (uint32_t i = 0; i < static_cast<uint32_t>(contributors.size()); i++)
                    {
                        // switch row
                        ImGui::TableNextRow();
    
                        // shift text down so that it's on the same line with the button
                        static const float y_shift = 8.0f;
    
                        // role
                        ImGui::TableSetColumnIndex(0);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                        ImGui::Text(comma_seperated_contributors[index++].c_str());
    
                        // name
                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                        ImGui::Text(comma_seperated_contributors[index++].c_str());
    
                        // country
                        ImGui::TableSetColumnIndex(2);
                        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                        ImGui::Text(comma_seperated_contributors[index++].c_str());
    
                        // button (url)
                        ImGui::TableSetColumnIndex(3);
                        string& button_text = comma_seperated_contributors[index++];
                        string& button_url  = comma_seperated_contributors[index++];

                        // calculate center position for the button
                        float cell_width    = ImGui::GetColumnWidth();
                        float button_width  = ImGui::CalcTextSize(button_text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                        float button_offset = (cell_width - button_width) * 0.5f;

                        // set cursor position to center the button
                        ImGui::PushID(static_cast<uint32_t>(ImGui::GetCursorScreenPos().y));
                        if (ImGui::Button(button_text.c_str()))
                        {
                            spartan::FileSystem::OpenUrl(button_url);
                        }
                        ImGui::PopID();
    
                        // contribution
                        ImGui::TableSetColumnIndex(4);
                        ImGui::Text(comma_seperated_contributors[index++].c_str());
    
                        // steam key award
                        ImGui::TableSetColumnIndex(5);
                        ImGui::Text(comma_seperated_contributors[index++].c_str());
                    }
                }
                ImGui::EndTable();
    
                // group: role explanations
                ImGui::BeginGroup();
                {
                    ImGui::Text("Roles");
    
                    // create a table with 3 columns
                    ImGui::BeginTable("RoleDescriptions", 3, ImGuiTableFlags_Borders, ImVec2(ImGui::GetContentRegionAvail().x * 0.725f, 0.0f));
                    ImGui::TableSetupColumn("Name");
                    ImGui::TableSetupColumn("In Ancient Sparta");
                    ImGui::TableSetupColumn("In The Engine");
                    ImGui::TableHeadersRow();
    
                    // add rows to the table
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("Spartan"); ImGui::TableNextColumn();
                    ImGui::Text("Elite warriors of Sparta, known for their strength and discipline."); ImGui::TableNextColumn();
                    ImGui::Text("Considerable contributions.");
                    
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("Hoplite"); ImGui::TableNextColumn();
                    ImGui::Text("Armed citizens and infantry soldiers, typically equipped with a spear and shield."); ImGui::TableNextColumn();
                    ImGui::Text("Minor contributions.");
                    
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("Patron"); ImGui::TableNextColumn();
                    ImGui::Text("Financial supporters and benefactors who provide resources for the community."); ImGui::TableNextColumn();
                    ImGui::Text("Monetary contributions.");
                    
                    ImGui::EndTable();
                }
                ImGui::EndGroup();
            }
                ImGui::EndGroup();
        
                // group: third party libraries
                ImGui::SameLine();
                ImGui::BeginGroup();
                {
                    ImGui::Text("Third party libraries");
                    if (ImGui::BeginTable("##third_party_libs_table", 3, table_flags))
                    {
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Version");
                        ImGui::TableSetupColumn("URL");
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
            ImGui::End();
        }
    }

    namespace shortcuts
    {
        bool visible = false;

        struct Shortcut
        {
            char* shortcut;
            char* usage;
        };

        static const Shortcut shortcuts[] =
        {
            {(char*)"Ctrl+P",       (char*)"Open shortcuts & input reference window"},
            {(char*)"Ctrl+S",       (char*)"Save world"},
            {(char*)"Ctrl+L",       (char*)"Load world"},
            {(char*)"Right click",  (char*)"Enable first person camera control"},
            {(char*)"W, A, S, D",   (char*)"Move camera"},
            {(char*)"Q, E",         (char*)"Change camera elevation"},
            {(char*)"F",            (char*)"Center camera on object"},
            {(char*)"Alt+Enter",    (char*)"Toggle fullscreen viewport"},
            {(char*)"Ctrl+Z",       (char*)"Undo"},
            {(char*)"Ctrl+Shift+Z", (char*)"Redo"}
        };

        void window()
        {
            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowFocus();
            ImGui::Begin("Shortcuts & Input Reference", &visible, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
            {
                const float col_a = 220.0f;
                const float col_b = 20.0f;

                {
                    ImGui::NewLine();
                    ImGui::SameLine(col_b);
                    ImGui::Text("Shortcut");
                    ImGui::SameLine(col_a);
                    ImGui::Text("Usage");

                    for (const Shortcut& shortcut : shortcuts)
                    {
                        ImGui::BulletText(shortcut.shortcut);
                        ImGui::SameLine(col_a);
                        ImGui::Text(shortcut.usage);
                    }
                }
            }
            ImGui::End();
        }
    }

    namespace worlds
    {
        const char* world_names[] =
        {
            "1. Objects",
            "2. Doom",
            "3. Minecraft",
            "4. Living Room",
            "5. Subway",
            "6. Sponza 4K (demanding)",
            "7. Bistro Exterior & Interior (demanding)",
            "8. Forest Car (demanding)",
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
             spartan::FileSystem::Command("python download_assets.py", world_on_download_finished, false);
             spartan::ProgressTracker::SetGlobalLoadingState(true);
             visible_download_prompt = false;
        }

        void window()
        {
            if (visible_download_prompt)
            {
                ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSize(ImVec2(300, 150), ImGuiCond_FirstUseEver);
                if (ImGui::Begin("Default worlds", &visible_download_prompt, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("No default worlds are present. Would you like to download them?");
                    ImGui::Separator();
            
                    // calculate the offset to center the group
                    float button_width = ImGui::CalcTextSize("Yes").x + ImGui::CalcTextSize("No").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;
                    float offset_x     = (ImGui::GetContentRegionAvail().x - button_width) * 0.5f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset_x);
            
                    // group yes and no buttons
                    ImGui::BeginGroup();
                    {
                        if (ImGui::Button("Yes"))
                        {
                            download_and_extract();
                        }
            
                        ImGui::SameLine();
            
                        if (ImGui::Button("No"))
                        {
                            visible_download_prompt = false;
                            visible_world_list      = false;
                        }
                    }
                    ImGui::EndGroup();
                }
                ImGui::End();
            }

            if (visible_world_list)
            {
                ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                if (ImGui::Begin("World selection", &visible_world_list, ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize))
                {
                    ImGui::Text("Select the world you would like to load and click \"Ok\"");
            
                    // list
                    ImGui::PushItemWidth(500.0f * spartan::Window::GetDpiScale());
                    ImGui::ListBox("##list_box", &world_index, world_names, IM_ARRAYSIZE(world_names), IM_ARRAYSIZE(world_names));
                    ImGui::PopItemWidth();
            
                    // button
                    if (ImGuiSp::button_centered_on_line("Ok"))
                    {
                        spartan::Game::Load(static_cast<spartan::DefaultWorld>(world_index));
                        visible_world_list = false;
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
        if (sponsor::visible)
        { 
            sponsor::window();
        }

        if (introduction::visible)
        { 
            introduction::window();
        }

        if (about::visible)
        {
            about::window();
        }

        if (shortcuts::visible)
        {
            shortcuts::window();
        }

        worlds::window();
    }

    // shortcuts
    {
        if (spartan::Input::GetKey(spartan::KeyCode::Ctrl_Left) && spartan::Input::GetKeyDown(spartan::KeyCode::P))
        {
            shortcuts::visible = !shortcuts::visible;
        }
    }
}

bool* GeneralWindows::GetVisiblityWindowAbout()
{
    return &about::visible;
}

bool* GeneralWindows::GetVisiblityWindowShortcuts()
{
    return &shortcuts::visible;
}
