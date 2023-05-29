/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =============================
#include "Widget.h"
#include <memory>
#include "../WidgetsDeferred/FileDialog.h"
#include "Toolbar.h"
//========================================

namespace Spartan { class Context; }
class Toolbar;

class MenuBar : public Widget
{
public:
    MenuBar(Editor* editor);

    void TickAlways() override;
    void ShowWorldSaveDialog();
    void ShowWorldLoadDialog();

    static float GetPadding() { return 8.0f; }
private:
    void DrawFileDialog() const;
    void HandleKeyShortcuts() const;
    void CreateWorldMenuItem();
    void CreateViewMenuItem();
    void CreateHelpMenuItem();

    std::unique_ptr<Toolbar> m_tool_bar;
    std::unique_ptr<FileDialog> m_file_dialog;
    Editor* m_editor = nullptr;
};
