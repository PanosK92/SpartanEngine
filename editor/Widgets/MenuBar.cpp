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
#include "Rendering/Mesh.h"
#include "AssetBrowser.h"
#include "Console.h"
#include "Properties.h"
#include "Viewport.h"
#include "WorldViewer.h"
//========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    bool show_shortcuts_window     = false;
    bool show_about_window         = false;
    bool show_contributors_window  = false;
    bool show_file_dialog          = false;
    bool show_imgui_metrics_window = false;
    bool show_imgui_style_window   = false;
    bool show_imgui_demo_widow     = false;
    string file_dialog_selection_path;

    static void window_about(Editor* editor)
    {
        if (!show_about_window)
            return;

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        ImGui::Begin("About", &show_about_window, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
        {
            ImGui::Text("Spartan %s", (to_string(sp_info::version_major) + "." + to_string(sp_info::version_minor) + "." + to_string(sp_info::version_revision)).c_str());
            ImGui::Text("Author: Panos Karabelas");
            ImGui::SameLine(ImGuiSp::GetWindowContentRegionWidth());
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 50 * Spartan::Window::GetDpiScale());
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5 * Spartan::Window::GetDpiScale());

            if (ImGuiSp::button("GitHub"))
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

            float col_a = 220.0f * Spartan::Window::GetDpiScale();
            float col_b = 320.0f * Spartan::Window::GetDpiScale();

            ImGui::Text("Third party libraries");
            {
                ImGui::Text("Name");
                ImGui::SameLine(col_a);
                ImGui::Text("Version");
                ImGui::SameLine(col_b);
                ImGui::Text("URL");

                for (const Spartan::third_party_lib& lib : Spartan::Settings::GetThirdPartyLibs())
                {
                    ImGui::BulletText(lib.name.c_str());
                    ImGui::SameLine(col_a);
                    ImGui::Text(lib.version.c_str());
                    ImGui::SameLine(col_b);
                    ImGui::PushID(lib.url.c_str());
                    if (ImGuiSp::button(lib.url.c_str()))
                    {
                        Spartan::FileSystem::OpenUrl(lib.url);
                    }
                    ImGui::PopID();
                }
            }
        }
        ImGui::End();
    }

    static void window_contributors(Editor* editor)
    {
        if (!show_contributors_window)
            return;

        // Contributor list (name, contribution, country, button text, button url)
        static vector<string> contributors =
        {
             // format: name,contribution,country,button text,button url
            "Jesse Guerrero,Editor UX improvements,US,LinkedIn,https://www.linkedin.com/in/jguer",
            "Konstantinos Benos,Editor transformation bug fixes,Greece,Twitter,https://twitter.com/deg3x",
            "Nick Polyderopoulos,Editor UX improvements,Greece,LinkedIn,https://www.linkedin.com/in/nick-polyderopoulos-21742397"
        };

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        ImGui::Begin("Spartans", &show_contributors_window, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
        {
            ImGui::Text("In alphabetical order");

            static ImGuiTableFlags flags =
                ImGuiTableFlags_Borders |
                ImGuiTableFlags_RowBg   |
                ImGuiTableFlags_SizingFixedFit;

            static ImVec2 size = ImVec2(-1.0f);
            if (ImGui::BeginTable("##contributors_table", 4, flags, size))
            {
                // Headers
                ImGui::TableSetupColumn("Name");
                ImGui::TableSetupColumn("Contribution");
                ImGui::TableSetupColumn("Country");
                ImGui::TableSetupColumn("URL");
                ImGui::TableHeadersRow();

                for (string& contributor : contributors)
                {
                    // Split the contributor string using commas
                    stringstream ss(contributor);
                    string token;
                    vector<string> contributor_parts;
                    while (getline(ss, token, ','))
                    {
                        contributor_parts.push_back(token);
                    }

                    // Switch row
                    ImGui::TableNextRow();

                    // Shit text down so that it's on the same line with the button
                    static const float y_shift = 6.0f;

                    // Name
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                    ImGui::Text(contributor_parts[0].c_str());

                    // Contribution
                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                    ImGui::Text(contributor_parts[1].c_str());

                    // Country
                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                    ImGui::Text(contributor_parts[2].c_str());

                    // Button (URL)
                    ImGui::TableSetColumnIndex(3);
                    if (ImGuiSp::button(contributor_parts[3].c_str()))
                    {
                        Spartan::FileSystem::OpenUrl(contributor_parts[4]);
                    }
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();
    }

    static void window_shortcuts(Editor* editor)
    {
        if (!show_shortcuts_window)
            return;

        ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowFocus();
        ImGui::Begin("Shortcuts & Input Reference", &show_shortcuts_window, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
        {
            static float col_a = 220.0f;
            static float col_b = 20.0f;

            {
                struct Shortcut
                {
                    char* shortcut;
                    char* usage;
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
}

MenuBar::MenuBar(Editor *editor) : Widget(editor)
{
    m_title        = "MenuBar";
    m_is_window    = false;
    m_tool_bar     = make_unique<Toolbar>(editor);
    m_file_dialog  = make_unique<FileDialog>(true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_World);
    m_editor       = editor;
}

void MenuBar::OnTick()
{
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(GetPadding(), GetPadding()));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (ImGui::BeginMainMenuBar())
    {
        CreateWorldMenuItem();

        CreateViewMenuItem();

        CreateHelpMenuItem();

        // Tool bar
        ImGui::Spacing();
        m_tool_bar->Tick();

        ImGui::EndMainMenuBar();
    }

    ImGui::PopStyleVar(2);

    if (show_imgui_metrics_window)
    {
        ImGui::ShowMetricsWindow();
    }

    if (show_imgui_style_window)
    {
        ImGui::Begin("Style Editor", nullptr, ImGuiWindowFlags_NoDocking);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }

    if (show_imgui_demo_widow)
    {
        ImGui::ShowDemoWindow(&show_imgui_demo_widow);
    }

    HandleKeyShortcuts();
    DrawFileDialog();
    window_about(m_editor);
    window_contributors(m_editor);
    window_shortcuts(m_editor);
}

void MenuBar::CreateWorldMenuItem()
{
    if (ImGui::BeginMenu("World"))
    {
        if (ImGui::MenuItem("New"))
        {
            Spartan::World::New();
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
}

void MenuBar::CreateViewMenuItem()
{
    if (ImGui::BeginMenu("View"))
    {
        widget_menu_item<Profiler>(m_editor);
        widget_menu_item<ShaderEditor>(m_editor);
        widget_menu_item<RenderOptions>(m_editor);
        widget_menu_item<TextureViewer>(m_editor);
        widget_menu_item<ResourceViewer>(m_editor);

        if (ImGui::BeginMenu("Widgets"))
        {
            widget_menu_item<AssetBrowser>(m_editor);
            widget_menu_item<Console>(m_editor);
            widget_menu_item<Properties>(m_editor);
            widget_menu_item<Viewport>(m_editor);
            widget_menu_item<WorldViewer>(m_editor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("ImGui"))
        {
            ImGui::MenuItem("Metrics", nullptr, &show_imgui_metrics_window);
            ImGui::MenuItem("Style", nullptr, &show_imgui_style_window);
            ImGui::MenuItem("Demo", nullptr, &show_imgui_demo_widow);
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }
}

void MenuBar::CreateHelpMenuItem()
{
    if (ImGui::BeginMenu("Help"))
    {
        ImGui::MenuItem("About", nullptr, &show_about_window);
        ImGui::MenuItem("Contributors", nullptr, &show_contributors_window);

        if (ImGui::MenuItem("How to contribute", nullptr, nullptr))
        {
            Spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/wiki/How-to-contribute");
        }

        if (ImGui::MenuItem("Join the Discord server", nullptr, nullptr))
        {
            Spartan::FileSystem::OpenUrl("https://discord.gg/TG5r2BS");
        }

        ImGui::MenuItem("Shortcuts & Input Reference", "Ctrl+P", &show_shortcuts_window);

        ImGui::EndMenu();
    }
}

void MenuBar::HandleKeyShortcuts() const
{
    if (Spartan::Input::GetKey(Spartan::KeyCode::Ctrl_Left) && Spartan::Input::GetKeyDown(Spartan::KeyCode::P))
    {
        show_shortcuts_window = !show_shortcuts_window;
    }
}

void MenuBar::ShowWorldSaveDialog()
{
    m_file_dialog->SetOperation(FileDialog_Op_Save);
    show_file_dialog = true;
}

void MenuBar::ShowWorldLoadDialog()
{
    m_file_dialog->SetOperation(FileDialog_Op_Load);
    show_file_dialog = true;
}

void MenuBar::DrawFileDialog() const
{
    if (show_file_dialog)
    {
        ImGui::SetNextWindowFocus();
    }

    if (m_file_dialog->Show(&show_file_dialog, m_editor, &file_dialog_selection_path))
    {
        // LOAD
        if (m_file_dialog->GetOperation() == FileDialog_Op_Open || m_file_dialog->GetOperation() == FileDialog_Op_Load)
        {
            // Scene
            if (Spartan::FileSystem::IsEngineSceneFile(file_dialog_selection_path))
            {
                EditorHelper::LoadWorld(file_dialog_selection_path);
                show_file_dialog = false;
            }
        }
        // SAVE
        else if (m_file_dialog->GetOperation() == FileDialog_Op_Save)
        {
            // Scene
            if (m_file_dialog->GetFilter() == FileDialog_Filter_World)
            {
                EditorHelper::SaveWorld(file_dialog_selection_path);
                show_file_dialog = false;
            }
        }
    }
}
