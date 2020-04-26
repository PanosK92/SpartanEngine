/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ============================
#include "Widget_ShaderEditor.h"
#include "Core/Context.h"
#include "Rendering/Renderer.h"
#include "Input/Input.h"
#include "RHI/RHI_Shader.h"
#include <fstream>
#include <sstream>
#include "../ImGui/Source/imgui_stdlib.h"
//=======================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
//======================

Widget_ShaderEditor::Widget_ShaderEditor(Context* context) : Widget(context)
{
    m_title         = "Shader Editor";
    m_flags         |= ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
    m_is_visible	= false;
    m_size          = ImVec2(1366, 1000);
    m_text_editor   = make_unique<Widget_TextEditor>();
    m_renderer      = m_context->GetSubsystem<Renderer>();
    m_input         = m_context->GetSubsystem<Input>();
}

void Widget_ShaderEditor::Tick()
{
    ShowShaderSource();
    ImGui::SameLine();
    ShowShaderList();
}

void Widget_ShaderEditor::ShowShaderSource()
{
    ImGui::BeginGroup();
    {
        ImGui::Text(m_shader ? m_shader_name.c_str() : "Select a shader");

        if (ImGui::BeginChild("##shader_source", ImVec2(m_size.x * 0.8f, 0.0f)))
        {
            // Shader source
            if (ImGui::BeginTabBar("#shader_tab_bar", ImGuiTabBarFlags_Reorderable))
            {
                for (int32_t i = 0; i < static_cast<int32_t>(m_shader_files.size()); i++)
                {
                    ShaderFile& shader_file = m_shader_files[i];

                    if (ImGui::BeginTabItem(shader_file.name.c_str()))
                    {
                        // Set text
                        if (m_displayed_file_index != i)
                        {
                            m_text_editor->SetText(shader_file.source);
                            m_displayed_file_index = i;
                        }

                        // Handle keyboard shortcuts
                        if (m_input->GetKeyDown(Ctrl_Left) && m_input->GetKeyDown(C))
                        {
                            m_text_editor->Copy();
                        }

                        if (m_input->GetKeyDown(Ctrl_Left) && m_input->GetKeyDown(X))
                        {
                            m_text_editor->Cut();
                        }

                        if (m_input->GetKeyDown(Ctrl_Left) && m_input->GetKeyDown(V))
                        {
                            m_text_editor->Paste();
                        }

                        if (m_input->GetKeyDown(Ctrl_Left) && m_input->GetKeyDown(Z))
                        {
                            m_text_editor->Undo();
                        }

                        if (m_input->GetKeyDown(Ctrl_Left) && m_input->GetKeyDown(Y))
                        {
                            m_text_editor->Redo();
                        }

                        // Render
                        m_text_editor->Render("Title", ImVec2(0.0f, ImGui::GetContentRegionMax().y - 60.0f)); // shrink y to bring the compile button into view

                        // Update shader
                        if (m_text_editor->IsTextChanged())
                        {
                            shader_file.source = m_text_editor->GetText();
                        }

                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            
            if (ImGui::Button("Compile"))
            {
                // Save all files
                for (ShaderFile& shader_file : m_shader_files)
                {
                    ofstream out(shader_file.path);
                    out << shader_file.source;
                    out.flush();
                    out.close();
                }

                // Compile synchronously so that the last frame switch immediately to the new shader, much better than seeing flickering.
                m_shader->Compile(m_shader->GetShaderStage(), m_shader->GetFilePath());
            }

            ImGui::EndChild();
        }
    }
    ImGui::EndGroup();
}

void Widget_ShaderEditor::ShowShaderList()
{
    auto& shaders = m_renderer->GetShaders();

    ImGui::BeginGroup();
    {
        ImGui::Text("Shaders");

        if (ImGui::BeginChild("##shader_list", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_HorizontalScrollbar))
        {
            for (const auto& it : shaders)
            {
                RHI_Shader* shader = it.second.get();

                // Get name
                string name = shader->GetName();

                // Append stage
                if (shader->GetShaderStage() == RHI_Shader_Vertex)
                {
                    name += "_Vertex";
                }
                else if (shader->GetShaderStage() == RHI_Shader_Pixel)
                {
                    name += "_Pixel";
                }
                else if (shader->GetShaderStage() == RHI_Shader_Compute)
                {
                    name += "_Compute";
                }
                else
                {
                    name += "_Unknown";
                }

                // Append defines
                for (const auto& define : shader->GetDefines())
                {
                    name += "_" + define.first;
                }

                if (ImGui::Button(name.c_str()) || m_first_run)
                {
                    m_shader                = shader;
                    m_shader_name           = name;
                    m_displayed_file_index  = -1;
                    m_first_run             = false;

                    GetAllShadersFiles(m_shader->GetFilePath());
                }
            }
            ImGui::EndChild();
        }
    }
    ImGui::EndGroup();
}

void Widget_ShaderEditor::GetAllShadersFiles(const string& file_path)
{
    // Get all files used by the shader
    vector<string> include_paths = { file_path };
    auto new_includes = FileSystem::GetIncludedFiles(file_path);
    include_paths.insert(include_paths.end(), new_includes.begin(), new_includes.end());

    // Resize shader files to fit
    m_shader_files.clear();
    if (include_paths.size() > m_shader_files.capacity())
    {
        m_shader_files.reserve(include_paths.size());
    }

    // Update shader files
    for (const string& include_path : include_paths)
    {
        ifstream in(include_path);
        stringstream buffer;
        buffer << in.rdbuf();
        m_shader_files.emplace_back(include_path, buffer.str());
    }
}
