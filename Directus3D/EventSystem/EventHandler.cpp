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

//= INCLUDES ============
#include "EventHandler.h"
//=======================

//= NAMESPACES =====
using namespace std;
//==================

//= HELPER FUNCTION ============================================
template<typename FunctionType, typename... ARGS>
size_t getAddress(std::function<FunctionType(ARGS...)> function)
{
	typedef FunctionType(fnType)(ARGS...);
	fnType** fptr = function.template target<fnType*>();
	return size_t(*fptr);
}
//==============================================================

vector<shared_ptr<Subscriber>> EventHandler::m_subscribers;

size_t Subscriber::GetAddress()
{
	return getAddress(m_subscribedFunction);
}

void EventHandler::Clear()
{
	m_subscribers.clear();
	m_subscribers.shrink_to_fit();
}

void EventHandler::AddSubscriber(shared_ptr<Subscriber> subscriber)
{
	m_subscribers.push_back(subscriber);
}

void EventHandler::RemoveSubscriber(int eventID, size_t functionAddress)
{
	for (auto it = m_subscribers.begin(); it != m_subscribers.end();)
	{
		auto subscriber = *it;
		if (subscriber->GetEventID() == eventID && subscriber->GetAddress() == functionAddress)
		{
			it = m_subscribers.erase(it);
			return;
		}
		++it;
	}
}
