/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======
#include <string>
#include <memory>
#include <vector>
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
        static std::vector<LogCmd> GetRecentEntries(uint32_t count);

        // buffer-based logging
        static void WriteBuffer(const char* text, LogType type);
        static void FormatBuffer(char* buffer, const char* function, const char* text, ...);

    private:
        static std::array<char[SP_LOG_BUFFER_SIZE], SP_LOG_BUFFER_COUNT> m_buffers;
        static std::array<std::mutex, SP_LOG_BUFFER_COUNT> m_buffer_mutexes;
        static size_t m_current_buffer;
    };
}
