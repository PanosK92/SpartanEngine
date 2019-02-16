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
#include <stdarg.h>
#include "../World/Entity.h"
#include "../FileSystem/FileSystem.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	weak_ptr<ILogger> Log::m_logger;
	ofstream Log::m_fout;
	mutex Log::m_mutex;
	string Log::m_callerName;
	string Log::m_logFileName	= "log.txt";
	bool Log::m_firstLog		= true;

	void Log::SetLogger(const weak_ptr<ILogger>& logger)
	{
		m_logger = logger;
	}

	void Log::WriteFInfo(const char* text, ...)
	{
		char buffer[1024];
		va_list args;
		va_start(args, text);
		int w = vsnprintf(buffer, sizeof(buffer), text, args);
		va_end(args);

		Write(buffer, Log_Info);
	}

	void Log::WriteFWarning(const char* text, ...)
	{
		char buffer[1024];
		va_list args;
		va_start(args, text);
		int w = vsnprintf(buffer, sizeof(buffer), text, args);
		va_end(args);

		Write(buffer, Log_Warning);
	}

	void Log::WriteFError(const char* text, ...)
	{
		char buffer[1024];
		va_list args;
		va_start(args, text);
		int w = vsnprintf(buffer, sizeof(buffer), text, args);
		va_end(args);

		Write(buffer, Log_Error);
	}

	void Log::Write(const weak_ptr<Entity>& entity, Log_Type type)
	{
		entity.expired() ? Write("Null", type) : Write(entity.lock()->GetName(), type);
	}

	void Log::Write(const Math::Vector2& value, Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Math::Vector3& value, Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Math::Vector4& value, Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Math::Quaternion& value, Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::Write(const Math::Matrix& value, Log_Type type)
	{
		Write(value.ToString(), type);
	}

	void Log::LogString(const char* text, Log_Type type)
	{
		lock_guard<mutex> guard(m_mutex);
		m_logger.lock()->Log(string(text), type);
	}

	void Log::LogToFile(const char* text, Log_Type type)
	{
		lock_guard<mutex> guard(m_mutex);

		string prefix		= (type == Log_Info) ? "Info:" : (type == Log_Warning) ? "Warning:" : "Error:";
		string finalText	= prefix + " " + text;

		// Delete the previous log file (if it exists)
		if (m_firstLog)
		{
			FileSystem::DeleteFile_(m_logFileName);
			m_firstLog = false;
		}

		// Open/Create a log file to write the error message to.
		m_fout.open(m_logFileName, ofstream::out | ofstream::app);

		// Write out the error message.
		m_fout << finalText << endl;

		// Close the file.
		m_fout.close();
	}
}