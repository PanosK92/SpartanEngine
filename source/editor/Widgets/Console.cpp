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

//= INCLUDES ========================
#include "pch.h"
#include "Console.h"

#include <ranges>
#include <utility>
#include "Window.h"
#include "../ImGui/ImGui_Extension.h"
#include "Commands/Console/ConsoleCommands.h"
//===================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    // Uncomment to enable.
    //TConsoleVar CVarConsoleTest_Int("console.test.int", 12, "int test console var");

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
        if (ImGuiSp::image_button(spartan::ResourceCache::GetIcon(IconType::Console), 15.0f * spartan::Window::GetDpiScale(), false, m_log_type_color[index]))
        {
            visibility = !visibility;
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::Text("%u", m_log_type_count[index]);
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

    // calculate split sizes - no longer reserving space for autocomplete (it's a popup now)
    const float input_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
    const float available_height = ImGui::GetContentRegionAvail().y;
    const float log_height = available_height - input_height;

    // safety first
    std::scoped_lock lock(m_mutex);

    // log output section
    {
        static const ImGuiTableFlags table_flags =
            ImGuiTableFlags_RowBg        |
            ImGuiTableFlags_BordersOuter |
            ImGuiTableFlags_ScrollX      |
            ImGuiTableFlags_ScrollY;

        const ImVec2 log_size = ImVec2(-1.0f, log_height);

        if (ImGui::BeginTable("##console_log_output", 1, table_flags, log_size))
        {
            ImGuiListClipper clipper;

            int visible_count = 0;
            for (const LogPackage& log : m_logs)
            {
                if (m_log_filter.PassFilter(log.text.c_str()) && m_log_type_visibility[log.error_level])
                {
                    visible_count++;
                }
            }

            clipper.Begin(visible_count);
            while (clipper.Step())
            {
                int visible_index = 0;
                for (uint32_t row = 0; row < m_logs.size() && visible_index < clipper.DisplayEnd; row++)
                {
                    LogPackage& log = m_logs[row];

                    if (m_log_filter.PassFilter(log.text.c_str()) && m_log_type_visibility[log.error_level])
                    {
                        if (visible_index >= clipper.DisplayStart)
                        {
                            // switch row
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);

                            // log
                            ImGui::PushID(static_cast<int>(row));
                            {
                                if (log.error_level != 0)
                                {
                                    ImGui::PushStyleColor(ImGuiCol_Text, m_log_type_color[log.error_level]);
                                }

                                ImGui::TextUnformatted(log.text.c_str());

                                if (log.error_level != 0)
                                {
                                    ImGui::PopStyleColor(1);
                                }

                                if (ImGui::BeginPopupContextItem("##console_context_menu"))
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
                        visible_index++;
                    }
                }
            }

            if (m_scroll_to_bottom)
            {
                ImGui::SetScrollHereY(1.0f);
                m_scroll_to_bottom = false;
            }

            ImGui::EndTable();
        }
    }

    // input field
    ImGui::Separator();
    ImGui::SetNextItemWidth(-1.0f);

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

    if (input_active)
    {
        if (m_show_autocomplete && !m_filtered_cvars.empty())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
            {
                m_autocomplete_selection = (m_autocomplete_selection + 1) % static_cast<int>(m_filtered_cvars.size());
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
            {
                m_autocomplete_selection = (m_autocomplete_selection - 1 + static_cast<int>(m_filtered_cvars.size())) % static_cast<int>(m_filtered_cvars.size());
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Tab))
            {
                ApplyAutocomplete();
                reclaim_focus = true;
            }
            else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
            {
                m_show_autocomplete = false;
            }
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
        const float row_height = ImGui::GetTextLineHeightWithSpacing();
        const float header_height = row_height + ImGui::GetStyle().ItemSpacing.y;
        const float content_height = std::min(popup_max_height, header_height + row_height * static_cast<float>(m_filtered_cvars.size()));

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

    m_logs.push_back(package);
    if (static_cast<uint32_t>(m_logs.size()) > m_log_max_count)
    {
        m_logs.pop_front();
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

    m_log_type_count[0] = 0;
    m_log_type_count[1] = 0;
    m_log_type_count[2] = 0;

    spartan::Log::Clear();
}

void Console::UpdateAutocomplete()
{
    m_filtered_cvars.clear();
    m_autocomplete_selection = 0;

    std::string input = m_input_buffer;

    if (input.empty())
    {
        m_show_autocomplete = false;
        return;
    }

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
            const int prev_pos = m_history_position;
            if (data->EventKey == ImGuiKey_UpArrow && !m_show_autocomplete)
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
            else if (data->EventKey == ImGuiKey_DownArrow && !m_show_autocomplete)
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
