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
#include "ShaderEditor.h"
#include <fstream>
#include "RHI/RHI_Shader.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/ImGui_Style.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan;
using namespace spartan::math;
//============================

namespace
{
    const char* shader_stage_name(const RHI_Shader_Type stage)
    {
        switch (stage)
        {
        case RHI_Shader_Type::Vertex:  return "vertex";
        case RHI_Shader_Type::Pixel:   return "pixel";
        case RHI_Shader_Type::Compute: return "compute";
        case RHI_Shader_Type::Domain:  return "domain";
        case RHI_Shader_Type::Hull:    return "hull";
        default:                       return "unknown";
        }
    }

    string shader_display_name(RHI_Shader* shader)
    {
        string name = shader->GetObjectName() + "  " + shader_stage_name(shader->GetShaderStage());
        for (const auto& define : shader->GetDefines())
        {
            if (define.second != "0")
            {
                name += "  " + define.first;
            }
        }
        return name;
    }
}

ShaderEditor::ShaderEditor(Editor* editor) : Widget(editor)
{
    m_title           = "Shader Editor";
    m_flags           = ImGuiWindowFlags_NoScrollbar;
    m_visible         = false;
    m_alpha           = 1.0f;
    m_index_displayed = -1;
}

void ShaderEditor::OnTickVisible()
{
    GetShaderInstances();
    if (m_first_run && !m_shaders.empty())
    {
        SelectShader(m_shaders.front(), shader_display_name(m_shaders.front()));
        m_first_run = false;
    }

    ShowControls();
    ImGui::Separator();

    const float dpi = Window::GetDpiScale();
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float list_width = min(clamp(available.x * 0.3f, 220.0f * dpi, 340.0f * dpi), available.x * 0.42f);
    const float source_width = max(1.0f, available.x - list_width - ImGui::GetStyle().ItemSpacing.x);

    ShowShaderSource(source_width, available.y);
    ImGui::SameLine();
    ShowShaderList(list_width, available.y);
}

