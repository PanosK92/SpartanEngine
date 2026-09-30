/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ================================
#include "pch.h"
#include "Console.h"
#include <fstream>
#include <ranges>
#include <utility>
#include "Window.h"
#include "../imgui/ImGui_EditorUi.h"
#include "../imgui/ImGui_Extension.h"
#include "../imgui/source/imgui_internal.h"
#include "commands/console/ConsoleCommands.h"
//===========================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

namespace
{
    const char* severity_names[3] = { "Info", "Warnings", "Errors" };

    // mcp requests arrive on the bridge thread, the widget applies them and publishes its state on the main thread
    mutex mcp_mutex;
    optional<ConsoleRequest> mcp_request;
    ConsoleState mcp_state;

    string to_lower(string_view text)
    {
        string result(text);
        ranges::transform(result, result.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
        return result;
    }

    string trim(const string& text)
    {
        const size_t begin = text.find_first_not_of(" \t");
        if (begin == string::npos)
        {
            return "";
        }
        const size_t end = text.find_last_not_of(" \t");
        return text.substr(begin, end - begin + 1);
    }

    // splits "[hh:mm:ss]: source: message" and strips compiler decorations from the source, a reader needs
    // "save_screenshot_async", not "spartan::`anonymous-namespace'::save_screenshot_async::<lambda_1>::operator ()",
    // the full signature is still in log.txt
    void tidy(LogPackage& log)
    {
        string& text = log.text;
        log.time_end = log.source_begin = log.source_end = log.message_begin = 0;
        if (text.size() < 12 || text[0] != '[' || text[9] != ']' || text.compare(10, 2, ": ") != 0)
        {
            return;
        }
        log.time_end = 10;

        const size_t separator = text.find(": ", 12);
        if (separator == string::npos || separator - 12 > 160)
        {
            log.source_begin = log.source_end = log.message_begin = 12;
            return;
        }

        string source = text.substr(12, separator - 12);
        for (size_t at = source.find("`anonymous-namespace'::"); at != string::npos; at = source.find("`anonymous-namespace'::"))
        {
            source.erase(at, strlen("`anonymous-namespace'::"));
        }
        for (size_t at = source.find("::<lambda_"); at != string::npos; at = source.find("::<lambda_"))
        {
            const size_t close = source.find(">::operator ()", at);
            if (close == string::npos)
            {
                break;
            }
            source.erase(at, close + strlen(">::operator ()") - at);
        }
        if (source.rfind("spartan::", 0) == 0)
        {
            source.erase(0, strlen("spartan::"));
        }

        // a message that happens to contain ": " without a function in front is all message
        if (source.empty() || source.find(' ') != string::npos)
        {
            log.source_begin = log.source_end = log.message_begin = 12;
            return;
        }

        text = text.substr(0, 12) + source + text.substr(separator);
        log.source_begin  = 12;
        log.source_end    = static_cast<uint16_t>(12 + source.size());
        log.message_begin = static_cast<uint16_t>(log.source_end + 2);
    }

    // the three severities keep the glyphs they had, a circle with an i, a triangle with a bang, a circle with a cross
    void draw_severity_icon(ImDrawList* draw_list, const ImVec2& center, const float radius, const uint32_t index, const ImU32 color, const float thickness)
    {
        if (index == 0)
        {
            draw_list->AddCircle(center, radius, color, 16, thickness);
            draw_list->AddCircleFilled(ImVec2(center.x, center.y - radius * 0.45f), thickness, color);
            draw_list->AddLine(ImVec2(center.x, center.y - radius * 0.08f), ImVec2(center.x, center.y + radius * 0.5f), color, thickness);
        }
        else if (index == 1)
        {
            draw_list->AddTriangle(ImVec2(center.x, center.y - radius), ImVec2(center.x - radius, center.y + radius * 0.85f), ImVec2(center.x + radius, center.y + radius * 0.85f), color, thickness);
            draw_list->AddLine(ImVec2(center.x, center.y - radius * 0.42f), ImVec2(center.x, center.y + radius * 0.2f), color, thickness);
            draw_list->AddCircleFilled(ImVec2(center.x, center.y + radius * 0.52f), thickness, color);
        }
        else
        {
            draw_list->AddCircle(center, radius, color, 16, thickness);
            draw_list->AddLine(ImVec2(center.x - radius * 0.42f, center.y - radius * 0.42f), ImVec2(center.x + radius * 0.42f, center.y + radius * 0.42f), color, thickness);
            draw_list->AddLine(ImVec2(center.x + radius * 0.42f, center.y - radius * 0.42f), ImVec2(center.x - radius * 0.42f, center.y + radius * 0.42f), color, thickness);
        }
    }

    string build_selection_text(const vector<pair<uint32_t, const LogPackage*>>& visible_logs, int start_line, int start_char, int end_line, int end_char)
    {
        if (start_line > end_line || (start_line == end_line && start_char > end_char))
        {
            swap(start_line, end_line);
            swap(start_char, end_char);
        }

        string selected_text;
        for (int i = start_line; i <= end_line && i < static_cast<int>(visible_logs.size()); i++)
        {
            const string& line_text = visible_logs[i].second->text;
            const int start_idx     = (i == start_line) ? min(start_char, static_cast<int>(line_text.size())) : 0;
            const int end_idx       = (i == end_line) ? min(end_char, static_cast<int>(line_text.size())) : static_cast<int>(line_text.size());

            if (start_idx < end_idx)
            {
                selected_text += line_text.substr(start_idx, end_idx - start_idx);
            }
            else if (i != end_line)
            {
                selected_text += line_text.substr(start_idx);
            }

            if (i < end_line)
            {
                selected_text += "\n";
            }
        }
        return selected_text;
    }

    // 0.250000 reads as 0.25, every decimal number in the string is trimmed so vectors read the same way
    string readable_value(const string& value)
    {
        string result;
        size_t i = 0;
        while (i < value.size())
        {
            const size_t start = i;
            while (i < value.size() && (isdigit(static_cast<unsigned char>(value[i])) || value[i] == '.'))
            {
                i++;
            }

            if (i == start)
            {
                result += value[i++];
                continue;
            }

            string number = value.substr(start, i - start);
            if (number.find('.') != string::npos && count(number.begin(), number.end(), '.') == 1)
            {
                number.erase(number.find_last_not_of('0') + 1);
                if (number.back() == '.')
                {
                    number.pop_back();
                }
            }
            result += number;
        }
        return result;
    }
}

Console::Console(Editor* editor) : Widget(editor)
{
    m_title = "Console";
    m_dock  = WidgetDock::Down;

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
    std::scoped_lock lock(m_mutex);

    ApplyMcpRequest();

    // the visible list feeds the toolbar count, the log and the mcp state
    m_visible_logs.clear();
    for (uint32_t i = 0; i < static_cast<uint32_t>(m_logs.size()); i++)
    {
        const LogPackage& log = m_logs[i];
        const bool type_visible = log.is_command || m_log_type_visibility[log.error_level];
        if (type_visible && m_log_filter.PassFilter(log.text.c_str()))
        {
            m_visible_logs.push_back({ i, &log });
        }
    }

    ShowToolbar();

    const float input_height = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y * 2.0f + 1.0f;
    ShowLog(ImGui::GetContentRegionAvail().y - input_height);
    ShowInput();

    PublishMcpState();
}

void Console::ShowToolbar()
{
    const float dpi         = spartan::Window::GetDpiScale();
    const ImGuiStyle& style = ImGui::GetStyle();
    ImDrawList* draw_list   = ImGui::GetWindowDrawList();
    const float height      = ImGui::GetFrameHeight();
    const float row_width   = ImGui::GetContentRegionAvail().x;
    const float right_edge  = ImGui::GetCursorScreenPos().x + row_width;

    // words where there is room, a narrow console keeps just the glyph and the count
    const bool with_names = row_width > 620.0f * dpi;

    // severity filters read like the asset chips, a quiet outline at zero so an empty error count looks calm,
    // tinted once there is something to see, and dimmed when that severity is hidden
    for (uint32_t index = 0; index < 3; index++)
    {
        if (index > 0)
        {
            ImGui::SameLine(0, ImGui::EditorUi::scaled(4.0f));
        }

        char label[48];
        if (with_names)
        {
            snprintf(label, sizeof(label), "%s %u", severity_names[index], m_log_type_count[index]);
        }
        else
        {
            snprintf(label, sizeof(label), "%u", m_log_type_count[index]);
        }

        const float icon_size = 14.0f * dpi;
        const float pad_x     = ImGui::EditorUi::scaled(9.0f);
        const float width     = pad_x * 2.0f + icon_size + style.ItemInnerSpacing.x + ImGui::CalcTextSize(label).x;

        ImGui::PushID(static_cast<int>(index));
        const bool pressed = ImGui::InvisibleButton("##severity", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        bool& visible = m_log_type_visibility[index];
        if (pressed)
        {
            visible = !visible;
            m_selection.Clear();
        }

        const ImVec4 tint       = m_log_type_color[index];
        const bool has_messages = m_log_type_count[index] > 0;
        const ImVec2 min_pos    = ImGui::GetItemRectMin();
        const ImVec2 max_pos    = ImGui::GetItemRectMax();
        const float rounding    = height * 0.5f;

        ImVec4 fill = ImVec4(0, 0, 0, 0);
        if (visible && has_messages)
        {
            fill = ImGui::EditorUi::alpha(tint, index == 0 ? 0.10f : 0.18f);
        }
        if (hovered)
        {
            fill = ImGui::EditorUi::alpha(tint, 0.24f);
        }
        const ImVec4 border     = visible && has_messages ? ImGui::EditorUi::alpha(tint, 0.5f) : ImGui::Style::color_border;
        const ImVec4 foreground = !visible ? ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.45f) : (has_messages ? tint : ImGui::Style::color_text_muted);

        draw_list->AddRectFilled(min_pos, max_pos, ImGui::EditorUi::color(fill), rounding);
        draw_list->AddRect(min_pos, max_pos, ImGui::EditorUi::color(border), rounding);

        const ImVec2 icon_center = ImVec2(min_pos.x + pad_x + icon_size * 0.5f, (min_pos.y + max_pos.y) * 0.5f);
        draw_severity_icon(draw_list, icon_center, icon_size * 0.4f, index, ImGui::EditorUi::color(foreground), max(1.0f, 1.25f * dpi));
        draw_list->AddText(ImVec2(min_pos.x + pad_x + icon_size + style.ItemInnerSpacing.x, min_pos.y + (height - ImGui::GetFontSize()) * 0.5f), ImGui::EditorUi::color(foreground), label);

        // hiding a severity is easy to forget, so the tooltip says what is hidden, not just what the button does
        if (hovered)
        {
            if (visible)
            {
                ImGui::SetTooltip("Hide %s", to_lower(severity_names[index]).c_str());
            }
            else
            {
                ImGui::SetTooltip("%s are hidden, click to show them", severity_names[index]);
            }
        }
    }

    // clear sits at the far end, away from the filters, it throws the history away and should not be hit by accident
    const char* clear_label  = "Clear";
    const float clear_width  = ImGui::CalcTextSize(clear_label).x + style.FramePadding.x * 2.0f;
    const float search_x     = ImGui::GetItemRectMax().x + ImGui::EditorUi::scaled(10.0f);
    const float search_width = right_edge - clear_width - ImGui::EditorUi::scaled(8.0f) - search_x;

    ImGui::SameLine(0, ImGui::EditorUi::scaled(10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::SetNextItemWidth(max(80.0f * dpi, search_width));
    ImGui::SetNextItemShortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_Tooltip);
    if (ImGui::InputTextWithHint("##console_filter", "Filter messages", m_log_filter.InputBuf, IM_ARRAYSIZE(m_log_filter.InputBuf), ImGuiInputTextFlags_EscapeClearsAll))
    {
        m_log_filter.Build();
        m_selection.Clear();
    }
    const ImVec2 search_min = ImGui::GetItemRectMin();
    const ImVec2 search_max = ImGui::GetItemRectMax();
    ImGui::PopStyleVar();

    // the result count lives inside the field, where the eyes are while typing
    if (m_log_filter.IsActive())
    {
        char matches[48];
        snprintf(matches, sizeof(matches), "%u of %u", static_cast<uint32_t>(m_visible_logs.size()), static_cast<uint32_t>(m_logs.size()));
        const ImVec2 size = ImGui::CalcTextSize(matches);
        const float x     = search_max.x - size.x - 8.0f;
        if (x > search_min.x + ImGui::CalcTextSize(m_log_filter.InputBuf).x + 24.0f)
        {
            const ImVec4 tint = m_visible_logs.empty() ? ImGui::Style::color_error : ImGui::Style::color_text_muted;
            draw_list->AddText(ImVec2(x, (search_min.y + search_max.y - size.y) * 0.5f), ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.8f)), matches);
        }
    }

