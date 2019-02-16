/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==================
#include <string>
#include <memory>
#include <mutex>
#include "../Core/EngineDefs.h"
//=============================

namespace Directus
{
	// Macros
#define LOG_INFO(text)			{ Directus::Log::m_callerName = __FUNCTION__; Directus::Log::Write(text, Directus::Log_Type::Log_Info); }
#define LOG_WARNING(text)		{ Directus::Log::m_callerName = __FUNCTION__; Directus::Log::Write(text, Directus::Log_Type::Log_Warning); }
#define LOG_ERROR(text)			{ Directus::Log::m_callerName = __FUNCTION__; Directus::Log::Write(text, Directus::Log_Type::Log_Error); }
#define LOGF_INFO(text, ...)	{ Directus::Log::m_callerName = __FUNCTION__; Directus::Log::WriteFInfo(text, __VA_ARGS__); }
#define LOGF_WARNING(text, ...)	{ Directus::Log::m_callerName = __FUNCTION__; Directus::Log::WriteFWarning(text, __VA_ARGS__); }
#define LOGF_ERROR(text, ...)	{ Directus::Log::m_callerName = __FUNCTION__; Directus::Log::WriteFError(text, __VA_ARGS__); }

	// Pre-Made
	#define LOG_ERROR_INVALID_PARAMETER() LOG_ERROR("Invalid parameter.")
	#define LOG_ERROR_INVALID_INTERNALS() LOG_ERROR("Invalid internals.")

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

	enum Log_Type
	{
		Log_Info,
		Log_Warning,
		Log_Error
	};

	class ENGINE_CLASS Log
	{
		friend class ILogger;
	public:
		// Set a logger to be used (if not set, logging will done in a text file.
		static void SetLogger(const std::weak_ptr<ILogger>& logger);

		// const char*
		static void Write(const char* text, Log_Type type) 
		{ 
			std::string formatedText = !m_callerName.empty() ? m_callerName + ": " + std::string(text) : std::string(text);
			!m_logger.expired() ? LogString(formatedText.c_str(), type) : LogToFile(formatedText.c_str(), type);
			m_callerName.clear();
		}
		static void WriteFInfo(const char* text, ...);
		static void WriteFWarning(const char* text, ...);
		static void WriteFError(const char* text, ...);

		// std::string
		static void Write(const std::string& text, Log_Type type) { Write(text.c_str(), type); }
		
		// to_string()
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
		static void Write(T value, Log_Type type)
		{
			Write(to_string(value), type);
		}

		// Math
		static void Write(const Math::Vector2& value, Log_Type type);
		static void Write(const Math::Vector3& value, Log_Type type);
		static void Write(const Math::Vector4& value, Log_Type type);
		static void Write(const Math::Quaternion& value, Log_Type type);
		static void Write(const Math::Matrix& value, Log_Type type);

		// Manually handled types
		static void Log::Write(bool value, Log_Type type)								{ Write(value ? "True" : "False", type); }
		template<typename T> static void Write(std::weak_ptr<T> ptr, Log_Type type)		{ Write(ptr.expired() ? "Expired" : typeid(ptr).name(), type); }
		template<typename T> static void Write(std::shared_ptr<T> ptr, Log_Type type)	{ Write(ptr ? typeid(ptr).name() : "Null", type); }
		static void Write(const std::weak_ptr<Entity>& entity, Log_Type type);

		static std::string m_callerName;

	private:
		static void LogString(const char* text, Log_Type type);
		static void LogToFile(const char* text, Log_Type type);

		static std::weak_ptr<ILogger> m_logger;
		static std::ofstream m_fout;
		static std::mutex m_mutex;
		static std::string m_logFileName;
		static bool m_firstLog;
	};
}
