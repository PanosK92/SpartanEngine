/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "pch.h"
#include "Input.h"
#include <SDL.h>
//==================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

// These need to be included on Windows or SDL will throw a bunch of linking errors.
#ifdef _MSC_VER
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")
#endif

namespace Spartan
{
    // Keys
    std::array<bool, 107> Input::m_keys;
    std::array<bool, 107> m_keys_previous_frame;
    uint32_t Input::m_start_index_mouse      = 83;
    uint32_t Input::m_start_index_controller = 86;

    void Input::Initialize()
    {
        // Initialise events subsystem (if needed)
        if (SDL_WasInit(SDL_INIT_EVENTS) != 1)
        {
            if (SDL_InitSubSystem(SDL_INIT_EVENTS) != 0)
            {
                SP_LOG_ERROR("Failed to initialise SDL events subsystem: %s.", SDL_GetError());
                return;
            }
        }

        // Initialise controller subsystem (if needed)
        if (SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 1)
        {
            if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
            {
                SP_LOG_ERROR("Failed to initialise SDL events subsystem: %s.", SDL_GetError());
                return;
            }
        }

        m_keys.fill(false);
        m_keys_previous_frame.fill(false);

        // Get events from the main Window's event processing loop
        SP_SUBSCRIBE_TO_EVENT(EventType::Sdl, SP_EVENT_HANDLER_VARIANT_STATIC(OnEvent));
    }

    void Input::Tick()
    {
        m_keys_previous_frame = m_keys;

        PollMouse();
        PollKeyboard();
        PollController();
    }

    void Input::OnEvent(sp_variant data)
    {
        SDL_Event* event_sdl = static_cast<SDL_Event*>(get<void*>(data));
        Uint32 event_type    = event_sdl->type;

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

    bool Input::GetKey(const KeyCode key)
    {
        return m_keys[static_cast<uint32_t>(key)];
    }

    bool Input::GetKeyDown(const KeyCode key)
    {
        return GetKey(key) && !m_keys_previous_frame[static_cast<uint32_t>(key)];
    }

    bool Input::GetKeyUp(const KeyCode key)
    {
        return !GetKey(key) && m_keys_previous_frame[static_cast<uint32_t>(key)];
    }

    array<bool, 107>& Input::GetKeys()
    {
        return m_keys;
    }

    uint32_t Input::GetKeyIndexMouse()
    {
        return m_start_index_mouse;
    }

    uint32_t Input::GetKeyIndexController()
    {
        return m_start_index_controller;
    }
}
