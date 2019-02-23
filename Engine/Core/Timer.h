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

#pragma once

//= INCLUDES =========
#include "ISubsystem.h"
#include <chrono>
//====================

namespace Directus
{
	class ENGINE_CLASS Timer : public ISubsystem
	{
	public:
		Timer(Context* context);
		~Timer() = default;

		//= ISubsystem ======
		void Tick() override;
		//===================

		float GetDeltaTimeMs() const	{ return static_cast<float>(m_delta_time_ms); }
		float GetDeltaTimeSec() const	{ return static_cast<float>(m_delta_time_ms) / 1000.0f; }

	private:		
		std::chrono::high_resolution_clock::time_point time_a;
		std::chrono::high_resolution_clock::time_point time_b;
		double m_delta_time_ms;
	};
}