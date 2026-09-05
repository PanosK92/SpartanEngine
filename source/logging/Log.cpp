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

//= INCLUDES =================
#include "pch.h"
#include <algorithm>
#include <deque>
#include <fstream>
#include <filesystem>
#include <system_error>
#include "../core/Debugging.h"
//============================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        vector<LogCmd> logs;
        deque<LogCmd> history;
        string log_file_name = "log.txt";
        ILogger* logger      = nullptr;
        bool log_to_file     = true;
        mutex log_output_mutex;
        constexpr uint32_t history_max_count = 2000;
        ofstream log_file;

        void write_to_file(const char* text, const LogType type)
        {
            // the log being replaced here is the only record of why the last run died, and the restart
            // after a gpu crash is what lands on this path, so it is rotated rather than deleted
            static bool is_first_log = true;
            if (is_first_log)
            {
                error_code ignored;
                filesystem::rename(log_file_name, "log_previous.txt", ignored);
                FileSystem::Delete(log_file_name);
                is_first_log = false;
            }

            // the file stays open for the session, opening and closing it per line was a syscall
            // storm during loading, the flush keeps every line on disk in case the run dies
            if (!log_file.is_open())
            {
                log_file.open(log_file_name, ofstream::out | ofstream::app);
                if (!log_file.is_open())
                {
                    return;
                }
            }

            const char* prefix = (type == LogType::Info) ? "Info: " : (type == LogType::Warning) ? "Warning: " : "Error: ";
            log_file << prefix << text << '\n';
            log_file.flush();
        }
    }

    std::array<char[SP_LOG_BUFFER_SIZE], SP_LOG_BUFFER_COUNT> Log::m_buffers;
    std::array<std::mutex, SP_LOG_BUFFER_COUNT> Log::m_buffer_mutexes;
    size_t Log::m_current_buffer = 0;

    void Log::Initialize()
    {
        // keep writing log.txt for the whole session, console is additive not a replacement
    }

    void Log::SetLogger(ILogger* logger_in)
    {
        lock_guard<mutex> guard(log_output_mutex);

        logger = logger_in;

        // flush the log buffer, if needed
        if (logger && !logs.empty())
        {
            for (const LogCmd& log : logs)
            {
                logger->Log(log.text, static_cast<uint32_t>(log.type));
            }
            logs.clear();
        }
    }

    void Log::SetLogToFile(const bool log)
    {
        log_to_file = log;
    }

    void Log::Clear()
    {
        lock_guard<mutex> guard(log_output_mutex);

        logs.clear();
        history.clear();

        if (log_to_file || Debugging::IsLoggingToFileEnabled())
        {
            // the log that gets truncated here is the only record of why the last run died, so it is kept as
            // log_previous.txt. a crash is only diagnosable if its log survives the restart that follows it
            // the open handle has to go first, windows refuses to rename a file that is open
            if (log_file.is_open())
            {
                log_file.close();
            }

            error_code ignored;
            filesystem::rename(log_file_name, "log_previous.txt", ignored);

            ofstream file(log_file_name, ios::out | ios::trunc);
            if (file.is_open())
            {
                file.close();
            }
        }
    }

    vector<LogCmd> Log::GetRecentEntries(uint32_t count)
    {
        lock_guard<mutex> guard(log_output_mutex);

        count = std::min<uint32_t>(count, static_cast<uint32_t>(history.size()));
        const size_t start = history.size() - count;
        return vector<LogCmd>(history.begin() + start, history.end());
    }

    void Log::WriteBuffer(const char* text, const LogType type)
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
    
        // add timestamp directly to buffer
        auto t = time(nullptr);
        tm tm_struct{};
        localtime_s(&tm_struct, &t);
        size_t timestamp_len = strftime(buffer, SP_LOG_BUFFER_SIZE, "[%H:%M:%S]: ", &tm_struct);
        size_t available_len = SP_LOG_BUFFER_SIZE - timestamp_len - 1; // -1 for null terminator
    
        // append text to buffer after timestamp
        strncpy_s(buffer + timestamp_len, available_len + 1, text, _TRUNCATE);
    
        // serialize access to the shared log vector, file, and logger
        {
            lock_guard<mutex> output_guard(log_output_mutex);

            history.emplace_back(buffer, type);
            while (history.size() > history_max_count)
            {
                history.pop_front();
            }

            // logs only buffers lines for a logger that is not attached yet, SetLogger replays and clears it,
            // pushing into it while a logger was attached grew it for the whole session
            if (!logger)
            {
                logs.emplace_back(buffer, type);
            }

            if (log_to_file || !logger || Debugging::IsLoggingToFileEnabled())
            {
                write_to_file(buffer, type);
            }

            if (logger)
            {
                logger->Log(buffer, static_cast<uint32_t>(type));
            }
        }
    }

    void Log::FormatBuffer(char* buffer, const char* function, const char* text, ...)
    {
        va_list args;
        va_start(args, text);

        // calculate lengths for safe formatting
        size_t function_len  = strlen(function);
        size_t prefix_len    = function_len + 2; // ": " after function name
        size_t available_len = SP_LOG_BUFFER_SIZE - prefix_len - 1; // -1 for null terminator

        // write function name and ": " directly to buffer
        snprintf(buffer, SP_LOG_BUFFER_SIZE, "%s: ", function);

        // append formatted text directly to buffer after function name
        vsnprintf(buffer + prefix_len, available_len, text, args);

        va_end(args);
    }
}
