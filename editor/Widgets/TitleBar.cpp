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

//= INCLUDES =============================
#include "TitleBar.h"
#include "Core/Settings.h"
#include "Profiler.h"
#include "ShaderEditor.h"
#include "RenderOptions.h"
#include "TextureViewer.h"
#include "ResourceViewer.h"
#include "AssetBrowser.h"
#include "Console.h"
#include "Properties.h"
#include "Viewport.h"
#include "WorldViewer.h"
#include "../WidgetsDeferred/FileDialog.h"
#include "Engine.h"
#include "Profiling/RenderDoc.h"
//========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    bool show_shortcuts_window     = false;
    bool show_about_window         = false;
    bool show_file_dialog          = false;
    bool show_imgui_metrics_window = false;
    bool show_imgui_style_window   = false;
    bool show_imgui_demo_widow     = false;
    Editor* editor                 = nullptr;
    string file_dialog_selection_path;;
    unique_ptr<FileDialog> file_dialog;

    namespace
    { 
        vector<string> contributors =
        {
            // role,  name,                country,       button text,   button url,                                               contribution,                      steam key
            "Spartan, Iker Galardi,        Basque Country,   LinkedIn,   https://www.linkedin.com/in/iker-galardi/,                Linux port (WIP),                  N/A",
            "Spartan, Jesse Guerrero,      United States,    LinkedIn,   https://www.linkedin.com/in/jguer,                        UX improvements,                   N/A",
            "Spartan, Konstantinos Benos,  Greece,           Twitter,    https://twitter.com/deg3x,                                Editor theme & bug fixes,          N/A",
            "Spartan, Nick Polyderopoulos, Greece,           LinkedIn,   https://www.linkedin.com/in/nick-polyderopoulos-21742397, UX improvements,                   N/A",
            "Spartan, Panos Kolyvakis,     Greece,           LinkedIn,   https://www.linkedin.com/in/panos-kolyvakis-66863421a/,   Improved water buoyancy,           N/A",
            "Spartan, Tri Tran,            Belgium,          LinkedIn,   https://www.linkedin.com/in/mtrantr/,                     Days Gone screen space shadows,    Starfield",
                                                             
            "Hoplite, Apostolos Bouzalas,  Greece,           LinkedIn,   https://www.linkedin.com/in/apostolos-bouzalas,           Provided performance reports,      N/A",
            "Hoplite, Marlon Wolfersdorf,  Northern Germany, N/A,        ,                                                         Nautilus ACES,                     N/A",
            "Hoplite, Sandro Mtchedlidze,  Georgia,          Artstation, https://www.artstation.com/sandromch,                     Identified performance bottleneck, N/A",
        };

        vector<string> comma_seperate_contributors(const vector<string>& contributors)
        {
            vector<string> result;

            for (const auto& entry : contributors)
            {
                string processed_entry;
                bool space_allowed = true;
                for (char c : entry)
                {
                    if (c == ',')
                    {
                        processed_entry.push_back(c);
                        space_allowed = false;
                    }
                    else if (!space_allowed && c != ' ')
                    {
                        space_allowed = true;
                        processed_entry.push_back(c);
                    }
                    else if (space_allowed)
                    {
                        processed_entry.push_back(c);
                    }
                }

                istringstream ss(processed_entry);
                string item;
                while (getline(ss, item, ','))
                {
                    result.push_back(item);
                }
            }

            return result;
        }

        void window_about()
        {
            if (!show_about_window)
                return;

            vector<string> comma_seperated_contributors = comma_seperate_contributors(contributors);

            static const string window_title  = "Spartan " + to_string(sp_info::version_major) + "." + to_string(sp_info::version_minor) + "." + to_string(sp_info::version_revision);
            const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowFocus();
            ImGui::Begin(window_title.c_str(), &show_about_window, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
            {
                ImGui::BeginGroup();
                {
                    // shift text that the buttons and the text align
                    static const float y_shift = 6.0f;

                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                    ImGui::Text("Creator");

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                    if (ImGuiSp::button("Panos Karabelas"))
                    {
                        Spartan::FileSystem::OpenUrl("https://panoskarabelas.com/");
                    }

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                    if (ImGuiSp::button("GitHub"))
                    {
                        Spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine");
                    }

                    ImGui::SameLine();
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - y_shift);
                    if (ImGuiSp::button("X"))
                    {
                        Spartan::FileSystem::OpenUrl("https://twitter.com/panoskarabelas1");
                    }
                }
                ImGui::EndGroup();

                // group: license
                ImGui::BeginGroup();
                {
                    static const char* license_text =
                        "MIT License"
                        "\n\n"
                        "Permission is hereby granted, free of charge, to any person obtaining a copy"
                        "of this software and associated documentation files (the \"Software\"), to deal"
                        "in the Software without restriction, including without limitation the rights"
                        "to use, copy, modify, merge, publish, distribute, sublicense, and/or sell"
                        "copies of the Software, and to permit persons to whom the Software is"
                        "furnished to do so, subject to the following conditions:"
                        "\n\n"
                        "The above copyright notice and this permission notice shall be included in all"
                        "copies or substantial portions of the Software."
                        "\n\n"
                        "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR"
                        "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,"
                        "FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE"
                        "AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER"
                        "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,"
                        "OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE"
                        "SOFTWARE.";

                    ImGui::Separator();
                    ImGui::TextWrapped(license_text);
                }
                ImGui::EndGroup();

                ImGui::Separator();
                ImGui::Dummy(ImVec2(0.0f, 10.0f));

                // group: contributors
                ImGui::SameLine();
                ImGui::BeginGroup();
                {
                    ImGui::Text("Contributors");
                    if (ImGui::BeginTable("##contributors_table", 6, table_flags, ImVec2(ImGui::GetContentRegionAvail().x * 0.71f, 0.0f)))
                    {
                        ImGui::TableSetupColumn("Title");
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Country");
                        ImGui::TableSetupColumn("URL");
                        ImGui::TableSetupColumn("Contribution");
                        ImGui::TableSetupColumn("Steam Key");
                        ImGui::TableHeadersRow();

                        uint32_t index = 0;
                        for (uint32_t i = 0; i < static_cast<uint32_t>(contributors.size()); i++)
                        {
                            // switch row
                            ImGui::TableNextRow();

                            // shift text down so that it's on the same line with the button
                            static const float y_shift = 8.0f;

                            // role
                            ImGui::TableSetColumnIndex(0);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                            ImGui::Text(comma_seperated_contributors[index++].c_str());

                            // name
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                            ImGui::Text(comma_seperated_contributors[index++].c_str());

                            // country
                            ImGui::TableSetColumnIndex(2);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                            ImGui::Text(comma_seperated_contributors[index++].c_str());

                            // button (url)
                            ImGui::TableSetColumnIndex(3);
                            string& button_text = comma_seperated_contributors[index++];
                            string& button_url  = comma_seperated_contributors[index++];
                            // calculate center position for the button
                            float cell_width    = ImGui::GetColumnWidth();
                            float button_width  = ImGui::CalcTextSize(button_text.c_str()).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                            float button_offset = (cell_width - button_width) * 0.5f;
                            // set cursor position to center the button
                            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + button_offset);
                            ImGui::PushID(static_cast<uint32_t>(ImGui::GetCursorScreenPos().y));
                            if (ImGui::Button(button_text.c_str()))
                            {
                                Spartan::FileSystem::OpenUrl(button_url);
                            }
                            ImGui::PopID();

                            // contribution
                            ImGui::TableSetColumnIndex(4);
                            ImGui::Text(comma_seperated_contributors[index++].c_str());

                            // steam key award
                            ImGui::TableSetColumnIndex(5);
                            ImGui::Text(comma_seperated_contributors[index++].c_str());
                        }
                    }
                    ImGui::EndTable();

                    // group: role explanations
                    ImGui::BeginGroup();
                    {
                        ImGui::Text("Title explanations");
                        ImGui::Text("Spartan: Made code contributions that fixed or added significant new functionality.");
                        ImGui::Text("Hoplite: Provided valuable insights, suggestions, or minor code contributions that improved the project.");
                    }
                    ImGui::EndGroup();
                }
                ImGui::EndGroup();

                // group: third party libraries
                ImGui::SameLine();
                ImGui::BeginGroup();
                {
                    ImGui::Text("Third party libraries");
                    if (ImGui::BeginTable("##third_party_libs_table", 3, table_flags))
                    {
                        ImGui::TableSetupColumn("Name");
                        ImGui::TableSetupColumn("Version");
                        ImGui::TableSetupColumn("URL");
                        ImGui::TableHeadersRow();

                        for (const Spartan::third_party_lib& lib : Spartan::Settings::GetThirdPartyLibs())
                        {
                            // switch row
                            ImGui::TableNextRow();

                            // shift text down so that it's on the same line with the button
                            static const float y_shift = 8.0f;

                            // name
                            ImGui::TableSetColumnIndex(0);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                            ImGui::Text(lib.name.c_str());

                            // version
                            ImGui::TableSetColumnIndex(1);
                            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + y_shift);
                            ImGui::Text(lib.version.c_str());

                            // url
                            ImGui::TableSetColumnIndex(2);
                            ImGui::PushID(lib.url.c_str());
                            if (ImGuiSp::button("URL"))
                            {
                                Spartan::FileSystem::OpenUrl(lib.url);
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                }
                ImGui::EndGroup();
            }
            ImGui::End();
        }

        void window_shortcuts()
        {
            if (!show_shortcuts_window)
                return;

            ImGui::SetNextWindowPos(editor->GetWidget<Viewport>()->GetCenter(), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowFocus();
            ImGui::Begin("Shortcuts & Input Reference", &show_shortcuts_window, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);
            {
                const float col_a = 220.0f;
                const float col_b = 20.0f;

                {
                    struct Shortcut
                    {
                        char* shortcut;
                        char* usage;
                    };

                    static const Shortcut shortcuts[] =
                    {
                        {(char*)"Ctrl+P",       (char*)"Open shortcuts & input reference window"},
                        {(char*)"Ctrl+S",       (char*)"Save world"},
                        {(char*)"Ctrl+L",       (char*)"Load world"},
                        {(char*)"Right click",  (char*)"Enable first person camera control"},
                        {(char*)"W, A, S, D",   (char*)"Move camera"},
                        {(char*)"Q, E",         (char*)"Change camera elevation"},
                        {(char*)"F",            (char*)"Center camera on object"},
                        {(char*)"Alt+Enter",    (char*)"Toggle fullscreen viewport"},
                        {(char*)"Ctrl+Z",       (char*)"Undo"},
                        {(char*)"Ctrl+Shift+Z", (char*)"Redo"}
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
        void menu_entry()
        {
            T* widget = editor->GetWidget<T>();

            // menu item with checkmark based on widget->GetVisible()
            if (ImGui::MenuItem(widget->GetTitle().c_str(), nullptr, widget->GetVisible()))
            {
                // toggle visibility
                widget->SetVisible(!widget->GetVisible());
            }
        }
    }

    namespace buttons_toolbar
    {
        float button_size               = 19.0f;
        ImVec4 button_color_play        = { 0.2f, 0.7f, 0.35f, 1.0f };
        ImVec4 button_color_play_hover  = { 0.22f, 0.8f, 0.4f, 1.0f };
        ImVec4 button_color_play_active = { 0.1f, 0.4f, 0.2f, 1.0f };
        ImVec4 button_color_doc         = { 0.25f, 0.7f, 0.75f, 0.9f };
        ImVec4 button_color_doc_hover   = { 0.3f, 0.75f, 0.8f, 0.9f };
        ImVec4 button_color_doc_active  = { 0.2f, 0.65f, 0.7f, 0.9f };
        unordered_map<IconType, Widget*> widgets;

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
            const float button_size_y = button_size + 2.0f * TitleBar::GetPadding().y;
            const float offset_y      = (button_size_y - size_avail_y) * 0.5f;

            ImGui::SetCursorPosY(offset_y);

            if (ImGuiSp::image_button(static_cast<uint64_t>(icon_type), nullptr, icon_type, button_size * Spartan::Window::GetDpiScale(), false))
            {
                on_press();
            }

            ImGui::PopStyleColor();

            ImGuiSp::tooltip(tooltip_text.c_str());
        }

        void tick()
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float size_avail_x      = viewport->Size.x;
            const float button_size_final = button_size * Spartan::Window::GetDpiScale() + TitleBar::GetPadding().x * 2.0f;
            float num_buttons             = 1.0f;
            float size_toolbar            = num_buttons * button_size_final;
            float cursor_pos_x            = (size_avail_x - size_toolbar) * 0.5f;

            // play button
            {
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { 18.0f, TitleBar::GetPadding().y - 2.0f });
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
          
            // all the other buttons
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, { TitleBar::GetPadding().x, TitleBar::GetPadding().y - 2.0f });
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2.0f , 0.0f });
            ImGui::PushStyleColor(ImGuiCol_Button, button_color_doc);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, button_color_doc_hover);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, button_color_doc_active);
            {
                num_buttons  = 6.0f;
                size_toolbar = num_buttons * button_size_final + (num_buttons - 1.0f) * ImGui::GetStyle().ItemSpacing.x;
                cursor_pos_x = size_avail_x - (size_toolbar - 2.0f);

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
                for (auto& widget_it : widgets)
                {
                    Widget* widget = widget_it.second;
                    const IconType widget_icon = widget_it.first;

                    toolbar_button(widget_icon, widget->GetTitle(),
                        [&widget]() { return widget->GetVisible(); },
                        [&widget]() { widget->SetVisible(true); }
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
    }

    namespace buttons_window
    {
        void tick()
        {
            // snap to the right
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float size_avail_x      = viewport->Size.x;
            const float offset_right      = 120.0f * Spartan::Window::GetDpiScale();
            ImGui::SetCursorPosX(size_avail_x - offset_right);

            Spartan::Math::Vector2 icon_size = Spartan::Math::Vector2(24.0f, 24.0f);
            if (ImGuiSp::image_button(0, nullptr, IconType::Window_Minimize, icon_size, false))
            {
                Spartan::Window::Minimize();
            }

            if (ImGuiSp::image_button(1, nullptr, IconType::Window_Maximize, icon_size, false))
            {
                Spartan::Window::Maximize();
            }

            if (ImGuiSp::image_button(2, nullptr, IconType::Window_Close, icon_size, false))
            {
                Spartan::Window::Close();
            }
        }
    }
}

TitleBar::TitleBar(Editor* _editor) : Widget(_editor)
{
    m_title     = "title_bar";
    m_is_window = false;
    editor      = _editor;
    file_dialog = make_unique<FileDialog>(true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_World);

    buttons_toolbar::widgets[IconType::Button_Profiler]        = editor->GetWidget<Profiler>();
    buttons_toolbar::widgets[IconType::Button_ResourceCache]   = editor->GetWidget<ResourceViewer>();
    buttons_toolbar::widgets[IconType::Button_Shader]          = editor->GetWidget<ShaderEditor>();
    buttons_toolbar::widgets[IconType::Component_Options]      = editor->GetWidget<RenderOptions>();
    buttons_toolbar::widgets[IconType::Directory_File_Texture] = editor->GetWidget<TextureViewer>();

    Spartan::Engine::SetFlag(Spartan::EngineMode::Game, false);
}

void TitleBar::OnTick()
{
    // menu
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, GetPadding());
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

        if (ImGui::BeginMainMenuBar())
        {
            EntryWorld();
            EntryView();
            EntryHelp();

            buttons_toolbar::tick();

            ImGui::EndMainMenuBar();
        }

        ImGui::PopStyleVar(2);
    }

    // windows
    {
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

        window_about();
        window_shortcuts();
    }

    HandleKeyShortcuts();
    DrawFileDialog();
}

