/*
Copyright(c) 2016-2020 Panos Karabelas

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

#pragma once

//= INCLUDES ==================
#include <array>
#include "../Math/Vector2.h"
#include "../Core/ISubsystem.h"
//=============================

namespace Spartan
{
    enum class KeyCode
    {
        // Keyboard
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,/*Function*/ 
        Alpha0, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,/*Numbers*/
        Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,/*Numpad*/
        Q, W, E, R, T, Y, U, I, O, P,/*Letters*/
        A, S, D, F, G, H, J, K, L,
        Z, X, C, V, B, N, M,
        Esc,/*Controls*/ 
        Tab,
        Shift_Left, Shift_Right,
        Ctrl_Left, Ctrl_Right,
        Alt_Left, Alt_Right,
        Space,
        CapsLock,
        Backspace,
        Enter,
        Delete,
        Arrow_Left, Arrow_Right, Arrow_Up, Arrow_Down,
        Page_Up, Page_Down,
        Home,
        End,
        Insert,

        // Mouse
        Click_Left,
        Click_Middle,
        Click_Right,

        // Gamepad
        DPad_Up,
        DPad_Down,
        DPad_Left,
        DPad_Right,
        Button_A,
        Button_B,
        Button_X,
        Button_Y,
        Start,
        Back,
        Left_Thumb,
        Right_Thumb,
        Left_Shoulder,
        Right_Shoulder
    };

    class SPARTAN_CLASS Input : public ISubsystem
    {
    public:
        Input(Context* context);
        ~Input() = default;

        void OnWindowData();
        //= ISubsystem ======================
        void Tick(float delta_time) override;
        //===================================
        
        // Keys
        bool GetKey(const KeyCode key)        { return m_keys[static_cast<uint32_t>(key)]; }                                    // Returns true while the button identified by KeyCode is held down.
        bool GetKeyDown(const KeyCode key)    { return GetKey(key) && !m_keys_previous_frame[static_cast<uint32_t>(key)]; }    // Returns true during the frame the user pressed down the button identified by KeyCode.
        bool GetKeyUp(const KeyCode key)    { return !GetKey(key) && m_keys_previous_frame[static_cast<uint32_t>(key)]; }    // Returns true the first frame the user releases the button identified by KeyCode.

        // Mouse
        const Math::Vector2& GetMousePosition() const { return m_mouse_position; }
        void SetMousePosition(const Math::Vector2& position);
        const Math::Vector2& GetMouseDelta()    const { return m_mouse_delta; }
        const float GetMouseWheelDelta()        const { return m_mouse_wheel_delta; }

        // Gamepad
        bool GamepadIsConnected() const                            { return m_gamepad_connected; }
        const Math::Vector2& GetGamepadThumbStickLeft() const    { return m_gamepad_thumb_left; }
        const Math::Vector2& GetGamepadThumbStickRight() const    { return m_gamepad_thumb_right; }
        float GetGamepadTriggerLeft() const                        { return m_gamepad_trigger_left; }
        float GetGamepadTriggerRight() const                    { return m_gamepad_trigger_right; }
        // Vibrate the gamepad. Motor speed range is from 0.0 to 1.0f
        // The left motor is the low-frequency rumble motor. The right motor is the high-frequency rumble motor. 
        // The two motors are not the same, and they create different vibration effects.
        bool GamepadVibrate(float left_motor_speed, float right_motor_speed) const;

    private:
        // Keys
        std::array<bool, 99> m_keys;
        std::array<bool, 99> m_keys_previous_frame;
        uint32_t start_index_mouse        = 83;
        uint32_t start_index_gamepad    = 86;

        // Mouse
        Math::Vector2 m_mouse_position    = Math::Vector2::Zero;
        Math::Vector2 m_mouse_delta        = Math::Vector2::Zero;
        float m_mouse_wheel_delta        = 0;

        // Gamepad   
        bool m_gamepad_connected            = false;
        Math::Vector2 m_gamepad_thumb_left  = Math::Vector2::Zero;
        Math::Vector2 m_gamepad_thumb_right = Math::Vector2::Zero;
        float m_gamepad_trigger_left        = 0.0f;
        float m_gamepad_trigger_right       = 0.0f;

        // Misc
        bool m_is_new_frame         = false;
        bool m_check_for_new_device = false;
    };
}
