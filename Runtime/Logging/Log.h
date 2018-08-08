/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../Core/EngineDefs.h"
#include <string>
#include <memory>
#include <mutex>
//=============================

namespace Directus
{
	#define LOG_INFO(text)		Directus::Log::Write(text, Directus::Log_Type::Log_Info)
	#define LOG_WARNING(text)	Directus::Log::Write(text, Directus::Log_Type::Log_Warning)
	#define LOG_ERROR(text)		Directus::Log::Write(text, Directus::Log_Type::Log_Error)

	#define LOGF_INFO(text, ...)		Directus::Log::WriteFInfo(text,		__VA_ARGS__)
	#define LOGF_WARNING(text, ...)		Directus::Log::WriteFWarning(text,	__VA_ARGS__)
	#define LOGF_ERROR(text, ...)		Directus::Log::WriteFError(text,	__VA_ARGS__)

	class Actor;

	namespace Math
	{
		class Quaternion;
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
		static void Initialize();
		static void Release();
		static void SetLogger(const std::weak_ptr<ILogger>& logger);

		// Text	
		static void Write(const char* text, Log_Type type);
		static void WriteFInfo(const char* text, ...);
		static void WriteFWarning(const char* text, ...);
		static void WriteFError(const char* text, ...);
		static void Write(const std::string& text, Log_Type type);

		// Math
		static void Write(const Math::Vector2& vector, Log_Type type);
		static void Write(const Math::Vector3& vector, Log_Type type);
		static void Write(const Math::Vector4& vector, Log_Type type);
		static void Write(const Math::Quaternion& quaternion, Log_Type type);

		// Primitives
		static void Write(float value, Log_Type type);
		static void Write(double value, Log_Type type);
		static void Write(int value, Log_Type type);
		static void Write(unsigned int value, Log_Type type);
		static void Write(size_t value, Log_Type type);
		static void Write(bool value, Log_Type type);

		// Pointers
		static void Write(const std::weak_ptr<Actor>& actor, Log_Type type);
		template<typename T>
		static void Write(std::weak_ptr<T> ptr, Log_Type type) { Write(ptr.expired() ? "Expired" : typeid(ptr).name(), type); }
		template<typename T>
		static void Write(std::shared_ptr<T> ptr, Log_Type type) { Write(ptr ? typeid(ptr).name() : "Null", type); }

		static void LogString(const char* text, Log_Type type);
		static void LogToFile(const char* text, Log_Type type);

	private:
		static std::weak_ptr<ILogger> m_logger;
		static std::ofstream m_fout;
		static std::string m_logFileName;
		static bool m_firstLog;
		static std::mutex m_mutex;
	};
}
