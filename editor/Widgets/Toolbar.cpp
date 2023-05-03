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

//= INCLUDES =======================
#include "Toolbar.h"
#include "Profiler.h"
#include "ResourceViewer.h"
#include "ShaderEditor.h"
#include "RenderOptions.h"
#include "TextureViewer.h"
#include "Core/Engine.h"
#include "RHI/RHI_RenderDoc.h"
#include "Rendering/Renderer.h"
#include "../Editor.h"
#include "../ImGui/ImGuiExtension.h"
//==================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

Toolbar::Toolbar(Editor* editor) : Widget(editor)
{
    m_title     = "Toolbar";
    m_is_window = false;

    m_flags =
        ImGuiWindowFlags_NoCollapse      |
        ImGuiWindowFlags_NoResize        |
        ImGuiWindowFlags_NoMove          |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar     |
        ImGuiWindowFlags_NoTitleBar;

    m_widgets[IconType::Button_Profiler]        = m_editor->GetWidget<Profiler>();
    m_widgets[IconType::Button_ResourceCache]   = m_editor->GetWidget<ResourceViewer>();
    m_widgets[IconType::Component_Script]       = m_editor->GetWidget<ShaderEditor>();
    m_widgets[IconType::Component_Options]      = m_editor->GetWidget<RenderOptions>();
    m_widgets[IconType::Directory_File_Texture] = m_editor->GetWidget<TextureViewer>();

    Spartan::Engine::RemoveFlag(Spartan::EngineMode::Game);
}

void Toolbar::TickAlways()
{
    // A button that when pressed will call "on press" and derives it's color (active/inactive) based on "get_visibility".
    auto widget_button = [this](IconType icon_type, const string title, const function<bool()>& get_visibility, const function<void()>& on_press)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, get_visibility() ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button]);
        if (ImGuiSp::image_button(0, nullptr, icon_type, m_button_size, false))
        {
            on_press();
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip(title.c_str());
        }
    };

    // Widget buttons
    for (auto& widget_it : m_widgets)
    {
        Widget* widget             = widget_it.second;
        const IconType widget_icon = widget_it.first;

        widget_button(widget_icon, widget->GetTitle(), [this, &widget](){ return widget->GetVisible(); }, [this, &widget]() { widget->SetVisible(true); });
    }

    // Play button
    widget_button(
        IconType::Button_Play, "play",
        []() { return Spartan::Engine::IsFlagSet(Spartan::EngineMode::Game); },
        []() { return Spartan::Engine::ToggleFlag(Spartan::EngineMode::Game); }
    );

    // RenderDoc button
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_Button]);
    if (ImGuiSp::image_button(0, nullptr, IconType::Button_RenderDoc, m_button_size, false))
    {
        if (Spartan::RHI_RenderDoc::IsEnabled())
        {
            Spartan::RHI_RenderDoc::FrameCapture();
        }
        else
        {
            SP_LOG_WARNING("RenderDoc integration is disabled. To enable, go to \"RHI_Implemenation.cpp\", and set \"renderdoc\" to \"true\"");
        }
    }
    ImGuiSp::tooltip("Captures the next frame and then launches RenderDoc");
    ImGui::PopStyleColor();
}
