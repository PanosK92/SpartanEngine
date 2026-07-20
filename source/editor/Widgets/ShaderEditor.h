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

#pragma once

//= INCLUDES ================
#include "Widget.h"
#include <vector>
#include "../ImGui/TextEditor.h"
//===========================

namespace spartan
{
    class RHI_Shader;
    class Renderer;
}

class ShaderEditor : public Widget
{
public:
    ShaderEditor(Editor* editor);

    void OnTickVisible() override;

private:
    void ShowShaderSource(float width, float height);
    void ShowShaderList(float width, float height);
    void ShowControls();
    void GetShaderInstances();
    void SelectShader(spartan::RHI_Shader* shader, const std::string& name);
    void SaveAndCompile();

    spartan::RHI_Shader* m_shader = nullptr;
    std::string m_shader_name     = "N/A";
    int32_t m_index_displayed     = -1;
    bool m_first_run              = true;
    bool m_source_dirty           = false;
    ImGuiTextFilter m_shader_filter;
    TextEditor m_text_editor;
    std::vector<spartan::RHI_Shader*> m_shaders;
};
