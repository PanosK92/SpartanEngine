/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include <vector>
#include <array>
#include <functional>
#include "../Core/Variant.h"
//==========================

/*
HOW TO USE
====================================================================================
To subscribe a function to an event     -> SP_SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To unsubscribe a function from an event -> SP_SUBSCRIBE_TO_EVENT(EVENT_ID, Handler);
To fire an event                        -> SP_FIRE_EVENT(EVENT_ID);
To fire an event with data              -> SP_FIRE_EVENT_DATA(EVENT_ID, Variant);

Note: This is a blocking event system
====================================================================================
*/

//= MACROS ===================================================================================================
#define SP_EVENT_HANDLER_EXPRESSION(expression)      [this](const Spartan::Variant& var)    { expression }

#define SP_EVENT_HANDLER(function)                   [this](const Spartan::Variant& var)    { function(); }
#define SP_EVENT_HANDLER_STATIC(function)            [](const Spartan::Variant& var)        { function(); }
                                                     
#define SP_EVENT_HANDLER_VARIANT(function)           [this](const Spartan::Variant& var)    { function(var); }
#define SP_EVENT_HANDLER_VARIANT_STATIC(function)    [](const Spartan::Variant& var)        { function(var); }
                                                     
#define SP_FIRE_EVENT(eventID)                       Spartan::Event::Fire(eventID)
#define SP_FIRE_EVENT_DATA(eventID, data)            Spartan::Event::Fire(eventID, data)
                                                     
#define SP_SUBSCRIBE_TO_EVENT(eventID, function)     Spartan::Event::Subscribe(eventID, function);
//============================================================================================================

enum class EventType
{
    // Renderer
    RendererOnFirstFrameCompleted,
    RendererPostPresent,
    RendererOnShutdown,
    // World
    WorldSaveStart, // The world is about to be saved to a file
    WorldSavedEnd,  // The world finished saving to file
    WorldLoadStart, // The world is about to be loaded from a file
    WorldLoadEnd,   // The world finished loading from file
    WorldClear,     // The world is about to clear everything
    WorldResolve,   // The world is resolving
    WorldResolved,  // The world has finished resolving
    // SDL
    EventSDL, // An SDL event
    // Window
    WindowOnFullScreenToggled
};

namespace Spartan
{
    using subscriber = std::function<void(const Variant&)>;

    class SP_CLASS Event
    {
    public:
        static void Shutdown();
        static void Subscribe(const EventType event_id, subscriber&& function);
        static void Fire(const EventType event_id, const Variant& data = 0);

    private:
        static std::array<std::vector<subscriber>, 12> m_event_subscribers;
    };
}
