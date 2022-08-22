/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES =============================
#include "MenuBar.h"
#include "Toolbar.h"
#include "Core/Settings.h"
#include "../WidgetsDeferred/FileDialog.h"
#include "Profiler.h"
#include "../Editor.h"
#include "ShaderEditor.h"
#include "RenderOptions.h"
#include "TextureViewer.h"
#include "ResourceViewer.h"
//========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace _Widget_MenuBar
{
    static bool g_showShortcutsWindow = false;
    static bool g_showAboutWindow     = false;
    static bool g_fileDialogVisible   = false;
    static bool imgui_metrics         = false;
    static bool imgui_style           = false;
    static bool imgui_demo            = false;
    static Spartan::Input* g_input    = nullptr;
    static Spartan::World* g_world    = nullptr;
    static string g_fileDialogSelection;
}

MenuBar::MenuBar(Editor *editor) : Widget(editor)
{
    m_title                  = "MenuBar";
    m_is_window              = false;
    m_tool_bar               = make_unique<Toolbar>(editor);
    m_file_dialog            = make_unique<FileDialog>(m_context, true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_World);
    _Widget_MenuBar::g_input = m_context->GetSubsystem<Spartan::Input>();
    _Widget_MenuBar::g_world = m_context->GetSubsystem<Spartan::World>();
    m_editor                 = editor;
}

template <class T>
static void widget_menu_item(Editor* editor)
{
    T* widget = editor->GetWidget<T>();

    // Menu item with checkmark based on widget->GetVisible()
    if (ImGui::MenuItem(widget->GetTitle().c_str(), nullptr, widget->GetVisible()))
    {
        // Toggle visibility
        widget->SetVisible(!widget->GetVisible());
    }
}