    ImGui::SameLine(0, ImGui::EditorUi::scaled(8.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::EditorUi::alpha(ImGui::Style::color_error, 0.18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::EditorUi::alpha(ImGui::Style::color_error, 0.28f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    if (ImGui::Button(clear_label))
    {
        Clear();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(4);
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip("Clear the console and start a fresh log.txt, the old one is kept as log_previous.txt");
    }

    ImGui::Dummy(ImVec2(0, 1));
}

void Console::ShowEmptyState()
{
    const bool filtered   = m_log_filter.IsActive() || !m_log_type_visibility[0] || !m_log_type_visibility[1] || !m_log_type_visibility[2];
    const char* title     = m_logs.empty() ? "Nothing logged yet" : "No messages match";

    // names what is actually filtering, so the fix is obvious without checking every control
    string detail_text = "Engine messages appear here. Type a console variable below to read it, add a value to change it.";
    if (!m_logs.empty())
    {
        static const char* severity_names[] = { "info", "warnings", "errors" };
        string hidden;
        for (uint32_t i = 0; i < 3; i++)
        {
            if (!m_log_type_visibility[i])
            {
                hidden += (hidden.empty() ? "" : " and ") + string(severity_names[i]);
            }
        }

        detail_text = "Nothing";
        if (m_log_filter.IsActive())
        {
            detail_text += " contains \"" + string(m_log_filter.InputBuf) + "\"";
        }
        if (!hidden.empty())
        {
            detail_text += (m_log_filter.IsActive() ? " among the visible messages, " : " is visible, ") + hidden + " hidden";
        }
        detail_text += ".";
    }
    const char* detail = detail_text.c_str();
    const float width     = ImGui::GetContentRegionAvail().x;
    const float wrap      = min(width - 32.0f, ImGui::EditorUi::scaled(440.0f));
    const float origin_x  = ImGui::GetCursorPosX();

    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + max(12.0f, ImGui::GetContentRegionAvail().y * 0.28f));

    const float title_width = ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(origin_x + max(0.0f, (width - title_width) * 0.5f));
    ImGui::TextColored(ImGui::Style::color_text, "%s", title);

    const ImVec2 detail_size = ImGui::CalcTextSize(detail, nullptr, false, wrap);
    const float detail_x     = origin_x + max(0.0f, (width - detail_size.x) * 0.5f);
    ImGui::SetCursorPosX(detail_x);
    ImGui::PushTextWrapPos(detail_x + wrap);
    ImGui::TextColored(ImGui::Style::color_text_muted, "%s", detail);
    ImGui::PopTextWrapPos();

    if (filtered && !m_logs.empty())
    {
        ImGui::Dummy(ImVec2(0, 4));
        const char* label        = "Clear filters";
        const float button_width = ImGui::CalcTextSize(label).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        ImGui::SetCursorPosX(origin_x + max(0.0f, (width - button_width) * 0.5f));
        if (ImGui::Button(label))
        {
            m_log_filter.Clear();
            m_log_type_visibility[0] = m_log_type_visibility[1] = m_log_type_visibility[2] = true;
        }
    }
}

void Console::ShowLog(const float height)
{
    const float dpi               = spartan::Window::GetDpiScale();
    const auto& visible_logs      = m_visible_logs;
    const float line_height       = ImGui::GetTextLineHeightWithSpacing();
    const float text_line_height  = ImGui::GetTextLineHeight();
    const float gutter_width      = max(2.0f, IM_ROUND(2.0f * dpi));
    const float text_indent       = gutter_width + ImGui::EditorUi::scaled(8.0f);
    const ImVec4 selection_color  = ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, 0.35f);
    const ImVec4 text_color       = ImGui::Style::color_text;
    const ImVec4 muted            = ImGui::Style::color_text_muted;

    // the parts of a line get different weights, the eye lands on the message, the time and source are there when you look for them
    const ImU32 color_time      = ImGui::EditorUi::color(ImGui::EditorUi::alpha(muted, 0.55f));
    const ImU32 color_separator = ImGui::EditorUi::color(ImGui::EditorUi::alpha(muted, 0.35f));
    const ImU32 color_source    = ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::lerp(muted, ImGui::Style::color_accent_1, 0.35f), 0.85f));
    string pending_result;

