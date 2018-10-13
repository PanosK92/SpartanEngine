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

//= INCLUDES =====
#include "Timer.h"
#include "Engine.h"
#include "Settings.h"
#include <thread>
//================

//= NAMESPACES ========
using namespace std;
using namespace chrono;
//=====================

namespace Directus
{
	Timer::Timer(Context* context) : Subsystem(context)
	{
		m_deltaTimeSec	= 0.0f;
		m_deltaTimeMs	= 0.0f;
		m_firstRun		= true;
		a				= system_clock::now();
		b				= system_clock::now();
	}

	void Timer::Tick()
	{
		auto currentTime				= high_resolution_clock::now();
		duration<double, milli> delta	= currentTime - m_previousTime;
		m_previousTime					= currentTime;
		m_deltaTimeMs					= (float)delta.count();
		m_deltaTimeSec					= (float)(delta.count() / 1000.0);

		if (m_firstRun)
		{
			m_deltaTimeMs	= 0.0f;
			m_deltaTimeSec	= 0.0f;
			m_firstRun		= false;
		}

		// FPS LIMIT
		{
			bool isEditor		= !Engine::EngineMode_IsSet(Engine_Game);
			float maxFPS_editor = 165.0f;
			float maxFPS_game	= Settings::Get().GetMaxFPS();
			float maxFPS		= isEditor ? maxFPS_editor : maxFPS_game;
			float maxMs			= (1.0f / maxFPS) * 1000;

			// Maintain designated frequency
			a = system_clock::now();
			duration<double, std::milli> work_time = a - b;

			if (work_time.count() < maxMs)
			{
				duration<double, std::milli> delta_ms(maxMs - work_time.count());
				auto delta_ms_duration = std::chrono::duration_cast<milliseconds>(delta_ms);
				std::this_thread::sleep_for(milliseconds(delta_ms_duration.count()));
			}

			b = system_clock::now();
		}
	}
}
