/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ====================================
#include "Widget.h"
#include <vector>
#include "../WidgetsDeferred/Widget_TextEditor.h"
//===============================================

namespace Spartan
{
    class RHI_Shader;
    class Renderer;
    class Input;
}

class Widget_ShaderEditor : public Widget
{
public:
    Widget_ShaderEditor(Editor* editor);

    void TickVisible() override;

private:
    void ShowShaderSource();
    void ShowShaderList();
    void GetShaderInstances();

    Spartan::RHI_Shader* m_shader   = nullptr;
    std::string m_shader_name       = "N/A";
    Spartan::Renderer* m_renderer   = nullptr;
    Spartan::Input* m_input         = nullptr;
    int32_t m_index_displayed       = -1;
    bool m_first_run                = true;
    std::unique_ptr<Widget_TextEditor> m_text_editor;
    std::vector<Spartan::RHI_Shader*> m_shaders;
};
