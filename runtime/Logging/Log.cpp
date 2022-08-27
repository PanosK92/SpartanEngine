/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ===============
#include "pch.h"
#include "ILogger.h"
#include <cstdarg>
#include "../World/Entity.h"
//==========================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static string log_file_name = "log.txt";
    static std::vector<LogCmd> logs;
#ifdef DEBUG
    static bool unique_logs  = true;
#else
    static bool unique_logs  = false;
#endif

    ILogger* Log::m_logger  = nullptr;
    bool Log::m_log_to_file = true;

    // All functions resolve to this one
    void Log::Write(const char* text, const LogType type)
    {
        SP_ASSERT(text != nullptr);

        // Lock mutex
        static std::mutex log_mutex;
        lock_guard<mutex> guard(log_mutex);

        // Only log unique text. Enabled only in debug configuration.
        static std::vector<std::string> logs_error_strings;
        if (unique_logs && type == LogType::Error)
        {
            if (find(logs_error_strings.begin(), logs_error_strings.end(), text) == logs_error_strings.end())
            {
                logs_error_strings.emplace_back(text);
            }
            else
            {
                return;
            }
        }

        // Always log in-engine
        if (m_logger)
        {
            FlushBuffer();
            LogString(text, type);
        }

        // Log to file if requested or if an in-engine logger is not available.
        if (m_log_to_file || !m_logger)
        {
            logs.emplace_back(text, type);
            LogToFile(text, type);
        }
    }

    void Log::WriteFInfo(const char* text, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, text);
        auto w = vsnprintf(buffer, sizeof(buffer), text, args);
        va_end(args);

        Write(buffer, LogType::Info);
    }

    void Log::WriteFWarning(const char* text, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, text);
        auto w = vsnprintf(buffer, sizeof(buffer), text, args);
        va_end(args);

        Write(buffer, LogType::Warning);
    }

    void Log::WriteFError(const char* text, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, text);
        auto w = vsnprintf(buffer, sizeof(buffer), text, args);
        va_end(args);

        Write(buffer, LogType::Error);
    }

    void Log::Write(const string& text, const LogType type)
    {
        Write(text.c_str(), type);
    }

    void Log::WriteFInfo(const string text, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, text);
        auto w = vsnprintf(buffer, sizeof(buffer), text.c_str(), args);
        va_end(args);

        Write(buffer, LogType::Info);
    }

    void Log::WriteFWarning(const string text, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, text);
        auto w = vsnprintf(buffer, sizeof(buffer), text.c_str(), args);
        va_end(args);

        Write(buffer, LogType::Warning);
    }

    void Log::WriteFError(const string text, ...)
    {
        char buffer[2048];
        va_list args;
        va_start(args, text);
        auto w = vsnprintf(buffer, sizeof(buffer), text.c_str(), args);
        va_end(args);

        Write(buffer, LogType::Error);
    }

    void Log::Write(const weak_ptr<Entity>& entity, const LogType type)
    {
        entity.expired() ? Write("Null", type) : Write(entity.lock()->GetName(), type);
    }

    void Log::Write(const std::shared_ptr<Entity>& entity, const LogType type)
    {
        entity ? Write(entity->GetName(), type) : Write("Null", type);
    }

    void Log::Write(const Vector2& value, const LogType type)
    {
        Write(value.ToString(), type);
    }

    void Log::Write(const Vector3& value, const LogType type)
    {
        Write(value.ToString(), type);
    }

    void Log::Write(const Vector4& value, const LogType type)
    {
        Write(value.ToString(), type);
    }

    void Log::Write(const Quaternion& value, const LogType type)
    {
        Write(value.ToString(), type);
    }

    void Log::Write(const Matrix& value, const LogType type)
    {
        Write(value.ToString(), type);
    }

    void Log::FlushBuffer()
    {
        if (m_logger || logs.empty())
            return;

         // Log everything from memory to the logger implementation
        for (const LogCmd& log : logs)
        {
            LogString(log.text.c_str(), log.type);
        }
        logs.clear();
    }

    void Log::LogString(const char* text, const LogType type)
    {
        SP_ASSERT_MSG(text != nullptr, "Text is null");
        SP_ASSERT_MSG(m_logger != nullptr, "Logger is null");

        m_logger->Log(string(text), static_cast<uint32_t>(type));
    }

    void Log::LogToFile(const char* text, const LogType type)
    {
        SP_ASSERT_MSG(text != nullptr, "Text is null");

        static const string prefix = (type == LogType::Info) ? "Info:" : (type == LogType::Warning) ? "Warning:" : "Error:";
        const string final_text    = prefix + " " + text;

        // Delete the previous log file (if it exists)
        static bool is_first_log = true;
        if (is_first_log)
        {
            FileSystem::Delete(log_file_name);
            is_first_log = false;
        }

        // Open/Create a log file to write the log into
        static std::ofstream fout;
        fout.open(log_file_name, ofstream::out | ofstream::app);

        if (fout.is_open())
        {
            // Write out the error message
            fout << final_text << endl;

            // Close the file
            fout.close();
        }
    }
}
