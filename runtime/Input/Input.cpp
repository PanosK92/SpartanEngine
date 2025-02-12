/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ========
#include "pch.h"
#include "Input.h"
SP_WARNINGS_OFF
#include <SDL3/SDL.h>
SP_WARNINGS_ON
//===================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    // keys
    std::array<bool, 107> Input::m_keys;
    std::array<bool, 107> m_keys_previous_frame;
    uint32_t Input::m_start_index_mouse   = 83;
    uint32_t Input::m_start_index_gamepad = 86;

    void Input::Initialize()
    {
        m_keys.fill(false);
        m_keys_previous_frame.fill(false);

        // get events from the main window's event processing loop
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

    uint32_t Input::GetKeyIndexGamepad()
    {
        return m_start_index_gamepad;
    }

    void Input::CheckGamepadState(uint32_t event_type, Controller* controller, ControllerType type_to_detect)
    {
        // connected
        if (!controller->is_connected && event_type == SDL_EVENT_GAMEPAD_ADDED)
        {
            int num_joysticks;
            SDL_JoystickID* joysticks = SDL_GetJoysticks(&num_joysticks);
            if (joysticks)
            {
                for (int i = 0; i < num_joysticks; i++)
                {
                    SDL_JoystickID instance_id = joysticks[i];
                    if (SDL_IsGamepad(instance_id))
                    {
                        SDL_Gamepad* controller_candidate = SDL_OpenGamepad(instance_id);
                        if (controller_candidate)
                        {
                            const char* name_ptr = SDL_GetGamepadName(controller_candidate);
                            string name = name_ptr ? name_ptr : "";
                            transform(name.begin(), name.end(), name.begin(), ::tolower);
    
                            bool is_wheel = name.find("wheel") != string::npos;
                            
                            // Check if the controller type matches what we're looking for
                            if ((type_to_detect == ControllerType::Gamepad && is_wheel) ||
                                (type_to_detect == ControllerType::SteeringWheel && !is_wheel))
                            {
                                SDL_CloseGamepad(controller_candidate);
                                continue;
                            }
    
                            controller->sdl_pointer  = controller_candidate;
                            controller->instance_id  = instance_id;
                            controller->is_connected = true;
                            controller->name         = name;
    
                            SP_LOG_INFO("Controller connected \"%s\"", controller->name.c_str());
                            break;
                        }
                        else
                        {
                            SP_LOG_ERROR("Failed to open gamepad: %s", SDL_GetError());
                        }
                    }
                }
                SDL_free(joysticks);
            }
            SDL_SetGamepadEventsEnabled(true);
        }
    
        // Disconnected
        if (controller->is_connected && event_type == SDL_EVENT_GAMEPAD_REMOVED)
        {
            SP_LOG_INFO("Controller disconnected \"%s\"", controller->name.c_str());
            
            if (controller->sdl_pointer)
            {
                SDL_CloseGamepad(static_cast<SDL_Gamepad*>(controller->sdl_pointer));
            }
            
            controller->sdl_pointer  = nullptr;
            controller->instance_id  = 0;
            controller->is_connected = false;
            controller->name         = "";
        }
    }

    float Input::GetNormalizedAxisValue(void* controller, const uint32_t axis)
    {
        int16_t value = SDL_GetGamepadAxis(static_cast<SDL_Gamepad*>(controller), static_cast<SDL_GamepadAxis>(axis));

        // account for deadzone
        static const uint16_t deadzone = 8000; // a good default as per SDL_Gamepad.h
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
