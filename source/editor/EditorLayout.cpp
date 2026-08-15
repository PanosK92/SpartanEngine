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

//= INCLUDES ==========================
#include "pch.h"
#include "Editor.h"
#include "EditorLayout.h"
#include "imgui/ImGui_Style.h"
#include "imgui/source/imgui.h"
#include "imgui/source/imgui_internal.h"
//=====================================

namespace
{
    constexpr const char* root_window_name = "##main_window";

    ImGuiID dock_id_for(const WidgetDock dock, const ImGuiID center, const ImGuiID right, const ImGuiID right_down, const ImGuiID down, const ImGuiID down_right)
    {
        switch (dock)
        {
        case WidgetDock::Center:    return center;
        case WidgetDock::Right:     return right;
        case WidgetDock::RightDown: return right_down;
        case WidgetDock::Down:      return down;
        case WidgetDock::DownRight: return down_right;
        default:                    return 0;
        }
    }

    void apply_default_layout(Editor* editor, const ImGuiID window_id)
    {
        ImGui::DockBuilderRemoveNode(window_id);
        ImGui::DockBuilderAddNode(window_id, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(window_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_main_id       = window_id;
        ImGuiID dock_right_id      = ImGui::DockBuilderSplitNode(dock_main_id,  ImGuiDir_Right, 0.17f, nullptr, &dock_main_id);
        ImGuiID dock_right_down_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down,  0.6f,  nullptr, &dock_right_id);
        ImGuiID dock_down_id       = ImGui::DockBuilderSplitNode(dock_main_id,  ImGuiDir_Down,  0.22f, nullptr, &dock_main_id);
        ImGuiID dock_down_right_id = ImGui::DockBuilderSplitNode(dock_down_id,  ImGuiDir_Right, 0.3f,  nullptr, &dock_down_id);

        editor->ForEachWidget([&](Widget* widget)
        {
            const ImGuiID dock_id = dock_id_for(
                widget->GetDock(),
                dock_main_id,
                dock_right_id,
                dock_right_down_id,
                dock_down_id,
                dock_down_right_id
            );
            if (dock_id != 0)
            {
                ImGui::DockBuilderDockWindow(widget->GetTitle(), dock_id);
            }
        });

        ImGui::DockBuilderFinish(dock_main_id);
    }
}

void editor_layout::begin_root(Editor* editor)
{
    const auto window_flags =
        ImGuiWindowFlags_NoDocking             |
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    {
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        ImVec2 min = viewport->Pos;
        ImVec2 max = ImVec2(
            viewport->Pos.x + viewport->Size.x,
            viewport->Pos.y + viewport->Size.y
        );
        ImU32 border_color = ImGui::ColorConvertFloat4ToU32(
            ImGui::Style::color_border
        );
        draw_list->AddRect(min, max, border_color, 0.0f, 1.0f);
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));

    bool open = true;
    ImGui::Begin(root_window_name, &open, window_flags);
    ImGui::PopStyleVar(3);

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        const ImGuiID window_id = ImGui::GetID(root_window_name);
        if (!ImGui::DockBuilderGetNode(window_id))
        {
            apply_default_layout(editor, window_id);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::DockSpace(
            window_id,
            ImVec2(0.0f, 0.0f),
            ImGuiDockNodeFlags_PassthruCentralNode
        );
        ImGui::PopStyleVar();
    }
}

void editor_layout::end_root()
{
    ImGui::End();
}
