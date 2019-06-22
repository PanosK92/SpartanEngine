/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ===============================
#include "Widget_ShaderEditor.h"
#include "Rendering/Renderer.h"
#include "Core/Context.h"
#include "RHI/RHI_Shader.h"
#include <fstream>
#include <sstream>
#include "../../ImGui/Source/imgui_stdlib.h"
//==========================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
//======================

Widget_ShaderEditor::Widget_ShaderEditor(Context* context) : Widget(context)
{
    m_title         = "Shader Editor";
    m_flags         |= ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;
    m_is_visible	= false;
}

void Widget_ShaderEditor::Tick(float delta_time)
{
    Renderer* renderer = m_context->GetSubsystem<Renderer>().get();
    auto& shaders = renderer->GetShaders();
    static RHI_Shader* shader_selected;
    static string source;

    // Shader list
    ImGui::BeginGroup();
    {
        for (const auto& shader_it : shaders)
        {
            if (ImGui::Button(shader_it.second->GetName().c_str()))
            {
                shader_selected = shader_it.second.get();
            }
        }
    }
    ImGui::EndGroup();

    // Shader text
    ImGui::SameLine();
    ImGui::BeginGroup();
    {
        if (shader_selected)
        {
            ifstream in(shader_selected->GetFilePath());
            stringstream buffer;
            buffer << in.rdbuf();
            source = buffer.str();
        }

        ImGui::InputTextMultiline("##shader_source", &source, ImVec2(800, ImGui::GetTextLineHeight() * 50), ImGuiInputTextFlags_None);

        if (ImGui::Button("Compile") && shader_selected)
        {
            ofstream out(shader_selected->GetFilePath());
            out << source;
            out.flush();
            out.close();

            shader_selected->CompileAsync(
                m_context,
                shader_selected->GetShaderStage(),
                shader_selected->GetFilePath()
            );
        }
    }
    ImGui::EndGroup();
}
