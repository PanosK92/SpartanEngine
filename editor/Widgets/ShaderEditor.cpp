/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "ShaderEditor.h"
#include <fstream>
#include "RHI/RHI_Shader.h"
#include "../ImGui/ImGuiExtension.h"
//==================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
//======================

namespace
{
    const float source_pane_vertical_split_percentage = 0.7f;
    const float source_pane_bottom_margin             = 30.0f;
}

ShaderEditor::ShaderEditor(Editor* editor) : Widget(editor)
{
    m_title         = "Shader Editor";
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_visible       = false;
    m_size_initial  = ImVec2(1366, 1000);
    m_text_editor   = make_unique<TextEditor>();
    m_alpha         = 1.0f;
}

void ShaderEditor::OnTickVisible()
{
    ShowShaderSource();
    ImGui::SameLine();
    ShowShaderList();
    ShowControls();
}

void ShaderEditor::ShowShaderSource()
{
    ImVec2 content_region = ImGui::GetContentRegionAvail();
    ImVec2 size           = ImVec2(content_region.x * source_pane_vertical_split_percentage, content_region.y - source_pane_bottom_margin * Spartan::Window::GetDpiScale());

    if (ImGui::BeginChild("##shader_editor_source", size, true, ImGuiWindowFlags_NoScrollbar))
    {
        // title
        ImGui::Text(m_shader ? m_shader_name.c_str() : "Select a shader");

        // content
        if (m_shader)
        {
            // shader source
            if (ImGui::BeginTabBar("##shader_editor_tab_bar", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyResizeDown))
            {
                const std::vector<std::string>& names   = m_shader->GetNames();
                const std::vector<std::string>& sources = m_shader->GetSources();
            
                for (uint32_t i = 0; i < static_cast<uint32_t>(names.size()); i++)
                {
                    if (ImGui::BeginTabItem(names[i].c_str()))
                    {
                        // set text
                        if (m_index_displayed != i)
                        {
                            m_text_editor->SetText(sources[i]);
                            m_index_displayed = i;
                        }
            
                        // render
                        m_text_editor->Render("##shader_text_editor", ImVec2(0.0f, 0.0f), true);
            
                        // update shader
                        if (m_text_editor->IsTextChanged())
                        {
                            m_shader->SetSource(i, m_text_editor->GetText());
                        }
            
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
        }
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowShaderList()
{
    GetShaderInstances();

    ImVec2 size = ImVec2(0.0f, ImGui::GetContentRegionAvail().y - source_pane_bottom_margin * Spartan::Window::GetDpiScale());

    if (ImGui::BeginChild("##shader_editor_list", size, true, ImGuiWindowFlags_HorizontalScrollbar))
    {
        // title
        ImGui::Text("Shaders");

        for (RHI_Shader* shader : m_shaders)
        {
            // get name
            string name = shader->GetObjectName();
    
            // append stage
            if (shader->GetShaderStage() == RHI_Shader_Type::Vertex)
            {
                name += "_Vertex";
            }
            else if (shader->GetShaderStage() == RHI_Shader_Type::Pixel)
            {
                name += "_Pixel";
            }
            else if (shader->GetShaderStage() == RHI_Shader_Type::Compute)
            {
                name += "_Compute";
            }
            else
            {
                name += "_Unknown";
            }
    
            // append defines
            for (const auto& define : shader->GetDefines())
            {
                if (define.second != "0")
                {
                    name += "_" + define.first;
                }
            }
    
            if (ImGuiSp::button(name.c_str()) || m_first_run)
            {
                m_shader          = shader;
                m_shader_name     = name;
                m_index_displayed = -1;
                m_first_run       = false;
    
                // reload in case it has been modified
                m_shader->LoadFromDrive(m_shader->GetFilePath());
            }
        }
    }
    ImGui::EndChild();
}

void ShaderEditor::ShowControls()
{
    // compile button
    if (ImGuiSp::button("Compile"))
    {
        if (m_index_displayed != -1)
        {
            static const std::vector<std::string>& file_paths = m_shader->GetFilePaths();
            static const std::vector<std::string>& sources = m_shader->GetSources();

            // save all files
            for (uint32_t i = 0; i < static_cast<uint32_t>(file_paths.size()); i++)
            {
                ofstream out(file_paths[i]);
                out << sources[i];
                out.flush();
                out.close();
            }

            // compile synchronously to make the new frame obvious
            bool async = false;
            m_shader->Compile(m_shader->GetShaderStage(), m_shader->GetFilePath(), async);
        }
    }

    // opacity slider
    ImGui::SameLine();
    ImGui::SliderFloat("Opacity", &m_alpha, 0.1f, 1.0f, "%.1f");
}

void ShaderEditor::GetShaderInstances()
{
    auto shaders = Renderer::GetShaders();
    m_shaders.clear();
    for (const shared_ptr<RHI_Shader>& shader : shaders)
    {
        if (shader && shader->IsCompiled())
        {
            m_shaders.emplace_back(shader.get());
        }
    }

    // order them alphabetically
    sort(m_shaders.begin(), m_shaders.end(), [](RHI_Shader* a, RHI_Shader* b) { return a->GetObjectName() < b->GetObjectName(); });
}
