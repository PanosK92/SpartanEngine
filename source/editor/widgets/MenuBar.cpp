/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include <algorithm>
#include "MenuBar.h"
#include "FileDialog.h"
#include "Style.h"
#include "../Editor.h"
#include "../EditorLayout.h"
#include "Engine.h"
#include "resource/ResourceCache.h"
#include "world/World.h"
#include "rendering/Renderer.h"
#include "profiling/RenderDoc.h"
#include "Settings.h"
#include "core/Definitions.h"
#include "core/ThreadPool.h"
#include "core/ProgressTracker.h"
#include "commands/CommandStack.h"
#include "mcp/McpServer.h"
#include "../mcp/McpAssistant.h"
#include "../WorldPreviews.h"
#include "../GeneralWindows.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Style.h"
#include "../imgui/ImGui_TransformGizmo.h"
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

    void menu_entry(Widget* widget)
    {
        if (ImGui::MenuItem(widget->GetTitle(), nullptr, widget->GetVisible()))
        {
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

            spartan::World::SaveToFileAsync(world_file_path);
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
                        spartan::World::SaveToFileAsync(
                            file_dialog_selection_path
                        );
                        show_file_dialog = false;
                    }
                }
            }
        }
    }

    namespace buttons_menu
    {
        void file()
        {
            bool open_new_world_confirmation = false;
            const bool world_saving = spartan::World::IsSaving();
            if (ImGui::BeginMenu("File"))
            {
                if (
                    ImGui::MenuItem(
                        "New World",
                        nullptr,
                        false,
                        !world_saving
                    )
                )
                {
                    if (spartan::World::GetEntities().empty())
                    {
                        spartan::World::Shutdown();
                    }
                    else
                    {
                        open_new_world_confirmation = true;
                    }
                }

                ImGui::Separator();

                if (
                    ImGui::MenuItem(
                        "Open World...",
                        "Ctrl+O",
                        false,
                        !world_saving
                    )
                )
                {
                    windows::ShowWorldLoadDialog();
                }

                ImGui::Separator();

                if (
                    ImGui::MenuItem(
                        "Save",
                        "Ctrl+S",
                        false,
                        !world_saving
                    )
                )
                {
                    windows::SaveWorld();
                }

                if (
                    ImGui::MenuItem(
                        "Save As...",
                        "Ctrl+Shift+S",
                        false,
                        !world_saving
                    )
                )
                {
                    windows::ShowWorldSaveDialog();
                }

                ImGui::Separator();

                if (
                    ImGui::MenuItem(
                        "Export Package...",
                        nullptr,
                        false,
                        !world_saving
                    )
                )
                {
                    windows::ExportWorld();
                }

                ImGui::EndMenu();
            }

            if (open_new_world_confirmation)
            {
                ImGui::OpenPopup("##new_world_confirmation");
            }
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            if (ImGui::BeginPopupModal("##new_world_confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
            {
                ImGui::TextUnformatted("Create a new world?");
                ImGui::TextDisabled("The current world will be cleared. Save it first if you need to keep your changes.");
                ImGui::Separator();

                if (ImGuiSp::button("Cancel", ImVec2(100.0f * spartan::Window::GetDpiScale(), 0.0f)))
                {
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGuiSp::button("Create New", ImVec2(100.0f * spartan::Window::GetDpiScale(), 0.0f)))
                {
                    spartan::World::Shutdown();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void edit()
        {
            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Undo", "Ctrl+Z"))
                {
                    spartan::CommandStack::Undo();
                }

                if (ImGui::MenuItem("Redo", "Ctrl+Y / Ctrl+Shift+Z"))
                {
                    spartan::CommandStack::Redo();
                }

                ImGui::EndMenu();
            }
        }

        void view()
        {
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Reset workspace layout"))
                {
                    editor_layout::reset();
                }
                ImGui::Separator();
                bool* controls_visible = GeneralWindows::GetVisibilityWindowControls();
                if (ImGui::MenuItem("Controls", "Ctrl+P", *controls_visible))
                {
                    *controls_visible = !*controls_visible;
                }

                if (ImGui::BeginMenu("Widgets"))
                {
                    editor->ForEachWidget([](Widget* widget)
                    {
                        if (widget->ShowInViewMenu())
                        {
                            menu_entry(widget);
                        }
                    });

                    ImGui::EndMenu();
                }

                if (ImGui::BeginMenu("Developer"))
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
                bool* about_visible = GeneralWindows::GetVisibilityWindowAbout();
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
                    spartan::FileSystem::OpenUrl("https://github.com/PanosK92/SpartanEngine/wiki/Contributing");
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

    // a short divider centered on the title bar, x is relative to the window
    void draw_title_separator(const float x, const float menubar_height)
    {
        const float dpi      = spartan::Window::GetDpiScale();
        const float height   = 18.0f * dpi;
        const ImVec2 window  = ImGui::GetWindowPos();
        const float line_x   = IM_ROUND(window.x + x) + 0.5f;
        const float center_y = window.y + menubar_height * 0.5f;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(line_x, IM_ROUND(center_y - height * 0.5f)), ImVec2(line_x, IM_ROUND(center_y + height * 0.5f)), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.26f)), 1.0f);
    }

    namespace buttons_toolbar
    {
        float button_size = 18.0f;
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
            return 0.0f;
        }

        float group_gap()
        {
            return 18.0f * dpi();
        }

        float button_gap()
        {
            return 3.0f * dpi();
        }

        float group_rounding()
        {
            return 4.0f * dpi();
        }

        float tool_icon_size()
        {
            return button_size * dpi();
        }

        ImVec2 tool_padding()
        {
            return ImVec2(6.0f * dpi(), 6.0f * dpi());
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
            return ImVec2(11.0f * dpi(), 5.0f * dpi());
        }

        float transport_icon_size()
        {
            return 20.0f * dpi();
        }

        float transport_button_width()
        {
            return transport_icon_size() + transport_padding().x * 2.0f;
        }

        float transport_button_height()
        {
            return transport_icon_size() + transport_padding().y * 2.0f;
        }

        float transport_inset()
        {
            return 3.0f * dpi();
        }

        float get_transport_width()
        {
            return transport_inset() * 2.0f + transport_button_width() * 2.0f + button_gap();
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
            // w/e/r/t + local/world + snap = 6 buttons
            return icon_group_width(6.0f);
        }

        float panel_group_width(size_t visible_widget_count = widgets.size(), bool show_overflow = false)
        {
            return icon_group_width(2.0f + static_cast<float>(visible_widget_count) + (show_overflow ? 1.0f : 0.0f));
        }

        float get_right_toolbar_width(size_t visible_widget_count = widgets.size(), bool show_overflow = false)
        {
            const float capture_buttons = 2.0f;
            return snap_group_width() + icon_group_width(capture_buttons) + panel_group_width(visible_widget_count, show_overflow) + group_gap() * 2.0f;
        }

        float get_right_toolbar_start(float menubar_width, size_t visible_widget_count = widgets.size(), bool show_overflow = false)
        {
            return max(0.0f, menubar_width - get_right_toolbar_width(visible_widget_count, show_overflow) - buttons_titlebar::get_total_width());
        }

        float centered_y(float menubar_height, float height)
        {
            return max(0.0f, (menubar_height - height) * 0.5f);
        }

        void push_button_colors(bool is_active)
        {
            const ImVec4 accent = ImGui::Style::color_accent_1;
            if (is_active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        with_alpha(accent, 0.16f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(accent, 0.24f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  with_alpha(accent, 0.32f));
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, with_alpha(ImGui::Style::color_text, 0.08f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive,  with_alpha(ImGui::Style::color_text, 0.14f));
            }
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

            const ImVec4 tint = is_active
                ? ImGui::Style::color_accent_1
                : ImGui::Style::color_text_muted;

            // image_button derives its id from the icon type, so two buttons sharing an icon share
            // an id, the tooltip is unique per button and makes the id unique whatever the icon is
            ImGui::PushID(tooltip_text);
            if (ImGuiSp::image_button(icon_type, spartan::math::Vector2(tool_icon_size(), tool_icon_size()), false, tint))
            {
                on_press(widget);
            }
            ImGui::PopID();


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

        void draw_gizmo_mode_button(
            float menubar_height,
            float cursor_pos_x,
            const char* label,
            bool active,
            const char* tooltip,
            ::TransformGizmo::Operation op
        )
        {
            ImGui::SetCursorPosX(cursor_pos_x);
            ImGui::SetCursorPosY(centered_y(menubar_height, tool_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tool_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(active);
            ImGui::PushStyleColor(ImGuiCol_Text, active ? ImGui::Style::color_accent_1 : ImGui::Style::color_text_muted);
            ImGui::PushFont(Editor::font_mono_medium, ImGui::GetFontSize() * 0.92f);

            if (ImGui::Button(label, ImVec2(tool_button_width(), tool_button_height())))
            {
                ImGui::TransformGizmo::set_operation(op);
            }

            ImGui::PopFont();
            ImGui::PopStyleColor();


            ImGuiSp::tooltip(tooltip);
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        void draw_space_button(float menubar_height, float cursor_pos_x)
        {
            const bool is_world = ImGui::TransformGizmo::space() == ::TransformGizmo::Space::World;
            ImGui::SetCursorPosX(cursor_pos_x);
            ImGui::SetCursorPosY(centered_y(menubar_height, tool_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tool_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(false);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text);
            ImGui::PushFont(Editor::font_mono_medium, ImGui::GetFontSize() * 0.72f);

            if (ImGui::Button(is_world ? "WLD" : "LOC", ImVec2(tool_button_width(), tool_button_height())))
            {
                ImGui::TransformGizmo::toggle_space();
            }

            ImGui::PopFont();
            ImGui::PopStyleColor();
            ImGuiSp::tooltip(is_world ? "World space (X)" : "Local space (X)");
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        void draw_snap_button(float menubar_height, float cursor_pos_x)
        {
            bool snap_enabled = spartan::cvar_transform_snap.GetValueAs<bool>();

            ImGui::SetCursorPosX(cursor_pos_x);
            ImGui::SetCursorPosY(centered_y(menubar_height, tool_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tool_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(snap_enabled);

            const ImVec4 tint = snap_enabled
                ? ImGui::Style::color_accent_1
                : ImGui::Style::color_text_muted;
            if (ImGuiSp::image_button(spartan::IconType::Snap, spartan::math::Vector2(tool_icon_size(), tool_icon_size()), false, tint))
            {
                spartan::ConsoleRegistry::Get().SetValueFromString("r.transform_snap", snap_enabled ? "0" : "1");
            }


            ImGuiSp::tooltip(snap_enabled ? "Disable transform snapping" : "Enable transform snapping");

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        void draw_transform_group(float menubar_height, float cursor_pos_x)
        {
            const float width = snap_group_width();

            float button_x = cursor_pos_x + group_padding_x();
            const ::TransformGizmo::Operation op = ImGui::TransformGizmo::operation();

            draw_gizmo_mode_button(menubar_height, button_x, "W", op == ::TransformGizmo::Operation::Translate, "Translate (W)", ::TransformGizmo::Operation::Translate);
            button_x += tool_button_width() + button_gap();

            draw_gizmo_mode_button(menubar_height, button_x, "E", op == ::TransformGizmo::Operation::Rotate, "Rotate (E)", ::TransformGizmo::Operation::Rotate);
            button_x += tool_button_width() + button_gap();

            draw_gizmo_mode_button(menubar_height, button_x, "R", op == ::TransformGizmo::Operation::Scale, "Scale (R)", ::TransformGizmo::Operation::Scale);
            button_x += tool_button_width() + button_gap();

            draw_gizmo_mode_button(menubar_height, button_x, "T", op == ::TransformGizmo::Operation::Universal, "Universal (T)", ::TransformGizmo::Operation::Universal);
            button_x += tool_button_width() + button_gap();

            draw_space_button(menubar_height, button_x);
            button_x += tool_button_width() + button_gap();

            draw_snap_button(menubar_height, button_x);
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

            const ImVec4 tint = is_running || is_visible
                ? ImGui::Style::color_accent_1
                : ImGui::Style::color_text_muted;
            if (ImGuiSp::image_button(spartan::IconType::Mcp, spartan::math::Vector2(tool_icon_size(), tool_icon_size()), false, tint))
            {
                if (assistant)
                {
                    assistant->SetVisible(!assistant->GetVisible());
                }
            }


            string tooltip = is_visible ? "Close Spartan AI" : "Open Spartan AI";
            tooltip += is_running ?
                "\nMCP active on 127.0.0.1:" + to_string(spartan::McpServer::GetPort()) :
                "\nMCP inactive";
            ImGuiSp::tooltip(tooltip.c_str());

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
        }

        bool draw_pause_button(float menubar_height, bool is_playing, bool is_active)
        {
            ImGui::SetCursorPosY(
                centered_y(
                    menubar_height,
                    transport_button_height()
                )
            );
            ImGui::PushStyleVar(
                ImGuiStyleVar_FramePadding,
                transport_padding()
            );
            ImGui::PushStyleVar(
                ImGuiStyleVar_FrameRounding,
                transport_button_height() * 0.5f
            );
            push_button_colors(false);
            if (is_active)
            {
                ImGui::PopStyleColor();
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::EditorUi::alpha(ImGui::Style::color_warning, 0.14f));
            }

            ImVec4 tint = is_active
                ? ImGui::Style::color_warning
                : ImGui::Style::color_text_muted;
            if (!is_playing)
            {
                tint.w = 0.45f;
            }

            ImGui::BeginDisabled(!is_playing);
            const bool pressed = ImGuiSp::image_button(
                spartan::IconType::Pause,
                spartan::math::Vector2(
                    transport_icon_size(),
                    transport_icon_size()
                ),
                false,
                tint
            );
            ImGui::EndDisabled();

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);
            ImGuiSp::tooltip(is_active ? "Resume (F6)" : "Pause (F6)");
            return pressed;
        }

        void draw_transport_group(float menubar_height, float cursor_pos_x)
        {
            const bool is_playing = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing);
            const bool is_paused  = spartan::Engine::IsFlagSet(spartan::EngineMode::Paused);
            const bool is_live    = is_playing && !is_paused;

            // the capsule is the one lit object on the bar, it glows while the simulation runs
            {
                const float scale      = dpi();
                const float height     = transport_button_height() + transport_inset() * 2.0f;
                const ImVec2 window    = ImGui::GetWindowPos();
                const ImVec2 min_pos   = ImVec2(IM_ROUND(window.x + cursor_pos_x), IM_ROUND(window.y + (menubar_height - height) * 0.5f));
                const ImVec2 max_pos   = ImVec2(IM_ROUND(min_pos.x + get_transport_width()), IM_ROUND(min_pos.y + height));
                const float rounding   = height * 0.5f;
                const float live       = ImGui::EditorUi::animate(ImGui::GetID("##transport_live"), is_live ? 1.0f : 0.0f, 8.0f);
                const float held       = is_paused ? 1.0f : live;
                const ImVec4 signal    = is_paused ? ImGui::Style::color_warning : ImGui::Style::color_accent_1;
                ImDrawList* draw_list  = ImGui::GetWindowDrawList();
                const float breathe    = live > 0.01f ? 0.85f + 0.15f * sinf(static_cast<float>(ImGui::GetTime()) * 3.0f) : 1.0f;
                if (held > 0.01f)
                {
                    ImGui::EditorUi::draw_glow(draw_list, min_pos, max_pos, signal, rounding, 14.0f * scale, held * breathe);
                }
                draw_list->AddRectFilled(min_pos, max_pos, ImGui::EditorUi::color(ImGui::Style::lerp(ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.06f), ImGui::EditorUi::alpha(signal, 0.14f), held)), rounding);
                if (held > 0.01f)
                {
                    draw_list->AddRect(min_pos, max_pos, ImGui::EditorUi::color(ImGui::EditorUi::alpha(signal, 0.75f * held)), rounding, max(1.0f, scale));
                }
            }

            float button_x = cursor_pos_x + transport_inset();
            ImGui::SetCursorPosX(button_x);
            ImGui::SetCursorPosY(centered_y(menubar_height, transport_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, transport_padding());
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, transport_button_height() * 0.5f);
            push_button_colors(is_live);

            const ImVec4 play_tint = is_live ? ImGui::Style::color_accent_hi : ImGui::Style::color_text;
            if (ImGuiSp::image_button(spartan::IconType::Play, spartan::math::Vector2(transport_icon_size(), transport_icon_size()), false, play_tint))
            {
                toggle_playing();
            }
            ImGuiSp::tooltip(is_playing ? "Stop (F5)" : "Play (F5)");

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(2);

            ImGui::SameLine(0, button_gap());
            if (draw_pause_button(menubar_height, is_playing, is_paused))
            {
                toggle_paused();
            }
        }

        void draw_overflow_button(float menubar_height, size_t first_hidden_widget)
        {
            bool has_active_widget = false;
            for (size_t i = first_hidden_widget; i < widgets.size(); i++)
            {
                has_active_widget |= widgets[i].second->GetVisible();
            }

            ImGui::SetCursorPosY(centered_y(menubar_height, tool_button_height()));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, group_rounding());
            push_button_colors(has_active_widget);

            if (ImGui::Button("...", ImVec2(tool_button_width(), tool_button_height())))
            {
                ImGui::OpenPopup("##toolbar_overflow");
            }


            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
            ImGuiSp::tooltip("More tools");

            if (ImGui::BeginPopup("##toolbar_overflow"))
            {
                for (size_t i = first_hidden_widget; i < widgets.size(); i++)
                {
                    Widget* widget = widgets[i].second;
                    if (ImGui::MenuItem(widget->GetTitle(), nullptr, widget->GetVisible()))
                    {
                        widget->SetVisible(!widget->GetVisible());
                    }
                }
                ImGui::EndPopup();
            }
        }

        void draw_right_groups(float menubar_height, float cursor_pos_x, size_t visible_widget_count, bool show_overflow)
        {
            float group_x = cursor_pos_x;

            {
                draw_transform_group(menubar_height, group_x);
                group_x += snap_group_width() + group_gap();
            }

            {
                float width = icon_group_width(2.0f);
                draw_title_separator(group_x - group_gap() * 0.5f, menubar_height);

                static auto screenshot_visible = [](Widget*) { return false; };
                static auto screenshot_press   = [](Widget*) { spartan::Renderer::Screenshot(); };
                toolbar_button(spartan::IconType::Screenshot, "Screenshot", screenshot_visible, screenshot_press, nullptr, group_x + group_padding_x());

                ImGui::SameLine(0, button_gap());

                static auto renderdoc_visible = [](Widget*)
                {
                    return spartan::cvar_debug_renderdoc.GetValue();
                };
                static auto renderdoc_press   = [](Widget*)
                {
                    if (spartan::cvar_debug_renderdoc.GetValue())
                    {
                        spartan::RenderDoc::FrameCapture();
                    }
                    else
                    {
                        SP_LOG_WARNING("RenderDoc integration is disabled. To enable, set \"debug_renderdoc\" to \"true\" in spartan.xml and restart");
                    }
                };
                toolbar_button(spartan::IconType::RenderDoc, "RenderDoc capture", renderdoc_visible, renderdoc_press, nullptr);

                group_x += width + group_gap();
            }

            {
                float width = panel_group_width(visible_widget_count, show_overflow);
                draw_title_separator(group_x - group_gap() * 0.5f, menubar_height);

                static auto world_visible = [](Widget*) { return GeneralWindows::GetVisibilityWorlds(); };
                static auto world_press   = [](Widget*) { GeneralWindows::SetVisibilityWorlds(!GeneralWindows::GetVisibilityWorlds()); };
                toolbar_button(spartan::IconType::World, "Worlds", world_visible, world_press, nullptr, group_x + group_padding_x());

                ImGui::SameLine(0, button_gap());
                draw_mcp_button(menubar_height, -1.0f);

                for (size_t i = 0; i < visible_widget_count; i++)
                {
                    ImGui::SameLine(0, button_gap());

                    auto& widget_it                = widgets[i];
                    Widget* widget_ptr             = widget_it.second;
                    spartan::IconType icon         = widget_it.first;
                    static auto is_widget_visible  = [](Widget* w) { return w->GetVisible(); };
                    static auto set_widget_visible = [](Widget* w) { w->SetVisible(!w->GetVisible()); };
                    toolbar_button(icon, widget_ptr->GetTitle(), is_widget_visible, set_widget_visible, widget_ptr);
                }

                if (show_overflow)
                {
                    ImGui::SameLine(0, button_gap());
                    draw_overflow_button(menubar_height, visible_widget_count);
                }
            }
        }

        void draw_compact_tools(float menubar_height, float cursor_pos_x)
        {
            ImGui::SetCursorPos(ImVec2(cursor_pos_x, centered_y(menubar_height, tool_button_height())));
            if (ImGui::Button("Tools", ImVec2(64.0f * dpi(), tool_button_height())))
            {
                ImGui::OpenPopup("##compact_tools");
            }
            if (ImGui::BeginPopup("##compact_tools"))
            {
                const auto operation = ImGui::TransformGizmo::operation();
                if (ImGui::MenuItem("Translate", "W", operation == ::TransformGizmo::Operation::Translate))
                    ImGui::TransformGizmo::set_operation(::TransformGizmo::Operation::Translate);
                if (ImGui::MenuItem("Rotate", "E", operation == ::TransformGizmo::Operation::Rotate))
                    ImGui::TransformGizmo::set_operation(::TransformGizmo::Operation::Rotate);
                if (ImGui::MenuItem("Scale", "R", operation == ::TransformGizmo::Operation::Scale))
                    ImGui::TransformGizmo::set_operation(::TransformGizmo::Operation::Scale);
                if (ImGui::MenuItem("Universal", "T", operation == ::TransformGizmo::Operation::Universal))
                    ImGui::TransformGizmo::set_operation(::TransformGizmo::Operation::Universal);
                if (ImGui::MenuItem("World space", "X", ImGui::TransformGizmo::space() == ::TransformGizmo::Space::World))
                    ImGui::TransformGizmo::toggle_space();
                const bool snap = spartan::cvar_transform_snap.GetValueAs<bool>();
                if (ImGui::MenuItem("Transform snapping", nullptr, snap))
                    spartan::ConsoleRegistry::Get().SetValueFromString("r.transform_snap", snap ? "0" : "1");
                ImGui::Separator();
                if (ImGui::MenuItem("Worlds", nullptr, GeneralWindows::GetVisibilityWorlds()))
                    GeneralWindows::SetVisibilityWorlds(!GeneralWindows::GetVisibilityWorlds());
                if (McpAssistant* assistant = editor->GetWidget<McpAssistant>())
                    menu_entry(assistant);
                for (const auto& entry : widgets)
                    menu_entry(entry.second);
                ImGui::Separator();
                if (ImGui::MenuItem("Screenshot"))
                    spartan::Renderer::Screenshot();
                if (ImGui::MenuItem("RenderDoc capture", nullptr, false, spartan::cvar_debug_renderdoc.GetValue()))
                    spartan::RenderDoc::FrameCapture();
                ImGui::EndPopup();
            }
        }

        void tick(float menubar_height, float left_content_end_x)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float size_avail_x      = viewport->Size.x;
            const float transport_width  = get_transport_width();
            const float transport_min_x  = left_content_end_x + group_gap();
            size_t visible_widget_count  = widgets.size();
            bool show_overflow            = false;
            float right_start_x           = 0.0f;

            while (true)
            {
                show_overflow = visible_widget_count < widgets.size();
                right_start_x = get_right_toolbar_start(size_avail_x, visible_widget_count, show_overflow);
                if (transport_min_x + transport_width + group_gap() <= right_start_x || visible_widget_count == 0)
                {
                    break;
                }
                visible_widget_count--;
            }

            const float transport_max_x  = right_start_x - transport_width - group_gap();
            if (transport_min_x > transport_max_x)
            {
                draw_transport_group(menubar_height, transport_min_x);
                draw_compact_tools(menubar_height, transport_min_x + transport_width + group_gap());
                return;
            }
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
            draw_right_groups(menubar_height, right_start_x, visible_widget_count, show_overflow);
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
            draw_title_separator(start_x - separator_gap * 0.5f * dpi, menubar_height);
            ImGui::SetCursorPosX(start_x);
            ImGui::SetCursorPosY(offset_y);

            // minimize button
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImGui::Style::color_surface_hover
            );
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ImGui::Style::color_surface_active
            );
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(button_padding_x * dpi, button_padding_y * dpi));

            if (ImGuiSp::image_button(spartan::IconType::Minimize, icon_size, false))
            {
                spartan::Window::Minimize();
            }
            ImGuiSp::tooltip("Minimize");

            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosY(offset_y);

            // maximize/restore button
            if (ImGuiSp::image_button(spartan::IconType::Maximize, icon_size, false))
            {
                spartan::Window::Maximize();
            }
            ImGuiSp::tooltip(spartan::Window::IsMaximized() ? "Restore" : "Maximize");

            ImGui::PopStyleColor(3);

            ImGui::SameLine(0, 0);
            ImGui::SetCursorPosY(offset_y);

            // close button with red hover
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.12f, 0.12f, 0.92f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.62f, 0.08f, 0.08f, 1.0f));

            if (ImGuiSp::image_button(spartan::IconType::X, icon_size, false))
            {
                spartan::Window::Close();
            }
            ImGuiSp::tooltip("Close");

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }
    }
}

