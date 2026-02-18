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

//= INCLUDES =======================
#include "FileDialog.h"
#include "../ImGui/ImGui_Extension.h"
#include <functional>
#include <memory>
#include <string>
//==================================

// file selection utilities for click-to-browse functionality
namespace file_selection
{
    inline std::unique_ptr<FileDialog> dialog;
    inline bool visible                                        = false;
    inline std::function<void(const std::string&)> callback;
    inline Editor* editor                                      = nullptr;

    inline void initialize(Editor* editor_in)
    {
        editor = editor_in;
    }

    inline void open(const std::function<void(const std::string&)>& on_selected)
    {
        if (!dialog)
        {
            dialog = std::make_unique<FileDialog>(true, FileDialog_Type_FileSelection, FileDialog_Op_Load, FileDialog_Filter_All);
        }
        callback = on_selected;
        visible  = true;
    }

    inline void tick()
    {
        if (!visible || !editor)
            return;

        std::string selected_path;
        if (dialog->Show(&visible, editor, nullptr, &selected_path))
        {
            if (callback && !selected_path.empty())
            {
                callback(selected_path);
            }
            visible  = false;
            callback = nullptr;
        }
    }

    // helper to render a browse button ("...") that can be used to open the file dialog
    inline bool browse_button(const char* id)
    {
        ImGui::PushID(id);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 2));
        bool clicked = ImGuiSp::button("...");
        ImGui::PopStyleVar();
        ImGui::PopID();
        return clicked;
    }
}
