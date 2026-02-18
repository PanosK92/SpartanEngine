/*
Copyright(c) 2015-2026 Panos Karabelas

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
    array<bool, 107> Input::m_keys;
    array<bool, 107> m_keys_previous_frame;
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

    void Input::CheckDeviceState(void* event, Controller* controller)
    {
        // cast event to sdl_event
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event);
        uint32_t event_type  = sdl_event->type;
    
        // handle device connection
        if (!controller->is_connected && (event_type == SDL_EVENT_GAMEPAD_ADDED || event_type == SDL_EVENT_JOYSTICK_ADDED))
        {
            int num_joysticks;
            SDL_JoystickID* joysticks = SDL_GetJoysticks(&num_joysticks);
            if (!joysticks)
            {
                SP_LOG_ERROR("failed to get joysticks: %s", SDL_GetError());
                return;
            }
    
            for (int i = 0; i < num_joysticks; i++)
            {
                SDL_JoystickID instance_id = joysticks[i];
                SDL_Joystick* joystick = SDL_OpenJoystick(instance_id);
                if (!joystick)
                {
                    SP_LOG_ERROR("failed to open joystick %d: %s", instance_id, SDL_GetError());
                    continue;
                }
    
                // get device name and type
                const char* name_ptr = SDL_GetJoystickName(joystick);
                string name          = name_ptr ? name_ptr : "";
                transform(name.begin(), name.end(), name.begin(), ::tolower);
                SDL_JoystickType joystick_type = SDL_GetJoystickType(joystick);
    
                // determine if this is a wheel or gamepad
                bool is_wheel                = (joystick_type == SDL_JOYSTICK_TYPE_WHEEL || name.find("wheel") != string::npos);
                ControllerType detected_type = is_wheel ? ControllerType::SteeringWheel : ControllerType::Gamepad;

                // skip if detected type doesn't match what we're looking for
                if (detected_type != controller->type)
                {
                    SDL_CloseJoystick(joystick);
                    continue;
                }
    
                // for gamepads, try to open as sdl_gamepad
                if (detected_type == ControllerType::Gamepad && SDL_IsGamepad(instance_id))
                {
                    SDL_Gamepad* gamepad = SDL_OpenGamepad(instance_id);
                    if (!gamepad)
                    {
                        SP_LOG_ERROR("failed to open gamepad %d: %s", instance_id, SDL_GetError());
                        SDL_CloseJoystick(joystick);
                        continue;
                    }

                    // close joystick since we're using gamepad
                    SDL_CloseJoystick(joystick);
                    controller->sdl_pointer = gamepad;
                }
                else
                {
                    // for wheels, keep the joystick handle
                    controller->sdl_pointer = joystick;
                }
    
                // set controller properties
                controller->instance_id  = instance_id;
                controller->is_connected = true;
                controller->name         = name;
                controller->type         = detected_type;
    
                SP_LOG_INFO("controller connected: \"%s\" (type: %s)", name.c_str(), is_wheel ? "steering wheel" : "gamepad");
                break;
            }
            SDL_free(joysticks);
    
            // enable events for both gamepads and joysticks
            SDL_SetGamepadEventsEnabled(true);
            SDL_SetJoystickEventsEnabled(true);
        }
    
        // handle device disconnection
        if (controller->is_connected && (event_type == SDL_EVENT_GAMEPAD_REMOVED || event_type == SDL_EVENT_JOYSTICK_REMOVED))
        {
            SDL_JoystickID event_instance_id = (event_type == SDL_EVENT_GAMEPAD_REMOVED) ? sdl_event->gdevice.which : sdl_event->jdevice.which;
            if (controller->instance_id == event_instance_id)
            {
                SP_LOG_INFO("controller disconnected: \"%s\"", controller->name.c_str());
    
                if (controller->sdl_pointer)
                {
                    if (controller->type == ControllerType::Gamepad)
                    {
                        SDL_CloseGamepad(static_cast<SDL_Gamepad*>(controller->sdl_pointer));
                    }
                    else
                    {
                        SDL_CloseJoystick(static_cast<SDL_Joystick*>(controller->sdl_pointer));
                    }
                }
    
                controller->sdl_pointer  = nullptr;
                controller->instance_id  = 0;
                controller->is_connected = false;
                controller->name         = "";
                controller->type         = ControllerType::Max;
            }
        }
    }
}