void TitleBar::EntryWorld()
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

void TitleBar::EntryView()
{
    if (ImGui::BeginMenu("View"))
    {
        if (ImGui::MenuItem("Shortcuts & Input Reference", "Ctrl+P", &show_shortcuts_window))
        {

        }

        if (ImGui::BeginMenu("Widgets"))
        {
            menu_entry<Profiler>();
            menu_entry<ShaderEditor>();
            menu_entry<RenderOptions>();
            menu_entry<TextureViewer>();
            menu_entry<ResourceViewer>();
            menu_entry<AssetBrowser>();
            menu_entry<Console>();
            menu_entry<Properties>();
            menu_entry<Viewport>();
            menu_entry<WorldViewer>();

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

void TitleBar::EntryHelp()
{
    if (ImGui::BeginMenu("Help"))
    {
        ImGui::MenuItem("About", nullptr, &show_about_window);

        if (ImGui::MenuItem("Contributing", nullptr, nullptr))
        {
            Spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md");
        }

        if (ImGui::MenuItem("Perks of a contributor", nullptr, nullptr))
        {
            Spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor");
        }

        if (ImGui::MenuItem("Report a bug", nullptr, nullptr))
        {
            Spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/issues/new/choose");
        }

        if (ImGui::MenuItem("Join the Discord server", nullptr, nullptr))
        {
            Spartan::FileSystem::OpenUrl("https://discord.gg/TG5r2BS");
        }

        ImGui::EndMenu();
    }
}

void TitleBar::HandleKeyShortcuts() const
{
    if (Spartan::Input::GetKey(Spartan::KeyCode::Ctrl_Left) && Spartan::Input::GetKeyDown(Spartan::KeyCode::P))
    {
        show_shortcuts_window = !show_shortcuts_window;
    }
}

void TitleBar::ShowWorldSaveDialog()
{
    file_dialog->SetOperation(FileDialog_Op_Save);
    show_file_dialog = true;
}

void TitleBar::ShowWorldLoadDialog()
{
    file_dialog->SetOperation(FileDialog_Op_Load);
    show_file_dialog = true;
}

void TitleBar::DrawFileDialog() const
{
    if (show_file_dialog)
    {
        ImGui::SetNextWindowFocus();
    }

    if (file_dialog->Show(&show_file_dialog, m_editor, nullptr, &file_dialog_selection_path))
    {
        // load world
        if (file_dialog->GetOperation() == FileDialog_Op_Open || file_dialog->GetOperation() == FileDialog_Op_Load)
        {
            if (Spartan::FileSystem::IsEngineSceneFile(file_dialog_selection_path))
            {
                EditorHelper::LoadWorld(file_dialog_selection_path);
                show_file_dialog = false;
            }
        }
        // save world
        else if (file_dialog->GetOperation() == FileDialog_Op_Save)
        {
            if (file_dialog->GetFilter() == FileDialog_Filter_World)
            {
                EditorHelper::SaveWorld(file_dialog_selection_path);
                show_file_dialog = false;
            }
        }
    }
}
