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

//= INCLUDES ===================
#include "Toolbar.h"
#include "MenuBar.h"
#include "Profiler.h"
#include "ResourceViewer.h"
#include "ShaderEditor.h"
#include "RenderOptions.h"
#include "TextureViewer.h"
#include "Core/Engine.h"
#include "Profiling/RenderDoc.h"
//==============================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace
{
    float button_size = 19.0f;

    ImVec4 button_color_play        = {0.2f, 0.7f, 0.35f, 1.0f};
    ImVec4 button_color_play_hover  = {0.22f, 0.8f, 0.4f, 1.0f};
    ImVec4 button_color_play_active = {0.1f, 0.4f, 0.2f, 1.0f};

    ImVec4 button_color_doc         = {0.25f, 0.7f, 0.75f, 0.9f};
    ImVec4 button_color_doc_hover   = {0.3f, 0.75f, 0.8f, 0.9f};
    ImVec4 button_color_doc_active  = {0.2f, 0.65f, 0.7f, 0.9f};

    // a button that when pressed will call "on press" and derives it's color (active/inactive) based on "get_visibility".
    void toolbar_button(IconType icon_type, const string tooltip_text, const function<bool()>& get_visibility, const function<void()>& on_press, float cursor_pos_x = -1.0f)
    {
        ImGui::SameLine();
        ImVec4 button_color = get_visibility() ? ImGui::GetStyle().Colors[ImGuiCol_ButtonActive] : ImGui::GetStyle().Colors[ImGuiCol_Button];
        ImGui::PushStyleColor(ImGuiCol_Button, button_color);
        if (cursor_pos_x > 0.0f)
        {
            ImGui::SetCursorPosX(cursor_pos_x);
        }

        const ImGuiStyle& style   = ImGui::GetStyle();
        const float size_avail_y  = 2.0f * style.FramePadding.y + button_size;
        const float button_size_y = button_size + 2.0f * MenuBar::GetPadding().y;
        const float offset_y      = (button_size_y - size_avail_y) * 0.5f;

        ImGui::SetCursorPosY(offset_y);

        if (ImGuiSp::image_button(static_cast<uint64_t>(icon_type), nullptr, icon_type, button_size * Spartan::Window::GetDpiScale(), false))
        {
            on_press();
        }

        ImGui::PopStyleColor();

        ImGuiSp::tooltip(tooltip_text.c_str());
    }
}

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
    m_widgets[IconType::Button_Shader]          = m_editor->GetWidget<ShaderEditor>();
    m_widgets[IconType::Component_Options]      = m_editor->GetWidget<RenderOptions>();
    m_widgets[IconType::Directory_File_Texture] = m_editor->GetWidget<TextureViewer>();

    Spartan::Engine::RemoveFlag(Spartan::EngineMode::Game);
}

void Toolbar::OnTick()
{
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float size_avail_x      = viewport->Size.x;
    const float button_size_final = button_size * Spartan::Window::GetDpiScale() + MenuBar::GetPadding().x * 2.0f;

    float num_buttons             = 1.0f;
    float size_toolbar            = num_buttons * button_size_final;
    float cursor_pos_x            = (size_avail_x - size_toolbar) * 0.5f;

    // play button
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  { 18.0f, MenuBar::GetPadding().y - 2.0f });
    {
        ImGui::PushStyleColor(ImGuiCol_Button, button_color_play);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_color_play_hover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_color_play_active);
        
        toolbar_button(
            IconType::Button_Play, "Play",
            []() { return Spartan::Engine::IsFlagSet(Spartan::EngineMode::Game);  },
            []() { return Spartan::Engine::ToggleFlag(Spartan::EngineMode::Game); },
            cursor_pos_x
        );
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(1);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { MenuBar::GetPadding().x, MenuBar::GetPadding().y - 2.0f });
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,  { 2.0f , 0.0f });


    // all the other buttons
    ImGui::PushStyleColor(ImGuiCol_Button,        button_color_doc);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_color_doc_hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  button_color_doc_active);
    {

        num_buttons     = 6.0f;
        size_toolbar    = num_buttons * button_size_final + (num_buttons - 1.0f) * ImGui::GetStyle().ItemSpacing.x;
        cursor_pos_x    = size_avail_x - (size_toolbar - 2.0f);

        // render doc button
        toolbar_button(
            IconType::Button_RenderDoc, "Captures the next frame and then launches RenderDoc",
            []() { return false; },
            []()
            {
                if (Spartan::Profiler::IsRenderdocEnabled())
                {
                    Spartan::RenderDoc::FrameCapture();
                }
                else
                {
                    SP_LOG_WARNING("RenderDoc integration is disabled. To enable, go to \"Profiler.cpp\", and set \"is_renderdoc_enabled\" to \"true\"");
                }
            },
            cursor_pos_x
        );

        // all the other buttons
        for (auto& widget_it : m_widgets)
        {
            Widget* widget = widget_it.second;
            const IconType widget_icon = widget_it.first;

            toolbar_button(widget_icon, widget->GetTitle(),
                [this, &widget]() { return widget->GetVisible(); },
                [this, &widget]() { widget->SetVisible(true); }
            );
        }
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);

    // screenshot
    //toolbar_button(
    //    IconType::Screenshot, "Screenshot",
    //    []() { return false; },
    //    []() { return Spartan::Renderer::Screenshot("screenshot.png"); }
    //);
}
