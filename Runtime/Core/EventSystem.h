/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include <unordered_map>
#include <vector>
#include <functional>
#include "../Core/Variant.h"
//==========================

/*
HOW TO USE
=================================================================================
To subscribe a function to an event        -> SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To unsubscribe a function from an event    -> SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To fire an event                        -> FIRE_EVENT(EVENT_ID);
To fire an event with data                -> FIRE_EVENT_DATA(EVENT_ID, Variant);

Note: Currently, this is a blocking event system
=================================================================================
*/

enum class EventType
{
    FrameEnd,                // A frame ends
    WindowData,             // The window has a message for processing
    WorldSave,                // The world must be saved to file
    WorldSaved,                // The world finished saving to file
    WorldLoad,                // The world must be loaded from file
    WorldLoaded,            // The world finished loading from file
    WorldUnload,            // The world should clear everything
    WorldResolve,            // The world should resolve
    WorldResolved,            // The world has finished resolving
    WorldStop,                // The world should stop ticking
    WorldStart,                // The world should start ticking
    FrameResolutionChanged
};

//= MACROS ====================================================================================================
#define EVENT_HANDLER_EXPRESSION(expression)        [this](const Spartan::Variant& var)    { ##expression }
#define EVENT_HANDLER_EXPRESSION_STATIC(expression)    [](const Spartan::Variant& var)        { ##expression }

#define EVENT_HANDLER(function)                        [this](const Spartan::Variant& var)    { function(); }
#define EVENT_HANDLER_STATIC(function)                [](const Spartan::Variant& var)        { function(); }

#define EVENT_HANDLER_VARIANT(function)                [this](const Spartan::Variant& var)    { function(var); }
#define EVENT_HANDLER_VARIANT_STATIC(function)        [](const Spartan::Variant& var)        { function(var); }

#define FIRE_EVENT(eventID)                            Spartan::EventSystem::Get().Fire(eventID)
#define FIRE_EVENT_DATA(eventID, data)                Spartan::EventSystem::Get().Fire(eventID, data)

#define SUBSCRIBE_TO_EVENT(eventID, function)        Spartan::EventSystem::Get().Subscribe(eventID, function);
#define UNSUBSCRIBE_FROM_EVENT(eventID, function)    Spartan::EventSystem::Get().Unsubscribe(eventID, function);
//=============================================================================================================

namespace Spartan
{
    using subscriber = std::function<void(const Variant&)>;

    class SPARTAN_CLASS EventSystem
    {
    public:
        static EventSystem& Get()
        {
            static EventSystem instance;
            return instance;
        }

        void Subscribe(const EventType event_id, subscriber&& function)
        {
            m_subscribers[event_id].push_back(std::forward<subscriber>(function));
        }

        void Unsubscribe(const EventType event_id, subscriber&& function)
        {
            const size_t function_adress    = *reinterpret_cast<long*>(reinterpret_cast<char*>(&function));
            auto& subscribers                = m_subscribers[event_id];

            for (auto it = subscribers.begin(); it != subscribers.end();)
            {
                const size_t subscriber_adress = *reinterpret_cast<long*>(reinterpret_cast<char*>(&(*it)));
                if (subscriber_adress == function_adress)
                {
                    it = subscribers.erase(it);
                    return;
                }
            }
        }

        void Fire(const EventType event_id, const Variant& data = 0)
        {
            if (m_subscribers.find(event_id) == m_subscribers.end())
                return;

            for (const auto& subscriber : m_subscribers[event_id])
            {
                subscriber(data);
            }
        }

        void Clear() 
        {
            m_subscribers.clear(); 
        }

    private:
        std::unordered_map<EventType, std::vector<subscriber>> m_subscribers;
    };
}
