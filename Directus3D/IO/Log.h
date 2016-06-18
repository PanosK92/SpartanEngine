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

#pragma once

//= INCLUDES ===============
#include <windows.h>
#include "../Math/Vector3.h"
#include "ILogger.h"
//==========================

#define LOG(text, type) Log::Write(text, type)
#define LOG(text) Log::Write(text, Log::Undefined)

class __declspec(dllexport) Log
{
public:
	enum LogType
	{
		Info,
		Warning,
		Error,
		Undefined
	};

	static void Initialize();
	static void Release();
	static void SetLogger(ILogger* logger);

	/*------------------------------------------------------------------------------
									[LOGGING]
	------------------------------------------------------------------------------*/
	static void Write(std::string text, LogType type);
	static void Write(const char* text, LogType type);
	static void Write(Directus::Math::Vector3 vector, LogType type);
	static void Write(Directus::Math::Quaternion quaternion, LogType type);
	static void Write(float value, LogType type);
	static void Write(int value, LogType type);
	static void Write(unsigned int value, LogType type);
	static void Write(bool value, LogType type);
	/*------------------------------------------------------------------------------
									[CONVERTIONS]
	------------------------------------------------------------------------------*/
	static std::string WCHARPToString(WCHAR*);

private:
	static ILogger* m_logger;
};
