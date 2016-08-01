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
#include "../IO/Log.h"

//====================

Timer::Timer()
{
	m_frequency = 0;
	m_ticksPerMs = 0.0f;
	m_deltaTime = 0.0f;
	m_startTime = 0.0f;
	m_lastKnownTime = 0.0f;
}

Timer::~Timer()
{
}

void Timer::Initialize()
{
	// Get ticks per second
	QueryPerformanceFrequency((LARGE_INTEGER*)&m_frequency);

	// If the frequency is 0, this machine doesn't support high performance timers.
	if (m_frequency == 0)
	LOG("The system does not support high performance timers.", Log::Warning);

	// Convert frequency from ticks per second to ticks every millisecond.
	m_ticksPerMs = (float)(m_frequency / 1000);

	m_startTime = GetTimeMs(); // Save current time
	m_lastKnownTime = m_startTime;
}

void Timer::Update()
{
	// Get current time
	INT64 currentTime = GetTimeMs();

	// Calculate delta time
	m_deltaTime = currentTime - m_lastKnownTime;

	// Save current time
	m_lastKnownTime = currentTime;
}

// Returns them time it took to complete the last frame in seconds 
float Timer::GetDeltaTime() const
{
	return GetDeltaTimeMs() / 1000;
}

// Returns them time it took to complete the last frame in milliseconds
float Timer::GetDeltaTimeMs() const
{
	return m_deltaTime;
}

// Returns current time in seconds
float Timer::GetTime()
{
	return GetTimeMs() / 1000;
}

// Returns current time in milliseconds
float Timer::GetTimeMs()
{
	INT64 currentTime;
	QueryPerformanceCounter((LARGE_INTEGER*)&currentTime);

	return currentTime / m_ticksPerMs;
}

// Returns them elapsed time since the engine initialization in seconds
float Timer::GetElapsedTime()
{
	return GetElapsedTimeMs() / 1000;
}

// Returns them elapsed time since the engine initialization in milliseconds
float Timer::GetElapsedTimeMs()
{
	return GetTimeMs() - m_startTime;
}
