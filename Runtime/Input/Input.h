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
#include <array>
#include "../Math/Vector2.h"
#include "../Core/ISubsystem.h"
//============================

namespace Directus
{
	enum KeyCode
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

	class ENGINE_CLASS Input : public ISubsystem
	{
	public:
		Input(Context* context);
		~Input();

		//= ISubsystem ======
		void Tick() override;
		//===================
		
		// Keys
		bool GetKey(KeyCode key)		{ return m_keys[(unsigned int)key]; }							// Returns true while the button identified by KeyCode is held down.
		bool GetKeyDown(KeyCode key)	{ return GetKey(key) && !m_keys_previous[(unsigned int)key]; }	// Returns true during the frame the user pressed down the button identified by KeyCode.
		bool GetKeyUp(KeyCode key)		{ return !GetKey(key) && m_keys_previous[(unsigned int)key]; }	// Returns true the first frame the user releases the button identified by KeyCode.

		// Mouse
		const Math::Vector2& GetMousePosition()	{ return m_mouse_position; }
		const Math::Vector2& GetMouseDelta()	{ return m_mouse_delta; }

		// Gamepad
		bool GamepadIsConnected()							{ return m_gamepad_connected; }
		const Math::Vector2& GetGamepadThumbStickLeft()		{ return m_gamepad_thumb_left; }
		const Math::Vector2& GetGamepadThumbStickRight()	{ return m_gamepad_thumb_right; }
		float GetGamepadTriggerLeft()						{ return m_gamepad_trigger_left; }
		float GetGamepadTriggerRight()						{ return m_gamepad_trigger_right; }
		// Vibrate the gamepad. Motor speed range is from 0.0 to 1.0f
		// The left motor is the low-frequency rumble motor. The right motor is the high-frequency rumble motor. 
		// The two motors are not the same, and they create different vibration effects.
		bool GamepadVibrate(float leftMotorSpeed, float rightMotorSpeed);

	private:
		bool ReadMouse();
		bool ReadKeyboard();
		bool ReadGamepad();

		// Keys
		std::array<bool, 99> m_keys;
		std::array<bool, 99> m_keys_previous; // A copy of m_keys, as it was during the previous frame
		unsigned int start_index_mouse		= 83;
		unsigned int start_index_gamepad	= 86;

		// Mouse
		Math::Vector2 m_mouse_position	= Math::Vector2::Zero;
		Math::Vector2 m_mouse_delta		= Math::Vector2::Zero;
		int m_mouseWheel				= 0;
		float m_mouseWheelDelta			= 0;

		// Gamepad
		bool m_gamepad_connected;
		Math::Vector2 m_gamepad_thumb_left;
		Math::Vector2 m_gamepad_thumb_right;
		float m_gamepad_trigger_left;
		float m_gamepad_trigger_right;
	};
}