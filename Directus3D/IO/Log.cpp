/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===
#include "Log.h"
#include "../Signals/Signaling.h"
//==============

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

ILogger* Log::m_logger;
map<string, Log::LogType> Log::m_queuedLogs;

void Log::Initialize()
{
	CONNECT_TO_SIGNAL(SIGNAL_ENGINE_SHUTDOWN, std::bind(&Log::Release));
}

void Log::Release()
{
	m_logger = nullptr;
}

void Log::SetLogger(ILogger* logger)
{
	m_logger = logger;
}

/*------------------------------------------------------------------------------
								[LOGGING]
------------------------------------------------------------------------------*/
void Log::Write(string text, LogType type) // all functions resolve to that one
{
	if (!m_logger)
	{
		m_queuedLogs.insert(make_pair(text, type));
		return;
	}

	string prefix = "";

	if (type == Info)
		prefix = "Info:";

	if (type == Warning)
		prefix = "Warning:";

	if (type == Error)
		prefix = "Error:";

	if (type == Undefined)
		prefix = "Undefined:";

	string finalText = prefix + " " + text;

	// Print any queued logs
	for (auto it = m_queuedLogs.begin(); it != m_queuedLogs.end(); ++it)
	{
		m_logger->Log(it->first, it->second);
	}
	m_queuedLogs.clear();

	// Print the log
	m_logger->Log(finalText, type);
}

void Log::Write(const char* text, LogType type)
{
	string str = text;
	Write(str, type);
}

void Log::Write(const Vector3& vector, LogType type)
{
	string x = "X: " + to_string(vector.x);
	string y = "Y: " + to_string(vector.y);
	string z = "Z: " + to_string(vector.z);

	Write(x + ", " + y + ", " + z, type);
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
	if (value)
		Write("True", type);
	else
		Write("False", type);
}

void Log::Write(size_t value, LogType type)
{
	Write(int(value), type);
}

/*------------------------------------------------------------------------------
							[CONVERTIONS]
------------------------------------------------------------------------------*/
string Log::WCHARPToString(WCHAR* text)
{
	wstring ws(text);
	string str(ws.begin(), ws.end());

	return str;
}
