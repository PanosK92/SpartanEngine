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

//= INCLUDES =======
#include <string>
#include <memory>
#include "ILogger.h"
//==================

namespace spartan
{
    // macros for easy logging across the engine
    #define SP_LOG_BUFFER_SIZE 2048
    #define SP_LOG_BUFFER_COUNT 16
    #define SP_LOG_INFO(text, ...)    { char buffer[SP_LOG_BUFFER_SIZE]; spartan::Log::FormatBuffer(buffer, __FUNCTION__, text, ##__VA_ARGS__); spartan::Log::WriteBuffer(buffer, spartan::LogType::Info); }
    #define SP_LOG_WARNING(text, ...) { char buffer[SP_LOG_BUFFER_SIZE]; spartan::Log::FormatBuffer(buffer, __FUNCTION__, text, ##__VA_ARGS__); spartan::Log::WriteBuffer(buffer, spartan::LogType::Warning); }
    #define SP_LOG_ERROR(text, ...)   { char buffer[SP_LOG_BUFFER_SIZE]; spartan::Log::FormatBuffer(buffer, __FUNCTION__, text, ##__VA_ARGS__); spartan::Log::WriteBuffer(buffer, spartan::LogType::Error); }

    // Forward declarations
    class Entity;
    namespace math
    {
        class Quaternion;
        class Matrix;
        class Vector2;
        class Vector3;
        class Vector4;
    }

    enum class LogType
    {
        Info,
        Warning,
        Error
    };

    struct LogCmd
    {
        LogCmd(const std::string& text, const LogType type)
        {
            this->text = text;
            this->type = type;
        }

        std::string text;
        LogType type;
    };

    class Log
    {
        friend class ILogger;
    public:
        Log() = default;

        // misc
        static void Initialize();
        static void SetLogger(ILogger* logger);
        static void SetLogToFile(const bool log_to_file);
        static void Clear();

        // buffer-based logging
        static void WriteBuffer(const char* text, LogType type);
        static void FormatBuffer(char* buffer, const char* function, const char* text, ...);

    private:
        static std::array<char[SP_LOG_BUFFER_SIZE], SP_LOG_BUFFER_COUNT> m_buffers;
        static std::array<std::mutex, SP_LOG_BUFFER_COUNT> m_buffer_mutexes;
        static size_t m_current_buffer;
    };
}
