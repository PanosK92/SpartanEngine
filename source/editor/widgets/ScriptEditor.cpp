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

#include "pch.h"
#include <filesystem>
#include "ScriptEditor.h"
#include "imgui/TextEditor.h"
#include "world/Entity.h"
#include "world/World.h"
#include "world/components/Script.h"

using namespace spartan;

namespace
{
    constexpr float source_pane_vertical_split_percentage = 0.7f;
    constexpr float source_pane_bottom_margin             = 30.0f;
}

ScriptEditor::ScriptEditor(Editor* editor)
    :Widget(editor)
{
    m_title           = "Script Editor";
    m_flags           = ImGuiWindowFlags_NoScrollbar;
    m_visible         = false;
    m_alpha           = 1.0f;

    TextEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    TextEditor.SetReadOnly(false);
}

void ScriptEditor::OnTickVisible()
{
    ImVec2 content_region = ImGui::GetContentRegionAvail();
    ImVec2 size           = ImVec2(content_region.x * 0.25f, content_region.y);

    if (ImGui::Button("Reload"))
    {
        for (Entity* Entity : World::GetEntities())
        {
            if (Script* script = Entity->GetComponent<Script>())
            {
                if (script->file_path != script_file)
                {
                    continue;
                }

                script->LoadScriptFile(script->file_path);
            }
        }
    }

    ImGui::SameLine();

    if (ImGui::Button("Open"))
    {
        FileSystem::OpenUrl(script_file);
    }

    ImGui::SameLine();

    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "VSCode with sumneko's Lua extension is the preferred lua-editor");


    if (ImGui::BeginChild("##script_selector_source", size, ImGuiChildFlags_Borders))
    {
        for (const auto& directory : std::filesystem::recursive_directory_iterator(std::filesystem::current_path()))
        {
            if (!directory.is_directory() && directory.path().extension() == ".lua")
            {
                if (ImGui::Selectable(directory.path().stem().string().c_str()))
                {
                    script_file = directory.path().string();
                    std::string script_contents;
                    FileSystem::ReadFile(script_file, script_contents);
                    std::memset(m_buffer, 0, buffer_size);
                    strncpy_s(m_buffer, buffer_size, script_contents.c_str(), script_contents.size() < buffer_size ? script_contents.size() : buffer_size - 1);

                    TextEditor.SetText(script_contents);
                }
            }
        }
    }


    ImGui::EndChild();

    ImGui::SameLine();

    if (!FileSystem::Exists(script_file))
    {
        return;
    }

    TextEditor.SetReadOnly(false);
    TextEditor.Render("Script Editor", ImGui::GetContentRegionAvail(), true);
}