    // validate selection bounds - clear if invalid (e.g., logs were removed or filtered)
    if (m_selection.HasSelection())
    {
        const int max_line = static_cast<int>(visible_logs.size()) - 1;
        if (visible_logs.empty() || m_selection.start_line > max_line || m_selection.end_line > max_line || m_selection.start_line < 0 || m_selection.end_line < 0)
        {
            m_selection.Clear();
        }
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::Style::lerp(ImGui::Style::bg_color_1, ImGui::Style::bg_color_2, 0.08f));

    if (ImGui::BeginChild("##console_log_child", ImVec2(-1.0f, height), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar))
    {
        ImDrawList* draw_list        = ImGui::GetWindowDrawList();
        const float window_width     = ImGui::GetContentRegionAvail().x;
        const ImVec2 mouse_pos       = ImGui::GetMousePos();
        const bool is_window_hovered = ImGui::IsWindowHovered();
        const float cursor_blink     = static_cast<float>(fmod(ImGui::GetTime(), 1.0));

        if (visible_logs.empty())
        {
            ShowEmptyState();
        }

        float content_origin_y  = 0.0f;
        int first_rendered_row  = -1;
        int mouse_hover_row     = -1;
        float mouse_hover_row_x = 0.0f;

        const bool mouse_clicked  = is_window_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        const bool mouse_dragging = m_selection.is_dragging && ImGui::IsMouseDown(ImGuiMouseButton_Left);
        const bool mouse_released = m_selection.is_dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left);

        if (mouse_clicked)
        {
            m_selection.Clear();
            m_selection.is_dragging = true;
        }

        if (mouse_released)
        {
            m_selection.is_dragging = false;
        }

