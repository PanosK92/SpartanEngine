/*
Copyright(c) 2016-2023 Panos Karabelas

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
        // Keyboard
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
        Back,
        Guide,
        Start,
        Left_Stick,
        Right_Stick,
        Left_Shoulder,
        Right_Shoulder,
        Misc1,    // Xbox Series X share button, PS5 microphone button, Nintendo Switch Pro capture button
        Paddle1,  // Xbox Elite paddle P1
        Paddle2,  // Xbox Elite paddle P3
        Paddle3,  // Xbox Elite paddle P2
        Paddle4,  // Xbox Elite paddle P4
        Touchpad, // PS4/PS5 touchpad button
    };

    class SP_CLASS Input
    {
    public:
        static void Initialize();
        static void Tick();
        static void PostTick();

        // Polling driven input
        static void PollMouse();
        static void PollKeyboard();
        static void PollController();

        // Event driven input
        static void OnEvent(sp_variant data);
        static void OnEventMouse(void* event_mouse);
        static void OnEventController(void* event_controller);

        // Keys
        static bool GetKey(const KeyCode key);     // Returns true while a button is held down
        static bool GetKeyDown(const KeyCode key); // Returns true during the frame the user pressed down the button
        static bool GetKeyUp(const KeyCode key);   // Returns true during the frame the user releases the button

        // Mouse
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

        // Controller
        static bool IsControllerConnected();
        static const Math::Vector2& GetControllerThumbStickLeft();
        static const Math::Vector2& GetControllerThumbStickRight();
        static float GetControllerTriggerLeft();
        static float GetControllerTriggerRight();

        // Vibrate the gamepad.
        // Motor speed range is from 0.0 to 1.0f.
        // The left motor is the low-frequency rumble motor.
        // The right motor is the high-frequency rumble motor. 
        // The two motors are not the same, and they create different vibration effects.
        static bool GamepadVibrate(const float left_motor_speed, const float right_motor_speed);

    private:
        static std::array<bool, 107>& GetKeys();
        static uint32_t GetKeyIndexMouse();
        static uint32_t GetKeyIndexController();

        // Keys
        static std::array<bool, 107> m_keys;
        static uint32_t m_start_index_mouse;
        static uint32_t m_start_index_controller;
    };
}
