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
#include "Runtime/Core/Spartan.h"
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
    weak_ptr<ILogger> Log::m_logger;
    ofstream Log::m_fout;
    mutex Log::m_mutex_log;
    vector<LogCmd> Log::m_log_buffer;
    vector<string> Log::m_error_logs;
    string Log::m_log_file_name = "log.txt";
    bool Log::m_log_to_file     = true; // start logging to file (this will eventually become false, e.g. Renderer initialization was successful, so logging can happen on screen)
    bool Log::m_first_log       = true;
#ifdef DEBUG
    bool Log::m_only_unique_logs = true;
#else
    bool Log::m_only_unique_logs = false;
#endif

    // All functions resolve to this one
    void Log::Write(const char* text, const LogType type)
    {
        SP_ASSERT(text != nullptr);

        lock_guard<mutex> guard(m_mutex_log);

        // Only log unique text. Enabled only in debug configuration.
        if (m_only_unique_logs && type == LogType::Error)
        {
            if (find(m_error_logs.begin(), m_error_logs.end(), text) == m_error_logs.end())
            {
                m_error_logs.emplace_back(text);
            }
            else
            {
                return;
            }
        }

        bool logger_enabled = !m_logger.expired();

        // Always log in-engine
        if (logger_enabled)
        {
            FlushBuffer();
            LogString(text, type);
        }

        // Log to file if requested or if an in-engine logger is not available.
        if (m_log_to_file || !logger_enabled)
        {
            m_log_buffer.emplace_back(text, type);
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
        entity.expired() ? Write("Null", type) : Write(entity.lock()->GetObjectName(), type);
    }

    void Log::Write(const std::shared_ptr<Entity>& entity, const LogType type)
    {
        entity ? Write(entity->GetObjectName(), type) : Write("Null", type);
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
        if (m_logger.expired() || m_log_buffer.empty())
            return;

         // Log everything from memory to the logger implementation
        for (const auto& log : m_log_buffer)
        {
            LogString(log.text.c_str(), log.type);
        }
        m_log_buffer.clear();
    }

    void Log::LogString(const char* text, const LogType type)
    {
        SP_ASSERT(text != nullptr);

        m_logger.lock()->Log(string(text), static_cast<uint32_t>(type));
    }

    void Log::LogToFile(const char* text, const LogType type)
    {
        SP_ASSERT(text != nullptr);

        const string prefix   = (type == LogType::Info) ? "Info:" : (type == LogType::Warning) ? "Warning:" : "Error:";
        const auto final_text = prefix + " " + text;

        // Delete the previous log file (if it exists)
        if (m_first_log)
        {
            FileSystem::Delete(m_log_file_name);
            m_first_log = false;
        }

        // Open/Create a log file to write the error message to
        m_fout.open(m_log_file_name, ofstream::out | ofstream::app);

        if (m_fout.is_open())
        {
            // Write out the error message
            m_fout << final_text << endl;

            // Close the file
            m_fout.close();
        }
    }
}
