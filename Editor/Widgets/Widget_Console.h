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

#pragma once

//= INCLUDES ===============
#include "Widget.h"
#include <memory>
#include <functional>
#include <deque>
#include <atomic>
#include "Logging/ILogger.h"
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

    void Log(const std::string& text, const unsigned int error_level) override
    {
        LogPackage package;
        package.text = text;
        package.error_level = error_level;
        m_log_func(package);
    }

private:
    log_func m_log_func;
};

class Widget_Console : public Widget
{
public:
    Widget_Console(Editor* editor);
    void Tick() override;
    void AddLogPackage(const LogPackage& package);
    void Clear();

private:
    bool m_scroll_to_bottom         = false;
    uint32_t m_log_max_count        = 1000;
    float m_log_type_max_width[3]   = { 0, 0, 0 };
    bool m_log_type_visibility[3]   = { true, true, true };
    uint32_t m_log_type_count[3]    = { 0, 0, 0 };
    const std::vector<Spartan::Math::Vector4> m_log_type_color =
    {
        Spartan::Math::Vector4(0.76f, 0.77f, 0.8f, 1.0f),    // Info
        Spartan::Math::Vector4(0.7f, 0.75f, 0.0f, 1.0f),    // Warning
        Spartan::Math::Vector4(0.7f, 0.3f, 0.3f, 1.0f)        // Error
    };
    std::atomic<bool> m_is_reading = false;
    std::shared_ptr<EngineLogger> m_logger;
    std::deque<LogPackage> m_logs;
    ImGuiTextFilter m_log_filter;
    LogPackage m_log_selected;
};
