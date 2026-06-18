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

//= INCLUDES ====================
#include "pch.h"
#include "MenuBar.h"
#include "Profiler.h"
#include "ShaderEditor.h"
#include "RenderOptions.h"
#include "TextureViewer.h"
#include "ResourceViewer.h"
#include "McpAssistant.h"
#include "AssetBrowser.h"
#include "Console.h"
#include "Properties.h"
#include "Viewport.h"
#include "WorldViewer.h"
#include "FileDialog.h"
#include "Style.h"
#include "Engine.h"
#include "Rendering/Renderer.h"
#include "Profiling/RenderDoc.h"
#include "Debugging.h"
#include "ScriptEditor.h"
#include "Sequence/Sequencer.h"
#include "Core/Definitions.h"
#include "Core/ThreadPool.h"
#include "MCP/McpServer.h"
#include "../WorldPreviews.h"
#include "../GeneralWindows.h"
#include "../ImGui/ImGui_Style.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    bool show_file_dialog          = false;
    bool show_imgui_metrics_window = false;
    bool show_imgui_style_window   = false;
    bool show_imgui_demo_widow     = false;
    Editor* editor                 = nullptr;
    string file_dialog_selection_path;
    unique_ptr<FileDialog> file_dialog;

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

    namespace windows
    {
        void ShowWorldSaveDialog()
        {
            file_dialog->SetOperation(FileDialog_Op_Save);

            // navigate to the directory of the currently loaded world
            const std::string& world_file_path = spartan::World::GetFilePath();
            if (!world_file_path.empty())
            {
                file_dialog->SetCurrentPath(world_file_path);
            }

            show_file_dialog = true;
        }

        // save the current world to its existing path, falls back to the save-as dialog
        // when there is no loaded world yet so the user can pick a destination
        void SaveWorld()
        {
            const std::string& world_file_path = spartan::World::GetFilePath();
            if (world_file_path.empty())
            {
                ShowWorldSaveDialog();
                return;
            }

            spartan::ThreadPool::AddTask([world_file_path]()
            {
                spartan::World::SaveToFile(world_file_path);
            });
        }

        void ShowWorldLoadDialog()
        {
            file_dialog->SetOperation(FileDialog_Op_Load);
            show_file_dialog = true;
        }

        void ExportWorld()
        {
            const std::string& world_file_path = spartan::World::GetFilePath();
            if (world_file_path.empty())
            {
                SP_LOG_WARNING("No world is currently loaded. Save the world first before exporting.");
                return;
            }

            spartan::ThreadPool::AddTask([world_file_path]()
            {
                // get the world name and construct paths
                std::string world_name     = spartan::FileSystem::GetFileNameWithoutExtensionFromFilePath(world_file_path);
                std::string world_dir      = spartan::FileSystem::GetDirectoryFromFilePath(world_file_path);
                std::string resources_dir  = world_dir + world_name + "_resources";
                std::string archive_path   = world_dir + world_name + ".7z";

                // collect paths to include in the archive
                std::vector<std::string> paths_to_include;
                paths_to_include.push_back(world_file_path);

                // add resources directory if it exists
                if (spartan::FileSystem::Exists(resources_dir))
                {
                    paths_to_include.push_back(resources_dir);
                }

                // create the archive
                if (spartan::FileSystem::CreateArchive(archive_path, paths_to_include))
                {
                    SP_LOG_INFO("World exported to: %s", archive_path.c_str());
                }
            });
        }

        void DrawFileDialog()
        {
            // focus only on the frame the dialog opens, otherwise focus is stolen from the overwrite popup every frame
            static bool show_file_dialog_prev = false;
            if (show_file_dialog && !show_file_dialog_prev)
            {
                ImGui::SetNextWindowFocus();
            }
            show_file_dialog_prev = show_file_dialog;

            if (file_dialog->Show(&show_file_dialog, editor, nullptr, &file_dialog_selection_path))
            {
                // load world
                if (file_dialog->GetOperation() == FileDialog_Op_Open || file_dialog->GetOperation() == FileDialog_Op_Load)
                {
                    if (spartan::FileSystem::IsEngineSceneFile(file_dialog_selection_path))
                    {
                        WorldPreviews::RequestGeneration(file_dialog_selection_path);
                        spartan::World::LoadFromFile(file_dialog_selection_path);
                        show_file_dialog = false;
                    }
                }

                // save world
                else if (file_dialog->GetOperation() == FileDialog_Op_Save)
                {
                    if (file_dialog->GetFilter() == FileDialog_Filter_World)
                    {
                        spartan::ThreadPool::AddTask([]()
                        {
                            spartan::World::SaveToFile(file_dialog_selection_path);
                        });

                        show_file_dialog = false;
                    }
                }
            }
        }
    }

    namespace buttons_menu
    {
        void world()
        {
            if (ImGui::BeginMenu("World"))
            {
                if (ImGui::MenuItem("New"))
                {
                    spartan::World::Shutdown();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Load"))
                {
                    windows::ShowWorldLoadDialog();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    windows::SaveWorld();
                }

                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                {
                    windows::ShowWorldSaveDialog();
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Export"))
                {
                    windows::ExportWorld();
                }

                ImGui::EndMenu();
            }
        }

        void view()
        {
            if (ImGui::BeginMenu("View"))
            {
                bool* controls_visible = GeneralWindows::GetVisiblityWindowControls();
                if (ImGui::MenuItem("Controls", "Ctrl+P", *controls_visible))
                {
                    *controls_visible = !*controls_visible;
                }

                if (ImGui::BeginMenu("Widgets"))
                {
                    menu_entry<Profiler>();
                    menu_entry<ShaderEditor>();
                    menu_entry<ScriptEditor>();
                    menu_entry<RenderOptions>();
                    menu_entry<TextureViewer>();
                    menu_entry<ResourceViewer>();
                    menu_entry<McpAssistant>();
                    menu_entry<AssetBrowser>();
                    menu_entry<Console>();
                    menu_entry<Properties>();
                    menu_entry<Viewport>();
                    menu_entry<WorldViewer>();
                    menu_entry<Sequencer>();

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

        void help()
        {
            if (ImGui::BeginMenu("Help"))
            {
                bool* about_visible = GeneralWindows::GetVisiblityWindowAbout();
                if (ImGui::MenuItem("About", nullptr, *about_visible))
                {
                    *about_visible = !*about_visible;
                }

                if (ImGui::MenuItem("Sponsor", nullptr, nullptr))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/sponsors/PanosK92");
                }

                if (ImGui::MenuItem("Contributing", nullptr, nullptr))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/blob/master/contributing.md");
                }

                if (ImGui::MenuItem("Perks of a contributor", nullptr, nullptr))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/wiki/Perks-of-a-contributor");
                }

                if (ImGui::MenuItem("Report a bug", nullptr, nullptr))
                {
                    spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/issues/new/choose");
                }

                if (ImGui::MenuItem("Join the Discord server", nullptr, nullptr))
                {
                    spartan::FileSystem::OpenUrl("https://discord.gg/TG5r2BS");
                }

                ImGui::EndMenu();
            }
        }
    }

    // forward declaration for buttons_titlebar
    namespace buttons_titlebar { float get_total_width(); }

    namespace buttons_toolbar
    {
        float button_size = 19.0f;
        vector<pair<spartan::IconType, Widget*>> widgets;

        float dpi()
        {
            return spartan::Window::GetDpiScale();
        }

        ImVec4 with_alpha(const ImVec4& color, float alpha)
        {
            return ImVec4(color.x, color.y, color.z, alpha);
        }

        float group_padding_x()
        {
            return 6.0f * dpi();
        }

        float group_gap()
        {
            return 8.0f * dpi();
        }

        float button_gap()
        {
            return 3.0f * dpi();
        }

        float group_rounding()
        {
            return 5.0f * dpi();
        }

        float tool_icon_size()
        {
            return button_size * dpi();
        }

        ImVec2 tool_padding()
        {
            return ImVec2(8.0f * dpi(), 4.0f * dpi());
        }

        float tool_button_width()
        {
            return tool_icon_size() + tool_padding().x * 2.0f;
        }

        float tool_button_height()
        {
            return tool_icon_size() + tool_padding().y * 2.0f;
        }

        ImVec2 transport_padding()
        {
            return ImVec2(13.0f * dpi(), 4.0f * dpi());
        }

        float transport_icon_size()
        {
            return 22.0f * dpi();
        }

        float transport_button_width()
        {
            return transport_icon_size() + transport_padding().x * 2.0f;
        }

        float transport_button_height()
        {
            return transport_icon_size() + transport_padding().y * 2.0f;
        }

        float get_transport_width()
        {
            return group_padding_x() * 2.0f + transport_button_width() * 2.0f + button_gap();
        }

        float icon_group_width(const float button_count)
        {
            if (button_count <= 0.0f)
            {
                return 0.0f;
            }

            return group_padding_x() * 2.0f + button_count * tool_button_width() + (button_count - 1.0f) * button_gap();
        }

        float snap_group_width()
        {
            return icon_group_width(1.0f);
        }

        float panel_group_width()
        {
            return icon_group_width(2.0f + static_cast<float>(widgets.size()));
        }

        float get_right_toolbar_width()
        {
            const float capture_buttons = 2.0f;
            return snap_group_width() + icon_group_width(capture_buttons) + panel_group_width() + group_gap() * 2.0f;
        }

        float get_right_toolbar_start(float menubar_width)
        {
            return max(0.0f, menubar_width - get_right_toolbar_width() - buttons_titlebar::get_total_width());
        }

        float centered_y(float menubar_height, float height)
        {
            return max(0.0f, (menubar_height - height) * 0.5f);
        }

        void draw_group_background(float start_x, float width, float menubar_height)
        {
            if (width <= 0.0f)
            {
                return;
            }

            const float scale      = dpi();
            const float inset_y    = 2.0f * scale;
            const float rounding   = group_rounding();
            ImVec2 window_pos      = ImGui::GetWindowPos();
            ImVec2 min_pos         = ImVec2(window_pos.x + start_x, window_pos.y + inset_y);
            ImVec2 max_pos         = ImVec2(window_pos.x + start_x + width, window_pos.y + menubar_height - inset_y);
            ImVec4 fill           = ImGui::Style::lerp(ImGui::Style::bg_color_1, ImGui::Style::bg_color_2, 0.35f);
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            draw_list->AddRectFilled(min_pos, max_pos, ImGui::GetColorU32(with_alpha(fill, 0.70f)), rounding);
            draw_list->AddRect(min_pos, max_pos, IM_COL32(255, 255, 255, 18), rounding);
        }

        void push_button_colors(bool is_active)
        {
            const ImVec4 accent = ImGui::Style::color_accent_1;
            if (is_active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        with_alpha(accent, 0.24f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(accent, 0.38f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  with_alpha(accent, 0.54f));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.09f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1, 1, 1, 0.16f));
            }
        }

        void draw_active_underline()
        {
            const float scale = dpi();
            ImVec2 min_pos    = ImGui::GetItemRectMin();
            ImVec2 max_pos    = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(min_pos.x + 5.0f * scale, max_pos.y - 2.0f * scale),
                ImVec2(max_pos.x - 5.0f * scale, max_pos.y - 2.0f * scale),
                ImGui::GetColorU32(ImGui::Style::color_accent_1),
                max(1.0f, 2.0f * scale)
            );
        }

        void toolbar_button(spartan::IconType icon_type, const char* tooltip_text, bool (*get_visibility)(Widget*), void (*on_press)(Widget*), Widget* widget = nullptr, float cursor_pos_x = -1.0f)
        {
            const bool is_active = get_visibility(widget);

            if (cursor_pos_x >= 0.0f)
            {
                ImGui::SetCursorPosX(cursor_pos_x);
            }

            ImGui::SetCursorPosY(centered_y(ImGui::GetWindowHeight(), tool_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tool_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(is_active);

            const ImVec4 tint = is_active ? ImGui::Style::color_accent_1 : ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
            if (ImGuiSp::image_button(icon_type, spartan::math::Vector2(tool_icon_size(), tool_icon_size()), false, tint))
            {
                on_press(widget);
            }

            if (is_active)
            {
                draw_active_underline();
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGuiSp::tooltip(tooltip_text);
        }

        void toggle_playing()
        {
            spartan::Engine::ToggleFlag(spartan::EngineMode::Playing);

            // clear paused state when leaving play mode
            if (!spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
            {
                spartan::Engine::SetFlag(spartan::EngineMode::Paused, false);
            }

            if (spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
            {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
            }
            else
            {
                ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            }
        }

        void toggle_paused()
        {
            if (spartan::Engine::IsFlagSet(spartan::EngineMode::Playing))
            {
                spartan::Engine::ToggleFlag(spartan::EngineMode::Paused);
            }
        }

        void draw_snap_button(float menubar_height, float cursor_pos_x)
        {
            bool snap_enabled = spartan::cvar_transform_snap.GetValueAs<bool>();

            ImGui::SetCursorPosX(cursor_pos_x);
            ImGui::SetCursorPosY(centered_y(menubar_height, tool_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tool_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(snap_enabled);

            const ImVec4 tint = snap_enabled ? ImGui::Style::color_accent_1 : ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
            if (ImGuiSp::image_button(spartan::IconType::Snap, spartan::math::Vector2(tool_icon_size(), tool_icon_size()), false, tint))
            {
                spartan::ConsoleRegistry::Get().SetValueFromString("r.transform_snap", snap_enabled ? "0" : "1");
            }

            if (snap_enabled)
            {
                draw_active_underline();
            }

            ImGuiSp::tooltip("Snap transforms");

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        void draw_mcp_button(float menubar_height, float cursor_pos_x)
        {
            const bool is_running = spartan::McpServer::IsRunning();
            McpAssistant* assistant = editor->GetWidget<McpAssistant>();
            const bool is_visible = assistant ? assistant->GetVisible() : false;

            if (cursor_pos_x >= 0.0f)
            {
                ImGui::SetCursorPosX(cursor_pos_x);
            }
            ImGui::SetCursorPosY(centered_y(menubar_height, tool_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tool_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(is_running || is_visible);

            const ImVec4 tint = (is_running || is_visible) ? ImGui::Style::color_accent_1 : ImVec4(0.86f, 0.86f, 0.86f, 1.0f);
            if (ImGuiSp::image_button(spartan::IconType::Mcp, spartan::math::Vector2(tool_icon_size(), tool_icon_size()), false, tint))
            {
                if (assistant)
                {
                    assistant->SetVisible(!assistant->GetVisible());
                }
            }

            if (is_running || is_visible)
            {
                draw_active_underline();
            }

            string tooltip = is_visible ? "Close MCP Assistant" : "Open MCP Assistant";
            tooltip += is_running ?
                "\nMCP active on 127.0.0.1:" + to_string(spartan::McpServer::GetPort()) :
                "\nMCP inactive";
            ImGuiSp::tooltip(tooltip.c_str());

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        bool draw_pause_button(float menubar_height, bool is_playing, bool is_active)
        {
            ImGui::SetCursorPosY(centered_y(menubar_height, transport_button_height()));
            ImVec2 btn_size = ImVec2(transport_button_width(), transport_button_height());

            ImGui::PushID("##pause_btn");
            bool pressed = ImGui::InvisibleButton("##pause", btn_size);
            bool hovered = ImGui::IsItemHovered();
            bool held    = ImGui::IsItemActive();
            ImGui::PopID();

            ImVec2 min_pos      = ImGui::GetItemRectMin();
            ImVec2 max_pos      = ImGui::GetItemRectMax();
            const ImVec4 accent = ImGui::Style::color_accent_1;
            ImVec4 bg           = ImVec4(0, 0, 0, 0);

            if (is_active)
            {
                bg = with_alpha(accent, held ? 0.54f : hovered ? 0.38f : 0.24f);
            }
            else if (hovered)
            {
                bg = ImVec4(1, 1, 1, held ? 0.16f : 0.09f);
            }

            if (bg.w > 0.0f)
            {
                ImGui::GetWindowDrawList()->AddRectFilled(min_pos, max_pos, ImGui::GetColorU32(bg), group_rounding());
            }

            float icon_size = transport_icon_size();
            float cx        = (min_pos.x + max_pos.x) * 0.5f;
            float cy        = (min_pos.y + max_pos.y) * 0.5f;
            float bar_h     = icon_size * 0.56f;
            float bar_w     = max(2.0f * dpi(), icon_size * 0.13f);
            float gap       = icon_size * 0.18f;
            ImU32 col       = is_active ? ImGui::GetColorU32(accent) : ImGui::GetColorU32(ImVec4(0.86f, 0.86f, 0.86f, is_playing ? 1.0f : 0.45f));

            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cx - gap - bar_w, cy - bar_h * 0.5f), ImVec2(cx - gap, cy + bar_h * 0.5f), col);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(cx + gap, cy - bar_h * 0.5f), ImVec2(cx + gap + bar_w, cy + bar_h * 0.5f), col);

            ImGuiSp::tooltip("Pause (F6)");
            return pressed;
        }

        void draw_transport_group(float menubar_height, float cursor_pos_x)
        {
            const bool is_playing = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing);
            const bool is_paused  = spartan::Engine::IsFlagSet(spartan::EngineMode::Paused);

            draw_group_background(cursor_pos_x, get_transport_width(), menubar_height);

            float button_x = cursor_pos_x + group_padding_x();
            ImGui::SetCursorPosX(button_x);
            ImGui::SetCursorPosY(centered_y(menubar_height, transport_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, transport_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(is_playing && !is_paused);

            const ImVec4 play_tint = (is_playing && !is_paused) ? ImGui::Style::color_accent_1 : ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
            if (ImGuiSp::image_button(spartan::IconType::Play, spartan::math::Vector2(transport_icon_size(), transport_icon_size()), false, play_tint))
            {
                toggle_playing();
            }
            ImGuiSp::tooltip("Play (F5)");

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);

            ImGui::SameLine(0, button_gap());
            if (draw_pause_button(menubar_height, is_playing, is_paused))
            {
                toggle_paused();
            }
        }

        void draw_right_groups(float menubar_height, float cursor_pos_x)
        {
            float group_x = cursor_pos_x;

            {
                float width = snap_group_width();
                draw_group_background(group_x, width, menubar_height);
                draw_snap_button(menubar_height, group_x + group_padding_x());
                group_x += width + group_gap();
            }

            {
                float width = icon_group_width(2.0f);
                draw_group_background(group_x, width, menubar_height);

                static auto screenshot_visible = [](Widget*) { return false; };
                static auto screenshot_press   = [](Widget*) { spartan::Renderer::Screenshot(); };
                toolbar_button(spartan::IconType::Screenshot, "Screenshot", screenshot_visible, screenshot_press, nullptr, group_x + group_padding_x());

                ImGui::SameLine(0, button_gap());

                static auto renderdoc_visible = [](Widget*) { return false; };
                static auto renderdoc_press   = [](Widget*)
                {
                    if (spartan::Debugging::IsRenderdocEnabled())
                    {
                        spartan::RenderDoc::FrameCapture();
                    }
                    else
                    {
                        SP_LOG_WARNING("RenderDoc integration is disabled. To enable, go to \"Debugging.h\", and set \"is_renderdoc_enabled\" to \"true\"");
                    }
                };
                toolbar_button(spartan::IconType::RenderDoc, "RenderDoc capture", renderdoc_visible, renderdoc_press, nullptr);

                group_x += width + group_gap();
            }

            {
                float width = panel_group_width();
                draw_group_background(group_x, width, menubar_height);

                static auto world_visible = [](Widget*) { return GeneralWindows::GetVisibilityWorlds(); };
                static auto world_press   = [](Widget*) { GeneralWindows::SetVisibilityWorlds(!GeneralWindows::GetVisibilityWorlds()); };
                toolbar_button(spartan::IconType::Terrain, "Worlds", world_visible, world_press, nullptr, group_x + group_padding_x());

                ImGui::SameLine(0, button_gap());
                draw_mcp_button(menubar_height, -1.0f);

                for (auto& widget_it : widgets)
                {
                    ImGui::SameLine(0, button_gap());

                    Widget* widget_ptr             = widget_it.second;
                    spartan::IconType icon         = widget_it.first;
                    static auto is_widget_visible  = [](Widget* w) { return w->GetVisible(); };
                    static auto set_widget_visible = [](Widget* w) { w->SetVisible(!w->GetVisible()); };
                    toolbar_button(icon, widget_ptr->GetTitle(), is_widget_visible, set_widget_visible, widget_ptr);
                }
            }
        }

        void tick(float menubar_height, float left_content_end_x)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float size_avail_x      = viewport->Size.x;
            const float right_start_x    = get_right_toolbar_start(size_avail_x);
            const float transport_width  = get_transport_width();
            const float transport_min_x  = left_content_end_x + group_gap();
            const float transport_max_x  = right_start_x - transport_width - group_gap();
            float transport_pos_x        = (size_avail_x - transport_width) * 0.5f;

            if (transport_min_x <= transport_max_x)
            {
                transport_pos_x = max(transport_min_x, min(transport_pos_x, transport_max_x));
            }
            else
            {
                transport_pos_x = transport_min_x;
            }

            draw_transport_group(menubar_height, transport_pos_x);
            draw_right_groups(menubar_height, right_start_x);
        }
    }

    // window buttons: minimize, maximize, close for custom title bar
    namespace buttons_titlebar
    {
        const float icon_size_base     = 12.0f;  // base icon size
        const float button_padding_x   = 18.0f;  // horizontal padding around each button
        const float button_padding_y   = 8.0f;   // vertical padding
        const float separator_gap      = 20.0f;  // gap between toolbar and window controls

        float get_total_width()
        {
            // 3 buttons width + separator gap + margin
            float dpi = spartan::Window::GetDpiScale();
            float margin = 2.0f;
            return (3.0f * (icon_size_base + button_padding_x * 2.0f) + separator_gap + margin) * dpi;
        }

        void tick(float menubar_height)
        {
            const float dpi = spartan::Window::GetDpiScale();

            const float icon_size_scaled = icon_size_base * dpi;
            const float button_width     = icon_size_scaled + button_padding_x * 2.0f * dpi;
            const spartan::math::Vector2 icon_size = spartan::math::Vector2(icon_size_scaled, icon_size_scaled);

            // calculate vertical centering
            const float button_height = icon_size_scaled + button_padding_y * 2.0f * dpi;
            const float offset_y = (menubar_height - button_height) * 0.5f;

            // position first button - use window width and account for small margin
            const float window_width = ImGui::GetWindowWidth();
            const float margin = 2.0f * dpi;  // small margin from edge
            float start_x = window_width - (3.0f * button_width) - margin;
            ImGui::SetCursorPosX(start_x);
            ImGui::SetCursorPosY(offset_y);

            // minimize button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.1f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.2f));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(button_padding_x * dpi, button_padding_y * dpi));

            if (ImGuiSp::image_button(spartan::IconType::Minimize, icon_size, false))
            {
                spartan::Window::Minimize();
            }

            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosY(offset_y);

            // maximize/restore button
            if (ImGuiSp::image_button(spartan::IconType::Maximize, icon_size, false))
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

            if (ImGuiSp::image_button(spartan::IconType::X, icon_size, false))
            {
                spartan::Window::Close();
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }
    }
}

void MenuBar::Initialize(Editor* _editor)
{
    editor      = _editor;
    file_dialog = make_unique<FileDialog>(true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_World);

    buttons_toolbar::widgets.push_back({ spartan::IconType::Profiler,      editor->GetWidget<Profiler>()       });
    buttons_toolbar::widgets.push_back({ spartan::IconType::ResourceCache, editor->GetWidget<ResourceViewer>() });
    buttons_toolbar::widgets.push_back({ spartan::IconType::Shader,        editor->GetWidget<ShaderEditor>()   });
    buttons_toolbar::widgets.push_back({ spartan::IconType::Gear,          editor->GetWidget<RenderOptions>()  });
    buttons_toolbar::widgets.push_back({ spartan::IconType::Texture,       editor->GetWidget<TextureViewer>()  });

    spartan::Engine::SetFlag(spartan::EngineMode::Playing, false);
}

void MenuBar::Tick()
{
    // keyboard shortcuts
    {
        const bool keyboard_captured = ImGui::GetIO().WantTextInput || ImGui::GetIO().WantCaptureKeyboard;
        if (!keyboard_captured)
        {
            if (ImGui::IsKeyPressed(ImGuiKey_F5, false))
            {
                buttons_toolbar::toggle_playing();
            }

            if (ImGui::IsKeyPressed(ImGuiKey_F6, false))
            {
                buttons_toolbar::toggle_paused();
            }

            const bool ctrl  = ImGui::GetIO().KeyCtrl;
            const bool shift = ImGui::GetIO().KeyShift;
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            {
                if (shift)
                {
                    windows::ShowWorldSaveDialog();
                }
                else
                {
                    windows::SaveWorld();
                }
            }
        }
    }

    // menu bar
    {
        ImGuiStyle& style = ImGui::GetStyle();
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, GetPaddingY()));

        if (ImGui::BeginMainMenuBar())
        {
            float menubar_height = ImGui::GetWindowHeight();

            // configure hit test regions for custom title bar
            spartan::Window::SetTitleBarHeight(menubar_height);
            spartan::Window::SetTitleBarButtonWidth(buttons_titlebar::get_total_width());

            // layout values
            float dpi              = spartan::Window::GetDpiScale();
            float icon_size        = 16.0f * dpi;
            float padding_x        = 6.0f * dpi;
            float frame_padding_y  = ImGui::GetStyle().FramePadding.y;
            float text_height      = ImGui::GetTextLineHeight();
            float menu_item_height = text_height + frame_padding_y * 2.0f;
            float menu_y           = (menubar_height - menu_item_height) * 0.5f;
            float icon_y           = (menubar_height - icon_size) * 0.5f;

            // logo
            ImGui::SetCursorPosX(padding_x);
            ImGui::SetCursorPosY(icon_y);
            const spartan::Icon& logo = spartan::ResourceCache::GetIcon(spartan::IconType::Logo);
            if (logo.texture)
            {
                ImGuiSp::image(logo.texture, ImVec2(icon_size, icon_size), logo.uv_min, logo.uv_max);
            }
            ImGui::SameLine(0, padding_x * 0.5f);

            // engine name and version
            static char title[64] = {};
            if (title[0] == '\0')
            {
                snprintf(title, sizeof(title), "Spartan v%d.%d",
                    spartan::version::major, spartan::version::minor);
            }
            ImGui::SetCursorPosY(menu_y);
            ImGui::MenuItem(title, nullptr, false, false);
            ImGui::SameLine(0, padding_x * 2.0f);

            // separator between version and menus
            ImGui::SetCursorPosY(menu_y);
            ImGui::TextDisabled("|");
            ImGui::SameLine(0, padding_x * 2.0f);

            // menus
            ImGui::SetCursorPosY(menu_y);
            buttons_menu::world();
            ImGui::SetCursorPosY(menu_y);
            buttons_menu::view();
            ImGui::SetCursorPosY(menu_y);
            buttons_menu::help();

            float left_content_end_x = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x;

            // world name
            {
                const string& world_name = spartan::World::GetName();
                if (!world_name.empty())
                {
                    float chip_start_x      = left_content_end_x + padding_x * 4.0f;
                    float transport_width   = buttons_toolbar::get_transport_width();
                    float transport_left_x  = min((ImGui::GetWindowWidth() - transport_width) * 0.5f, buttons_toolbar::get_right_toolbar_start(ImGui::GetWindowWidth()) - transport_width - 12.0f * dpi);
                    float chip_width_max    = min(240.0f * dpi, transport_left_x - chip_start_x - 12.0f * dpi);

                    if (chip_width_max > 48.0f * dpi)
                    {
                        float chip_padding_x = 9.0f * dpi;
                        float chip_height    = text_height + 7.0f * dpi;
                        float chip_width     = min(ImGui::CalcTextSize(world_name.c_str()).x + chip_padding_x * 2.0f, chip_width_max);
                        float chip_y         = (menubar_height - chip_height) * 0.5f;

                        ImGui::SameLine(0, padding_x * 2.0f);
                        ImGui::SetCursorPosY(menu_y);
                        ImGui::TextDisabled("|");
                        ImGui::SameLine(0, padding_x);
                        ImGui::SetCursorPosY(chip_y);
                        ImGui::Dummy(ImVec2(chip_width, chip_height));

                        ImVec2 min_pos      = ImGui::GetItemRectMin();
                        ImVec2 max_pos      = ImGui::GetItemRectMax();
                        ImDrawList* draw    = ImGui::GetWindowDrawList();
                        ImVec4 chip_color   = ImGui::Style::lerp(ImGui::Style::bg_color_1, ImGui::Style::color_accent_1, 0.18f);
                        ImU32 text_color    = ImGui::GetColorU32(ImGui::Style::color_accent_1);
                        float text_y        = min_pos.y + (chip_height - text_height) * 0.5f;

                        draw->AddRectFilled(min_pos, max_pos, ImGui::GetColorU32(ImVec4(chip_color.x, chip_color.y, chip_color.z, 0.78f)), 5.0f * dpi);
                        draw->AddRect(min_pos, max_pos, ImGui::GetColorU32(ImVec4(ImGui::Style::color_accent_1.x, ImGui::Style::color_accent_1.y, ImGui::Style::color_accent_1.z, 0.24f)), 5.0f * dpi);
                        draw->PushClipRect(ImVec2(min_pos.x + chip_padding_x, min_pos.y), ImVec2(max_pos.x - chip_padding_x, max_pos.y), true);
                        draw->AddText(ImVec2(min_pos.x + chip_padding_x, text_y), text_color, world_name.c_str());
                        draw->PopClipRect();

                        ImGuiSp::tooltip(world_name.c_str());
                        left_content_end_x = max_pos.x - ImGui::GetWindowPos().x;
                    }
                }
            }

            // transport + tool buttons
            buttons_toolbar::tick(menubar_height, left_content_end_x);

            // window control buttons (minimize, maximize, close)
            buttons_titlebar::tick(menubar_height);

            // title bar drag and double-click handling
            {
                bool any_item_hovered = ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
                spartan::Window::SetTitleBarHovered(any_item_hovered);

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

    // auxiliary windows
    {
        if (show_imgui_metrics_window)
        {
            ImGui::ShowMetricsWindow();
        }

        if (show_imgui_demo_widow)
        {
            ImGui::ShowDemoWindow(&show_imgui_demo_widow);
        }

        editor->GetWidget<Style>()->SetVisible(show_imgui_style_window);
    }

    windows::DrawFileDialog();
}

void MenuBar::ShowWorldSaveDialog()
{
    windows::ShowWorldSaveDialog();
}

void MenuBar::ShowWorldLoadDialog()
{
    windows::ShowWorldLoadDialog();
}
