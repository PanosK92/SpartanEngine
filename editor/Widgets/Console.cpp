/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES ========================
#include "Console.h"
#include "Window.h"
#include "../ImGui/ImGui_Extension.h"
//===================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    ImVec4 color_to_imvec4(const Color& color)
    {
        return { color.r, color.g, color.b, color.a };
    }
}

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
    // clear button
    if (ImGuiSp::button("Clear"))
    {
        Clear();
    }
    ImGui::SameLine();

    // lambda for info, warning, error filter buttons
    const auto button_log_type_visibility_toggle = [this](uint32_t index)
    {
        bool& visibility = m_log_type_visibility[index];
        ImGui::PushStyleColor(ImGuiCol_Button, visibility ? ImGui::GetStyle().Colors[ImGuiCol_Button] : ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        if (ImGuiSp::image_button(nullptr, IconType::Console, 15.0f * spartan::Window::GetDpiScale(), false, m_log_type_color[index]))
        {
            visibility = !visibility;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%d", m_log_type_count[index]);
        ImGui::SameLine();
    };

    // log category visibility buttons
    button_log_type_visibility_toggle(0);
    button_log_type_visibility_toggle(1);
    button_log_type_visibility_toggle(2);

    // text filter
    const float label_width = 37.0f * spartan::Window::GetDpiScale();
    m_log_filter.Draw("Filter", ImGui::GetContentRegionAvail().x - label_width);
    ImGui::Separator();

    // safety first
    lock_guard lock(m_mutex);

    // content properties
    static const ImGuiTableFlags table_flags =
        ImGuiTableFlags_RowBg        |
        ImGuiTableFlags_BordersOuter |
        ImGuiTableFlags_ScrollX      |
        ImGuiTableFlags_ScrollY;

    static const ImVec2 size = ImVec2(-1.0f);

    // content
    if (ImGui::BeginTable("##widget_console_content", 1, table_flags, size))
    {
        // logs
        for (uint32_t row = 0; row < m_logs.size(); row++)
        {
            LogPackage& log = m_logs[row];

            // text and visibility filters
            if (m_log_filter.PassFilter(log.text.c_str()) && m_log_type_visibility[log.error_level])
            {
                // switch row
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);

                // log
                ImGui::PushID(row);
                {
                    if(log.error_level != 0) // dont style info text's color
                    { 
                        ImGui::PushStyleColor(ImGuiCol_Text, m_log_type_color[log.error_level]);
                    }

                    ImGui::TextUnformatted(log.text.c_str());

                    if(log.error_level != 0)
                    { 
                        ImGui::PopStyleColor(1);
                    }

                    // context menu
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
                            FileSystem::OpenUrl("https://www.google.com/search?q=" + log.text);
                        }

                        ImGui::EndPopup();
                    }
                }
                ImGui::PopID();
            }
        }

        // scroll to bottom (if requested)
        if (m_scroll_to_bottom)
        {
            ImGui::SetScrollHereY();
            m_scroll_to_bottom = false;
        }

        ImGui::EndTable();
    }
}

void Console::AddLogPackage(const LogPackage& package)
{
    lock_guard lock(m_mutex);

    // Save to deque
    m_logs.push_back(package);
    if (static_cast<uint32_t>(m_logs.size()) > m_log_max_count)
    {
        m_logs.pop_front();
    }

    // update count
    m_log_type_count[package.error_level]++;

    // if the user is displaying this type of messages, scroll to bottom
    if (m_log_type_visibility[package.error_level])
    {
        m_scroll_to_bottom = true;
    }
}

void Console::Clear()
{
    m_logs.clear();
    m_logs.shrink_to_fit();

    m_log_type_count[0] = 0;
    m_log_type_count[1] = 0;
    m_log_type_count[2] = 0;
}
