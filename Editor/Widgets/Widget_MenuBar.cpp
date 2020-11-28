/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Widget_MenuBar.h"
#include "../WidgetsDeferred/FileDialog.h"
#include "Core/Settings.h"
#include "Rendering/Model.h"
//========================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

namespace _Widget_MenuBar
{
    static bool g_showAboutWindow   = false;
    static bool g_fileDialogVisible = false;
    static bool imgui_metrics       = false;
    static bool imgui_style         = false;
    static bool imgui_demo          = false;
    World* world                    = nullptr;
    static string g_fileDialogSelection;
}

Widget_MenuBar::Widget_MenuBar(Editor* editor) : Widget(editor)
{
    m_title                 = "MenuBar";
    m_is_window             = false;
    m_fileDialog            = make_unique<FileDialog>(m_context, true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_Scene);
    _Widget_MenuBar::world  = m_context->GetSubsystem<World>();
}

void Widget_MenuBar::Tick()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("World"))
        {
            if (ImGui::MenuItem("New"))
            {
                m_context->GetSubsystem<World>()->Unload();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Load"))
            {
                m_fileDialog->SetOperation(FileDialog_Op_Load);
                _Widget_MenuBar::g_fileDialogVisible = true;
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Save"))
            {
                m_fileDialog->SetOperation(FileDialog_Op_Save);
                _Widget_MenuBar::g_fileDialogVisible = true;
            }

            if (ImGui::MenuItem("Save As..."))
            {
                m_fileDialog->SetOperation(FileDialog_Op_Save);
                _Widget_MenuBar::g_fileDialogVisible = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("ImGui Metrics",    nullptr, &_Widget_MenuBar::imgui_metrics);
            ImGui::MenuItem("ImGui Style",      nullptr, &_Widget_MenuBar::imgui_style);
            ImGui::MenuItem("ImGui Demo",       nullptr, &_Widget_MenuBar::imgui_demo);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help"))
        {
            ImGui::MenuItem("About", nullptr, &_Widget_MenuBar::g_showAboutWindow);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

    if (_Widget_MenuBar::imgui_metrics) { ImGui::ShowMetricsWindow(); }
    if (_Widget_MenuBar::imgui_style)   { ImGui::Begin("Style Editor", nullptr, ImGuiWindowFlags_NoDocking); ImGui::ShowStyleEditor(); ImGui::End(); }
    if (_Widget_MenuBar::imgui_demo)    { ImGui::ShowDemoWindow(&_Widget_MenuBar::imgui_demo); }

    ShowFileDialog();
    ShowAboutWindow();
}

void Widget_MenuBar::ShowFileDialog() const
{
    if (_Widget_MenuBar::g_fileDialogVisible)
    {
        ImGui::SetNextWindowFocus();
    }

    if (m_fileDialog->Show(&_Widget_MenuBar::g_fileDialogVisible, nullptr, &_Widget_MenuBar::g_fileDialogSelection))
    {
        // LOAD
        if (m_fileDialog->GetOperation() == FileDialog_Op_Open || m_fileDialog->GetOperation() == FileDialog_Op_Load)
        {
            // Scene
            if (FileSystem::IsEngineSceneFile(_Widget_MenuBar::g_fileDialogSelection))
            {
                EditorHelper::Get().LoadWorld(_Widget_MenuBar::g_fileDialogSelection);
                _Widget_MenuBar::g_fileDialogVisible = false;
            }
        }
        // SAVE
        else if (m_fileDialog->GetOperation() == FileDialog_Op_Save)
        {
            // Scene
            if (m_fileDialog->GetFilter() == FileDialog_Filter_Scene)
            {
                EditorHelper::Get().SaveWorld(_Widget_MenuBar::g_fileDialogSelection);
                _Widget_MenuBar::g_fileDialogVisible = false;
            }
        }
    }
}

void Widget_MenuBar::ShowAboutWindow() const
{
    if (!_Widget_MenuBar::g_showAboutWindow)
        return;

    ImGui::SetNextWindowFocus();
    ImGui::Begin("About", &_Widget_MenuBar::g_showAboutWindow, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

    ImGui::Text("Spartan %s", sp_version);
    ImGui::Text("Author: Panos Karabelas");
    ImGui::SameLine(ImGui::GetWindowContentRegionWidth());
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 55);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5);
    if (ImGui::Button("GitHub"))
    {
        FileSystem::OpenDirectoryWindow("https://github.com/PanosK92/SpartanEngine");
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
        ImGui::SameLine(col_a); ImGui::Text("Version");
        ImGui::SameLine(col_b); ImGui::Text("URL");
        for (const ThirdPartyLib& lib : m_context->GetSubsystem<Settings>()->GetThirdPartyLibs())
        {
            ImGui::BulletText(lib.name.c_str());
            ImGui::SameLine(col_a); ImGui::Text(lib.version.c_str());
            ImGui::SameLine(col_b); ImGui::PushID(lib.url.c_str());  if (ImGui::Button(lib.url.c_str())) { FileSystem::OpenDirectoryWindow(lib.url); } ImGui::PopID();
        }
    }

    ImGui::End();
}
