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

#pragma once

//= INCLUDES ===============
#include "Widget.h"
#include <memory>
#include <functional>
#include <deque>
#include <atomic>
#include <mutex>
#include "Logging/ILogger.h"
#include "../ImGui/Implementation/ImGui_Style.h"
//==========================

struct LogPackage
{
    std::string text;
    unsigned int error_level = 0;
};

// Implementation of Spartan::ILogger so the engine can log into the editor
class EngineLogger : public Spartan::ILogger
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
    Console(Editor* editor);
    ~Console();

    void OnTickVisible() override;
    void AddLogPackage(const LogPackage& package);
    void Clear();

private:
    bool m_scroll_to_bottom       = false;
    uint32_t m_log_max_count      = 1000;
    bool m_log_type_visibility[3] = { true, true, true };
    uint32_t m_log_type_count[3]  = { 0, 0, 0 };

    const std::vector<ImVec4> m_log_type_color =
    {
        ImGui::Style::color_info,
        ImGui::Style::color_warning,
        ImGui::Style::color_error,
    };

    std::shared_ptr<EngineLogger> m_logger;
    std::deque<LogPackage> m_logs;
    std::mutex m_mutex;
    ImGuiTextFilter m_log_filter;
};
