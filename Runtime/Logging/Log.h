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

//= INCLUDES ===========================
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include "../Core/Spartan_Definitions.h"
//======================================

namespace Spartan
{
    #define LOG_INFO(text, ...)        { Spartan::Log::WriteFInfo(std::string(__FUNCTION__)    + ": " + std::string(text), __VA_ARGS__); }
    #define LOG_WARNING(text, ...)    { Spartan::Log::WriteFWarning(std::string(__FUNCTION__) + ": " + std::string(text), __VA_ARGS__); }
    #define LOG_ERROR(text, ...)    { Spartan::Log::WriteFError(std::string(__FUNCTION__)   + ": " + std::string(text), __VA_ARGS__); }

    // Standard errors
    #define LOG_ERROR_GENERIC_FAILURE()        LOG_ERROR("Failed.")
    #define LOG_ERROR_INVALID_PARAMETER()    LOG_ERROR("Invalid parameter.")
    #define LOG_ERROR_INVALID_INTERNALS()    LOG_ERROR("Invalid internals.")

    // Misc
    #define LOG_TO_FILE(value) { Spartan::Log::m_log_to_file = value; }

    // Forward declarations
    class Entity;
    namespace Math
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

    class SPARTAN_CLASS Log
    {
        friend class ILogger;
    public:
        Log() = default;

        // Set a logger to be used (if not set, logging will done in a text file.
        static void SetLogger(const std::weak_ptr<ILogger>& logger) { m_logger = logger; }

        // Alpha
        static void Write(const char* text, const LogType type);
        static void WriteFInfo(const char* text, ...);
        static void WriteFWarning(const char* text, ...);
        static void WriteFError(const char* text, ...);
        static void Write(const std::string& text, const LogType type);
        static void WriteFInfo(const std::string text, ...);
        static void WriteFWarning(const std::string text, ...);
        static void WriteFError(const std::string text, ...);

        // Numeric
        template <class T, class = typename std::enable_if<
            std::is_same<T, int>::value ||
            std::is_same<T, long>::value ||
            std::is_same<T, long long>::value ||
            std::is_same<T, unsigned>::value ||
            std::is_same<T, unsigned long>::value ||
            std::is_same<T, unsigned long long>::value ||
            std::is_same<T, float>::value || 
            std::is_same<T, double>::value ||
            std::is_same<T, long double>::value
        >::type>
        static void Write(T value, LogType type)
        {
            Write(to_string(value), type);
        }

        // Math
        static void Write(const Math::Vector2& value, LogType type);
        static void Write(const Math::Vector3& value, LogType type);
        static void Write(const Math::Vector4& value, LogType type);
        static void Write(const Math::Quaternion& value, LogType type);
        static void Write(const Math::Matrix& value, LogType type);

        // Manually handled types
        static void Write(const bool value, const LogType type)                                { Write(value ? "True" : "False", type); }
        template<typename T> static void Write(std::weak_ptr<T> ptr, const LogType type)    { Write(ptr.expired() ? "Expired" : typeid(ptr).name(), type); }
        template<typename T> static void Write(std::shared_ptr<T> ptr, const LogType type)    { Write(ptr ? typeid(ptr).name() : "Null", type); }
        static void Write(const std::weak_ptr<Entity>& entity, LogType type);
        static void Write(const std::shared_ptr<Entity>& entity, LogType type);

        static bool m_log_to_file;     

    private:
        static void FlushBuffer();
        static void LogString(const char* text, LogType type);
        static void LogToFile(const char* text, LogType type);

        static std::mutex m_mutex_log;
        static std::weak_ptr<ILogger> m_logger;
        static std::ofstream m_fout;    
        static std::string m_log_file_name;
        static bool m_first_log;
        static std::vector<LogCmd> m_log_buffer;
    };
}
