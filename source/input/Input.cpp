/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
    array<bool, Input::key_count> Input::m_keys;
    bool Input::m_blocked_by_ui = false;

    namespace
    {
        // GetKeyDown and GetKeyUp compare against this
        array<bool, Input::key_count> keys_previous_frame;
    }

    void Input::Initialize()
    {
        m_keys.fill(false);
        keys_previous_frame.fill(false);

        // get events from the main window's event processing loop
        SP_SUBSCRIBE_TO_EVENT(EventType::Sdl, SP_EVENT_HANDLER_VARIANT_STATIC(OnEvent));
    }

    void Input::Tick()
    {
        keys_previous_frame = m_keys;

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
        if (m_blocked_by_ui)
        {
            return false;
        }

        return m_keys[static_cast<uint32_t>(key)];
    }

    bool Input::GetKeyDown(const KeyCode key)
    {
        if (m_blocked_by_ui)
        {
            return false;
        }

        return GetKey(key) && !keys_previous_frame[static_cast<uint32_t>(key)];
    }

    bool Input::GetKeyUp(const KeyCode key)
    {
        if (m_blocked_by_ui)
        {
            return false;
        }

        return !GetKey(key) && keys_previous_frame[static_cast<uint32_t>(key)];
    }

    void Input::SetBlockedByUi(bool blocked)
    {
        if (blocked && !m_blocked_by_ui)
        {
            GamepadStopFeedback();
        }
        m_blocked_by_ui = blocked;
    }

    bool Input::IsBlockedByUi()
    {
        return m_blocked_by_ui;
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

                // An unmapped joystick is not an SDL_Gamepad handle.
                if (!is_wheel && !SDL_IsGamepad(instance_id))
                {
                    SDL_CloseJoystick(joystick);
                    continue;
                }

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
