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

//= INCLUDES ================================
#include "pch.h"
#include "Console.h"
#include <fstream>
#include <ranges>
#include <utility>
#include "Window.h"
#include "../ImGui/ImGui_Extension.h"
#include "../ImGui/Source/imgui_internal.h"
#include "Commands/Console/ConsoleCommands.h"
//===========================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

Console::Console(Editor* editor) : Widget(editor)
{
    m_title = "Console";

    // create an implementation of EngineLogger
    m_logger = make_shared<EngineLogger>();
    m_logger->SetCallback([this](const LogPackage& package) { AddLogPackage(package); });

    // set the logger implementation for the engine to use
    Log::SetLogger(m_logger.get());
}

Console::~Console()
{
    Log::SetLogger(nullptr);
}

void Console::OnTickVisible()
{
    const float dpi = spartan::Window::GetDpiScale();

    // toolbar: [clear] | [info] [warn] [error] | [timestamps] | [filter]
    if (ImGuiSp::button("Clear"))
    {
        Clear();
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    const auto button_log_type_visibility_toggle = [this, dpi](uint32_t index)
    {
        ImGui::PushID(static_cast<int>(index));
        bool& visibility = m_log_type_visibility[index];
        ImGui::PushStyleColor(ImGuiCol_Button, visibility ? ImGui::GetStyle().Colors[ImGuiCol_Button] : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        if (ImGuiSp::image_button(spartan::ResourceCache::GetIcon(IconType::Console), 15.0f * dpi, false, m_log_type_color[index]))
        {
            visibility = !visibility;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%u", m_log_type_count[index]);
        ImGui::SameLine();
        ImGui::PopID();
    };

    button_log_type_visibility_toggle(0);
    button_log_type_visibility_toggle(1);
    button_log_type_visibility_toggle(2);

    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();

    const float label_width = 37.0f * dpi;
    m_log_filter.Draw("Filter", ImGui::GetContentRegionAvail().x - label_width);
    ImGui::Separator();

    // calculate split sizes - no longer reserving space for autocomplete (it's a popup now)
    const float input_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float available_height = ImGui::GetContentRegionAvail().y;
    const float log_height = available_height - input_height;

    // safety first
    std::scoped_lock lock(m_mutex);

    // log output section with custom text selection support
    {
        // build list of visible logs for rendering (reuses member vector to avoid per-frame allocation)
        m_visible_logs.clear();
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_logs.size()); i++)
        {
            const LogPackage& log = m_logs[i];
            if (m_log_filter.PassFilter(log.text.c_str()) && m_log_type_visibility[log.error_level])
            {
                m_visible_logs.push_back({i, &log});
            }
        }
        const auto& visible_logs = m_visible_logs;

        const float gutter_width = 3.0f * dpi;
        const ImVec2 log_size = ImVec2(-1.0f, log_height);
        const float line_height = ImGui::GetTextLineHeightWithSpacing();
        const ImVec4 bg_color_even = ImGui::GetStyle().Colors[ImGuiCol_TableRowBg];
        const ImVec4 bg_color_odd  = ImGui::GetStyle().Colors[ImGuiCol_TableRowBgAlt];
        const ImVec4 selection_color = ImVec4(0.26f, 0.59f, 0.98f, 0.35f);
        const ImVec4 cursor_color = ImGui::GetStyle().Colors[ImGuiCol_Text];

        // validate selection bounds - clear if invalid (e.g., logs were removed or filtered)
        if (m_selection.HasSelection())
        {
            if (visible_logs.empty())
            {
                m_selection.Clear();
            }
            else
            {
                int max_line = static_cast<int>(visible_logs.size()) - 1;
                if (m_selection.start_line > max_line || m_selection.end_line > max_line ||
                    m_selection.start_line < 0 || m_selection.end_line < 0)
                {
                    m_selection.Clear();
                }
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);

        if (ImGui::BeginChild("##console_log_child", log_size, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            const float window_width = ImGui::GetContentRegionAvail().x;
            const ImVec2 mouse_pos = ImGui::GetMousePos();
            const bool is_window_hovered = ImGui::IsWindowHovered();
            const float cursor_blink_time = static_cast<float>(fmod(ImGui::GetTime(), 1.0));
            // track first rendered row position for mouse calculations
            float content_origin_y = 0.0f;
            int first_rendered_row = -1;

            // track which row the mouse is hovering over (detected during render)
            int mouse_hover_row = -1;
            float mouse_hover_row_x = 0.0f;

            // handle mouse button events
            bool mouse_clicked = is_window_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
            bool mouse_dragging = m_selection.is_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left);
            bool mouse_released = m_selection.is_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left);

            if (mouse_clicked)
            {
                m_selection.Clear();
                m_selection.is_dragging = true;
            }

            if (mouse_released)
            {
                m_selection.is_dragging = false;
            }

            // handle ctrl+a to select all
            if (is_window_hovered && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
            {
                if (!visible_logs.empty())
                {
                    m_selection.start_line = 0;
                    m_selection.start_char = 0;
                    m_selection.end_line = static_cast<int>(visible_logs.size()) - 1;
                    m_selection.end_char = static_cast<int>(visible_logs.back().second->text.size());
                }
            }

            // handle ctrl+c to copy selection
            if (m_selection.HasSelection() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
            {
                // normalize selection (start should be before end)
                int sel_start_line = m_selection.start_line;
                int sel_start_char = m_selection.start_char;
                int sel_end_line = m_selection.end_line;
                int sel_end_char = m_selection.end_char;

                if (sel_start_line > sel_end_line || (sel_start_line == sel_end_line && sel_start_char > sel_end_char))
                {
                    std::swap(sel_start_line, sel_end_line);
                    std::swap(sel_start_char, sel_end_char);
                }

                // build selected text
                std::string selected_text;
                for (int i = sel_start_line; i <= sel_end_line && i < static_cast<int>(visible_logs.size()); i++)
                {
                    const std::string& line_text = visible_logs[i].second->text;
                    int start_idx = (i == sel_start_line) ? std::min(sel_start_char, static_cast<int>(line_text.size())) : 0;
                    int end_idx = (i == sel_end_line) ? std::min(sel_end_char, static_cast<int>(line_text.size())) : static_cast<int>(line_text.size());

                    if (start_idx < end_idx)
                    {
                        selected_text += line_text.substr(start_idx, end_idx - start_idx);
                    }
                    else if (i != sel_end_line)
                    {
                        selected_text += line_text.substr(start_idx);
                    }

                    if (i < sel_end_line)
                    {
                        selected_text += "\n";
                    }
                }

                if (!selected_text.empty())
                {
                    ImGui::SetClipboardText(selected_text.c_str());
                }
            }

            // normalize selection for rendering
            int sel_start_line = m_selection.start_line;
            int sel_start_char = m_selection.start_char;
            int sel_end_line = m_selection.end_line;
            int sel_end_char = m_selection.end_char;
            if (m_selection.HasSelection())
            {
                if (sel_start_line > sel_end_line || (sel_start_line == sel_end_line && sel_start_char > sel_end_char))
                {
                    std::swap(sel_start_line, sel_end_line);
                    std::swap(sel_start_char, sel_end_char);
                }
            }

            // render visible logs with clipping
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(visible_logs.size()), line_height);

            while (clipper.Step())
            {
                for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
                {
                    const LogPackage& log = *visible_logs[row].second;
                    const char* display_text = log.text.c_str();

                    ImVec2 row_pos = ImGui::GetCursorScreenPos();

                    if (first_rendered_row < 0)
                    {
                        first_rendered_row = row;
                        content_origin_y = row_pos.y;
                    }

                    if (mouse_pos.y >= row_pos.y && mouse_pos.y < row_pos.y + line_height)
                    {
                        mouse_hover_row = row;
                        mouse_hover_row_x = row_pos.x;
                    }

                    const float text_width = ImGui::CalcTextSize(display_text).x;
                    const float row_width  = std::max(window_width, text_width + gutter_width + 8.0f);

                    // alternating row background
                    const ImVec4& bg_color = (row % 2 == 0) ? bg_color_even : bg_color_odd;
                    draw_list->AddRectFilled(
                        row_pos,
                        ImVec2(row_pos.x + row_width, row_pos.y + line_height),
                        ImGui::ColorConvertFloat4ToU32(bg_color)
                    );

                    // severity gutter strip on the left edge (only for warnings and errors)
                    if (log.error_level != 0)
                    {
                        draw_list->AddRectFilled(
                            row_pos,
                            ImVec2(row_pos.x + gutter_width, row_pos.y + line_height),
                            ImGui::ColorConvertFloat4ToU32(m_log_type_color[log.error_level])
                        );
                    }

                    // selection highlight
                    if (m_selection.HasSelection() && row >= sel_start_line && row <= sel_end_line)
                    {
                        float sel_x_start = row_pos.x;
                        float sel_x_end = row_pos.x + text_width;

                        if (row == sel_start_line && sel_start_char > 0)
                        {
                            std::string prefix = log.text.substr(0, std::min(sel_start_char, static_cast<int>(log.text.size())));
                            sel_x_start += ImGui::CalcTextSize(prefix.c_str()).x;
                        }

                        if (row == sel_end_line && sel_end_char < static_cast<int>(log.text.size()))
                        {
                            std::string prefix = log.text.substr(0, std::min(sel_end_char, static_cast<int>(log.text.size())));
                            sel_x_end = row_pos.x + ImGui::CalcTextSize(prefix.c_str()).x;
                        }

                        if (sel_start_line != sel_end_line || sel_start_char != sel_end_char)
                        {
                            draw_list->AddRectFilled(
                                ImVec2(sel_x_start, row_pos.y),
                                ImVec2(sel_x_end, row_pos.y + line_height),
                                ImGui::ColorConvertFloat4ToU32(selection_color)
                            );
                        }
                    }

                    // blinking text cursor at selection endpoint
                    if (m_selection.HasSelection() && cursor_blink_time < 0.5f)
                    {
                        if (row == m_selection.end_line)
                        {
                            float cursor_x = row_pos.x;
                            if (m_selection.end_char > 0 && m_selection.end_char <= static_cast<int>(log.text.size()))
                            {
                                std::string prefix = log.text.substr(0, m_selection.end_char);
                                cursor_x += ImGui::CalcTextSize(prefix.c_str()).x;
                            }

                            const float cursor_width = 1.0f * dpi;
                            draw_list->AddRectFilled(
                                ImVec2(cursor_x, row_pos.y + 2.0f),
                                ImVec2(cursor_x + cursor_width, row_pos.y + line_height - 2.0f),
                                ImGui::ColorConvertFloat4ToU32(cursor_color)
                            );
                        }
                    }

                    // indent past gutter, then draw text
                    ImGui::SetCursorScreenPos(ImVec2(row_pos.x + gutter_width + 2.0f, row_pos.y));
                    ImGui::PushID(row);
                    {
                        if (log.error_level != 0)
                        {
                            ImGui::PushStyleColor(ImGuiCol_Text, m_log_type_color[log.error_level]);
                        }

                        ImGui::TextUnformatted(display_text);

                        if (log.error_level != 0)
                        {
                            ImGui::PopStyleColor(1);
                        }

                        // repeat count badge for collapsed duplicates
                        if (log.repeat_count > 1)
                        {
                            char badge[16];
                            snprintf(badge, sizeof(badge), "x%u", log.repeat_count);
                            ImVec2 badge_size = ImGui::CalcTextSize(badge);
                            float badge_pad   = 4.0f * dpi;
                            float badge_x     = row_pos.x + gutter_width + 4.0f + text_width + badge_pad * 2.0f;
                            float badge_y     = row_pos.y + (line_height - badge_size.y) * 0.5f;

                            draw_list->AddRectFilled(
                                ImVec2(badge_x - badge_pad, badge_y - 1.0f),
                                ImVec2(badge_x + badge_size.x + badge_pad, badge_y + badge_size.y + 1.0f),
                                ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 0.4f, 0.4f, 0.5f)),
                                3.0f * dpi
                            );
                            draw_list->AddText(ImVec2(badge_x, badge_y), ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text]), badge);
                        }

                        // right-click context menu
                        if (ImGui::BeginPopupContextItem("##console_context_menu"))
                        {
                            if (ImGui::MenuItem("Copy Line"))
                            {
                                ImGui::SetClipboardText(log.text.c_str());
                            }

                            if (m_selection.HasSelection() && ImGui::MenuItem("Copy Selection"))
                            {
                                std::string selected_text;
                                for (int i = sel_start_line; i <= sel_end_line && i < static_cast<int>(visible_logs.size()); i++)
                                {
                                    const std::string& line_text = visible_logs[i].second->text;
                                    int start_idx = (i == sel_start_line) ? std::min(sel_start_char, static_cast<int>(line_text.size())) : 0;
                                    int end_idx = (i == sel_end_line) ? std::min(sel_end_char, static_cast<int>(line_text.size())) : static_cast<int>(line_text.size());

                                    if (start_idx < end_idx)
                                        selected_text += line_text.substr(start_idx, end_idx - start_idx);
                                    else if (i != sel_end_line)
                                        selected_text += line_text.substr(start_idx);

                                    if (i < sel_end_line)
                                        selected_text += "\n";
                                }
                                if (!selected_text.empty())
                                    ImGui::SetClipboardText(selected_text.c_str());
                            }

                            ImGui::Separator();

                            if (ImGui::MenuItem("Copy All"))
                            {
                                std::string all_text;
                                for (size_t i = 0; i < visible_logs.size(); i++)
                                {
                                    all_text += visible_logs[i].second->text;
                                    if (i + 1 < visible_logs.size())
                                        all_text += "\n";
                                }
                                if (!all_text.empty())
                                    ImGui::SetClipboardText(all_text.c_str());
                            }

                            if (ImGui::MenuItem("Save to File"))
                            {
                                std::ofstream file("console_output.txt", std::ios::out | std::ios::trunc);
                                if (file.is_open())
                                {
                                    for (size_t i = 0; i < visible_logs.size(); i++)
                                    {
                                        file << visible_logs[i].second->text;
                                        if (i + 1 < visible_logs.size())
                                            file << "\n";
                                    }
                                    file.close();
                                    SP_LOG_INFO("Console output saved to console_output.txt")
                                }
                            }

                            if (ImGui::MenuItem("Select All"))
                            {
                                if (!visible_logs.empty())
                                {
                                    m_selection.start_line = 0;
                                    m_selection.start_char = 0;
                                    m_selection.end_line   = static_cast<int>(visible_logs.size()) - 1;
                                    m_selection.end_char   = static_cast<int>(visible_logs.back().second->text.size());
                                }
                            }

                            ImGui::Separator();

                            if (ImGui::MenuItem("Search"))
                            {
                                FileSystem::OpenUrl("https://www.google.com/search?q=" + log.text);
                            }

                            ImGui::EndPopup();
                        }
                    }
                    ImGui::PopID();
                }
            }

            // update selection based on mouse hover (using actual rendered row positions)
            if (mouse_hover_row >= 0)
            {
                // helper to get char index from actual row X position
                auto get_char_at_x = [&](const char* text, float row_x, float mouse_x) -> int
                {
                    float content_x = mouse_x - row_x;
                    if (content_x <= 0) return 0;

                    int char_idx = 0;
                    float current_x = 0;
                    const char* p = text;
                    while (*p)
                    {
                        float char_width = ImGui::CalcTextSize(p, p + 1).x;
                        if (current_x + char_width * 0.5f > content_x)
                            break;
                        current_x += char_width;
                        char_idx++;
                        p++;
                    }
                    return char_idx;
                };

                if (mouse_clicked)
                {
                    m_selection.start_line = mouse_hover_row;
                    m_selection.start_char = get_char_at_x(
                        visible_logs[mouse_hover_row].second->text.c_str(),
                        mouse_hover_row_x,
                        mouse_pos.x
                    );
                    m_selection.end_line = m_selection.start_line;
                    m_selection.end_char = m_selection.start_char;
                }
                else if (mouse_dragging)
                {
                    m_selection.end_line = mouse_hover_row;
                    m_selection.end_char = get_char_at_x(
                        visible_logs[mouse_hover_row].second->text.c_str(),
                        mouse_hover_row_x,
                        mouse_pos.x
                    );
                }
            }
            else if (mouse_dragging && first_rendered_row >= 0 && !visible_logs.empty())
            {
                // mouse is outside rendered rows - clamp to first or last visible row
                if (mouse_pos.y < content_origin_y)
                {
                    // above visible area - select to first row
                    m_selection.end_line = clipper.DisplayStart;
                    m_selection.end_char = 0;
                }
                else
                {
                    // below visible area - select to last row
                    m_selection.end_line = std::min(clipper.DisplayEnd - 1, static_cast<int>(visible_logs.size()) - 1);
                    m_selection.end_char = static_cast<int>(visible_logs[m_selection.end_line].second->text.size());
                }
            }

            // track whether the user has scrolled away from the bottom
            float scroll_y     = ImGui::GetScrollY();
            float scroll_max_y = ImGui::GetScrollMaxY();
            m_user_scrolled_up = (scroll_max_y > 0.0f) && (scroll_y < scroll_max_y - line_height);

            if (m_scroll_to_bottom)
            {
                ImGui::SetScrollHereY(1.0f);
                m_scroll_to_bottom = false;
            }

            // floating "scroll to bottom" button (must be inside the child window to receive clicks)
            if (m_user_scrolled_up)
            {
                float btn_size    = 24.0f * dpi;
                ImVec2 child_min  = ImGui::GetWindowPos();
                ImVec2 child_size = ImGui::GetWindowSize();
                float btn_x       = child_min.x + child_size.x - btn_size - 14.0f * dpi;
                float btn_y       = child_min.y + child_size.y - btn_size - 8.0f * dpi;

                ImGui::SetCursorScreenPos(ImVec2(btn_x, btn_y));
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, btn_size * 0.5f);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 0.85f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 0.95f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
                if (ImGui::Button("##scroll_to_bottom", ImVec2(btn_size, btn_size)))
                {
                    m_scroll_to_bottom = true;
                }

                // draw a down-arrow glyph centered on the button
                ImVec2 btn_center = ImVec2(btn_x + btn_size * 0.5f, btn_y + btn_size * 0.5f);
                float arrow_half  = 4.0f * dpi;
                ImGui::GetForegroundDrawList()->AddTriangleFilled(
                    ImVec2(btn_center.x - arrow_half, btn_center.y - arrow_half * 0.5f),
                    ImVec2(btn_center.x + arrow_half, btn_center.y - arrow_half * 0.5f),
                    ImVec2(btn_center.x, btn_center.y + arrow_half * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(ImGui::GetStyle().Colors[ImGuiCol_Text])
                );

                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar();
            }
        }
        ImGui::EndChild();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // input field with command prompt indicator
    ImGui::Separator();

    // draw ">" prompt in accent color
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_accent_1);
    ImGui::TextUnformatted(">");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);

    ImGuiInputTextFlags input_flags =
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackHistory  |
        ImGuiInputTextFlags_CallbackAlways;

    bool reclaim_focus = false;

    // store input field position for popup placement
    ImVec2 input_pos = ImGui::GetCursorScreenPos();
    float input_width = ImGui::GetContentRegionAvail().x;

    if (ImGui::InputText("##console_input", m_input_buffer, IM_ARRAYSIZE(m_input_buffer), input_flags, [](ImGuiInputTextCallbackData* data) -> int
    {
        Console* console = static_cast<Console*>(data->UserData);
        return console->InputCallback(data);
    }, this))
    {
        ExecuteCommand(m_input_buffer);
        m_input_buffer[0] = '\0';
        m_show_autocomplete = false;
        reclaim_focus = true;
    }

    bool input_active = ImGui::IsItemActive();

    if (input_active && m_show_autocomplete && !m_filtered_cvars.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Tab))
        {
            ApplyAutocomplete();
            reclaim_focus = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            m_show_autocomplete = false;
        }
    }

    if (ImGui::IsWindowAppearing() || reclaim_focus)
    {
        ImGui::SetKeyboardFocusHere(-1);
    }

    // autocomplete popup - rendered as a floating window above the input
    if (m_show_autocomplete && !m_filtered_cvars.empty())
    {
        const float popup_max_height = 250.0f * spartan::Window::GetDpiScale();
        const float row_height      = ImGui::GetTextLineHeightWithSpacing();
        const float header_height   = row_height + ImGui::GetStyle().ItemSpacing.y;
        const float padding         = ImGui::GetStyle().WindowPadding.y * 2.0f + ImGui::GetStyle().ItemSpacing.y;
        const float content_height  = std::min(popup_max_height, header_height + row_height * static_cast<float>(m_filtered_cvars.size()) + padding);

        // position popup above the input field
        ImVec2 popup_pos = ImVec2(input_pos.x, input_pos.y - content_height - ImGui::GetStyle().ItemSpacing.y);

        ImGui::SetNextWindowPos(popup_pos);
        ImGui::SetNextWindowSize(ImVec2(input_width, content_height));
        ImGui::SetNextWindowBgAlpha(0.92f);

        ImGuiWindowFlags popup_flags =
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));

        if (ImGui::Begin("##console_autocomplete_popup", nullptr, popup_flags))
        {
            static const ImGuiTableFlags table_flags =
                ImGuiTableFlags_RowBg            |
                ImGuiTableFlags_ScrollY          |
                ImGuiTableFlags_SizingStretchProp;

            if (ImGui::BeginTable("##console_autocomplete", 3, table_flags))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.2f);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 0.4f);
                ImGui::TableHeadersRow();

                for (size_t i = 0; i < m_filtered_cvars.size(); i++)
                {
                    const ConsoleVariable* cvar = ConsoleRegistry::Get().Find(m_filtered_cvars[i]);

                    ImGui::TableNextRow();
                    ImGui::PushID(static_cast<int>(i));

                    bool is_selected = (std::cmp_equal(i, m_autocomplete_selection));
                    if (is_selected)
                    {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
                    }

                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(std::string(cvar->m_name).c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        m_autocomplete_selection = static_cast<int>(i);
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            ApplyAutocomplete();
                        }
                    }

                    // scroll to keep selected item visible
                    if (is_selected && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
                    {
                        ImGui::SetScrollHereY(0.5f);
                    }
                    if (is_selected && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
                    {
                        ImGui::SetScrollHereY(0.5f);
                    }

                    ImGui::TableSetColumnIndex(1);
                    std::string value_str = ConsoleRegistry::Get().GetValueAsString(cvar->m_name).value();
                    ImGui::TextUnformatted(value_str.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextWrapped("%s", std::string(cvar->m_hint).c_str());

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }
        }
        ImGui::End();

        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
    }
}

