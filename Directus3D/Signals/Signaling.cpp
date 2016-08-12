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
#include "Signaling.h"
//====================

//= NAMESPACES =====
using namespace std;
//==================

vector<Slot> Signaling::m_slots;

void Signaling::Connect(int signalID, function<void()> function)
{
	Slot slot;
	slot.signalID = signalID;
	slot.function = function;

	m_slots.push_back(slot);
}

template<typename FunctionType, typename... ARGS>
size_t getAddress(function<FunctionType(ARGS...)> function)
{
	typedef FunctionType(fnType)(ARGS...);
	fnType** fptr = function.template target<fnType*>();
	return size_t(*fptr);
}

void Signaling::Disconnect(int signalID, function<void()> function)
{
	for (auto it = m_slots.begin(); it != m_slots.end();)
	{
		Slot slot = *it;
		if (slot.signalID == signalID && getAddress(slot.function) == getAddress(function))
		{
			it = m_slots.erase(it);
			return;
		}
		++it;
	}
}

void Signaling::EmitSignal(int signalID)
{
	for (int i = 0; i < m_slots.size(); i++)
	{
		if (m_slots[i].signalID == signalID)
			m_slots[i].function();
	}
}

void Signaling::DeleteAll()
{
	m_slots.clear();
	m_slots.shrink_to_fit();
}
