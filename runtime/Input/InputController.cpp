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

//= INCLUDES ========
#include "pch.h"
#include "Input.h"
#include <SDL.h>
//===================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    // Controller
    static void* m_controller                     = nullptr;
    static bool m_is_controller_connected         = false;
    static std::string m_controller_name          = "";
    static uint32_t m_controller_index            = 0;
    static Math::Vector2 m_controller_thumb_left  = Math::Vector2::Zero;
    static Math::Vector2 m_controller_thumb_right = Math::Vector2::Zero;
    static float m_controller_trigger_left        = 0.0f;
    static float m_controller_trigger_right       = 0.0f;

    static float get_normalized_axis_value(SDL_GameController* controller, const SDL_GameControllerAxis axis)
    {
        int16_t value = SDL_GameControllerGetAxis(controller, axis);

        // Account for deadzone.
        static const uint16_t deadzone = 8000; // A good default as per SDL_GameController.h
        if ((abs(value) - deadzone) < 0)
        {
            value = 0;
        }
        else
        {
            value -= value > 0 ? deadzone : -deadzone;
        }

        // Compute range
        static const float range_negative = 32768.0f;
        static const float range_positive = 32767.0f;
        float range                       = value < 0 ? range_negative : range_positive;

        // Normalize
        return static_cast<float>(value) / (range - deadzone);
    }

    void Input::PollController()
    {
        SDL_GameController* controller = static_cast<SDL_GameController*>(m_controller);
        if (!m_controller)
            return;

        m_controller_trigger_left  = get_normalized_axis_value(controller, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_TRIGGERLEFT);  // L2
        m_controller_trigger_right = get_normalized_axis_value(controller, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_TRIGGERRIGHT); // R2
        m_controller_thumb_left.x  = get_normalized_axis_value(controller, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_LEFTX);        // LEFT THUMBSTICK
        m_controller_thumb_left.y  = get_normalized_axis_value(controller, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_LEFTY);        // LEFT THUMBSTICK
        m_controller_thumb_right.x = get_normalized_axis_value(controller, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_RIGHTX);       // RIGHT THUMBSTICK
        m_controller_thumb_right.y = get_normalized_axis_value(controller, SDL_GameControllerAxis::SDL_CONTROLLER_AXIS_RIGHTY);       // RIGHT THUMBSTICK
    }

    void Input::OnEventController(void* event_controller)
    {
        // Validate event
        SP_ASSERT(event_controller != nullptr);
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event_controller);
        Uint32 event_type = sdl_event->type;

        // Detect controller
        if (!m_is_controller_connected)
        {
            for (int i = 0; i < SDL_NumJoysticks(); i++)
            {
                if (SDL_IsGameController(i))
                {
                    SDL_GameController* controller = SDL_GameControllerOpen(i);
                    if (SDL_GameControllerGetAttached(controller) == 1)
                    {
                        m_controller              = controller;
                        m_controller_index        = i;
                        m_is_controller_connected = true;
                    }
                    else
                    {
                        SP_LOG_ERROR("Failed to get controller: %s.", SDL_GetError());
                    }
                }
            }

            SDL_GameControllerEventState(SDL_ENABLE);
        }

        // Connected
        if (event_type == SDL_CONTROLLERDEVICEADDED)
        {
            // Get first available controller
            for (uint32_t i = 0; i < static_cast<uint32_t>(SDL_NumJoysticks()); i++)
            {
                if (SDL_IsGameController(i))
                {
                    SDL_GameController* controller = SDL_GameControllerOpen(i);
                    if (SDL_GameControllerGetAttached(controller) == SDL_TRUE)
                    {
                        m_controller              = controller;
                        m_is_controller_connected = true;
                        m_controller_name         = SDL_GameControllerNameForIndex(i);
                        break;
                    }
                }
            }

            if (m_is_controller_connected)
            {
                SP_LOG_INFO("Controller connected \"%s\".", m_controller_name.c_str());
            }
            else
            {
                SP_LOG_ERROR("Failed to get controller: %s.", SDL_GetError());
            }
        }

        // Disconnected
        if (event_type == SDL_CONTROLLERDEVICEREMOVED)
        {
            m_controller              = nullptr;
            m_is_controller_connected = false;
            m_controller_name         = "";

            SP_LOG_INFO("Controller disconnected.");
        }

        // Keys
        if (event_type == SDL_CONTROLLERBUTTONDOWN)
        {
            Uint8 button = sdl_event->cbutton.button;

            m_keys[GetKeyIndexController()]      = button == SDL_CONTROLLER_BUTTON_DPAD_UP;
            m_keys[GetKeyIndexController() + 1]  = button == SDL_CONTROLLER_BUTTON_DPAD_DOWN;
            m_keys[GetKeyIndexController() + 2]  = button == SDL_CONTROLLER_BUTTON_DPAD_LEFT;
            m_keys[GetKeyIndexController() + 3]  = button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
            m_keys[GetKeyIndexController() + 4]  = button == SDL_CONTROLLER_BUTTON_A;
            m_keys[GetKeyIndexController() + 5]  = button == SDL_CONTROLLER_BUTTON_B;
            m_keys[GetKeyIndexController() + 6]  = button == SDL_CONTROLLER_BUTTON_X;
            m_keys[GetKeyIndexController() + 7]  = button == SDL_CONTROLLER_BUTTON_Y;
            m_keys[GetKeyIndexController() + 8]  = button == SDL_CONTROLLER_BUTTON_BACK;
            m_keys[GetKeyIndexController() + 9]  = button == SDL_CONTROLLER_BUTTON_GUIDE;
            m_keys[GetKeyIndexController() + 10] = button == SDL_CONTROLLER_BUTTON_START;
            m_keys[GetKeyIndexController() + 11] = button == SDL_CONTROLLER_BUTTON_LEFTSTICK;
            m_keys[GetKeyIndexController() + 12] = button == SDL_CONTROLLER_BUTTON_RIGHTSTICK;
            m_keys[GetKeyIndexController() + 13] = button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
            m_keys[GetKeyIndexController() + 14] = button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
            m_keys[GetKeyIndexController() + 15] = button == SDL_CONTROLLER_BUTTON_MISC1;
            m_keys[GetKeyIndexController() + 16] = button == SDL_CONTROLLER_BUTTON_PADDLE1;
            m_keys[GetKeyIndexController() + 17] = button == SDL_CONTROLLER_BUTTON_PADDLE2;
            m_keys[GetKeyIndexController() + 18] = button == SDL_CONTROLLER_BUTTON_PADDLE3;
            m_keys[GetKeyIndexController() + 19] = button == SDL_CONTROLLER_BUTTON_PADDLE4;
            m_keys[GetKeyIndexController() + 20] = button == SDL_CONTROLLER_BUTTON_TOUCHPAD;
        }
        else
        {
            for (auto i = GetKeyIndexController(); i <= GetKeyIndexController() + 11; i++)
            {
                m_keys[i] = false;
            }
        }
    }

    bool Input::GamepadVibrate(const float left_motor_speed, const float right_motor_speed)
    {
        if (!m_is_controller_connected)
            return false;

        Uint16 low_frequency_rumble  = static_cast<uint16_t>(Helper::Clamp(left_motor_speed, 0.0f, 1.0f) * 65535);  // Convert [0, 1] to [0, 65535]
        Uint16 high_frequency_rumble = static_cast<uint16_t>(Helper::Clamp(right_motor_speed, 0.0f, 1.0f) * 65535); // Convert [0, 1] to [0, 65535]
        Uint32 duration_ms           = 0xFFFFFFFF;

        if (SDL_GameControllerRumble(static_cast<SDL_GameController*>(m_controller), low_frequency_rumble, high_frequency_rumble, duration_ms) == -1)
        {
            SP_LOG_ERROR("Failed to vibrate controller");
            return false;
        }

        return true;
    }

    bool Input::IsControllerConnected()
    {
        return m_is_controller_connected;
    }

    const Vector2& Input::GetControllerThumbStickLeft()
    {
        return m_controller_thumb_left;
    }

    const Vector2& Input::GetControllerThumbStickRight()
    {
        return m_controller_thumb_right;
    }

    float Input::GetControllerTriggerLeft()
    {
        return m_controller_trigger_left;
    }

    float Input::GetControllerTriggerRight()
    {
        return m_controller_trigger_right;
    }
}