void MenuBar::Initialize(Editor* _editor)
{
    editor      = _editor;
    file_dialog = make_unique<FileDialog>(true, FileDialog_Type_FileSelection, FileDialog_Op_Open, FileDialog_Filter_World);

    vector<pair<int, Widget*>> toolbar;
    editor->ForEachWidget([&toolbar](Widget* widget)
    {
        if (widget->GetToolbarOrder() > 0)
        {
            toolbar.push_back({ widget->GetToolbarOrder(), widget });
        }
    });
    sort(toolbar.begin(), toolbar.end());
    for (const auto& [order, widget] : toolbar)
    {
        buttons_toolbar::widgets.push_back({
            static_cast<spartan::IconType>(widget->GetToolbarIcon()),
            widget
        });
    }

    spartan::Engine::SetFlag(spartan::EngineMode::Playing, false);
}

void MenuBar::Tick()
{
    // Global history shortcuts also work when the hierarchy window is closed.
    if (ImGui::GetIO().KeyCtrl && !ImGuiSp::editor_shortcuts_blocked() && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (ImGui::GetIO().KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false)))
            spartan::CommandStack::Redo();
        else if (ImGui::IsKeyPressed(ImGuiKey_Z, false))
            spartan::CommandStack::Undo();
    }

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
            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_O, false) && !ImGuiSp::editor_shortcuts_blocked())
            {
                windows::ShowWorldLoadDialog();
            }

            if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !ImGuiSp::editor_shortcuts_blocked())
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
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(style.FramePadding.x, GetPaddingY() * spartan::Window::GetDpiScale()));

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
            float icon_y           = IM_ROUND((menubar_height - icon_size) * 0.5f + 0.5f);
            // logo
            ImGui::SetCursorPosX(14.0f * dpi);
            ImGui::SetCursorPosY(icon_y);
            const spartan::Icon& logo = spartan::ResourceCache::GetIcon(spartan::IconType::Logo);
            if (logo.texture)
            {
                ImGuiSp::image(logo.texture, ImVec2(icon_size, icon_size), logo.uv_min, logo.uv_max);
            }
            ImGui::SameLine(0, 10.0f * dpi);

            // the wordmark is an entry point to engine information
            {
                const char* wordmark   = "SPARTAN";
                const float font_size  = ImGui::GetFontSize();
                const float tracking   = font_size * 0.22f;
                const float width      = ImGui::EditorUi::calc_text_tracked(wordmark, tracking, Editor::font_bold, font_size);
                ImGui::SetCursorPosY(menu_y);
                if (ImGui::InvisibleButton("##wordmark", ImVec2(width, menu_item_height)))
                {
                    *GeneralWindows::GetVisibilityWindowAbout() = true;
                }
                // same baseline as the menu labels next to it
                const float hover       = ImGui::EditorUi::animate(ImGui::GetID("##wordmark_hover"), ImGui::IsItemHovered() ? 1.0f : 0.0f, 12.0f);
                const ImVec2 item_min   = ImGui::GetItemRectMin();
                const ImVec2 position   = ImVec2(IM_ROUND(item_min.x), IM_ROUND(item_min.y + frame_padding_y));
                const ImVec4 tint       = ImGui::Style::lerp(ImGui::Style::color_text, ImGui::Style::color_accent_hi, hover);
                ImGui::EditorUi::draw_text_tracked(ImGui::GetWindowDrawList(), position, ImGui::EditorUi::color(tint), wordmark, tracking, Editor::font_bold, font_size);
                ImGuiSp::tooltip("Spartan Engine by Panos Karabelas");
            }
            draw_title_separator(ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + 14.0f * dpi, menubar_height);
            ImGui::SameLine(0, 28.0f * dpi);

            // menus
            ImGui::SetCursorPosY(menu_y);
            buttons_menu::file();
            ImGui::SetCursorPosY(menu_y);
            buttons_menu::edit();
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

                        draw_title_separator(left_content_end_x + 12.0f * dpi, menubar_height);
                        ImGui::SameLine(0, 24.0f * dpi);
                        ImGui::SetCursorPosY(chip_y);
                        ImGui::Dummy(ImVec2(chip_width, chip_height));

                        ImVec2 min_pos      = ImGui::GetItemRectMin();
                        ImVec2 max_pos      = ImGui::GetItemRectMax();
                        ImDrawList* draw    = ImGui::GetWindowDrawList();
                        float text_y        = min_pos.y + (chip_height - text_height) * 0.5f;
                        const bool loading  = spartan::ProgressTracker::IsLoading();
                        const ImVec4 signal = loading ? ImGui::Style::color_warning : ImGui::Style::color_accent_1;

                        ImGui::EditorUi::status_dot(draw, ImVec2(min_pos.x + 3.0f * dpi, IM_ROUND((min_pos.y + max_pos.y) * 0.5f)), 2.5f * dpi, signal, loading);
                        draw->PushClipRect(ImVec2(min_pos.x + chip_padding_x, min_pos.y), ImVec2(max_pos.x, max_pos.y), true);
                        draw->AddText(ImVec2(min_pos.x + chip_padding_x + 3.0f * dpi, text_y), ImGui::EditorUi::color(ImGui::Style::color_text), world_name.c_str());
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

            // while the simulation runs, a signal spreads out of the black under the transport
            const bool live       = spartan::Engine::IsFlagSet(spartan::EngineMode::Playing) && !spartan::Engine::IsFlagSet(spartan::EngineMode::Paused);
            const float intensity = ImGui::EditorUi::animate(ImGui::GetID("##titlebar_signal"), live ? 1.0f : 0.0f, 6.0f);
            if (intensity > 0.01f)
            {
                ImDrawList* draw_list = ImGui::GetWindowDrawList();
                const ImVec2 window   = ImGui::GetWindowPos();
                const float width     = ImGui::GetWindowWidth();
                const float y         = window.y + menubar_height - 1.0f;
                const float center_x  = window.x + width * 0.5f;
                const float spread    = width * 0.28f * intensity;
                const ImU32 clear     = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, 0.0f));
                const ImU32 signal    = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, intensity));
                draw_list->AddRectFilledMultiColor(ImVec2(center_x - spread, y), ImVec2(center_x, y + 1.0f), clear, signal, signal, clear);
                draw_list->AddRectFilledMultiColor(ImVec2(center_x, y), ImVec2(center_x + spread, y + 1.0f), signal, clear, clear, signal);
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
