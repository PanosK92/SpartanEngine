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

//= INCLUDES =====================
#include "pch.h"
#include "Contributors.h"
#include "ImGui/Source/imgui.h"
#include "FileSystem/FileSystem.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Contributors
{
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
        { "Spartan", "Iker Galardi",         "Basque Country", "LinkedIn",  "https://www.linkedin.com/in/iker-galardi/",               "Linux port (WIP)",                                            "N/A" },
        { "Spartan", "Jesse Guerrero",       "United States",  "LinkedIn",  "https://www.linkedin.com/in/jguer",                       "UX improvements",                                             "N/A" },
        { "Spartan", "Konstantinos Benos",   "Greece",         "X",         "https://x.com/deg3x",                                     "First editor facelift",                                       "N/A" },
        { "Spartan", "Nick Polyderopoulos",  "Greece",         "LinkedIn",  "https://www.linkedin.com/in/nick-polyderopoulos-21742397","UX improvements",                                             "N/A" },
        { "Spartan", "Panos Kolyvakis",      "Greece",         "LinkedIn",  "https://www.linkedin.com/in/panos-kolyvakis-66863421a/",  "Water buoyancy improvements",                                 "N/A" },
        { "Spartan", "Tri Tran",             "Belgium",        "LinkedIn",  "https://www.linkedin.com/in/mtrantr/",                    "Screen space shadows (Days Gone)",                            "Starfield" },
        { "Spartan", "Ege",                  "Turkey",         "X",         "https://x.com/egedq",                                     "Second editor facelift and theme support",                    "N/A" },
        { "Spartan", "Sandro Mtchedlidze",   "Georgia",        "Artstation","https://www.artstation.com/sandromch",                    "Provided some assets and artistic direction",                 "N/A" },
        { "Spartan", "Dimitris Kalyvas",     "Greece",         "X",         "https://x.com/punctuator_",                               "UX improvements, volumetric clouds, helping with plan.world", "BeamNG.drive" },
        { "Spartan", "Bryan Casagrande ",    "United States",  "X",         "https://x.com/mrdrelliot",                                "Console variables",                                           "N/A" },
        { "Hoplite", "Apostolos Bouzalas",   "Greece",         "LinkedIn",  "https://www.linkedin.com/in/apostolos-bouzalas",          "A few performance reports",                                   "N/A" },
        { "Hoplite", "Nikolas Pattakos",     "Greece",         "LinkedIn",  "https://www.linkedin.com/in/nikolaspattakos/",            "GCC fixes",                                                   "N/A" },
        { "Hoplite", "Roman Koshchei",       "Ukraine",        "X",         "https://x.com/roman_koshchei",                            "Circular stack (undo/redo)",                                  "N/A" },
        { "Hoplite", "Kristi Kercyku",       "Albania",        "GitHub",    "https://github.com/kristiker",                            "G-buffer depth issue fix",                                    "N/A" },
        { "Hoplite", "Kinjal Kishor",        "India",          "X",         "https://x.com/kinjalkishor",                              "A few testing reports",                                       "N/A" },
        { "Hoplite", "Jose Jiménez López ",  "Spain",          "X",         "https://x.com/kerbehee",                                  "Smoke tests proof of concept",                                "N/A" },
    };

    void RenderTable()
    {
        // reserve space for the legend at the bottom
        float legend_height = ImGui::GetTextLineHeightWithSpacing() * 3.0f;
        float table_height  = ImGui::GetContentRegionAvail().y - legend_height;

        // use SizingFixedFit so columns auto-size to fit their content
        ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("##contributors_table", 6, flags, ImVec2(0.0f, table_height)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Title");
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Country");
            ImGui::TableSetupColumn("Link");
            ImGui::TableSetupColumn("Contribution", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Steam Key");
            ImGui::TableHeadersRow();

            for (const auto& c : contributors)
            {
                ImGui::TableNextRow();

                // column 0: role
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(c.role.c_str());

                // column 1: name
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(c.name.c_str());

                // column 2: country
                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(c.country.c_str());

                // column 3: button
                ImGui::TableSetColumnIndex(3);
                ImGui::PushID(&c);
                if (ImGui::Button(c.button_text.c_str()))
                {
                    spartan::FileSystem::OpenUrl(c.button_url);
                }
                ImGui::PopID();

                // column 4: contribution
                ImGui::TableSetColumnIndex(4);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(c.contribution.c_str());

                // column 5: key
                ImGui::TableSetColumnIndex(5);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(c.steam_key.c_str());
            }
            ImGui::EndTable();
        }

        // title legend
        ImGui::Spacing();
        ImGui::TextDisabled("Spartan: considerable contributor | Hoplite: lightweight contributor");
        ImGui::TextDisabled("These titles are also attributed in Discord.");
    }
}