        if (is_window_hovered && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A) && !visible_logs.empty())
        {
            m_selection.start_line = 0;
            m_selection.start_char = 0;
            m_selection.end_line   = static_cast<int>(visible_logs.size()) - 1;
            m_selection.end_char   = static_cast<int>(visible_logs.back().second->text.size());
        }

        if (m_selection.HasSelection() && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
        {
            const string selected_text = build_selection_text(visible_logs, m_selection.start_line, m_selection.start_char, m_selection.end_line, m_selection.end_char);
            if (!selected_text.empty())
            {
                ImGui::SetClipboardText(selected_text.c_str());
            }
        }

        // normalize selection for rendering
        int sel_start_line = m_selection.start_line;
        int sel_start_char = m_selection.start_char;
        int sel_end_line   = m_selection.end_line;
        int sel_end_char   = m_selection.end_char;
        if (m_selection.HasSelection() && (sel_start_line > sel_end_line || (sel_start_line == sel_end_line && sel_start_char > sel_end_char)))
        {
            swap(sel_start_line, sel_end_line);
            swap(sel_start_char, sel_end_char);
        }

        // the filter is case insensitive and "-term" excludes, only the terms that include are worth highlighting
        vector<string> match_terms;
        for (const ImGuiTextFilter::ImGuiTextRange& range : m_log_filter.Filters)
        {
            if (range.empty() || range.b[0] == '-')
            {
                continue;
            }
            string term(range.b, range.e);
            transform(term.begin(), term.end(), term.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            match_terms.push_back(term);
        }

        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible_logs.size()), line_height);

        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
            {
                const LogPackage& log = *visible_logs[row].second;
                const char* text      = log.text.c_str();
                const ImVec2 row_pos  = ImGui::GetCursorScreenPos();
                const float text_x    = row_pos.x + text_indent;
                const float text_y    = row_pos.y + (line_height - text_line_height) * 0.5f;

                if (first_rendered_row < 0)
                {
                    first_rendered_row = row;
                    content_origin_y   = row_pos.y;
                }

                const bool row_hovered = is_window_hovered && mouse_pos.y >= row_pos.y && mouse_pos.y < row_pos.y + line_height;
                if (mouse_pos.y >= row_pos.y && mouse_pos.y < row_pos.y + line_height)
                {
                    mouse_hover_row   = row;
                    mouse_hover_row_x = text_x;
                }

                const float text_width = ImGui::CalcTextSize(text).x;
                const float row_width  = max(window_width, text_indent + text_width + ImGui::EditorUi::scaled(60.0f));
                const ImVec2 row_max   = ImVec2(row_pos.x + row_width, row_pos.y + line_height);

                // alternating rows keep the eye on its line across a wide log, the severity tints go on top
                draw_list->AddRectFilled(row_pos, row_max, ImGui::GetColorU32(row % 2 == 0 ? ImGuiCol_TableRowBg : ImGuiCol_TableRowBgAlt));

                // warnings and errors tint their whole row, so they can be found while scrolling fast without reading
                if (!log.is_command && log.error_level != 0)
                {
                    const ImVec4 tint = m_log_type_color[log.error_level];
                    draw_list->AddRectFilled(row_pos, row_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(tint, 0.07f)));
                    draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + gutter_width, row_max.y), ImGui::EditorUi::color(tint));
                }
                else if (log.is_command)
                {
                    draw_list->AddRectFilled(row_pos, ImVec2(row_pos.x + gutter_width, row_max.y), ImGui::EditorUi::color(ImGui::Style::color_accent_1));
                }
                if (row_hovered && !m_selection.is_dragging)
                {
                    draw_list->AddRectFilled(row_pos, row_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(text_color, 0.04f)));
                }

                // selection highlight
                if (m_selection.HasSelection() && row >= sel_start_line && row <= sel_end_line && (sel_start_line != sel_end_line || sel_start_char != sel_end_char))
                {
                    float sel_x_start = text_x;
                    float sel_x_end   = text_x + text_width;
                    if (row == sel_start_line && sel_start_char > 0)
                    {
                        sel_x_start += ImGui::CalcTextSize(text, text + min(sel_start_char, static_cast<int>(log.text.size()))).x;
                    }
                    if (row == sel_end_line && sel_end_char < static_cast<int>(log.text.size()))
                    {
                        sel_x_end = text_x + ImGui::CalcTextSize(text, text + min(sel_end_char, static_cast<int>(log.text.size()))).x;
                    }
                    draw_list->AddRectFilled(ImVec2(sel_x_start, row_pos.y), ImVec2(sel_x_end, row_max.y), ImGui::EditorUi::color(selection_color));
                }

                // where the filter matched, so a filtered list does not have to be read line by line to find why it is there
                for (const string& term : match_terms)
                {
                    const size_t term_length = term.size();
                    for (size_t at = 0; at + term_length <= log.text.size(); at++)
                    {
                        bool equal = true;
                        for (size_t k = 0; k < term_length && equal; k++)
                        {
                            equal = tolower(static_cast<unsigned char>(text[at + k])) == term[k];
                        }
                        if (!equal)
                        {
                            continue;
                        }

                        const float match_x0 = text_x + ImGui::CalcTextSize(text, text + at).x;
                        const float match_x1 = match_x0 + ImGui::CalcTextSize(text + at, text + at + term_length).x;
                        draw_list->AddRectFilled(ImVec2(match_x0 - 1.0f, text_y - 1.0f), ImVec2(match_x1 + 1.0f, text_y + text_line_height + 1.0f), ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, 0.32f)), 3.0f);
                        at += term_length - 1;
                    }
                }

                // blinking text cursor at selection endpoint
                if (m_selection.HasSelection() && cursor_blink < 0.5f && row == m_selection.end_line)
                {
                    float cursor_x = text_x;
                    if (m_selection.end_char > 0 && m_selection.end_char <= static_cast<int>(log.text.size()))
                    {
                        cursor_x += ImGui::CalcTextSize(text, text + m_selection.end_char).x;
                    }
                    draw_list->AddRectFilled(ImVec2(cursor_x, row_pos.y + 2.0f), ImVec2(cursor_x + dpi, row_max.y - 2.0f), ImGui::EditorUi::color(text_color));
                }

                // the text, drawn part by part, the characters are the stored ones so selection and copy stay exact
                {
                    float x = text_x;
                    auto segment = [&](const size_t begin, const size_t end, const ImU32 color)
                    {
                        if (end <= begin)
                        {
                            return;
                        }
                        draw_list->AddText(ImVec2(x, text_y), color, text + begin, text + end);
                        x += ImGui::CalcTextSize(text + begin, text + end).x;
                    };

                    const size_t length = log.text.size();
                    if (log.is_command)
                    {
                        segment(0, length, ImGui::EditorUi::color(ImGui::Style::color_accent_hi));
                    }
                    else
                    {
                        const ImVec4 message_color = log.error_level == 0 ? text_color : m_log_type_color[log.error_level];
                        segment(0, log.time_end, color_time);
                        segment(log.time_end, log.source_begin, color_separator);
                        segment(log.source_begin, log.source_end, color_source);
                        segment(log.source_end, log.message_begin, color_separator);
                        segment(max<size_t>(log.message_begin, log.time_end), length, ImGui::EditorUi::color(message_color));
                    }
                }

                // repeat count badge for collapsed duplicates
                float badge_end = text_x + text_width;
                if (log.repeat_count > 1)
                {
                    char badge[16];
                    snprintf(badge, sizeof(badge), "\xC3\x97%u", log.repeat_count);
                    const ImVec2 badge_size = ImGui::CalcTextSize(badge);
                    const float badge_pad   = 5.0f * dpi;
                    const float badge_x     = text_x + text_width + badge_pad * 2.0f;
                    const float badge_y     = row_pos.y + (line_height - badge_size.y) * 0.5f;
                    draw_list->AddRectFilled(
                        ImVec2(badge_x - badge_pad, badge_y - 1.0f),
                        ImVec2(badge_x + badge_size.x + badge_pad, badge_y + badge_size.y + 1.0f),
                        ImGui::EditorUi::color(ImGui::EditorUi::alpha(text_color, 0.10f)),
                        (badge_size.y + 2.0f) * 0.5f
                    );
                    draw_list->AddText(ImVec2(badge_x, badge_y), ImGui::EditorUi::color(muted), badge);
                    badge_end = badge_x + badge_size.x + badge_pad;
                }

                // an item the size of the line, it carries the context menu and sets the width for horizontal scrolling
                ImGui::SetCursorScreenPos(row_pos);
                ImGui::PushID(row);
                ImGui::Dummy(ImVec2(max(window_width, badge_end - row_pos.x + 8.0f), text_line_height));

                if (ImGui::BeginPopupContextItem("##console_context_menu"))
                {
                    const string message = log.text.substr(min<size_t>(log.message_begin, log.text.size()));
                    const string source  = log.text.substr(log.source_begin, log.source_end - log.source_begin);

                    if (ImGui::MenuItem("Copy message"))
                    {
                        ImGui::SetClipboardText(message.c_str());
                    }
                    if (ImGui::MenuItem("Copy line"))
                    {
                        ImGui::SetClipboardText(log.text.c_str());
                    }
                    if (m_selection.HasSelection() && ImGui::MenuItem("Copy selection"))
                    {
                        const string selected_text = build_selection_text(visible_logs, sel_start_line, sel_start_char, sel_end_line, sel_end_char);
                        if (!selected_text.empty())
                        {
                            ImGui::SetClipboardText(selected_text.c_str());
                        }
                    }

                    // one click isolates a subsystem, the fastest way to follow one system through a noisy log
                    if (!source.empty())
                    {
                        ImGui::Separator();
                        const string filter_label = "Show only " + source;
                        if (ImGui::MenuItem(filter_label.c_str()))
                        {
                            strncpy_s(m_log_filter.InputBuf, sizeof(m_log_filter.InputBuf), source.c_str(), _TRUNCATE);
                            m_log_filter.Build();
                            m_selection.Clear();
                        }
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Copy all"))
                    {
                        string all_text;
                        for (size_t i = 0; i < visible_logs.size(); i++)
                        {
                            all_text += visible_logs[i].second->text;
                            if (i + 1 < visible_logs.size())
                            {
                                all_text += "\n";
                            }
                        }
                        if (!all_text.empty())
                        {
                            ImGui::SetClipboardText(all_text.c_str());
                        }
                    }

                    if (ImGui::MenuItem("Select all") && !visible_logs.empty())
                    {
                        m_selection.start_line = 0;
                        m_selection.start_char = 0;
                        m_selection.end_line   = static_cast<int>(visible_logs.size()) - 1;
                        m_selection.end_char   = static_cast<int>(visible_logs.back().second->text.size());
                    }

                    if (ImGui::MenuItem("Save to file"))
                    {
                        ofstream file("console_output.txt", ios::out | ios::trunc);
                        if (file.is_open())
                        {
                            for (size_t i = 0; i < visible_logs.size(); i++)
                            {
                                file << visible_logs[i].second->text;
                                if (i + 1 < visible_logs.size())
                                {
                                    file << "\n";
                                }
                            }
                            file.close();
                            pending_result = "saved " + to_string(visible_logs.size()) + " lines to " + FileSystem::GetWorkingDirectory() + "/console_output.txt";
                        }
                    }

                    ImGui::Separator();

                    // the message alone, a search for the timestamp and function name finds nothing useful
                    if (ImGui::MenuItem("Search the web for this message"))
                    {
                        string query;
                        for (const unsigned char c : message)
                        {
                            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                            {
                                query += static_cast<char>(c);
                            }
                            else
                            {
                                char encoded[4];
                                snprintf(encoded, sizeof(encoded), "%%%02X", c);
                                query += encoded;
                            }
                        }
                        FileSystem::OpenUrl("https://www.google.com/search?q=" + query);
                    }

                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }

        // rows reserve only the text height, the last one needs its half of the line spacing to not touch the edge
        if (!visible_logs.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, (line_height - text_line_height) * 0.5f));
        }

        // update selection based on mouse hover (using actual rendered row positions)
        if (mouse_hover_row >= 0)
        {
            auto get_char_at_x = [&](const char* text, const float row_x, const float mouse_x) -> int
            {
                const float content_x = mouse_x - row_x;
                if (content_x <= 0)
                {
                    return 0;
                }

                int char_idx    = 0;
                float current_x = 0;
                for (const char* p = text; *p; p++)
                {
                    const float char_width = ImGui::CalcTextSize(p, p + 1).x;
                    if (current_x + char_width * 0.5f > content_x)
                    {
                        break;
                    }
                    current_x += char_width;
                    char_idx++;
                }
                return char_idx;
            };

            if (mouse_clicked)
            {
                m_selection.start_line = mouse_hover_row;
                m_selection.start_char = get_char_at_x(visible_logs[mouse_hover_row].second->text.c_str(), mouse_hover_row_x, mouse_pos.x);
                m_selection.end_line   = m_selection.start_line;
                m_selection.end_char   = m_selection.start_char;
            }
            else if (mouse_dragging)
            {
                m_selection.end_line = mouse_hover_row;
                m_selection.end_char = get_char_at_x(visible_logs[mouse_hover_row].second->text.c_str(), mouse_hover_row_x, mouse_pos.x);
            }
        }
        else if (mouse_dragging && first_rendered_row >= 0 && !visible_logs.empty())
        {
            if (mouse_pos.y < content_origin_y)
            {
                m_selection.end_line = clipper.DisplayStart;
                m_selection.end_char = 0;
            }
            else
            {
                m_selection.end_line = min(clipper.DisplayEnd - 1, static_cast<int>(visible_logs.size()) - 1);
                m_selection.end_char = static_cast<int>(visible_logs[m_selection.end_line].second->text.size());
            }
        }

        // track whether the user has scrolled away from the bottom
        const float scroll_y     = ImGui::GetScrollY();
        const float scroll_max_y = ImGui::GetScrollMaxY();
        m_user_scrolled_up       = (scroll_max_y > 0.0f) && (scroll_y < scroll_max_y - line_height);
        if (!m_user_scrolled_up)
        {
            m_unseen_count = 0;
        }

        if (m_scroll_to_bottom)
        {
            ImGui::SetScrollY(ImGui::GetScrollMaxY() + line_height * 4.0f);
            m_scroll_to_bottom = false;
        }

        // reading back through the log pauses auto scroll, a pill says so and counts what arrived meanwhile,
        // drawn in this window rather than the foreground list so it no longer paints over popups and other windows
        if (m_user_scrolled_up)
        {
            char label[64];
            if (m_unseen_count > 0)
            {
                snprintf(label, sizeof(label), m_unseen_count == 1 ? "1 new message" : "%u new messages", m_unseen_count);
            }
            else
            {
                snprintf(label, sizeof(label), "Jump to latest");
            }

            const ImVec2 text_size  = ImGui::CalcTextSize(label);
            const float chevron     = ImGui::GetFontSize() * 0.9f;
            const float pad_x       = ImGui::EditorUi::scaled(12.0f);
            const ImVec2 pill_size  = ImVec2(pad_x * 2.0f + chevron + ImGui::EditorUi::scaled(4.0f) + text_size.x, text_size.y + ImGui::EditorUi::scaled(10.0f));
            const ImVec2 child_min  = ImGui::GetWindowPos();
            const ImVec2 child_size = ImGui::GetWindowSize();
            const ImVec2 pill_min   = ImVec2(child_min.x + (child_size.x - pill_size.x) * 0.5f, child_min.y + child_size.y - pill_size.y - ImGui::EditorUi::scaled(10.0f));
            const ImVec2 pill_max   = ImVec2(pill_min.x + pill_size.x, pill_min.y + pill_size.y);

            ImGui::SetCursorScreenPos(pill_min);
            if (ImGui::InvisibleButton("##jump_to_latest", pill_size))
            {
                m_scroll_to_bottom = true;
            }
            const bool hovered   = ImGui::IsItemHovered();
            const ImVec4 accent  = ImGui::Style::color_accent_1;
            const float rounding = pill_size.y * 0.5f;
            draw_list->AddRectFilled(pill_min, pill_max, ImGui::EditorUi::color(ImGui::Style::lerp(ImGui::Style::color_panel, accent, hovered ? 0.35f : 0.22f)), rounding);
            draw_list->AddRect(pill_min, pill_max, ImGui::EditorUi::color(ImGui::EditorUi::alpha(accent, 0.7f)), rounding);
            ImGui::EditorUi::draw_chevron(draw_list, ImVec2(pill_min.x + pad_x + chevron * 0.5f, (pill_min.y + pill_max.y) * 0.5f), ImGui::GetFontSize() * 0.3f, 1.0f, ImGui::Style::color_text);
            draw_list->AddText(ImVec2(pill_min.x + pad_x + chevron + ImGui::EditorUi::scaled(4.0f), pill_min.y + (pill_size.y - text_size.y) * 0.5f), ImGui::EditorUi::color(ImGui::Style::color_text), label);
        }
    }
    ImGui::EndChild();

    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);

    // written after the rows are done, a new line can evict the oldest one that the visible list still points to
    if (!pending_result.empty())
    {
        WriteResult(pending_result, false);
    }
}

