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

#pragma once

//= INCLUDES ===============
#include <map>
#include <vector>
#include <functional>
#include "../Core/Variant.h"
//==========================

/*
HOW TO USE
=============================================================================
To subscribe a function to an event	-> SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To fire an event					-> FIRE_EVENT(EVENT_ID);
To fire an event with data			-> FIRE_EVENT_DATA(EVENT_ID, Variant)
=============================================================================
*/

//= EVENTS =================================================================================
#define EVENT_FRAME_START			0	// Fired when a new frame begins
#define EVENT_FRAME_END				1	// Fired when a frame ends

#define EVENT_TICK					2	// Fired when the most engine subsystems should tick
#define EVENT_RENDER				3	// Fired when the Renderer should start rendering

#define EVENT_SCENE_SAVED			4	// Fired when the Scene finished saving to file
#define EVENT_SCENE_LOADED			5	// Fired when the Scene finished loading from file
#define EVENT_SCENE_UNLOAD			6	// Fired when the Scene should clear everything
#define EVENT_SCENE_RESOLVE_START	7	// Signifies that the scene should resolve
#define EVENT_SCENE_RESOLVE_END		8	// Signifies that the scene just finished resolving

#define EVENT_MODEL_LOADED			9	// Fired when the ModelImporter finished loading
//==========================================================================================

//= MACROS ===============================================================================================
#define EVENT_HANDLER_STATIC(function)			[](Directus::Variant var)		{ function(); }
#define EVENT_HANDLER(function)					[this](Directus::Variant var)	{ function(); }
#define EVENT_HANDLER_VARIANT(function)			[this](Directus::Variant var)	{ function(var); }
#define EVENT_HANDLER_VARIANT_STATIC(function)	[](Directus::Variant var)		{ function(var); }
#define SUBSCRIBE_TO_EVENT(eventID, function)	Directus::EventSystem::Get().Subscribe(eventID, function);
#define FIRE_EVENT(eventID)						Directus::EventSystem::Get().Fire(eventID)
#define FIRE_EVENT_DATA(eventID, data)			Directus::EventSystem::Get().Fire(eventID, data)
//========================================================================================================

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

		void Subscribe(int eventID, subscriber&& func)
		{
			m_subscribers[eventID].push_back(std::forward<subscriber>(func));
		}

		void Fire(int eventID, const Variant& data = 0)
		{
			if (m_subscribers.find(eventID) == m_subscribers.end())
				return;

			for (const auto& subscriber : m_subscribers[eventID])
			{
				subscriber(data);
			}
		}
		void Clear() { m_subscribers.clear(); }

	private:
		std::map<uint8_t, std::vector<subscriber>> m_subscribers;
	};
}