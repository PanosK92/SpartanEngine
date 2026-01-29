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

#pragma once

//= INCLUDES ====================
#include "Widget.h"
#include <memory>
#include <functional>
#include <deque>
#include <mutex>
#include "Logging/ILogger.h"
#include "../ImGui/ImGui_Style.h"
//===============================

namespace spartan
{
    struct ConsoleVariable;
}

struct LogPackage
{
    std::string text;
    unsigned int error_level = 0;
};

// Implementation of spartan::ILogger so the engine can log into the editor
class EngineLogger : public spartan::ILogger
{
public:
    typedef std::function<void(LogPackage)> log_func;
    void SetCallback(log_func&& func)
    {
        m_log_func = std::forward<log_func>(func);
    }

    void Log(const std::string& text, const uint32_t error_level) override
    {
        LogPackage package  = {};
        package.text        = text;
        package.error_level = error_level;

        m_log_func(package);
    }

private:
    log_func m_log_func;
};

class Console : public Widget
{
public:

    static constexpr size_t INPUT_BUFFER_SIZE = 512;

    Console(Editor* editor);
    ~Console() override;

    void OnTickVisible() override;
    void AddLogPackage(const LogPackage& package);
    void Clear();

private:


    void ExecuteCommand(const char* command);
    int InputCallback(ImGuiInputTextCallbackData* data);

    void UpdateAutocomplete();
    void ApplyAutocomplete();

private:

    std::shared_ptr<EngineLogger>      m_logger;
    std::deque<LogPackage>             m_logs;
    std::vector<std::string>           m_command_history;
    std::vector<std::string_view>      m_filtered_cvars;
    std::recursive_mutex               m_mutex;
    ImGuiTextFilter                    m_log_filter;

    // cached for rendering - avoids per-frame heap allocation
    std::vector<std::pair<uint32_t, const LogPackage*>> m_visible_logs;

    // text selection state
    struct TextSelection
    {
        int start_line   = -1;
        int start_char   = -1;
        int end_line     = -1;
        int end_char     = -1;
        bool is_dragging = false;

        bool HasSelection() const { return start_line >= 0 && end_line >= 0; }
        void Clear() { start_line = start_char = end_line = end_char = -1; is_dragging = false; }
    };
    TextSelection m_selection;

    char                               m_input_buffer[INPUT_BUFFER_SIZE] = {};

    const std::vector<ImVec4>          m_log_type_color =
    {
        ImGui::Style::color_info,
        ImGui::Style::color_warning,
        ImGui::Style::color_error,
    };

    uint32_t                           m_log_max_count      = 1000;
    uint32_t                           m_log_type_count[3]  = { 0, 0, 0 };
    int                                m_history_position   = -1;
    int                                m_autocomplete_selection = 0;

    bool                               m_show_autocomplete  = false;
    bool                               m_scroll_to_bottom   = false;
    bool                               m_log_type_visibility[3] = { true, true, true };
};