void Console::ShowInput()
{
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    // the prompt and the field are one frame, so the chevron reads as part of the input rather than a stray character
    ImGui::Dummy(ImVec2(0, 1));
    const float height     = ImGui::GetFrameHeight();
    const ImVec2 frame_min = ImGui::GetCursorScreenPos();
    const float width      = ImGui::GetContentRegionAvail().x;
    const ImVec2 frame_max = ImVec2(frame_min.x + width, frame_min.y + height);
    const float prompt_w   = ImGui::GetFontSize() * 1.4f;
    const bool focused     = ImGui::GetActiveID() == ImGui::GetID("##console_input");

    draw_list->AddRectFilled(frame_min, frame_max, ImGui::GetColorU32(ImGuiCol_FrameBg), 4.0f);
    draw_list->AddRect(frame_min, frame_max, ImGui::EditorUi::color(focused ? ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, 0.7f) : ImGui::Style::color_border), 4.0f);
    ImGui::EditorUi::draw_chevron(draw_list, ImVec2(frame_min.x + prompt_w * 0.55f, (frame_min.y + frame_max.y) * 0.5f), ImGui::GetFontSize() * 0.3f, 0.0f, focused ? ImGui::Style::color_accent_hi : ImGui::Style::color_accent_1);

    ImGui::SetCursorScreenPos(ImVec2(frame_min.x + prompt_w, frame_min.y));
    ImGui::SetNextItemWidth(width - prompt_w);

    const ImGuiInputTextFlags input_flags =
        ImGuiInputTextFlags_EnterReturnsTrue |
        ImGuiInputTextFlags_CallbackHistory  |
        ImGuiInputTextFlags_CallbackAlways;

    const ImVec2 input_pos  = frame_min;
    const float input_width = width;

    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const bool submitted = ImGui::InputTextWithHint("##console_input", "Type a console variable to read it, add a value to set it, e.g. r.bloom 0.5", m_input_buffer, IM_ARRAYSIZE(m_input_buffer), input_flags, [](ImGuiInputTextCallbackData* data) -> int
    {
        Console* console = static_cast<Console*>(data->UserData);
        return console->InputCallback(data);
    }, this);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    bool reclaim_focus = m_reclaim_focus;
    m_reclaim_focus    = false;
    if (submitted)
    {
        ExecuteCommand(m_input_buffer);
        m_input_buffer[0]   = '\0';
        m_show_autocomplete = false;
        reclaim_focus       = true;
    }

    const bool input_active = ImGui::IsItemActive();
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

    // autocomplete floats above the input, the matched part of each name is lit so it is clear why it is listed
    if (m_show_autocomplete && !m_filtered_cvars.empty())
    {
        const ImGuiStyle& style      = ImGui::GetStyle();
        const float popup_max_height = 260.0f * spartan::Window::GetDpiScale();
        const float row_height       = ImGui::GetTextLineHeightWithSpacing() + 4.0f;
        const float footer_height    = ImGui::GetTextLineHeightWithSpacing() + style.ItemSpacing.y;
        const float padding          = style.WindowPadding.y * 2.0f;
        const float content_height   = min(popup_max_height, row_height * static_cast<float>(m_filtered_cvars.size()) + footer_height + padding);

        ImGui::SetNextWindowPos(ImVec2(input_pos.x, input_pos.y - content_height - style.ItemSpacing.y));
        ImGui::SetNextWindowSize(ImVec2(input_width, content_height));

        const ImGuiWindowFlags popup_flags =
            ImGuiWindowFlags_NoTitleBar      |
            ImGuiWindowFlags_NoResize        |
            ImGuiWindowFlags_NoMove          |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::Style::color_border_strong);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::Style::color_panel);

        if (ImGui::Begin("##console_autocomplete_popup", nullptr, popup_flags))
        {
            const ImGuiTableFlags table_flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##console_autocomplete", 3, table_flags, ImVec2(0.0f, ImGui::GetContentRegionAvail().y - footer_height)))
            {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.38f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.14f);
                ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch, 0.48f);

                for (size_t i = 0; i < m_filtered_cvars.size(); i++)
                {
                    const ConsoleVariable* cvar = ConsoleRegistry::Get().Find(m_filtered_cvars[i]);
                    if (!cvar)
                    {
                        continue;
                    }

                    ImGui::TableNextRow(ImGuiTableRowFlags_None, row_height);
                    ImGui::PushID(static_cast<int>(i));

                    const bool is_selected = cmp_equal(i, m_autocomplete_selection);
                    if (is_selected)
                    {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::EditorUi::color(ImGui::EditorUi::alpha(ImGui::Style::color_accent_1, 0.2f)));
                    }

                    ImGui::TableSetColumnIndex(0);
                    const ImVec2 name_pos = ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##pick", is_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, row_height - style.ItemSpacing.y)))
                    {
                        m_autocomplete_selection = static_cast<int>(i);
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            ApplyAutocomplete();
                            m_reclaim_focus = true;
                        }
                    }

                    if (is_selected && (ImGui::IsKeyPressed(ImGuiKey_DownArrow) || ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
                    {
                        ImGui::SetScrollHereY(0.5f);
                    }

                    // name with the typed part lit
                    {
                        const string name       = string(cvar->m_name);
                        const size_t match      = m_autocomplete_query.empty() ? string::npos : to_lower(name).find(m_autocomplete_query);
                        const float y           = name_pos.y + (row_height - style.ItemSpacing.y - ImGui::GetTextLineHeight()) * 0.5f;
                        ImDrawList* popup_list  = ImGui::GetWindowDrawList();
                        const ImU32 base        = ImGui::EditorUi::color(is_selected ? ImGui::Style::color_text : ImGui::EditorUi::alpha(ImGui::Style::color_text, 0.85f));
                        const ImU32 lit         = ImGui::EditorUi::color(ImGui::Style::color_accent_hi);
                        float x                 = name_pos.x;
                        auto part = [&](const size_t begin, const size_t end, const ImU32 color)
                        {
                            if (end <= begin)
                            {
                                return;
                            }
                            popup_list->AddText(ImVec2(x, y), color, name.c_str() + begin, name.c_str() + end);
                            x += ImGui::CalcTextSize(name.c_str() + begin, name.c_str() + end).x;
                        };
                        if (match == string::npos)
                        {
                            part(0, name.size(), base);
                        }
                        else
                        {
                            part(0, match, base);
                            part(match, match + m_autocomplete_query.size(), lit);
                            part(match + m_autocomplete_query.size(), name.size(), base);
                        }
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (row_height - style.ItemSpacing.y - ImGui::GetTextLineHeight()) * 0.5f);
                    const string value_str = readable_value(ConsoleRegistry::Get().GetValueAsString(cvar->m_name).value_or(""));
                    ImGui::TextColored(ImGui::Style::color_accent_1, "%s", value_str.c_str());

                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (row_height - style.ItemSpacing.y - ImGui::GetTextLineHeight()) * 0.5f);
                    // one line per variable keeps the list scannable, the full description is in the tooltip
                    const string hint        = string(cvar->m_hint);
                    const ImVec2 hint_min    = ImGui::GetCursorScreenPos();
                    const ImVec2 hint_max    = ImVec2(hint_min.x + ImGui::GetContentRegionAvail().x, hint_min.y + ImGui::GetTextLineHeight());
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::Style::color_text_muted);
                    ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), hint_min, hint_max, hint_max.x, hint.c_str(), nullptr, nullptr);
                    ImGui::PopStyleColor();
                    if (!hint.empty() && ImGui::IsMouseHoveringRect(hint_min, hint_max) && ImGui::CalcTextSize(hint.c_str()).x > hint_max.x - hint_min.x)
                    {
                        ImGui::SetTooltip("%s", hint.c_str());
                    }

                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            // the keys are not discoverable any other way
            char footer[96];
            snprintf(footer, sizeof(footer), "%u matches   \xC2\xB7   Tab completes   \xC2\xB7   Up/Down choose   \xC2\xB7   Esc closes", static_cast<uint32_t>(m_filtered_cvars.size()));
            ImGui::TextColored(ImGui::EditorUi::alpha(ImGui::Style::color_text_muted, 0.8f), "%s", footer);
        }
        ImGui::End();

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }
}

