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

//= INCLUDES ========
#include "Timer.h"
#include "Settings.h"
#include <thread>
//===================

//= NAMESPACES ========
using namespace std;
using namespace chrono;
//=====================

namespace Spartan
{
	Timer::Timer(Context* context) : ISubsystem(context)
	{
		time_a			= high_resolution_clock::now();
		time_b			= high_resolution_clock::now();
		m_delta_time_ms	= 0.0f;
	}

	void Timer::Tick()
	{
		// Compute work time
		time_a								= high_resolution_clock::now();
		duration<double, milli> time_work	= time_a - time_b;
		
		// Compute sleep time (fps limiting)
		const double max_fps		= Settings::Get().GetFpsLimit();
		const double min_dt			= 1000.0 / max_fps;
		const double dt_remaining	= min_dt - time_work.count();
		if (dt_remaining >  0)
		{
			this_thread::sleep_for(milliseconds(static_cast<int64_t>(dt_remaining)));
		}

		// Compute delta
		time_b										= high_resolution_clock::now();
		const duration<double, milli> time_sleep	= time_b - time_a;
		m_delta_time_ms								= (time_work + time_sleep).count();

		m_delta_time_sec = GetDeltaTimeSec();
	}
}
