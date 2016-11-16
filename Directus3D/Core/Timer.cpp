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

//= INCLUDES =========
#include "Timer.h"
#include "../Logging/Log.h"
//====================

//= NAMESPACES =====
using namespace std;
//==================

Timer::Timer(Context* context) : Subsystem(context)
{
	m_ticksPerSec = 0;
	m_ticksPerMs = 0.0f;
	m_startTime = 0.0f;
	m_previousTime = 0.0f;
	m_deltaTime = 0.0f;

	LARGE_INTEGER ticksPerSec;
	if (QueryPerformanceFrequency(&ticksPerSec))
	{
		m_ticksPerSec = ticksPerSec.QuadPart;
	}
	else
	{
		LOG_ERROR("The system does not support high performance timers.");
		m_ticksPerSec = 1000000;
	}

	m_ticksPerMs = m_ticksPerSec / 1000;
	m_startTime = GetTime();
	m_previousTime = m_startTime;
}

Timer::~Timer()
{
	
}

void Timer::Update()
{
	// Get current time
	int currentTime = GetTime();
	
	// Calculate delta time
	m_deltaTime = currentTime - m_previousTime;

	// Save current time
	m_previousTime = currentTime;
}

// Returns them time it took to complete the last frame in milliseconds
int Timer::GetDeltaTime()
{
	return m_deltaTime;
}

// Returns current time in milliseconds
int Timer::GetTime()
{
	INT64 currentTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);

	return currentTime / m_ticksPerMs;
}

// Returns them elapsed time since the engine initialization in milliseconds
int Timer::GetElapsedTime()
{
	return GetTime() - m_startTime;
}

float Timer::GetDeltaTimeSec()
{
	return (float)GetDeltaTime() / 1000.0f;
}