void Console::AddLogPackage(const LogPackage& package)
{
    std::scoped_lock lock(m_mutex);

    LogPackage log = package;
    if (!log.is_command)
    {
        tidy(log);
    }

    // collapse identical messages, ignoring the timestamp, a message repeating every second is still one message
    LogPackage* last = m_logs.empty() ? nullptr : &m_logs.back();
    const bool same  = last && !log.is_command && !last->is_command && last->error_level == log.error_level &&
                       last->time_end == log.time_end && last->text.compare(last->time_end, string::npos, log.text, log.time_end, string::npos) == 0;
    if (same)
    {
        last->repeat_count++;
        last->text.replace(0, log.time_end, log.text, 0, log.time_end);
    }
    else
    {
        m_logs.push_back(move(log));
        if (static_cast<uint32_t>(m_logs.size()) > m_log_max_count)
        {
            m_logs.pop_front();
            if (m_selection.HasSelection())
            {
                m_selection.Clear();
            }
        }
    }

    if (package.is_command)
    {
        return;
    }

    m_log_type_count[package.error_level]++;

    if (m_log_type_visibility[package.error_level])
    {
        if (m_user_scrolled_up)
        {
            m_unseen_count++;
        }
        else
        {
            m_scroll_to_bottom = true;
        }
    }
}

