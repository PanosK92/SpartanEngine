/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

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

//= INCLUDES =====================

#include "pch.h"
#include "StatusBar.h"
#include "GeneralWindows.h"
#include "Widgets/Profiler.h"
#include "Widgets/ShaderEditor.h"
#include "Widgets/RenderOptions.h"
#include "Widgets/TextureViewer.h"
#include "Widgets/ResourceViewer.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/Console.h"
#include "Widgets/Properties.h"
#include "Widgets/Viewport.h"
#include "Widgets/WorldViewer.h"
#include "Widgets/FileDialog.h"
#include "Widgets/Style.h"
#include "Engine.h"
#include "Profiling/RenderDoc.h"
#include "Debugging.h"
#include "ImGui/Source/Animation/im_anim.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

/*
namespace
{
    Editor* editor = nullptr;

    template <class T>
    void menu_entry()
    {
        T* widget = editor->GetWidget<T>();

        // menu item with checkmark based on widget->GetVisible()
        if (ImGui::MenuItem(widget->GetTitle(), nullptr, widget->GetVisible()))
        {
            // toggle visibility
            widget->SetVisible(!widget->GetVisible());
        }
    }


    void tick(float menubar_height)
    {
        const float dpi = spartan::Window::GetDpiScale();

        const float icon_size_scaled           = icon_size_base * dpi;
        const float button_width               = icon_size_scaled + button_padding_x * 2.0f * dpi;
        const spartan::math::Vector2 icon_size = spartan::math::Vector2(icon_size_scaled, icon_size_scaled);

        // calculate vertical centering
        const float button_height = icon_size_scaled + button_padding_y * 2.0f * dpi;
        const float offset_y      = (menubar_height - button_height) * 0.5f;

        // position first button - use window width and account for small margin
        const float window_width = ImGui::GetWindowWidth();
        const float margin       = 2.0f * dpi;  // small margin from edge
        float start_x            = window_width - (3.0f * button_width) - margin;
        ImGui::SetCursorPosX(start_x);
        ImGui::SetCursorPosY(offset_y);

        // minimize button
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(button_padding_x * dpi, button_padding_y * dpi));

        if (ImGuiSp::image_button(spartan::ResourceCache::GetIcon(spartan::IconType::Minimize), icon_size, false))
        {
            spartan::Window::Minimize();
        }

        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosY(offset_y);

        // maximize/restore button
        if (ImGuiSp::image_button(spartan::ResourceCache::GetIcon(spartan::IconType::Maximize), icon_size, false))
        {
            spartan::Window::Maximize();
        }

        ImGui::PopStyleColor(3);

        ImGui::SameLine(0, 0);
        ImGui::SetCursorPosY(offset_y);

        // close button with red hover
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.7f, 0.1f, 0.1f, 1.0f));

        if (ImGuiSp::image_button(spartan::ResourceCache::GetIcon(spartan::IconType::X), icon_size, false))
        {
            spartan::Window::Close();
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
    }

}  // namespace

void StatusBar::Initialize(Editor* _editor)
{
    editor = _editor;
}

void StatusBar::Tick()
{
    // menu
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, 8.0f));

        if (ImGui::BeginMainMenuBar())
        {
            // get menu bar height for hit test configuration
            float bar_height = ImGui::GetWindowHeight();

            // configure hit test regions for custom title bar
            spartan::Window::SetStatusBar(bar_height);
            spartan::Window::SetTitleBarButtonWidth(buttons_titlebar::get_total_width());

            // engine logo on the left side of the title bar
            float dpi       = spartan::Window::GetDpiScale();
            float icon_size = 16.0f * dpi;
            float padding_x = 6.0f * dpi;
            {
                float vertical_padding = (bar_height - icon_size) * 0.5f;

                ImGui::SetCursorPosX(padding_x);
                ImGui::SetCursorPosY(vertical_padding);

                spartan::RHI_Texture* logo = spartan::ResourceCache::GetIcon(spartan::IconType::Logo);
                if (logo) { ImGui::Image(reinterpret_cast<ImTextureID>(logo), ImVec2(icon_size, icon_size)); }

                ImGui::SameLine(0, padding_x);
            }

            // vertically center menu items - account for frame padding in menu item height
            float frame_padding_y  = ImGui::GetStyle().FramePadding.y;
            float text_height      = ImGui::GetTextLineHeight();
            float menu_item_height = text_height + frame_padding_y * 2.0f;
            float menu_y           = (bar_height - menu_item_height) * 0.5f;

            ImGui::SetCursorPosY(menu_y);
            buttons_menu::world();
            ImGui::SetCursorPosY(menu_y);
            buttons_menu::view();
            ImGui::SetCursorPosY(menu_y);
            buttons_menu::help();
            buttons_toolbar::tick();

            // render window control buttons (minimize, maximize, close)
            buttons_titlebar::tick(bar_height);

            // update title bar hovered state for hit test callback
            // this allows sdl to make the title bar draggable only when no imgui items are hovered
            {
                bool any_item_hovered = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive() ||
                                        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
                spartan::Window::SetTitleBarHovered(any_item_hovered);

                // double-click on empty space to maximize/restore
                bool mouse_in_menubar = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
                if (mouse_in_menubar && !any_item_hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    spartan::Window::Maximize();
                }
            }

            ImGui::EndMainMenuBar();
        }

        ImGui::PopStyleVar();
    }

    // windows
    {
        if (show_imgui_metrics_window)
        {
            ImGui::ShowMetricsWindow();
        }

        if (show_imgui_demo_widow)
        {
            ImGui::ShowDemoWindow(&show_imgui_demo_widow);
        }

    }

}
*/