void MenuBar::TickAlways()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(GetPadding(), GetPadding()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("World"))
        {
            if (ImGui::MenuItem("New"))
            {
                m_context->GetSubsystem<Spartan::World>()->New();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Load"))
            {
                ShowWorldLoadDialog();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save", "Ctrl+S"))
            {
                ShowWorldSaveDialog();
            }

            if (ImGui::MenuItem("Save As...", "Ctrl+S"))
            {
                ShowWorldSaveDialog();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            widget_menu_item<Profiler>(m_editor);
            widget_menu_item<ShaderEditor>(m_editor);
            widget_menu_item<RenderOptions>(m_editor);
            widget_menu_item<TextureViewer>(m_editor);
            widget_menu_item<ResourceViewer>(m_editor);

            if (ImGui::BeginMenu("ImGui"))
            {
                ImGui::MenuItem("Metrics", nullptr, &_Widget_MenuBar::imgui_metrics);
                ImGui::MenuItem("Style",   nullptr, &_Widget_MenuBar::imgui_style);
                ImGui::MenuItem("Demo",    nullptr, &_Widget_MenuBar::imgui_demo);
                ImGui::EndMenu();
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("About", nullptr, &_Widget_MenuBar::g_showAboutWindow);
            ImGui::MenuItem("Shortcuts & Input Reference", "Ctrl+P", &_Widget_MenuBar::g_showShortcutsWindow);
            ImGui::EndMenu();
        }

        // Tool bar
        ImGui::Spacing();
        m_tool_bar->Tick();

        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar(2);

    if (_Widget_MenuBar::imgui_metrics)
    {
        ImGui::ShowMetricsWindow();
    }

    if (_Widget_MenuBar::imgui_style)
    {
        ImGui::Begin("Style Editor", nullptr, ImGuiWindowFlags_NoDocking);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }

    if (_Widget_MenuBar::imgui_demo)
    {
        ImGui::ShowDemoWindow(&_Widget_MenuBar::imgui_demo);
    }

    HandleKeyShortcuts();
    DrawFileDialog();
    DrawAboutWindow();
    DrawShortcutsWindow();
}

void MenuBar::HandleKeyShortcuts() const
{
    if (_Widget_MenuBar::g_input->GetKey(Spartan::KeyCode::Ctrl_Left) && _Widget_MenuBar::g_input->GetKeyDown(Spartan::KeyCode::P))
    {
        _Widget_MenuBar::g_showShortcutsWindow = !_Widget_MenuBar::g_showShortcutsWindow;
    }
}

void MenuBar::ShowWorldSaveDialog()
{
    m_file_dialog->SetOperation(FileDialog_Op_Save);
    _Widget_MenuBar::g_fileDialogVisible = true;
}

void MenuBar::ShowWorldLoadDialog()
{
    m_file_dialog->SetOperation(FileDialog_Op_Load);
    _Widget_MenuBar::g_fileDialogVisible = true;
}

void MenuBar::DrawFileDialog() const
{
    if (_Widget_MenuBar::g_fileDialogVisible)
    {
        ImGui::SetNextWindowFocus();
    }

    if (m_file_dialog->Show(&_Widget_MenuBar::g_fileDialogVisible, nullptr, &_Widget_MenuBar::g_fileDialogSelection))
    {
        // LOAD
        if (m_file_dialog->GetOperation() == FileDialog_Op_Open || m_file_dialog->GetOperation() == FileDialog_Op_Load)
        {
            // Scene
            if (Spartan::FileSystem::IsEngineSceneFile(_Widget_MenuBar::g_fileDialogSelection))
            {
                EditorHelper::Get().LoadWorld(_Widget_MenuBar::g_fileDialogSelection);
                _Widget_MenuBar::g_fileDialogVisible = false;
            }
        }
        // SAVE
        else if (m_file_dialog->GetOperation() == FileDialog_Op_Save)
        {
            // Scene
            if (m_file_dialog->GetFilter() == FileDialog_Filter_World)
            {
                EditorHelper::Get().SaveWorld(_Widget_MenuBar::g_fileDialogSelection);
                _Widget_MenuBar::g_fileDialogVisible = false;
            }
        }
    }
}

void MenuBar::DrawShortcutsWindow() const
{
    if (!_Widget_MenuBar::g_showShortcutsWindow)
        return;

    ImGui::SetNextWindowContentSize(ImVec2(540.f, 360.f));
    ImGui::SetNextWindowFocus();
    ImGui::Begin("Shortcuts & Input Reference", &_Widget_MenuBar::g_showShortcutsWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
    {
        static float col_a = 220.0f;
        static float col_b = 20.0f;

        {
            struct Shortcut
            {
                char *shortcut;
                char *usage;
            };

            static const Shortcut shortcuts[] =
            {
                {(char*)"Ctrl+P",      (char*)"Open shortcuts & input reference window"},
                {(char*)"Ctrl+S",      (char*)"Save world"},
                {(char*)"Ctrl+L",      (char*)"Load world"},
                {(char*)"Right click", (char*)"Enable first person camera control"},
                {(char*)"W, A, S, D",  (char*)"Move camera"},
                {(char*)"Q, E",        (char*)"Change camera elevation"},
                {(char*)"F",           (char*)"Center camera on object"},
                {(char*)"Alt+Enter",   (char*)"Toggle fullscreen viewport"}
            };

            ImGui::NewLine();
            ImGui::SameLine(col_b);
            ImGui::Text("Shortcut");
            ImGui::SameLine(col_a);
            ImGui::Text("Usage");

            for (const Shortcut& shortcut : shortcuts)
            {
                ImGui::BulletText(shortcut.shortcut);
                ImGui::SameLine(col_a);
                ImGui::Text(shortcut.usage);
            }
        }
    }
    ImGui::End();
}

void MenuBar::DrawAboutWindow() const
{
    if (!_Widget_MenuBar::g_showAboutWindow)
        return;

    ImGui::SetNextWindowFocus();
    ImGui::Begin("About", &_Widget_MenuBar::g_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
    {
        ImGui::Text("Spartan %s", (to_string(sp_version_major) + "." + to_string(sp_version_minor) + "." + to_string(sp_version_revision)).c_str());
        ImGui::Text("Author: Panos Karabelas");
        ImGui::SameLine(imgui_extension::GetWindowContentRegionWidth());
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 55);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);

        if (imgui_extension::button("GitHub"))
        {
            Spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine");
        }

        ImGui::Separator();

        ImGui::BeginChildFrame(ImGui::GetID("about_license"), ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 15.5f), ImGuiWindowFlags_NoMove);
        ImGui::Text("MIT License");
        ImGui::Text("Permission is hereby granted, free of charge, to any person obtaining a copy");
        ImGui::Text("of this software and associated documentation files(the \"Software\"), to deal");
        ImGui::Text("in the Software without restriction, including without limitation the rights");
        ImGui::Text("to use, copy, modify, merge, publish, distribute, sublicense, and / or sell");
        ImGui::Text("copies of the Software, and to permit persons to whom the Software is furnished");
        ImGui::Text("to do so, subject to the following conditions :");
        ImGui::Text("The above copyright notice and this permission notice shall be included in");
        ImGui::Text("all copies or substantial portions of the Software.");
        ImGui::Text("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR");
        ImGui::Text("IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS");
        ImGui::Text("FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR");
        ImGui::Text("COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER");
        ImGui::Text("IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN");
        ImGui::Text("CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.");
        ImGui::EndChildFrame();

        ImGui::Separator();

        static float col_a = 220.0f;
        static float col_b = 340.0f;

        ImGui::Text("Third party libraries");
        {
            ImGui::Text("Name");
            ImGui::SameLine(col_a);
            ImGui::Text("Version");
            ImGui::SameLine(col_b);
            ImGui::Text("URL");

            for (const Spartan::ThirdPartyLib &lib : m_context->GetSubsystem<Spartan::Settings>()->GetThirdPartyLibs())
            {
                ImGui::BulletText(lib.name.c_str());
                ImGui::SameLine(col_a);
                ImGui::Text(lib.version.c_str());
                ImGui::SameLine(col_b);
                ImGui::PushID(lib.url.c_str());
                if (imgui_extension::button(lib.url.c_str()))
                {
                    Spartan::FileSystem::OpenUrl(lib.url);
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}
