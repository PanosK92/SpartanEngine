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

//= INCLUDES ==============
#include "Events.h"
#include <vector>
#include <functional>
#include <memory>
#include "../Core/Helper.h"
//=========================

#define SUBSCRIBE_TO_EVENT(signalID, function)		EventHandler::Subscribe(signalID, function)
#define UNSUBSCRIBE_FROM_EVENT(signalID, function)	EventHandler::Unsubscribe(signalID, function)
#define FIRE_EVENT(signalID)						EventHandler::Fire(signalID)

/*
HOW TO USE
==========
SUBSCRIBE_TO_EVENT		(SOME_EVENT, std::bind(&Class::Func, args));
SUBSCRIBE_TO_EVENT		(SOME_EVENT, std::bind(&Class::Func, this, args));
UNSUBSCRIBE_FROM_EVENT	(SOME_EVENT, std::bind(&Class::Func, this));
FIRE_EVENT				(SOME_EVENT);
*/

//= HELPER FUNCTION ==========================================
template<typename FunctionType, typename... ARGS>
size_t getAddress(std::function<FunctionType(ARGS...)> function)
{
	typedef FunctionType(fnType)(ARGS...);
	fnType** fptr = function.template target<fnType*>();
	return size_t(*fptr);
}
//============================================================

//= EVENT ===================================================
class DllExport Event
{
public:
	using functionType = std::function<void()>;

	Event(int eventID, functionType&& arguments)
	{
		m_ID = eventID;
		m_function = std::forward<functionType>(arguments);
	}
	int GetEventID() { return m_ID; }
	size_t GetAddress() { return getAddress(m_function); }
	void Fire() { m_function(); }

private:
	int m_ID;
	functionType m_function;
};
//============================================================

//= EVENT HANDLER ============================================
class DllExport EventHandler
{
public:
	template <typename Function>
	static void Subscribe(int eventID, Function&& function);

	template <typename Function>
	static void Unsubscribe(int eventID, Function&& function);

	static void Fire(int eventID);
	static void Clear();

	static std::vector<std::shared_ptr<Event>> m_events;
};
//============================================================

template <typename Function>
void EventHandler::Subscribe(int eventID, Function&& function)
{
	m_events.push_back(std::make_shared<Event>(eventID, std::bind(std::forward<Function>(function))));
}

template <typename Function>
void EventHandler::Unsubscribe(int eventID, Function&& function)
{
	for (auto it = m_events.begin(); it != m_events.end();)
	{
		auto slot = *it;
		if (slot->GetEventID() == eventID && slot->GetAddress() == getAddress(function))
		{
			it = m_events.erase(it);
			return;
		}
		++it;
	}
}