void Console::Clear()
{
    std::scoped_lock lock(m_mutex);

    m_logs.clear();
    m_logs.shrink_to_fit();

    m_visible_logs.clear();
    m_visible_logs.shrink_to_fit();

    m_log_type_count[0] = 0;
    m_log_type_count[1] = 0;
    m_log_type_count[2] = 0;
    m_unseen_count      = 0;
    m_selection.Clear();
    m_scroll_to_bottom = false;
    m_user_scrolled_up = false;

    spartan::Log::Clear();
}

void Console::UpdateAutocomplete()
{
    // if the user is arrow-navigating, the input text matches the selected
    // cvar name -- skip re-filtering so the full suggestion list stays open
    if (m_autocomplete_navigating && m_show_autocomplete &&
        m_autocomplete_selection >= 0 && m_autocomplete_selection < static_cast<int>(m_filtered_cvars.size()))
    {
        string input = m_input_buffer;
        while (!input.empty() && input.back() == ' ')
        {
            input.pop_back();
        }

        if (input == m_filtered_cvars[m_autocomplete_selection])
        {
            return;
        }

        // text no longer matches the selected item -- the user typed something
        m_autocomplete_navigating = false;
    }

    string input = m_input_buffer;

    // strip trailing space (appended by ApplyAutocomplete / Tab)
    while (!input.empty() && input.back() == ' ')
    {
        input.pop_back();
    }

    // once a value is being typed the list has done its job
    if (input.empty() || input.find(' ') != string::npos)
    {
        m_filtered_cvars.clear();
        m_autocomplete_query.clear();
        m_autocomplete_selection = 0;
        m_show_autocomplete      = false;
        return;
    }

    m_autocomplete_query     = to_lower(input);
    m_autocomplete_selection = 0;

    // names that start with what was typed come first, then the ones that merely contain it, each group alphabetical
    vector<pair<bool, string_view>> matches;
    for (const auto& cvar : ConsoleRegistry::Get().GetAll() | views::values)
    {
        const size_t position = to_lower(cvar.m_name).find(m_autocomplete_query);
        if (position != string::npos)
        {
            matches.push_back({ position != 0, cvar.m_name });
        }
    }
    ranges::sort(matches);

    m_filtered_cvars.clear();
    for (const auto& match : matches)
    {
        m_filtered_cvars.push_back(match.second);
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
    if (!selected)
    {
        return;
    }
    const string completion = string(selected->m_name) + " ";
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

                if (const ConsoleVariable* cvar = ConsoleRegistry::Get().Find(m_filtered_cvars[m_autocomplete_selection]))
                {
                    const string name = string(cvar->m_name) + " ";
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
                        if (cmp_greater_equal(m_history_position, m_command_history.size()))
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

void Console::WriteResult(const string& text, const bool is_warning)
{
    // through the log, not straight into the widget, so log.txt and the mcp console_read see the answer too
    const string line = "console: " + text;
    spartan::Log::WriteBuffer(line.c_str(), is_warning ? LogType::Warning : LogType::Info);
}

string Console::ClosestVariable(const string& name) const
{
    const string query = to_lower(name);

    // something that contains what was typed, else the name sharing the longest start with it
    string best;
    size_t best_prefix = 0;
    for (const auto& cvar : ConsoleRegistry::Get().GetAll() | views::values)
    {
        const string candidate = to_lower(cvar.m_name);
        if (!query.empty() && candidate.find(query) != string::npos)
        {
            if (best.empty() || best_prefix != SIZE_MAX || candidate < to_lower(best))
            {
                best        = string(cvar.m_name);
                best_prefix = SIZE_MAX;
            }
            continue;
        }
        if (best_prefix == SIZE_MAX)
        {
            continue;
        }
        size_t prefix = 0;
        while (prefix < candidate.size() && prefix < query.size() && candidate[prefix] == query[prefix])
        {
            prefix++;
        }
        if (prefix > best_prefix)
        {
            best_prefix = prefix;
            best        = string(cvar.m_name);
        }
    }
    return (best_prefix == SIZE_MAX || best_prefix >= 3) ? best : "";
}

void Console::ExecuteCommand(const char* command)
{
    const string line = trim(command);
    if (line.empty())
    {
        return;
    }

    if (m_command_history.empty() || m_command_history.back() != line)
    {
        m_command_history.push_back(line);
    }
    m_history_position = -1;

    // the command is echoed, so the answer below it has a question
    LogPackage echo;
    echo.text       = "> " + line;
    echo.is_command = true;
    AddLogPackage(echo);

    const size_t space_pos = line.find(' ');
    const string name      = line.substr(0, space_pos);
    const string value     = space_pos == string::npos ? "" : trim(line.substr(space_pos + 1));

    ConsoleRegistry& registry = ConsoleRegistry::Get();
    const ConsoleVariable* cvar = registry.Find(name);

    // an unknown name used to do nothing at all, now it says so and offers the closest real one
    if (!cvar)
    {
        const string suggestion = ClosestVariable(name);
        if (suggestion.empty())
        {
            WriteResult("there is no console variable called '" + name + "'", true);
        }
        else
        {
            WriteResult("there is no console variable called '" + name + "', did you mean " + suggestion + "?", true);
        }
    }
    else if (value.empty())
    {
        // reading a value is not a warning, it was logged as one before
        const string current = readable_value(registry.GetValueAsString(name).value_or("?"));
        const string hint    = string(cvar->m_hint);
        WriteResult(name + " = " + current + (hint.empty() ? "" : "   (" + hint + ")"), false);
    }
    else
    {
        const string previous = readable_value(registry.GetValueAsString(name).value_or("?"));
        if (registry.SetValueFromString(name, value))
        {
            const string current = readable_value(registry.GetValueAsString(name).value_or(value));
            WriteResult(name + " = " + current + (current == previous ? "   (unchanged)" : "   (was " + previous + ")"), false);
        }
        else
        {
            WriteResult("'" + value + "' is not a valid value for " + name + ", it stays " + previous, true);
        }
    }

    m_scroll_to_bottom = true;
    m_user_scrolled_up = false;
}

void Console::ApplyMcpRequest()
{
    optional<ConsoleRequest> request;
    {
        lock_guard<mutex> lock(mcp_mutex);
        request.swap(mcp_request);
    }
    if (!request)
    {
        return;
    }

    if (request->clear)
    {
        Clear();
    }
    if (request->search)
    {
        strncpy_s(m_log_filter.InputBuf, sizeof(m_log_filter.InputBuf), request->search->c_str(), _TRUNCATE);
        m_log_filter.Build();
        m_selection.Clear();
    }
    for (uint32_t i = 0; i < 3; i++)
    {
        if (request->show[i])
        {
            m_log_type_visibility[i] = *request->show[i];
        }
    }
    if (request->execute)
    {
        ExecuteCommand(request->execute->c_str());
    }
}

void Console::PublishMcpState()
{
    ConsoleState state;
    state.total   = static_cast<uint32_t>(m_logs.size());
    state.visible = static_cast<uint32_t>(m_visible_logs.size());
    for (uint32_t i = 0; i < 3; i++)
    {
        state.counts[i] = m_log_type_count[i];
        state.shown[i]  = m_log_type_visibility[i];
    }
    state.search = m_log_filter.InputBuf;

    // read from the log itself, lines added this frame may have evicted entries the visible list points to
    for (auto it = m_logs.rbegin(); it != m_logs.rend() && state.tail.size() < 12; ++it)
    {
        const bool type_visible = it->is_command || m_log_type_visibility[it->error_level];
        if (type_visible && m_log_filter.PassFilter(it->text.c_str()))
        {
            state.tail.insert(state.tail.begin(), it->text);
        }
    }

    lock_guard<mutex> lock(mcp_mutex);
    mcp_state = move(state);
}

void Console::Request(const ConsoleRequest& request)
{
    lock_guard<mutex> lock(mcp_mutex);
    mcp_request = request;
}

ConsoleState Console::GetState()
{
    lock_guard<mutex> lock(mcp_mutex);
    return mcp_state;
}
