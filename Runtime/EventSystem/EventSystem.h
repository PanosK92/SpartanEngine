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

#pragma once

//= INCLUDES ==========
#include <vector>
#include <memory>
#include "Subscriber.h"
//=====================

/*
HOW TO USE
===================================================================================================================
To subscribe a function to an event						-> SUBSCRIBE_TO_EVENT(SOME_EVENT, Class::Func, this);
To subscribe a function (with parameters) to an event	-> SUBSCRIBE_TO_EVENT(SOME_EVENT, Class::Func, this, args);
To unsubscribe a function from an event					-> UNSUBSCRIBE_FROM_EVENT(SOME_EVENT, Class::Func, this);
To fire an event										-> FIRE_EVENT(SOME_EVENT);
===================================================================================================================
*/

//= EVENTS =========================================================================
#define EVENT_UPDATE				0	// Fired when it's time to update the engine
#define EVENT_RENDER				1	// Fired when it's time to do rendering
//==================================================================================

//= MACROS =======================================================================================================================
#define SUBSCRIBE_TO_EVENT(signalID, instance, function)		EventSystem::Subscribe(signalID, std::bind(&function, instance))
#define UNSUBSCRIBE_FROM_EVENT(signalID, instance, function)	EventSystem::Unsubscribe(signalID, std::bind(&function, instance))
#define FIRE_EVENT(signalID)									EventSystem::Fire(signalID)
//================================================================================================================================

namespace Directus
{
	class DLL_API EventSystem
	{
	public:
		template <typename Function>
		static void Subscribe(int eventID, Function&& subscriber)
		{
			AddSubscriber(std::make_shared<Subscriber>(eventID, std::bind(std::forward<Function>(subscriber))));
		}

		template <typename Function>
		static void Unsubscribe(int eventID, Function&& subscriber)
		{
			RemoveSubscriber(eventID, getAddress(subscriber));
		}

		static void Fire(int eventID)
		{
			CallSubscriber(eventID);
		}

		static void Clear();

	private:
		static std::vector<std::shared_ptr<Subscriber>> m_subscribers;

		// Hiding implementations on purpose to allow cross-dll usage without linking errors
		static void AddSubscriber(std::shared_ptr<Subscriber> subscriber);
		static void RemoveSubscriber(int eventID, std::size_t functionAddress);
		static void CallSubscriber(int eventID);
	};
}