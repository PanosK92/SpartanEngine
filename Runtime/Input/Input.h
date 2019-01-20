/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =================
#include "../Math/Vector2.h"
#include "../Core/SubSystem.h"
//============================

namespace Directus
{
	enum Button_Mouse
	{
		Click_Left,
		Click_Middle,
		Click_Right
	};

	enum Button_Keyboard
	{
		// FUNCTION
		F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12, F13, F14, F15,
		// NUMBERS
		Alpha0, Alpha1, Alpha2, Alpha3, Alpha4, Alpha5, Alpha6, Alpha7, Alpha8, Alpha9,
		// NUMPAD
		Keypad0, Keypad1, Keypad2, Keypad3, Keypad4, Keypad5, Keypad6, Keypad7, Keypad8, Keypad9,
		// LETTERS
		Q, W, E, R, T, Y, U, I, O, P,
		A, S, D, F, G, H, J, K, L,
		Z, X, C, V, B, N, M,
		// CONTROLS
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
		Insert
	};

	enum Button_Gamepad
	{
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

	class ENGINE_CLASS Input : public Subsystem
	{
	public:
		Input(Context* context);
		~Input();

		// SUBSYSTEM ==============
		bool Initialize() override;
		//=========================

		void Tick();
		
		bool GetButtonKeyboard(Button_Keyboard button)	{ return m_keyboardButtons[(int)button]; }
		bool GetButtonMouse(Button_Mouse button)		{ return m_mouseButtons[(int)button]; }
		const Math::Vector2& GetMousePosition()			{ return m_mouse_position_client; }
		const Math::Vector2& GetMouseDelta()			{ return m_mouseDelta; }

	private:
		bool ReadMouse();
		bool ReadKeyboard();
		bool ReadGamepad();

		// Vibrate the gamepad. Motor speed range is from 0.0 to 1.0f
		// The left motor is the low-frequency rumble motor. The right motor is the high-frequency rumble motor. 
		// The two motors are not the same, and they create different vibration effects.
		bool VibrateGamepad(float leftMotorSpeed, float rightMotorSpeed);

		// Mouse
		bool m_mouseButtons[3]					= { false };
		Math::Vector2 m_mouse_position_screen	= Math::Vector2::Zero;
		Math::Vector2 m_mouse_position_client	= Math::Vector2::Zero;
		Math::Vector2 m_mouseDelta				= Math::Vector2::Zero;
		float m_mouseWheel						= 0;
		float m_mouseWheelDelta					= 0;

		// Keyboard
		bool m_keyboardButtons[83] = { false };

		// Gamepad
		bool m_isGamepadConnected;
		bool m_gamepadButtons[13] = { false };
		Math::Vector2 m_thumbstickLeft;
		Math::Vector2 m_thumbstickRight;
		float m_triggerLeft;
		float m_triggerRight;
	};
}