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

//= INCLUDES ========================
#include "Log.h"
#include "ILogger.h"
#include <fstream>
#include <cstdarg>
#include "../World/Entity.h"
#include "../FileSystem/FileSystem.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	weak_ptr<ILogger> Log::m_logger;
	ofstream Log::m_fout;
	mutex Log::m_mutex;
	string Log::m_caller_name;
	string Log::m_log_file_name	= "log.txt";
	bool Log::m_log_to_file		= true; // start logging to file (unless changed by the user, e.g. Renderer initialization was succesfull, so logging can happen on screen)
	bool Log::m_first_log		= true;

	void Log::SetLogger(const weak_ptr<ILogger>& logger)
	{
		m_logger = logger;
	}

	// Everything resolves to this
	void Log::Write(const char* text, const Log_Type type)
	{
		auto formated_text = !m_caller_name.empty() ? m_caller_name + ": " + std::string(text) : std::string(text);

		const auto log_to_file = true;
		log_to_file ? LogToFile(formated_text.c_str(), type) : LogString(formated_text.c_str(), type);
		
		m_caller_name.clear();
	}

	void Log::WriteFInfo(const char* text, ...)
	{
		char buffer[1024];
		va_list args;
		va_start(args, text);
		auto w = vsnprintf(buffer, sizeof(buffer), text, args);
		va_end(args);

		Write(buffer, Log_Info);
	}

	void Log::WriteFWarning(const char* text, ...)
	{
		char buffer[1024];
		va_list args;
		va_start(args, text);
		auto w = vsnprintf(buffer, sizeof(buffer), text, args);
		va_end(args);

		Write(buffer, Log_Warning);
	}

	void Log::WriteFError(const char* text, ...)
	{
		char buffer[1024];
		va_list args;
		va_start(args, text);
		auto w = vsnprintf(buffer, sizeof(buffer), text, args);
		va_end(args);

		Write(buffer, Log_Error);
	}

	void Log::Write(const weak_ptr<Entity>& entity, const Log_Type type)
	{
		entity.expired() ? Write("Null", type) : Write(entity.lock()->GetName(), type);
	}

	void Log::Write(const std::shared_ptr<Entity>& entity, const Log_Type type)
	{
		entity ? Write(entity->GetName(), type) : Write("Null", type);
	}

	void Log::Write(const Vector2& value, const Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Vector3& value, const Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Vector4& value, const Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Quaternion& value, const Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Matrix& value, const Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::LogString(const char* text, const Log_Type type)
	{
		lock_guard<mutex> guard(m_mutex);
		m_logger.lock()->Log(string(text), type);
	}
	void Log::LogToFile(const char* text, const Log_Type type)
	{
		lock_guard<mutex> guard(m_mutex);

		const string prefix		= (type == Log_Info) ? "Info:" : (type == Log_Warning) ? "Warning:" : "Error:";
		const auto final_text	= prefix + " " + text;

		// Delete the previous log file (if it exists)
		if (m_first_log)
		{
			FileSystem::DeleteFile_(m_log_file_name);
			m_first_log = false;
		}

		// Open/Create a log file to write the error message to.
		m_fout.open(m_log_file_name, ofstream::out | ofstream::app);

		// Write out the error message.
		m_fout << final_text << endl;

		// Close the file.
		m_fout.close();
	}
}