/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include "DX8Input.h"
#include "../Math/Vector2.h"
#include <memory>
#include "../Core/Subsystem.h"
//==========================

enum KeyCode
{
	// Function keys
	F1,
	F2,
	F3,
	F4,
	F5,
	F6,
	F7,
	F8,
	F9,
	F10,
	F11,
	F12,
	F13,
	F14,
	F15,
	// Numeric keypad
	Keypad0,
	Keypad1,
	Keypad2,
	Keypad3,
	Keypad4,
	Keypad5,
	Keypad6,
	Keypad7,
	Keypad8,
	Keypad9, 
	// Alphanumeric keys
	Alpha0,
	Alpha1,
	Alpha2,
	Alpha3,
	Alpha4,
	Alpha5,
	Alpha6,
	Alpha7,
	Alpha8,
	Alpha9,
	Q,
	W,
	E,
	R,
	T,
	Y,
	U,
	I,
	O,
	P,
	A,
	S,
	D,
	F,
	G,
	H,
	J,
	K,
	L,
	Z,
	X,
	C,
	V,
	B,
	N,
	M,
	// Controls
	Esc,
	Tab,
	LeftShift,
	RightShift,
	LeftControl,
	RightControl,
	LeftAlt,
	RightAlt,
	Space,
	CapsLock,
	Backspace,
	Return,
};

class Input : public Subsystem
{
public:
	Input(Context* context);
	~Input();

	void Initialize(HINSTANCE instance, HWND handle);
	void Update();

	bool GetKey(KeyCode key);
	bool GetMouseButton(int button) { return m_initialized ? m_DX8Input->IsMouseKeyDown(button) : false; }
	Directus::Math::Vector2 GetMousePosition() { return m_mousePos; }
	Directus::Math::Vector2 GetMousePositionDelta() { return m_mousePosDelta; }

private:
	Directus::Math::Vector2 m_mousePos;
	Directus::Math::Vector2 m_mousePosDelta;
	std::shared_ptr<DX8Input> m_DX8Input;
	bool m_initialized;
};
