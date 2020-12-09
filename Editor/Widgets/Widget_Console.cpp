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

//= INCLUDES ==============================
#include "Widget_Console.h"
#include "Rendering/Model.h"
#include "../ImGui_Extension.h"
#include "../ImGui/Source/imgui_stdlib.h"
#include "../ImGui/Source/imgui_internal.h"
//=========================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
//======================

Widget_Console::Widget_Console(Editor* editor) : Widget(editor)
{
    m_title = "Console";

    // Create an implementation of EngineLogger
    m_logger = make_shared<EngineLogger>();
    m_logger->SetCallback([this](const LogPackage& package) { AddLogPackage(package); });

    // Set the logger implementation for the engine to use
    Log::SetLogger(m_logger);
}

void Widget_Console::TickVisible()
{
    // Clear Button
    if (ImGui::Button("Clear")) { Clear();} ImGui::SameLine();

    // Lambda for info, warning, error filter buttons
    const auto button_log_type_visibility_toggle = [this](const Icon_Type icon, uint32_t index)
    {
        bool& visibility = m_log_type_visibility[index];
        ImGui::PushStyleColor(ImGuiCol_Button, visibility ? ImGui::GetStyle().Colors[ImGuiCol_Button] : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        if (ImGuiEx::ImageButton(icon, 15.0f))
        {
            visibility = !visibility;
            m_scroll_to_bottom = true;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%d", m_log_type_count[index]);
        ImGui::SameLine();
    };

    // Log category visibility buttons
    button_log_type_visibility_toggle(Icon_Console_Info,       0);
    button_log_type_visibility_toggle(Icon_Console_Warning,    1);
    button_log_type_visibility_toggle(Icon_Console_Error,      2);

    // Text filter
    const float label_width = 37.0f; //ImGui::CalcTextSize("Filter", nullptr, true).x;
    m_log_filter.Draw("Filter", ImGui::GetContentRegionAvail().x - label_width);
    ImGui::Separator();

    // Wait for reading to finish
    while (m_is_reading) { this_thread::sleep_for(std::chrono::milliseconds(16)); }

    m_is_reading = true;

    // Content properties
    static const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg           |
        ImGuiTableFlags_BordersOuter    |
        ImGuiTableFlags_ScrollX         |
        ImGuiTableFlags_ScrollY;
    static const ImVec2 size = ImVec2(-1.0f);

    // Content
    if (ImGui::BeginTable("##widget_console_content", 1, table_flags, size))
    {
        // Logs
        for (uint32_t row = 0; row < m_logs.size(); row++)
        {
            LogPackage& log = m_logs[row];
    
            // Text and visibility filters
            if (m_log_filter.PassFilter(log.text.c_str()) && m_log_type_visibility[log.error_level])
            {
                // Switch row
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
    
                // Log
                ImGui::PushID(row);
                {
                    // Text
                    ImGui::PushStyleColor(ImGuiCol_Text, m_log_type_color[log.error_level]);
                    ImGui::Text(log.text.c_str());
                    ImGui::PopStyleColor(1);
                
                    // Context menu
                    if (ImGui::BeginPopupContextItem("##widget_console_contextMenu"))
                    {
                        if (ImGui::MenuItem("Copy"))
                        {
                            ImGui::LogToClipboard();
                            ImGui::LogText("%s", log.text.c_str());
                            ImGui::LogFinish();
                        }
                
                        ImGui::Separator();
                
                        if (ImGui::MenuItem("Search"))
                        {
                            FileSystem::OpenDirectoryWindow("https://www.google.com/search?q=" + log.text);
                        }
                
                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }
        }

        // Scroll to bottom (if requested)
        if (m_scroll_to_bottom)
        {
            ImGui::SetScrollHereY();
            m_scroll_to_bottom = false;
        }

        ImGui::EndTable();
    }
    
    m_is_reading = false;
}

void Widget_Console::AddLogPackage(const LogPackage& package)
{
    // Wait for reading to finish
    while (m_is_reading) { this_thread::sleep_for(std::chrono::milliseconds(16)); }

    // Save to deque
    m_logs.push_back(package);
    if (static_cast<uint32_t>(m_logs.size()) > m_log_max_count)
    {
        m_logs.pop_front();
    }

    // Update count
    m_log_type_count[package.error_level]++;

    // If the user is displaying this type of messages, scroll to bottom
    if (m_log_type_visibility[package.error_level])
    {
        m_scroll_to_bottom = true;
    }
}

void Widget_Console::Clear()
{
    m_logs.clear();
    m_logs.shrink_to_fit();

    m_log_type_count[0] = 0;
    m_log_type_count[1] = 0;
    m_log_type_count[2] = 0;
}