void Console::AddLogPackage(const LogPackage& package)
{
    std::scoped_lock lock(m_mutex);

    // collapse consecutive identical messages into a single entry with a repeat count
    if (!m_logs.empty() && m_logs.back().text == package.text && m_logs.back().error_level == package.error_level)
    {
        m_logs.back().repeat_count++;
    }
    else
    {
        m_logs.push_back(package);
        if (static_cast<uint32_t>(m_logs.size()) > m_log_max_count)
        {
            m_logs.pop_front();
        }
    }

    m_log_type_count[package.error_level]++;

    if (m_log_type_visibility[package.error_level])
    {
        m_scroll_to_bottom = true;
    }
}

void Console::Clear()
{
    m_logs.clear();
    m_logs.shrink_to_fit();

    m_visible_logs.clear();
    m_visible_logs.shrink_to_fit();

    m_log_type_count[0] = 0;
    m_log_type_count[1] = 0;
    m_log_type_count[2] = 0;

    spartan::Log::Clear();
}

void Console::UpdateAutocomplete()
{
    // if the user is arrow-navigating, the input text matches the selected
    // cvar name -- skip re-filtering so the full suggestion list stays open
    if (m_autocomplete_navigating && m_show_autocomplete &&
        m_autocomplete_selection >= 0 && m_autocomplete_selection < static_cast<int>(m_filtered_cvars.size()))
    {
        std::string input = m_input_buffer;
        while (!input.empty() && input.back() == ' ')
            input.pop_back();

        if (input == m_filtered_cvars[m_autocomplete_selection])
            return;

        // text no longer matches the selected item -- the user typed something
        m_autocomplete_navigating = false;
    }

    std::string input = m_input_buffer;

    // strip trailing space (appended by ApplyAutocomplete / Tab)
    while (!input.empty() && input.back() == ' ')
        input.pop_back();

    if (input.empty())
    {
        m_filtered_cvars.clear();
        m_autocomplete_selection = 0;
        m_show_autocomplete = false;
        return;
    }

    m_filtered_cvars.clear();
    m_autocomplete_selection = 0;

    std::string input_lower = input;
    ranges::transform(input_lower, input_lower.begin(), ::tolower);

    const auto& all_cvars = ConsoleRegistry::Get().GetAll();
    for (const auto& cvar : all_cvars | views::values)
    {
        std::string name_lower = std::string(cvar.m_name);
        ranges::transform(name_lower, name_lower.begin(), ::tolower);

        if (name_lower.find(input_lower) != std::string::npos)
        {
            m_filtered_cvars.push_back(cvar.m_name);
        }
    }

    m_show_autocomplete = !m_filtered_cvars.empty();
}

