#include "pch.h"
#include "ScriptEditor.h"
#include "ImGui/TextEditor.h"
#include "World/Entity.h"
#include "World/World.h"
#include "World/Components/Script.h"

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

    m_text_editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
    m_text_editor.SetReadOnly(false);
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
                    std::memset(m_buffer, 0, kBufferSize);
                    strncpy_s(m_buffer, kBufferSize, script_contents.c_str(), script_contents.size() < kBufferSize ? script_contents.size() : kBufferSize - 1);

                    m_text_editor.SetText(script_contents);
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

    m_text_editor.SetReadOnly(false);
    m_text_editor.Render("Script Editor", ImGui::GetContentRegionAvail(), true);
}
