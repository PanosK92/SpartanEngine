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

//= INCLUDES =================
#include "pch.h"
#include "../Core/Debugging.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        vector<LogCmd> logs;
        string log_file_name = "log.txt";
        ILogger* logger      = nullptr;
        bool log_to_file     = true;

        void write_to_file(string text, const LogType type)
        {
            const string prefix = (type == LogType::Info) ? "Info:" : (type == LogType::Warning) ? "Warning:" : "Error:";
            text = prefix + " " + text;

            // delete the previous log file (if it exists)
            static bool is_first_log = true;
            if (is_first_log)
            {
                FileSystem::Delete(log_file_name);
                is_first_log = false;
            }

            // open/create a log file to write the log into
            static ofstream fout;
            fout.open(log_file_name, ofstream::out | ofstream::app);

            if (fout.is_open())
            {
                // write out the error message
                fout << text << endl;

                // close the file
                fout.close();
            }
        }
    }

    std::array<char[SP_LOG_BUFFER_SIZE], SP_LOG_BUFFER_COUNT> Log::m_buffers;
    std::array<std::mutex, SP_LOG_BUFFER_COUNT> Log::m_buffer_mutexes;
    size_t Log::m_current_buffer = 0;

    void Log::Initialize()
    {
        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnFirstFrameCompleted, SP_EVENT_HANDLER_EXPRESSION_STATIC( SetLogToFile(false); ));
        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnShutdown,            SP_EVENT_HANDLER_EXPRESSION_STATIC( SetLogToFile(true);  ));
    }

    void Log::SetLogger(ILogger* logger_in)
    {
        logger = logger_in;

        // flush the log buffer, if needed
        if (logger && !logs.empty())
        {
            if (!logs.empty())
            {
                for (const LogCmd& log : logs)
                {
                    logger->Log(log.text, static_cast<uint32_t>(log.type));
                }
                logs.clear();
            }
        }
    }

    void Log::SetLogToFile(const bool log)
    {
        log_to_file = log;
    }

    void Log::Clear()
    {
        // lock mutex to ensure thread safety
        static std::mutex log_mutex;
        std::lock_guard<std::mutex> guard(log_mutex);

        // clear the in-memory logs
        logs.clear();

        // clear the file if logging to file is enabled
        if (log_to_file || Debugging::IsLoggingToFileEnabled())
        {
            // open the file in truncate mode to clear its contents
            std::ofstream file("log.txt", std::ios::out | std::ios::trunc);
            if (file.is_open())
            {
                file.close();
            }
        }
    }

    void Log::WriteBuffer(const char* text, LogType type)
    {
        SP_ASSERT_MSG(text != nullptr, "Text is null");
        
        // get a buffer
        size_t buffer_index;
        {
            static std::mutex buffer_select_mutex;
            std::lock_guard<std::mutex> guard(buffer_select_mutex);
            buffer_index = m_current_buffer;
            m_current_buffer = (m_current_buffer + 1) % SP_LOG_BUFFER_COUNT;
        }
        
        // lock the selected buffer
        std::lock_guard<std::mutex> guard(m_buffer_mutexes[buffer_index]);
        char* buffer = m_buffers[buffer_index];
        
        // add timestamp
        auto t = time(nullptr);
        tm tm_struct{};
        localtime_s(&tm_struct, &t);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "[%H:%M:%S]: ", &tm_struct);
        
        // combine timestamp and text
        snprintf(buffer, SP_LOG_BUFFER_SIZE, "%s%s", timestamp, text);
        
        // log to file if requested or if an in-engine logger is not available
        if (log_to_file || !logger || Debugging::IsLoggingToFileEnabled())
        {
            logs.emplace_back(string(buffer), type);
            write_to_file(buffer, type);
        }
        
        if (logger)
        {
            logger->Log(buffer, static_cast<uint32_t>(type));
        }
    }

    void Log::FormatBuffer(char* buffer, const char* function, const char* text, ...)
    {
        va_list args;
        va_start(args, text);
        char temp[SP_LOG_BUFFER_SIZE];
        vsnprintf(temp, SP_LOG_BUFFER_SIZE, text, args);
        va_end(args);
        snprintf(buffer, SP_LOG_BUFFER_SIZE, "%s: %s", function, temp);
    }
}
