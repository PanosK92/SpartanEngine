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
	#define LOG_INFO(text)		Directus::Log::Write(text, Directus::Log::Info)
	#define LOG_WARNING(text)	Directus::Log::Write(text, Directus::Log::Warning)
	#define LOG_ERROR(text)		Directus::Log::Write(text, Directus::Log::Error)

	#define LOGF_INFO(text, ...)		Directus::Log::WriteFInfo(text,		__VA_ARGS__)
	#define LOGF_WARNING(text, ...)		Directus::Log::WriteFWarning(text,	__VA_ARGS__)
	#define LOGF_ERROR(text, ...)		Directus::Log::WriteFError(text,	__VA_ARGS__)

	class GameObject;

	namespace Math
	{
		class Quaternion;
		class Vector2;
		class Vector3;
		class Vector4;
	}

	class ENGINE_CLASS Log
	{
		friend class ILogger;
	public:
		enum LogType
		{
			Info,
			Warning,
			Error
		};

		static void Initialize();
		static void Release();
		static void SetLogger(const std::weak_ptr<ILogger>& logger);

		// Text	
		static void Write(const char* text, LogType type);
		static void WriteFInfo(const char* text, ...);
		static void WriteFWarning(const char* text, ...);
		static void WriteFError(const char* text, ...);
		static void Write(const std::string& text, LogType type);

		// Math
		static void Write(const Math::Vector2& vector, LogType type);
		static void Write(const Math::Vector3& vector, LogType type);
		static void Write(const Math::Vector4& vector, LogType type);
		static void Write(const Math::Quaternion& quaternion, LogType type);

		// Primitives
		static void Write(float value, LogType type);
		static void Write(double value, LogType type);
		static void Write(int value, LogType type);
		static void Write(unsigned int value, LogType type);
		static void Write(size_t value, LogType type);
		static void Write(bool value, LogType type);

		// Pointers
		static void Write(const std::weak_ptr<GameObject>& gameObject, LogType type);
		template<typename T>
		static void Write(std::weak_ptr<T> ptr, LogType type) { Write(ptr.expired() ? "Expired" : typeid(ptr).name(), type); }
		template<typename T>
		static void Write(std::shared_ptr<T> ptr, LogType type) { Write(ptr ? typeid(ptr).name() : "Null", type); }

	private:
		static void LogString(const char* text, LogType type);
		static void LogToFile(const char* text, LogType type);

		static std::weak_ptr<ILogger> m_logger;
		static std::ofstream m_fout;
		static std::string m_logFileName;
		static bool m_firstLog;
		static std::mutex m_mutex;
	};
}
