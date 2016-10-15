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

//= INCLUDES =======
#include <Windows.h>
#include <memory>
//==================

class Stopwatch;

class __declspec(dllexport) Timer
{
public:
	Timer();
	~Timer();

	void Initialize();
	void Update();
	void Reset();

	float GetDeltaTime() const;
	float GetDeltaTimeMs() const;

	float GetTime();
	float GetTimeMs() const;

	float GetElapsedTime();
	float GetElapsedTimeMs() const;

	float GetFPS() const;

	void RenderStart() const;
	void RenderEnd() const;
	float GetRenderTimeMs() const;

	Stopwatch* GetStopwatch();
private:
	INT64 m_ticksPerSec;
	float m_ticksPerMs;
	float m_deltaTime;
	float m_startTime;
	float m_lastKnownTime;

	//= FPS CALCULATION
	int m_frameCount;
	float m_fpsLastKnownTime;
	float m_fps;

	//= STATS ===================
	Stopwatch* m_renderStopwatch;
	//===========================
};

class Stopwatch
{
public:
	Stopwatch(Timer* timer)
	{
		m_timer = timer;

		m_startTime = 0.0f;
		m_endTime = 0.0f;
		m_deltaTime = 0.0f;
	}

	void Start()
	{
		m_startTime = m_timer->GetTime();
	}

	void Stop()
	{
		m_endTime = m_timer->GetTime();
	}

	float GetDeltaTime() const
	{
		return m_endTime - m_startTime;
	}

	float GetDeltaTimeMs() const
	{
		return GetDeltaTime() / 1000.0f;
	}

	float m_startTime;
	float m_endTime;
	float m_deltaTime;
	Timer* m_timer;
};