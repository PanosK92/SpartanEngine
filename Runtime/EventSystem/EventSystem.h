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

//= INCLUDES ===============
#include <map>
#include <vector>
#include <functional>
#include "../Core/Variant.h"
//==========================

/*
HOW TO USE
=================================================================================================
To subscribe a function to an event						-> SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To fire an event										-> FIRE_EVENT(EVENT_ID);
To fire an event with data								-> FIRE_EVENT_DATA(EVENT_ID, Variant)
==================================================================================================
*/

//= EVENTS =================================================================================
#define EVENT_UPDATE			0	// Fired when the engine should update
#define EVENT_RENDER			1	// Fired when it's time to do rendering
#define EVENT_SCENE_SAVED		2
#define EVENT_SCENE_LOADED		3
#define EVENT_SCENE_UPDATED		4	// Fired when the scene resolves renderables
#define EVENT_SCENE_CLEARED		5	// Fired when the scene resolves renderables
#define EVENT_MODEL_LOADED		6
//==========================================================================================

//= MACROS =======================================================================================
#define EVENT_HANDLER_STATIC(function)					[](Variant var) { function(); }
#define EVENT_HANDLER(function)							[this](Variant var) { function(); }
#define EVENT_HANDLER_VARIANT(function)					[this](Variant var) { function(var); }
#define EVENT_HANDLER_VARIANT_STATIC(function)			[](Variant var) { function(var); }

#define SUBSCRIBE_TO_EVENT(eventID, function)			EventSystem::Subscribe(eventID, function);

#define FIRE_EVENT(eventID)								EventSystem::Fire(eventID)
#define FIRE_EVENT_DATA(eventID, data)					EventSystem::Fire(eventID, data)
//================================================================================================

namespace Directus
{
	class ENGINE_CLASS EventSystem
	{
	public:
		typedef std::function<void(Variant)> subscriber;

		static void Subscribe(int eventID, subscriber&& func) { m_subscribers[eventID].push_back(std::forward<subscriber>(func)); }
		static void Fire(int eventID, Variant data = 0);
		static void Clear() { m_subscribers.clear(); }

	private:
		static std::map<uint8_t, std::vector<subscriber>> m_subscribers;
	};
}