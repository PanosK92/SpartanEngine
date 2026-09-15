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
    namespace
    { 
        Controller gamepad;
        math::Vector2 controller_thumb_left  = math::Vector2::Zero;
        math::Vector2 controller_thumb_right = math::Vector2::Zero;
        float controller_trigger_left        = 0.0f;
        float controller_trigger_right       = 0.0f;
        bool feedback_active = false;
        bool effects_failed = false;
        Uint64 feedback_refreshed = 0;
        Uint64 feedback_sent = 0;
        std::array<Uint8, 47> last_effect = {};

        // SDL's PS5 effect payload (test/testcontroller.c), without transport headers.
        // SDL supplies USB/Bluetooth framing and CRC. Trigger zones follow
        // https://github.com/nowrep/dualsensectl/blob/main/main.c (trigger_bitpacking_array).
        void encode_trigger(Uint8* effect, float resistance, bool pulse)
        {
            effect[0] = 0x05; // explicitly release the actuator
            if (!std::isfinite(resistance) || resistance <= 0.0f)
                return;

            effect[0] = pulse ? 0x26 : 0x21;
            uint16_t zones = 0;
            uint32_t forces = 0;
            for (int zone = 1; zone < 10; ++zone)
            {
                // Light initial travel, progressively firmer toward full pedal travel.
                const float ramp = pulse ? 1.0f : (0.35f + 0.65f * static_cast<float>(zone) / 9.0f);
                const int force = std::clamp(static_cast<int>(std::clamp(resistance, 0.0f, 1.0f) * ramp * 7.0f + 1.0f), 1, 8);
                zones |= static_cast<uint16_t>(1u << zone);
                forces |= static_cast<uint32_t>(force - 1) << (3 * zone);
            }
            effect[1] = static_cast<Uint8>(zones);
            effect[2] = static_cast<Uint8>(zones >> 8);
            for (int byte = 0; byte < 4; ++byte)
                effect[3 + byte] = static_cast<Uint8>(forces >> (8 * byte));
            effect[9] = pulse ? 25 : 0;
        }

        float get_normalized_axis_value(const Controller& controller, uint32_t axis)
        {
            // initialize result
            float normalized = 0.0f;
        
            // check if controller is a gamepad
            if (controller.type == ControllerType::Gamepad && controller.sdl_pointer)
            {
                // get raw axis value
                int16_t value = SDL_GetGamepadAxis(static_cast<SDL_Gamepad*>(controller.sdl_pointer), static_cast<SDL_GamepadAxis>(axis));

                // Pedals are unipolar; a stick dead zone discards 24% of their travel.
                if (axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER)
                    return std::max(0.0f, static_cast<float>(value) / 32767.0f);
        
                // account for deadzone
                static const uint16_t deadzone = 8000; // a good default as per sdl_gamepad.h
                if (abs(value) < deadzone)
                {
                    value = 0;
                }
                else
                {
                    value -= (value > 0) ? deadzone : -deadzone;
                }
        
                // compute range
                const float range_negative = 32768.0f;
                const float range_positive = 32767.0f;
                float range = (value < 0) ? range_negative : range_positive;
        
                // normalize to [-1.0, 1.0]
                normalized = static_cast<float>(value) / (range - deadzone);
            }
        
            return normalized;
        }
    }

    void Input::PollGamepad()
    {
        if (!gamepad.is_connected)
        {
            controller_thumb_left = controller_thumb_right = Vector2::Zero;
            controller_trigger_left = controller_trigger_right = 0.0f;
            std::fill(m_keys.begin() + key_index_gamepad, m_keys.end(), false);
            return;
        }

        // Trigger resistance persists on the device until explicitly cleared.
        if (feedback_active && (IsBlockedByUi() || !SDL_GetKeyboardFocus() || SDL_GetTicks() - feedback_refreshed > 250))
            GamepadStopFeedback();
    
        SDL_Gamepad* sdl_gamepad = static_cast<SDL_Gamepad*>(gamepad.sdl_pointer);
    
        // analog inputs
        controller_trigger_left  = get_normalized_axis_value(gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        controller_trigger_right = get_normalized_axis_value(gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
        controller_thumb_left.x  = get_normalized_axis_value(gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_LEFTX);
        controller_thumb_left.y  = get_normalized_axis_value(gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_LEFTY);
        controller_thumb_right.x = get_normalized_axis_value(gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_RIGHTX);
        controller_thumb_right.y = get_normalized_axis_value(gamepad, SDL_GamepadAxis::SDL_GAMEPAD_AXIS_RIGHTY);
    
        //button states
        m_keys[key_index_gamepad]      = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        m_keys[key_index_gamepad + 1]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        m_keys[key_index_gamepad + 2]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        m_keys[key_index_gamepad + 3]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        m_keys[key_index_gamepad + 4]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        m_keys[key_index_gamepad + 5]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_EAST);
        m_keys[key_index_gamepad + 6]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_WEST);
        m_keys[key_index_gamepad + 7]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_NORTH);
        m_keys[key_index_gamepad + 8]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_BACK);
        m_keys[key_index_gamepad + 9]  = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
        m_keys[key_index_gamepad + 10] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_START);
        m_keys[key_index_gamepad + 11] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
        m_keys[key_index_gamepad + 12] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        m_keys[key_index_gamepad + 13] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        m_keys[key_index_gamepad + 14] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        m_keys[key_index_gamepad + 15] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_MISC1);
        m_keys[key_index_gamepad + 16] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);
        m_keys[key_index_gamepad + 17] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1);
        m_keys[key_index_gamepad + 18] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2);
        m_keys[key_index_gamepad + 19] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2);
        m_keys[key_index_gamepad + 20] = SDL_GetGamepadButton(sdl_gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
    }

    void Input::OnEventGamepad(void* event)
    {
        const uint32_t previous_id = gamepad.instance_id;
        gamepad.type = ControllerType::Gamepad;
        CheckDeviceState(event, &gamepad);
        if (previous_id != gamepad.instance_id)
        {
            feedback_active = false;
            effects_failed = false;
            feedback_sent = 0;
            last_effect = {};
            controller_thumb_left = controller_thumb_right = Vector2::Zero;
            controller_trigger_left = controller_trigger_right = 0.0f;
            std::fill(m_keys.begin() + key_index_gamepad, m_keys.end(), false);
        }
    }

    bool Input::GamepadVibrate(const float left_motor_speed, const float right_motor_speed)
    {
        if (!gamepad.is_connected)
        {
            return false;
        }

        Uint16 low_frequency_rumble  = std::isfinite(left_motor_speed) ? static_cast<uint16_t>(clamp(left_motor_speed, 0.0f, 1.0f) * 65535) : 0;
        Uint16 high_frequency_rumble = std::isfinite(right_motor_speed) ? static_cast<uint16_t>(clamp(right_motor_speed, 0.0f, 1.0f) * 65535) : 0;
        Uint32 duration_ms           = 200; // a stalled or stopped simulation must not leave rumble running

        if (!SDL_GetBooleanProperty(SDL_GetGamepadProperties(static_cast<SDL_Gamepad*>(gamepad.sdl_pointer)), SDL_PROP_GAMEPAD_CAP_RUMBLE_BOOLEAN, false))
            return false;

        if (!SDL_RumbleGamepad(static_cast<SDL_Gamepad*>(gamepad.sdl_pointer), low_frequency_rumble, high_frequency_rumble, duration_ms))
        {
            return false;
        }

        return true;
    }

    void Input::GamepadDrivingFeedback(float low, float high, float brake_resistance,
        float throttle_resistance, bool abs, bool traction_control, float rpm, bool limiter)
    {
        if (!gamepad.is_connected || IsBlockedByUi() || !SDL_GetKeyboardFocus())
        {
            GamepadStopFeedback();
            return;
        }

        feedback_refreshed = SDL_GetTicks();
        if (feedback_active && feedback_refreshed - feedback_sent < 16)
            return; // bound output traffic independently of rendering frame rate
        feedback_sent = feedback_refreshed;
        feedback_active = true;
        GamepadVibrate(low, high);

        SDL_Gamepad* pad = static_cast<SDL_Gamepad*>(gamepad.sdl_pointer);
        if (SDL_GetGamepadType(pad) != SDL_GAMEPAD_TYPE_PS5 || effects_failed)
            return;

        std::array<Uint8, 47> effect = {};
        effect[0] = 0x0c; // right and left adaptive triggers only; preserve SDL rumble
        encode_trigger(effect.data() + 10, throttle_resistance, traction_control);
        encode_trigger(effect.data() + 21, brake_resistance, abs);
        if (effect != last_effect)
        {
            if (!SDL_SendGamepadEffect(pad, effect.data(), static_cast<int>(effect.size())))
            {
                effects_failed = true;
                SP_LOG_WARNING("PS5 adaptive triggers unavailable: %s", SDL_GetError());
            }
            else
            {
                last_effect = effect;
            }
        }

        if (SDL_GetBooleanProperty(SDL_GetGamepadProperties(pad), SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false))
        {
            const float revs = std::isfinite(rpm) ? std::clamp(rpm, 0.0f, 1.0f) : 0.0f;
            const bool flash = limiter && (feedback_sent / 80) % 2 == 0;
            SDL_SetGamepadLED(pad, flash ? 255 : static_cast<Uint8>(255.0f * revs),
                flash ? 255 : static_cast<Uint8>(180.0f * (1.0f - revs)), flash ? 255 : 24);
        }
    }

    void Input::GamepadStopFeedback()
    {
        if (gamepad.is_connected && feedback_active)
        {
            GamepadVibrate(0.0f, 0.0f);
            SDL_Gamepad* pad = static_cast<SDL_Gamepad*>(gamepad.sdl_pointer);
            if (SDL_GetGamepadType(pad) == SDL_GAMEPAD_TYPE_PS5)
            {
                std::array<Uint8, 47> effect = {};
                effect[0] = 0x0c;
                effect[10] = effect[21] = 0x05;
                SDL_SendGamepadEffect(pad, effect.data(), static_cast<int>(effect.size()));
                SDL_SetGamepadLED(pad, 0, 0, 64);
            }
        }
        feedback_active = false;
        last_effect = {};
    }

    bool Input::IsGamepadConnected()
    {
        return gamepad.is_connected;
    }

    const Vector2& Input::GetGamepadThumbStickLeft()
    {
        if (Input::IsBlockedByUi())
        {
            return Vector2::Zero;
        }

        return controller_thumb_left;
    }

    const Vector2& Input::GetGamepadThumbStickRight()
    {
        if (Input::IsBlockedByUi())
        {
            return Vector2::Zero;
        }

        return controller_thumb_right;
    }

    float Input::GetGamepadTriggerLeft()
    {
        if (Input::IsBlockedByUi())
        {
            return 0.0f;
        }

        return controller_trigger_left;
    }

    float Input::GetGamepadTriggerRight()
    {
        if (Input::IsBlockedByUi())
        {
            return 0.0f;
        }

        return controller_trigger_right;
    }
}
