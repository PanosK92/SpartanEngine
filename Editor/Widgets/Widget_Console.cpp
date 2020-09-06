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

void Widget_Console::Tick()
{
    // Clear Button
    if (ImGui::Button("Clear"))    { Clear();} ImGui::SameLine();

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

    // Content
    if (ImGui::BeginChild("scrolling", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImVec4 color_odd    = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
        ImVec4 color_even   = color_odd; color_even.w = 0;

        // Set max log width
        float max_log_width = 0;
        max_log_width = m_log_type_visibility[0] ? Math::Helper::Max(max_log_width, m_log_type_max_width[0]) : max_log_width;
        max_log_width = m_log_type_visibility[1] ? Math::Helper::Max(max_log_width, m_log_type_max_width[1]) : max_log_width;
        max_log_width = m_log_type_visibility[2] ? Math::Helper::Max(max_log_width, m_log_type_max_width[2]) : max_log_width;
        max_log_width = Math::Helper::Max(max_log_width, ImGui::GetWindowContentRegionWidth());
        ImGui::PushItemWidth(max_log_width);

        // Wait for reading to finish
        while (m_is_reading)
        {
            this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        uint32_t index = 0;
        m_is_reading = true;
        for (LogPackage& log : m_logs)
        {
            if (m_log_filter.PassFilter(log.text.c_str()))
            {
                if (m_log_type_visibility[log.error_level])
                {
                    // Log entry
                    ImGui::BeginGroup();
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, m_log_type_color[log.error_level]);            // text color
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, index % 2 != 0 ? color_odd : color_even);   // background color
                        ImGui::InputText("##log", &log.text, ImGuiInputTextFlags_ReadOnly);
                        ImGui::PopStyleColor(2);

                        ImGui::EndGroup();

                        // Trigger context menu
                        if (ImGui::IsMouseClicked(1) && ImGui::IsItemHovered(ImGuiHoveredFlags_RectOnly))
                        {
                            m_log_selected = log;
                            ImGui::OpenPopup("##ConsoleContextMenu");
                        }
                    }

                    index++;
                }
            }
        }
        m_is_reading = false;

        ImGui::PopItemWidth();

        // Context menu (if requested)
        if (ImGui::BeginPopup("##ConsoleContextMenu"))
        {
            if (ImGui::MenuItem("Copy"))
            {
                ImGui::LogToClipboard();
                ImGui::LogText("%s", m_log_selected.text.c_str());
                ImGui::LogFinish();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Search"))
            {
                FileSystem::OpenDirectoryWindow("https://www.google.com/search?q=" + m_log_selected.text);
            }

            ImGui::EndPopup();
        }

        // Scroll to bottom (if requested)
        if (m_scroll_to_bottom)
        {
            ImGui::SetScrollHereY();
            m_scroll_to_bottom = false;
        }

        ImGui::EndChild();
    }
}

void Widget_Console::AddLogPackage(const LogPackage& package)
{
    // Wait for reading to finish
    while (m_is_reading) {}

    // Save to deque
    m_logs.push_back(package);
    if (static_cast<uint32_t>(m_logs.size()) > m_log_max_count)
    {
        m_logs.pop_front();
    }

    // Update count
    m_log_type_count[package.error_level]++;

    // Compute max width
    float& width = m_log_type_max_width[package.error_level];
    if (ImGui::GetCurrentContext()->Font)
    {
        width = Math::Helper::Max(width, ImGui::CalcTextSize(package.text.c_str()).x + 10);
    }
    else
    {
        // During startup, the font can be null, so compute a poor man's width
        width = Math::Helper::Max(width, package.text.size() * 23.0f);
    }

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

    m_log_type_max_width[0] = 0;
    m_log_type_max_width[1] = 0;
    m_log_type_max_width[2] = 0;

    m_log_type_count[0] = 0;
    m_log_type_count[1] = 0;
    m_log_type_count[2] = 0;
}
