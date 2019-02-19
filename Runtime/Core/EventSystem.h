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

//= INCLUDES ===============
#include <map>
#include <vector>
#include <functional>
#include "../Core/Variant.h"
//==========================

/*
HOW TO USE
=================================================================================
To subscribe a function to an event		-> SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To unsubscribe a function from an event	-> SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To fire an event						-> FIRE_EVENT(EVENT_ID);
To fire an event with data				-> FIRE_EVENT_DATA(EVENT_ID, Variant);

Note: Currently, this is a blocking event system
=================================================================================
*/

enum Event_Type
{
	Event_Frame_Start,			// A frame begins
	Event_Frame_End,			// A frame ends
	Event_World_Saved,			// The world finished saving to file
	Event_World_Loaded,			// The world finished loading from file
	Event_World_Unload,			// The world should clear everything
	Event_World_Resolve,		// The world should resolve
	Event_World_Submit,			// The world is submitting entities to the renderer
	Event_World_Stop,			// The world should stop ticking
	Event_World_Start,			// The world should start ticking
	Event_World_EntitySelected	// An entity was clicked in the viewport
};

//= MACROS =====================================================================================================
#define EVENT_HANDLER_STATIC(function)				[](Directus::Variant var)		{ function(); }
#define EVENT_HANDLER(function)						[this](Directus::Variant var)	{ function(); }
#define EVENT_HANDLER_VARIANT(function)				[this](Directus::Variant var)	{ function(var); }
#define EVENT_HANDLER_VARIANT_STATIC(function)		[](Directus::Variant var)		{ function(var); }
#define SUBSCRIBE_TO_EVENT(eventID, function)		Directus::EventSystem::Get().Subscribe(eventID, function);
#define UNSUBSCRIBE_FROM_EVENT(eventID, function)	Directus::EventSystem::Get().Unsubscribe(eventID, function);
#define FIRE_EVENT(eventID)							Directus::EventSystem::Get().Fire(eventID)
#define FIRE_EVENT_DATA(eventID, data)				Directus::EventSystem::Get().Fire(eventID, data)
//==============================================================================================================

namespace Directus
{
	class ENGINE_CLASS EventSystem
	{
	public:
		static EventSystem& Get()
		{
			static EventSystem instance;
			return instance;
		}

		typedef std::function<void(Variant)> subscriber;

		void Subscribe(Event_Type eventID, subscriber&& function)
		{
			m_subscribers[eventID].push_back(std::forward<subscriber>(function));
		}

		void Unsubscribe(Event_Type eventID, subscriber&& function)
		{
			size_t function_adress	= *(long*)(char*)&function;
			auto& subscribers		= m_subscribers[eventID];

			for (auto it = subscribers.begin(); it != subscribers.end();)
			{
				size_t subscriber_adress = *(long*)(char*)&(*it);
				if (subscriber_adress == function_adress)
				{
					it = subscribers.erase(it);
					return;
				}
			}
		}

		void Fire(Event_Type eventID, const Variant& data = 0)
		{
			if (m_subscribers.find(eventID) == m_subscribers.end())
				return;

			for (const auto& subscriber : m_subscribers[eventID])
			{
				subscriber(data);
			}
		}

		void Clear() 
		{
			m_subscribers.clear(); 
		}

	private:
		std::map<Event_Type, std::vector<subscriber>> m_subscribers;
	};
}