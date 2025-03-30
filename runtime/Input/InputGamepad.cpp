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
    namespace
    { 
        Controller gamepad;
        math::Vector2 controller_thumb_left  = math::Vector2::Zero;
        math::Vector2 controller_thumb_right = math::Vector2::Zero;
        float controller_trigger_left        = 0.0f;
        float controller_trigger_right       = 0.0f;
    }

    void Input::PollGamepad()
    {
        if (!gamepad.is_connected)
            return;
    
        SDL_Gamepad* sdl_gamepad = static_cast<SDL_Gamepad*>(gamepad.sdl_pointer);
    
        // analog inputs
        controller_trigger_left  = GetNormalizedAxisValue(sdl_gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        controller_trigger_right = GetNormalizedAxisValue(sdl_gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        controller_thumb_left.x  = GetNormalizedAxisValue(sdl_gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_LEFTX);
        controller_thumb_left.y  = GetNormalizedAxisValue(sdl_gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_LEFTY);
        controller_thumb_right.x = GetNormalizedAxisValue(sdl_gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_RIGHTX);
        controller_thumb_right.y = GetNormalizedAxisValue(sdl_gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_RIGHTY);
    
        //button states
        m_keys[GetKeyIndexGamepad()]      = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        m_keys[GetKeyIndexGamepad() + 1]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        m_keys[GetKeyIndexGamepad() + 2]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        m_keys[GetKeyIndexGamepad() + 3]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        m_keys[GetKeyIndexGamepad() + 4]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        m_keys[GetKeyIndexGamepad() + 5]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_EAST);
        m_keys[GetKeyIndexGamepad() + 6]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_WEST);
        m_keys[GetKeyIndexGamepad() + 7]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_NORTH);
        m_keys[GetKeyIndexGamepad() + 8]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_BACK);
        m_keys[GetKeyIndexGamepad() + 9]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
        m_keys[GetKeyIndexGamepad() + 10] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_START);
        m_keys[GetKeyIndexGamepad() + 11] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
        m_keys[GetKeyIndexGamepad() + 12] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        m_keys[GetKeyIndexGamepad() + 13] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        m_keys[GetKeyIndexGamepad() + 14] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        m_keys[GetKeyIndexGamepad() + 15] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_MISC1);
        m_keys[GetKeyIndexGamepad() + 16] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);
        m_keys[GetKeyIndexGamepad() + 17] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1);
        m_keys[GetKeyIndexGamepad() + 18] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2);
        m_keys[GetKeyIndexGamepad() + 19] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2);
        m_keys[GetKeyIndexGamepad() + 20] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
    }

    void Input::OnEventGamepad(void* event)
    {
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event);
        uint32_t event_type  = sdl_event->type;
        CheckGamepadState(event_type, &gamepad, ControllerType::Gamepad);
    }

    bool Input::GamepadVibrate(const float left_motor_speed, const float right_motor_speed)
    {
        if (!gamepad.is_connected)
            return false;

        Uint16 low_frequency_rumble  = static_cast<uint16_t>(clamp(left_motor_speed, 0.0f, 1.0f) * 65535);  // convert [0, 1] to [0, 65535]
        Uint16 high_frequency_rumble = static_cast<uint16_t>(clamp(right_motor_speed, 0.0f, 1.0f) * 65535); // convert [0, 1] to [0, 65535]
        Uint32 duration_ms           = 0xFFFFFFFF;

        if (!SDL_RumbleGamepad(static_cast<SDL_Gamepad*>(gamepad.sdl_pointer), low_frequency_rumble, high_frequency_rumble, duration_ms))
        {
            SP_LOG_ERROR("Failed to vibrate controller");
            return false;
        }

        return true;
    }

    bool Input::IsGamepadConnected()
    {
        return gamepad.is_connected;
    }

    const Vector2& Input::GetGamepadThumbStickLeft()
    {
        return controller_thumb_left;
    }

    const Vector2& Input::GetGamepadThumbStickRight()
    {
        return controller_thumb_right;
    }

    float Input::GetGamepadTriggerLeft()
    {
        return controller_trigger_left;
    }

    float Input::GetGamepadTriggerRight()
    {
        return controller_trigger_right;
    }
}
