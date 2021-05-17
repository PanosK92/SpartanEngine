/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES =======
#include "Spartan.h"
#include "Input.h"
#include "SDL.h"
//==================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Input::Input(Context* context) : ISubsystem(context)
    {
        // Initialise events subsystem (if needed)
        if (SDL_WasInit(SDL_INIT_EVENTS) != 1)
        {
            if (SDL_InitSubSystem(SDL_INIT_EVENTS) != 0)
            {
                LOG_ERROR("Failed to initialise SDL events subsystem: %s.", SDL_GetError());
                return;
            }
        }

        // Initialise controller subsystem (if needed)
        if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 1)
        {
            if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
            {
                LOG_ERROR("Failed to initialise SDL events subsystem: %s.", SDL_GetError());
                return;
            }
        }

        m_keys.fill(false);
        m_keys_previous_frame.fill(false);

        // Get events from the main Window's event processing loop
        SUBSCRIBE_TO_EVENT(EventType::EventSDL, EVENT_HANDLER_VARIANT(OnEvent));
    }

    void Input::OnTick(float delta_time)
    {
        m_keys_previous_frame = m_keys;

        PollMouse();
        PollKeyboard();
    }

    void Input::OnPostTick()
    {
        m_mouse_wheel_delta = Vector2::Zero;
    }

    void Input::OnEvent(const Variant& event_variant)
    {
        SDL_Event* event_sdl    = event_variant.Get<SDL_Event*>();
        Uint32 event_type       = event_sdl->type;

        if (event_type == SDL_MOUSEWHEEL)
        {
            OnEventMouse(event_sdl);
        }

        if (event_type == SDL_CONTROLLERAXISMOTION ||
            event_type == SDL_CONTROLLERBUTTONDOWN ||
            event_type == SDL_CONTROLLERBUTTONUP ||
            event_type == SDL_CONTROLLERDEVICEADDED ||
            event_type == SDL_CONTROLLERDEVICEREMOVED ||
            event_type == SDL_CONTROLLERDEVICEREMAPPED ||
            event_type == SDL_CONTROLLERTOUCHPADDOWN ||
            event_type == SDL_CONTROLLERTOUCHPADMOTION ||
            event_type == SDL_CONTROLLERTOUCHPADUP ||
            event_type == SDL_CONTROLLERSENSORUPDATE)
        {
            OnEventController(event_sdl);
        }
    }
}
