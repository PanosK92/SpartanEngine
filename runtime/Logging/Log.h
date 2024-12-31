/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ===================
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include "../Core/Definitions.h"
#include "ILogger.h"
//==============================

namespace Spartan
{
    #define SP_LOG_INFO(text, ...)    { Spartan::Log::WriteFInfo(std::string(__FUNCTION__)    + ": " + std::string(text), ## __VA_ARGS__); }
    #define SP_LOG_WARNING(text, ...) { Spartan::Log::WriteFWarning(std::string(__FUNCTION__) + ": " + std::string(text), ## __VA_ARGS__); }
    #define SP_LOG_ERROR(text, ...)   { Spartan::Log::WriteFError(std::string(__FUNCTION__)   + ": " + std::string(text), ## __VA_ARGS__); }

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

    class Log
    {
        friend class ILogger;
    public:
        Log() = default;

        // misc
        static void Initialize();
        static void SetLogger(ILogger* logger);
        static void SetLogToFile(const bool log_to_file);

        // alpha
        static void Write(const char* text, const LogType type);
        static void WriteFInfo(const char* text, ...);
        static void WriteFWarning(const char* text, ...);
        static void WriteFError(const char* text, ...);
        static void Write(const std::string& text, const LogType type);
        static void WriteFInfo(const std::string text, ...);
        static void WriteFWarning(const std::string text, ...);
        static void WriteFError(const std::string text, ...);

        // numeric
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

        // nath
        static void Write(const Math::Vector2& value, LogType type);
        static void Write(const Math::Vector3& value, LogType type);
        static void Write(const Math::Vector4& value, LogType type);
        static void Write(const Math::Quaternion& value, LogType type);
        static void Write(const Math::Matrix& value, LogType type);

        // manually handled types
        static void Write(const bool value, const LogType type)                            { Write(value ? "True" : "False", type); }
        template<typename T> static void Write(std::weak_ptr<T> ptr, const LogType type)   { Write(ptr.expired() ? "Expired" : typeid(ptr).name(), type); }
        template<typename T> static void Write(std::shared_ptr<T> ptr, const LogType type) { Write(ptr ? typeid(ptr).name() : "Null", type); }
        static void Write(const std::weak_ptr<Entity>& entity, LogType type);
        static void Write(const std::shared_ptr<Entity>& entity, LogType type);
    };
}
