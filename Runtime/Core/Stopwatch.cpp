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

//= INCLUDES =========
#include "Stopwatch.h"
#include <chrono>
//====================

//= NAMESPACES ========
using namespace std;
using namespace chrono;
//=====================

namespace Directus
{
	time_point<system_clock> m_start;
	time_point<system_clock> m_end;

	Stopwatch::Stopwatch()
	{
		m_milliseconds = 0.0f;
	}

	Stopwatch::~Stopwatch()
	{

	}

	void Stopwatch::Start()
	{
		m_start = system_clock::now();
	}

	float Stopwatch::Stop()
	{
		m_end = system_clock::now();
		m_milliseconds = duration_cast<microseconds>(m_end - m_start).count() / 1000.0;

		return m_milliseconds;
	}
}