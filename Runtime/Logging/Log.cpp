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

//= INCLUDES ============================
#include "Log.h"
#include <sstream> 
#include <fstream>
#include "ILogger.h"
#include "../EventSystem/EventSystem.h"
#include "../FileSystem/FileSystem.h"
#include "../Core/GameObject.h"
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Vector4.h"
#include "../Math/Quaternion.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

#define LOG_FILE "log.txt"

namespace Directus
{
	weak_ptr<ILogger> Log::m_logger;
	ofstream Log::m_fout;
	bool Log::m_firstLog = true;

	void Log::Initialize()
	{

	}

	void Log::Release()
	{

	}

	void Log::SetLogger(weak_ptr<ILogger> logger)
	{
		m_logger = logger;
	}

	//= LOGGING ==========================================================================
	void Log::Write(const string& text, LogType type) // all functions resolve to that one
	{
		// if a logger is available use it, if not, write to file
		if (!m_logger.expired())
		{
			m_logger._Get()->Log(text, type);
		}
		else
		{
			string prefix = (type == Info) ? "Info:" : (type == Warning) ? "Warning:" : "Error:";
			string finalText = prefix + " " + text;
			WriteToFile(finalText);
		}
	}

	void Log::Write(const char* text, LogType type)
	{
		string str = text;
		Write(str, type);
	}

	void Log::Write(weak_ptr<GameObject> gameObject, LogType type)
	{
		gameObject.expired() ? Write("Null", type) : Write(gameObject._Get()->GetName(), type);
	}

	void Log::Write(const Vector2& vector, LogType type)
	{
		string x = "X: " + to_string(vector.x);
		string y = "Y: " + to_string(vector.y);

		Write(x + ", " + y, type);
	}

	void Log::Write(const Vector3& vector, LogType type)
	{
		string x = "X: " + to_string(vector.x);
		string y = "Y: " + to_string(vector.y);
		string z = "Z: " + to_string(vector.z);

		Write(x + ", " + y + ", " + z, type);
	}

	void Log::Write(const Vector4& vector, LogType type)
	{
		string x = "X: " + to_string(vector.x);
		string y = "Y: " + to_string(vector.y);
		string z = "Z: " + to_string(vector.z);
		string w = "W: " + to_string(vector.w);

		Write(x + ", " + y + ", " + z + ", " + w, type);
	}

	void Log::Write(const Quaternion& quaternion, LogType type)
	{
		string x = "X: " + to_string(quaternion.x);
		string y = "Y: " + to_string(quaternion.y);
		string z = "Z: " + to_string(quaternion.z);
		string w = "W: " + to_string(quaternion.w);

		Write(x + ", " + y + ", " + z + ", " + w, type);
	}

	void Log::Write(float value, LogType type)
	{
		Write(to_string(value), type);
	}

	void Log::Write(int value, LogType type)
	{
		Write(to_string(value), type);
	}

	void Log::Write(unsigned int value, LogType type)
	{
		Write(int(value), type);
	}

	void Log::Write(bool value, LogType type)
	{
		value ? Write("True", type) : Write("False", type);
	}

	void Log::Write(size_t value, LogType type)
	{
		Write(int(value), type);
	}

	void Log::WriteToFile(const string& text)
	{
		// Delete the previous log file (if it exists)
		if (m_firstLog)
		{
			FileSystem::DeleteFile_(LOG_FILE);
			m_firstLog = false;
		}

		// Open/Create a log file to write the error message to.
		m_fout.open(LOG_FILE, ofstream::out | ofstream::app);

		// Write out the error message.
		m_fout << text << endl;

		// Close the file.
		m_fout.close();
	}

	//=================================================================================
}