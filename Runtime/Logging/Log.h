/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ==============
#include "../Core/Helper.h"
#include <string>
#include <memory>
//=========================

namespace Directus
{
#define LOG_INFO(text) Log::Write(text, Log::Info)
#define LOG_WARNING(text) Log::Write(text, Log::Warning)
#define LOG_ERROR(text) Log::Write(text, Log::Error)

	class GameObject;

	namespace Math
	{
		class Quaternion;
		class Vector2;
		class Vector3;
		class Vector4;
	}

	class DLL_API Log
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
		static void SetLogger(std::weak_ptr<ILogger> logger);

		// STRING
		static void Write(const std::string& text, LogType type);

		// CHAR_PTR
		static void Write(const char* text, LogType type);

		// GAMEOBJECT
		static void Write(std::weak_ptr<GameObject> gameObject, LogType type);

		// VECTORS
		static void Write(const Math::Vector2& vector, LogType type);
		static void Write(const Math::Vector3& vector, LogType type);
		static void Write(const Math::Vector4& vector, LogType type);

		// QUATERNION
		static void Write(const Math::Quaternion& quaternion, LogType type);

		// FLOAT
		static void Write(float value, LogType type);

		// DOUBLE
		static void Write(double value, LogType type);

		// INT
		static void Write(int value, LogType type);

		// UNSIGNED INT
		static void Write(unsigned int value, LogType type);

		// BOOL
		static void Write(bool value, LogType type);

		// SIZE_T
		static void Write(size_t value, LogType type);

		// WEAK_PTR
		template<typename T>
		static void Write(std::weak_ptr<T> ptr, LogType type) { Write(ptr.expired() ? "Expired" : typeid(ptr).name(), type); }

		// SHARED_PTR
		template<typename T>
		static void Write(std::shared_ptr<T> ptr, LogType type) { Write(ptr ? typeid(ptr).name() : "Null", type); }

		static void LogString(const std::string& text, LogType type);
		static void LogToFile(const std::string& text, LogType type);

	private:
		static std::weak_ptr<ILogger> m_logger;
		static std::ofstream m_fout;
		static std::string m_logFileName;
		static bool m_firstLog;
	};
}