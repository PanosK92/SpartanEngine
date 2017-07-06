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

namespace Directus
{
	int Stopwatch::m_milliseconds;
	static std::chrono::time_point<std::chrono::system_clock> m_start;
	static std::chrono::time_point<std::chrono::system_clock> m_end;

	void Stopwatch::Start()
	{
		m_start = std::chrono::system_clock::now();
	}

	int Stopwatch::Stop()
	{
		m_end = std::chrono::system_clock::now();
		m_milliseconds = (int)std::chrono::duration_cast<std::chrono::milliseconds>(m_end - m_start).count();
		return m_milliseconds;
	}
}