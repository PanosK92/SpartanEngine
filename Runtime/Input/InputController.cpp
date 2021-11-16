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
    void Input::OnEventController(void* event_controller)
    {
        // Validate event
        SP_ASSERT(event_controller != nullptr);
        SDL_Event* sdl_event = static_cast<SDL_Event*>(event_controller);
        Uint32 event_type = sdl_event->type;

        // Detect controller
        if (!m_controller_connected)
        {
            for (int i = 0; i < SDL_NumJoysticks(); i++)
            {
                if (SDL_IsGameController(i))
                {
                    SDL_GameController* controller = SDL_GameControllerOpen(i);
                    if (SDL_GameControllerGetAttached(controller) == 1)
                    {
                        m_controller = controller;
                        m_controller_connected = true;
                    }
                    else
                    {
                        LOG_ERROR("Failed to get controller: %s.", SDL_GetError());
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
                        m_controller            = controller;
                        m_controller_connected  = true;
                        break;
                    }
                }
            }

            if (m_controller_connected)
            {
                LOG_INFO("Controller connected.");
            }
            else
            {
                LOG_ERROR("Failed to get controller: %s.", SDL_GetError());
            }
        }

        // Disconnected
        if (event_type == SDL_CONTROLLERDEVICEREMOVED)
        {
            m_controller = nullptr;
            m_controller_connected = false;
            LOG_INFO("Controller disconnected.");
        }

        // Keys
        if (event_type == SDL_CONTROLLERBUTTONDOWN)
        {
            Uint8 button = sdl_event->cbutton.button;

            m_keys[start_index_gamepad]         = button == SDL_CONTROLLER_BUTTON_DPAD_UP;
            m_keys[start_index_gamepad + 1]     = button == SDL_CONTROLLER_BUTTON_DPAD_DOWN;
            m_keys[start_index_gamepad + 2]     = button == SDL_CONTROLLER_BUTTON_DPAD_LEFT;
            m_keys[start_index_gamepad + 3]     = button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
            m_keys[start_index_gamepad + 4]     = button == SDL_CONTROLLER_BUTTON_A;
            m_keys[start_index_gamepad + 5]     = button == SDL_CONTROLLER_BUTTON_B;
            m_keys[start_index_gamepad + 6]     = button == SDL_CONTROLLER_BUTTON_X;
            m_keys[start_index_gamepad + 7]     = button == SDL_CONTROLLER_BUTTON_Y;
            m_keys[start_index_gamepad + 8]     = button == SDL_CONTROLLER_BUTTON_BACK;
            m_keys[start_index_gamepad + 9]     = button == SDL_CONTROLLER_BUTTON_GUIDE;
            m_keys[start_index_gamepad + 10]    = button == SDL_CONTROLLER_BUTTON_START;
            m_keys[start_index_gamepad + 11]    = button == SDL_CONTROLLER_BUTTON_LEFTSTICK;
            m_keys[start_index_gamepad + 12]    = button == SDL_CONTROLLER_BUTTON_RIGHTSTICK;
            m_keys[start_index_gamepad + 13]    = button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
            m_keys[start_index_gamepad + 14]    = button == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
            m_keys[start_index_gamepad + 15]    = button == SDL_CONTROLLER_BUTTON_MISC1;
            m_keys[start_index_gamepad + 16]    = button == SDL_CONTROLLER_BUTTON_PADDLE1;
            m_keys[start_index_gamepad + 17]    = button == SDL_CONTROLLER_BUTTON_PADDLE2;
            m_keys[start_index_gamepad + 18]    = button == SDL_CONTROLLER_BUTTON_PADDLE3;
            m_keys[start_index_gamepad + 19]    = button == SDL_CONTROLLER_BUTTON_PADDLE4;
            m_keys[start_index_gamepad + 20]    = button == SDL_CONTROLLER_BUTTON_TOUCHPAD;
        }
        else
        {
            for (auto i = start_index_gamepad; i <= start_index_gamepad + 11; i++)
            {
                m_keys[i] = false;
            }
        }

        // Axes
        if (event_type == SDL_CONTROLLERAXISMOTION)
        {
            SDL_ControllerAxisEvent event_axis = sdl_event->caxis;

            switch (event_axis.axis)
            {
            case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
                m_controller_trigger_left = static_cast<float>(event_axis.value) / 32768.0f;
                break;
            case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
                m_gamepad_trigger_right = static_cast<float>(event_axis.value) / 32768.0f;
                break;
            case SDL_CONTROLLER_AXIS_LEFTX:
                m_controller_thumb_left.x = static_cast<float>(event_axis.value) / 32768.0f;
                break;
            case SDL_CONTROLLER_AXIS_LEFTY:
                m_controller_thumb_left.y = static_cast<float>(event_axis.value) / 32768.0f;
                break;
            case SDL_CONTROLLER_AXIS_RIGHTX:
                m_controller_thumb_right.x = static_cast<float>(event_axis.value) / 32768.0f;
                break;
            case SDL_CONTROLLER_AXIS_RIGHTY:
                m_controller_thumb_right.y = static_cast<float>(event_axis.value) / 32768.0f;
                break;
            }
        }
    }

    bool Input::GamepadVibrate(const float left_motor_speed, const float right_motor_speed) const
    {
        if (!m_controller_connected)
            return false;

        Uint16 low_frequency_rumble     = static_cast<uint16_t>(Helper::Clamp(left_motor_speed, 0.0f, 1.0f) * 65535);    // Convert [0, 1] to [0, 65535]
        Uint16 high_frequency_rumble    = static_cast<uint16_t>(Helper::Clamp(right_motor_speed, 0.0f, 1.0f) * 65535);   // Convert [0, 1] to [0, 65535]
        Uint32 duration_ms              = 0xFFFFFFFF;

        if (SDL_GameControllerRumble(static_cast<SDL_GameController*>(m_controller), low_frequency_rumble, high_frequency_rumble, duration_ms) == -1)
        {
            LOG_ERROR("Failed to vibrate controller");
            return false;
        }

        return true;
    }
}