void ShaderEditor::ShowShaderSource(const float width, const float height)
{
    if (ImGui::BeginChild("##shader_editor_source", ImVec2(width, height), true, ImGuiWindowFlags_NoScrollbar))
    {
        if (m_shader)
        {
            if (Editor::font_bold)
            {
                ImGui::PushFont(Editor::font_bold, 0.0f);
            }
            ImGui::TextUnformatted(m_shader_name.c_str());
            if (Editor::font_bold)
            {
                ImGui::PopFont();
            }
            ImGui::TextDisabled("%s", m_shader->GetFilePath().c_str());
            ImGuiSp::tooltip(m_shader->GetFilePath().c_str());

            if (ImGui::BeginTabBar("##shader_editor_tab_bar", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyShrink))
            {
                const std::vector<std::string>& names   = m_shader->GetNames();
                const std::vector<std::string>& sources = m_shader->GetSources();

                const uint32_t source_count = min(static_cast<uint32_t>(names.size()), static_cast<uint32_t>(sources.size()));
                for (uint32_t i = 0; i < source_count; i++)
                {
                    if (ImGui::BeginTabItem(names[i].c_str()))
                    {
                        if (m_index_displayed != static_cast<int32_t>(i))
                        {
                            m_text_editor.SetText(sources[i]);
                            m_index_displayed = static_cast<int32_t>(i);
                        }

                        const float status_height = ImGui::GetTextLineHeightWithSpacing();
                        m_text_editor.Render("##shader_source", ImVec2(-FLT_MIN, max(1.0f, ImGui::GetContentRegionAvail().y - status_height)), false);
                        if (m_text_editor.IsTextChanged())
                        {
                            m_shader->SetSource(i, m_text_editor.GetText());
                            m_source_dirty = true;
                        }

                        const TextEditor::Coordinates cursor = m_text_editor.GetCursorPosition();
                        ImGui::TextDisabled("Ln %d, Col %d  |  %d lines", cursor.mLine + 1, cursor.mColumn + 1, m_text_editor.GetTotalLines());
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }
        else
        {
            const char* message = "Select a shader to view its source";
            const ImVec2 message_size = ImGui::CalcTextSize(message);
            ImGui::SetCursorPos(ImVec2(max(ImGui::GetCursorPosX(), (width - message_size.x) * 0.5f), max(ImGui::GetCursorPosY(), height * 0.42f)));
            ImGui::TextDisabled("%s", message);
        }
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowShaderList(const float width, const float height)
{
    if (ImGui::BeginChild("##shader_editor_list", ImVec2(width, height), true))
    {
        uint32_t filtered_count = 0;
        for (RHI_Shader* shader : m_shaders)
        {
            const string name = shader_display_name(shader);
            filtered_count += m_shader_filter.PassFilter(name.c_str()) ? 1 : 0;
        }

        const string count_text = m_shader_filter.IsActive() ? to_string(filtered_count) + " of " + to_string(m_shaders.size()) : to_string(m_shaders.size()) + (m_shaders.size() == 1 ? " shader" : " shaders");
        ImGui::TextUnformatted("Shaders");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", count_text.c_str());

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
        if (ImGui::InputTextWithHint("##shader_filter", "Search shaders", m_shader_filter.InputBuf, IM_ARRAYSIZE(m_shader_filter.InputBuf), ImGuiInputTextFlags_EscapeClearsAll))
        {
            m_shader_filter.Build();
        }
        ImGui::Separator();

        uint32_t visible_count = 0;
        for (RHI_Shader* shader : m_shaders)
        {
            const string name = shader_display_name(shader);
            if (!m_shader_filter.PassFilter(name.c_str()))
            {
                continue;
            }

            visible_count++;
            ImGui::PushID(shader);
            const RHI_ShaderCompilationState state = shader->GetCompilationState();
            if (state == RHI_ShaderCompilationState::Failed)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_error);
            }
            else if (state == RHI_ShaderCompilationState::Compiling)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_warning);
            }
            if (ImGui::Selectable(name.c_str(), m_shader == shader))
            {
                SelectShader(shader, name);
            }
            if (state == RHI_ShaderCompilationState::Failed || state == RHI_ShaderCompilationState::Compiling)
            {
                ImGui::PopStyleColor();
            }
            const string tooltip = string(state == RHI_ShaderCompilationState::Failed ? "Compilation failed\n" : state == RHI_ShaderCompilationState::Compiling ? "Compiling\n" : "") + shader->GetFilePath();
            ImGuiSp::tooltip(tooltip.c_str());
            ImGui::PopID();
        }

        if (visible_count == 0)
        {
            const char* message = m_shaders.empty() ? "No shaders available" : "No shaders match your search";
            const ImVec2 message_size = ImGui::CalcTextSize(message);
            ImGui::SetCursorPosX(max(ImGui::GetCursorPosX(), (width - message_size.x) * 0.5f));
            ImGui::Dummy(ImVec2(0.0f, 24.0f * Window::GetDpiScale()));
            ImGui::TextDisabled("%s", message);
        }
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowControls()
{
    ImGui::BeginDisabled(!m_shader || m_index_displayed == -1);
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_Tooltip);
    if (ImGuiSp::button(m_source_dirty ? "Compile *" : "Compile"))
    {
        SaveAndCompile();
    }
    ImGui::EndDisabled();
    ImGuiSp::tooltip("Save and compile, Ctrl+S");

    ImGui::SameLine();
    ImGui::TextDisabled("Window opacity");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f * Window::GetDpiScale());
    ImGui::SliderFloat("##window_opacity", &m_alpha, 0.1f, 1.0f, "%.1f");
}

void ShaderEditor::SelectShader(RHI_Shader* shader, const string& name)
{
    if (m_shader == shader)
    {
        return;
    }

    m_shader          = shader;
    m_shader_name     = name;
    m_index_displayed = -1;
    m_source_dirty    = false;
    m_shader->LoadFromDrive(m_shader->GetFilePath());
}

void ShaderEditor::SaveAndCompile()
{
    if (!m_shader || m_index_displayed == -1)
    {
        return;
    }

    const vector<string>& file_paths = m_shader->GetFilePaths();
    const vector<string>& sources = m_shader->GetSources();
    const uint32_t source_count = min(static_cast<uint32_t>(file_paths.size()), static_cast<uint32_t>(sources.size()));
    bool all_sources_saved = source_count != 0;
    for (uint32_t i = 0; i < source_count; i++)
    {
        ofstream out(file_paths[i], ios::binary | ios::trunc);
        if (!out)
        {
            SP_LOG_ERROR("Failed to open shader source for writing: %s", file_paths[i].c_str());
            all_sources_saved = false;
            continue;
        }
        out.write(sources[i].data(), static_cast<streamsize>(sources[i].size()));
        if (!out)
        {
            SP_LOG_ERROR("Failed to write shader source: %s", file_paths[i].c_str());
            all_sources_saved = false;
        }
    }

    if (!all_sources_saved)
    {
        return;
    }

    const bool async = false;
    m_shader->Compile(m_shader->GetShaderStage(), m_shader->GetFilePath(), async);
    m_source_dirty = false;
}

void ShaderEditor::GetShaderInstances()
{
    auto shaders = Renderer::GetShaders();
    m_shaders.clear();
    for (const shared_ptr<RHI_Shader>& shader : shaders)
    {
        if (shader)
        {
            m_shaders.emplace_back(shader.get());
        }
    }

    // order them alphabetically
    sort(m_shaders.begin(), m_shaders.end(), [](RHI_Shader* a, RHI_Shader* b) { return a->GetObjectName() < b->GetObjectName(); });

    if (m_shader && find(m_shaders.begin(), m_shaders.end(), m_shader) == m_shaders.end())
    {
        m_shader = nullptr;
        m_shader_name = "N/A";
        m_index_displayed = -1;
        m_source_dirty = false;
    }
}
