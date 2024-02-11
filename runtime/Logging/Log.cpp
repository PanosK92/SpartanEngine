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

//= INCLUDES ===============
#include "pch.h"
#include "ILogger.h"
#include "../World/Entity.h"
//==========================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
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

    // all functions resolve to this one
    void Log::Write(const char* text, const LogType type)
    {
        SP_ASSERT_MSG(text != nullptr, "Text is null");

        // lock mutex
        static mutex log_mutex;
        lock_guard<mutex> guard(log_mutex);

        // add time to the text
        auto t  = time(nullptr);
        auto tm = *localtime(&t);
        ostringstream oss;
        oss << put_time(&tm, "[%H:%M:%S]");
        const string final_text = oss.str() + ": " + string(text);

        // log to file if requested or if an in-engine logger is not available.
        if (log_to_file || !logger)
        {
            logs.emplace_back(final_text, type);
            write_to_file(final_text, type);
        }

        if (logger)
        {
            logger->Log(final_text, static_cast<uint32_t>(type));
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

    void Log::Write(const shared_ptr<Entity>& entity, const LogType type)
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
}