void Console::ApplyAutocomplete()
{
    if (!m_show_autocomplete || m_filtered_cvars.empty() || m_autocomplete_selection < 0)
    {
        return;
    }

    const ConsoleVariable* selected = ConsoleRegistry::Get().Find(m_filtered_cvars[m_autocomplete_selection]);

    std::string completion = std::string(selected->m_name) + " ";
    strncpy_s(m_input_buffer, completion.c_str(), IM_ARRAYSIZE(m_input_buffer) - 1);
    m_input_buffer[IM_ARRAYSIZE(m_input_buffer) - 1] = '\0';

    m_show_autocomplete = false;
}

int Console::InputCallback(ImGuiInputTextCallbackData* data)
{
    switch (data->EventFlag)
    {
    case ImGuiInputTextFlags_CallbackHistory:
        {
            if (m_show_autocomplete && !m_filtered_cvars.empty())
            {
                // navigate the autocomplete list and fill the input with the selected name
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    m_autocomplete_selection = (m_autocomplete_selection - 1 + static_cast<int>(m_filtered_cvars.size())) % static_cast<int>(m_filtered_cvars.size());
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    m_autocomplete_selection = (m_autocomplete_selection + 1) % static_cast<int>(m_filtered_cvars.size());
                }

                // suppress re-filtering so the full suggestion list stays visible
                m_autocomplete_navigating = true;

                const ConsoleVariable* cvar = ConsoleRegistry::Get().Find(m_filtered_cvars[m_autocomplete_selection]);
                if (cvar)
                {
                    std::string name = std::string(cvar->m_name) + " ";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, name.c_str());
                }
            }
            else
            {
                // no autocomplete visible -- navigate command history
                const int prev_pos = m_history_position;
                if (data->EventKey == ImGuiKey_UpArrow)
                {
                    if (m_history_position == -1)
                    {
                        m_history_position = static_cast<int>(m_command_history.size()) - 1;
                    }
                    else if (m_history_position > 0)
                    {
                        m_history_position--;
                    }
                }
                else if (data->EventKey == ImGuiKey_DownArrow)
                {
                    if (m_history_position != -1)
                    {
                        m_history_position++;
                        if (std::cmp_greater_equal(m_history_position, m_command_history.size()))
                        {
                            m_history_position = -1;
                        }
                    }
                }

                if (prev_pos != m_history_position)
                {
                    const char* history_str = (m_history_position >= 0) ? m_command_history[m_history_position].c_str() : "";
                    data->DeleteChars(0, data->BufTextLen);
                    data->InsertChars(0, history_str);
                }
            }
            break;
        }
    case ImGuiInputTextFlags_CallbackAlways:
        {
            UpdateAutocomplete();
            break;
        }
    }
    return 0;
}

void Console::ExecuteCommand(const char* command)
{
    if (command[0] == '\0')
    {
        return;
    }

    m_command_history.emplace_back(command);
    m_history_position = -1;

    std::string cmd_str = command;
    size_t space_pos = cmd_str.find(' ');

    if (space_pos != std::string::npos)
    {
        std::string cvar_name = cmd_str.substr(0, space_pos);
        std::string value_str = cmd_str.substr(space_pos + 1);

        if (ConsoleRegistry::Get().SetValueFromString(cvar_name, value_str))
        {
            SP_LOG_INFO("Setting %s to %s", cvar_name.c_str(), value_str.c_str())
        }
        else
        {
            SP_LOG_WARNING("Failed to set %s to %s", cvar_name.c_str(), value_str.c_str())
        }
    }
    else
    {
        if (std::optional<std::string> maybe_val = ConsoleRegistry::Get().GetValueAsString(command); maybe_val.has_value())
        {
            const char* Val = maybe_val.value().c_str();
            SP_LOG_WARNING("Current Value of %s : ", command, Val)
        }
    }

    m_scroll_to_bottom = true;
}
