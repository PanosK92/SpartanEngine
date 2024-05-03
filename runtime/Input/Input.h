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

#pragma once

//= INCLUDES ===============
#include "Definitions.h"
#include "../Math/Vector2.h"
#include "Event.h"
//==========================

namespace Spartan
{
    enum class KeyCode
    {
        // keyboard
        F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
        Alpha0, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
        Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
        Q, W, E, R, T, Y, U, I, O, P,
        A, S, D, F, G, H, J, K, L,
        Z, X, C, V, B, N, M,
        Esc,
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

        // mouse
        Click_Left,
        Click_Middle,
        Click_Right,

        // gamepad - xbox as basis but works with other brands (like playstation)
        DPad_Up,
        DPad_Down,
        DPad_Left,
        DPad_Right,
        Button_A,
        Button_B,
        Button_X,
        Button_Y,
        Back,
        Guide,
        Start,
        Left_Stick,
        Right_Stick,
        Left_Shoulder,
        Right_Shoulder,
        Misc1,    // xbox series x share button, ps5 microphone button, nintendo switch pro capture button
        Paddle1,  // xbox elite paddle P1
        Paddle2,  // xbox elite paddle P3
        Paddle3,  // xbox elite paddle P2
        Paddle4,  // xbox elite paddle P4
        Touchpad, // ps4/Pps5 touchpad button
    };

    enum class ControllerType
    {
        Gamepad,
        SteeringWheel,
        Max
    };

    struct Controller
    {
        void* sdl_pointer   = nullptr;
        bool is_connected   = false;
        std::string name    = "";
        uint32_t index      = 0;
        ControllerType type = ControllerType::Max;
    };

    class SP_CLASS Input
    {
    public:
        static void Initialize();
        static void Tick();
        static void PreTick();

        // polling driven input
        static void PollMouse();
        static void PollKeyboard();
        static void PollGamepad();
        static void PollSteeringWheel();

        // event driven input
        static void OnEvent(sp_variant data);
        static void OnEventMouse(void* event);
        static void OnEventGamepad(void* event);
        static void OnEventSteeringWheel(void* event);

        // returns true while a button is held down
        static bool GetKey(const KeyCode key);
        // returns true during the frame the user pressed down the button
        static bool GetKeyDown(const KeyCode key);
        // returns true during the frame the user releases the button
        static bool GetKeyUp(const KeyCode key);

        // mouse
        static void SetMouseCursorVisible(const bool visible);
        static bool GetMouseCursorVisible();
        static void SetMouseIsInViewport(const bool is_in_viewport);
        static bool GetMouseIsInViewport();
        static const Math::Vector2& GetMousePosition();
        static void SetMousePosition(const Math::Vector2& position);
        static const Math::Vector2& GetMouseDelta();
        static const Math::Vector2& GetMouseWheelDelta();
        static void SetEditorViewportOffset(const Math::Vector2& offset);
        static const Math::Vector2 GetMousePositionRelativeToWindow();
        static const Math::Vector2 GetMousePositionRelativeToEditorViewport();

        // gamepad
        static bool IsGamepadConnected();
        static const Math::Vector2& GetGamepadThumbStickLeft();
        static const Math::Vector2& GetGamepadThumbStickRight();
        static float GetGamepadTriggerLeft();
        static float GetGamepadTriggerRight();

        // steering wheel
        static float GetSteeringWheelSteering();
        static float GetSteeringWheelAccelerator();
        static float GetSteeringWheelBrake();

        // vibrate the gamepad
        // motor speed range is from 0.0 to 1.0f
        // the left motor is the low-frequency rumble motor
        // the right motor is the high-frequency rumble motor
        // the two motors are not the same, and they create different vibration effects
        static bool GamepadVibrate(const float left_motor_speed, const float right_motor_speed);

    private:
        static std::array<bool, 107>& GetKeys();
        static uint32_t GetKeyIndexMouse();
        static uint32_t GetKeyIndexController();
        static void CheckControllerState(uint32_t event_type, Controller* controller, ControllerType type_to_detect);
        static float GetNormalizedAxisValue(void* controller, const uint32_t axis);

        // keys
        static std::array<bool, 107> m_keys;
        static uint32_t m_start_index_mouse;
        static uint32_t m_start_index_controller;
    };
}
