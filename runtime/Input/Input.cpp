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
#include <SDL/SDL.h>
//==================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

// these need to be included on windows or sdl will throw a bunch of linking errors
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
        // initialise events subsystem (if needed)
        if (SDL_WasInit(SDL_INIT_EVENTS) != 1)
        {
            if (SDL_InitSubSystem(SDL_INIT_EVENTS) != 0)
            {
                SP_LOG_ERROR("Failed to initialise SDL events subsystem: %s.", SDL_GetError());
                return;
            }
        }

        // initialise controller subsystem (if needed)
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

        // get events from the main Window's event processing loop
        SP_SUBSCRIBE_TO_EVENT(EventType::Sdl, SP_EVENT_HANDLER_VARIANT_STATIC(OnEvent));
    }

    void Input::Tick()
    {
        m_keys_previous_frame = m_keys;

        PollMouse();
        PollKeyboard();
        PollGamepad();
        PollSteeringWheel();
    }

    void Input::OnEvent(sp_variant data)
    {
        SDL_Event* event_sdl = static_cast<SDL_Event*>(get<void*>(data));

        OnEventMouse(event_sdl);
        OnEventGamepad(event_sdl);
        OnEventSteeringWheel(event_sdl);
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

    void Input::CheckControllerState(uint32_t event_type, Controller* controller, ControllerType type_to_detect)
    {
        // connected
        if (!controller->is_connected && event_type == SDL_CONTROLLERDEVICEADDED)
        {
            for (int i = 0; i < SDL_NumJoysticks(); i++)
            {
                if (SDL_IsGameController(i))
                {
                    SDL_GameController* controller_candidate = SDL_GameControllerOpen(i);
                    if (SDL_GameControllerGetAttached(controller_candidate) == SDL_TRUE)
                    {
                        string name = SDL_GameControllerNameForIndex(i);
                        transform(name.begin(), name.end(), name.begin(), ::tolower);

                        bool is_wheel = name.find("wheel") != string::npos;
                        if (type_to_detect == ControllerType::Gamepad && is_wheel)
                            continue;

                        if (type_to_detect == ControllerType::SteeringWheel && !is_wheel)
                            continue;

                        controller->sdl_pointer  = controller_candidate;
                        controller->index        = i;
                        controller->is_connected = true;
                        controller->name         = SDL_GameControllerNameForIndex(i);

                        SP_LOG_INFO("Controller connected \"%s\".", controller->name.c_str());
                        break;
                    }
                    else
                    {
                        SP_LOG_ERROR("Failed to get controller: %s.", SDL_GetError());
                    }
                }
            }

            SDL_GameControllerEventState(SDL_ENABLE);
        }

        // disconnected
        if (controller->is_connected && event_type == SDL_CONTROLLERDEVICEREMOVED)
        {
            SP_LOG_INFO("Controller disconnected \"%s\".", controller->name.c_str());

            controller->sdl_pointer  = nullptr;
            controller->index        = 0;
            controller->is_connected = false;
            controller->name         = "";
        }
    }

    float Input::GetNormalizedAxisValue(void* controller, const uint32_t axis)
    {
        int16_t value = SDL_GameControllerGetAxis(static_cast<SDL_GameController*>(controller), static_cast<SDL_GameControllerAxis>(axis));

        // account for deadzone
        static const uint16_t deadzone = 8000; // a good default as per SDL_GameController.h
        if ((abs(value) - deadzone) < 0)
        {
            value = 0;
        }
        else
        {
            value -= value > 0 ? deadzone : -deadzone;
        }

        // compute range
        const float range_negative = 32768.0f;
        const float range_positive = 32767.0f;
        float range = value < 0 ? range_negative : range_positive;

        // normalize
        return static_cast<float>(value) / (range - deadzone);
    }
}
