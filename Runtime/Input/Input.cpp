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

//= INCLUDES ==========================
#include "Input.h"
#include "../Core/Settings.h"
#include "../Logging/Log.h"
#include "../EventSystem/EventSystem.h"
//=====================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Input::Input(Context* context) : Subsystem(context)
	{
		m_initialized = false;

		// Subscribe to update event
		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER(Update));
	}

	Input::~Input()
	{
		m_DX8Input->Release();
	}

	bool Input::Initialize()
	{
		m_DX8Input = make_unique<DX8Input>();
		m_initialized = m_DX8Input->Initialize();

		return m_initialized;
	}

	void Input::Update()
	{
		if (!m_initialized)
			return;

		m_DX8Input->Update();

		// get mouse delta position
		m_mousePosDelta = Vector2(m_DX8Input->GetMouseDelta().x, m_DX8Input->GetMouseDelta().y);

		// Update the location of the mouse cursor based on the change of the mouse location during the frame.
		m_mousePos.x += m_mousePosDelta.x;
		m_mousePos.y += m_mousePosDelta.y;

		// keep mouse position inside the screen
		m_mousePos.x = m_mousePos.x < 0 ? 0 : m_mousePos.x > RESOLUTION_WIDTH ? RESOLUTION_WIDTH : m_mousePos.x;
		m_mousePos.y = m_mousePos.y < 0 ? 0 : m_mousePos.y > RESOLUTION_HEIGHT ? RESOLUTION_HEIGHT : m_mousePos.y;
	}

	bool Input::GetKey(KeyCode key)
	{
		if (!m_initialized)
			return false;

		// FUNCTION KEYS
		if (key == F1) return m_DX8Input->IsKeyboardKeyDown(DIK_F1);
		if (key == F2) return m_DX8Input->IsKeyboardKeyDown(DIK_F2);
		if (key == F3) return m_DX8Input->IsKeyboardKeyDown(DIK_F3);
		if (key == F4) return m_DX8Input->IsKeyboardKeyDown(DIK_F4);
		if (key == F5) return m_DX8Input->IsKeyboardKeyDown(DIK_F5);
		if (key == F6) return m_DX8Input->IsKeyboardKeyDown(DIK_F6);
		if (key == F7) return m_DX8Input->IsKeyboardKeyDown(DIK_F7);
		if (key == F8) return m_DX8Input->IsKeyboardKeyDown(DIK_F8);
		if (key == F9) return m_DX8Input->IsKeyboardKeyDown(DIK_F9);
		if (key == F10) return m_DX8Input->IsKeyboardKeyDown(DIK_F10);
		if (key == F11) return m_DX8Input->IsKeyboardKeyDown(DIK_F11);
		if (key == F12) return m_DX8Input->IsKeyboardKeyDown(DIK_F12);
		if (key == F13) return m_DX8Input->IsKeyboardKeyDown(DIK_F13);
		if (key == F14) return m_DX8Input->IsKeyboardKeyDown(DIK_F14);
		if (key == F15) return m_DX8Input->IsKeyboardKeyDown(DIK_F15);

		// NUMERIC KEYPAD
		if (key == Keypad0) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD0);
		if (key == Keypad1) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD1);
		if (key == Keypad2) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD2);
		if (key == Keypad3) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD3);
		if (key == Keypad4) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD4);
		if (key == Keypad5) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD5);
		if (key == Keypad6) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD6);
		if (key == Keypad7) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD7);
		if (key == Keypad8) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD8);
		if (key == Keypad9) return m_DX8Input->IsKeyboardKeyDown(DIK_NUMPAD9);

		// ALPHANUMERIC KEYS
		if (key == Alpha0) return m_DX8Input->IsKeyboardKeyDown(DIK_0);
		if (key == Alpha1) return m_DX8Input->IsKeyboardKeyDown(DIK_1);
		if (key == Alpha2) return m_DX8Input->IsKeyboardKeyDown(DIK_2);
		if (key == Alpha3) return m_DX8Input->IsKeyboardKeyDown(DIK_3);
		if (key == Alpha4) return m_DX8Input->IsKeyboardKeyDown(DIK_4);
		if (key == Alpha5) return m_DX8Input->IsKeyboardKeyDown(DIK_5);
		if (key == Alpha6) return m_DX8Input->IsKeyboardKeyDown(DIK_6);
		if (key == Alpha7) return m_DX8Input->IsKeyboardKeyDown(DIK_7);
		if (key == Alpha8) return m_DX8Input->IsKeyboardKeyDown(DIK_8);
		if (key == Alpha9) return m_DX8Input->IsKeyboardKeyDown(DIK_9);
		if (key == Q) return m_DX8Input->IsKeyboardKeyDown(DIK_Q);
		if (key == W) return m_DX8Input->IsKeyboardKeyDown(DIK_W);
		if (key == E) return m_DX8Input->IsKeyboardKeyDown(DIK_E);
		if (key == R) return m_DX8Input->IsKeyboardKeyDown(DIK_R);
		if (key == T) return m_DX8Input->IsKeyboardKeyDown(DIK_T);
		if (key == Y) return m_DX8Input->IsKeyboardKeyDown(DIK_Y);
		if (key == U) return m_DX8Input->IsKeyboardKeyDown(DIK_U);
		if (key == I) return m_DX8Input->IsKeyboardKeyDown(DIK_I);
		if (key == O) return m_DX8Input->IsKeyboardKeyDown(DIK_O);
		if (key == P) return m_DX8Input->IsKeyboardKeyDown(DIK_P);
		if (key == A) return m_DX8Input->IsKeyboardKeyDown(DIK_A);
		if (key == S) return m_DX8Input->IsKeyboardKeyDown(DIK_S);
		if (key == D) return m_DX8Input->IsKeyboardKeyDown(DIK_D);
		if (key == F) return m_DX8Input->IsKeyboardKeyDown(DIK_F);
		if (key == G) return m_DX8Input->IsKeyboardKeyDown(DIK_G);
		if (key == H) return m_DX8Input->IsKeyboardKeyDown(DIK_H);
		if (key == J) return m_DX8Input->IsKeyboardKeyDown(DIK_J);
		if (key == K) return m_DX8Input->IsKeyboardKeyDown(DIK_K);
		if (key == L) return m_DX8Input->IsKeyboardKeyDown(DIK_L);
		if (key == Z) return m_DX8Input->IsKeyboardKeyDown(DIK_Z);
		if (key == X) return m_DX8Input->IsKeyboardKeyDown(DIK_X);
		if (key == C) return m_DX8Input->IsKeyboardKeyDown(DIK_C);
		if (key == V) return m_DX8Input->IsKeyboardKeyDown(DIK_V);
		if (key == B) return m_DX8Input->IsKeyboardKeyDown(DIK_B);
		if (key == N) return m_DX8Input->IsKeyboardKeyDown(DIK_N);
		if (key == M) return m_DX8Input->IsKeyboardKeyDown(DIK_M);

		// CONTROLS
		if (key == Esc)				return m_DX8Input->IsKeyboardKeyDown(DIK_ESCAPE);
		if (key == Tab)				return m_DX8Input->IsKeyboardKeyDown(DIK_TAB);
		if (key == LeftShift)		return m_DX8Input->IsKeyboardKeyDown(DIK_LSHIFT);
		if (key == RightShift)		return m_DX8Input->IsKeyboardKeyDown(DIK_RSHIFT);
		if (key == LeftControl)		return m_DX8Input->IsKeyboardKeyDown(DIK_LCONTROL);
		if (key == RightControl)	return m_DX8Input->IsKeyboardKeyDown(DIK_RCONTROL);
		if (key == LeftAlt)			return m_DX8Input->IsKeyboardKeyDown(DIK_LALT);
		if (key == RightAlt)		return m_DX8Input->IsKeyboardKeyDown(DIK_RALT);
		if (key == Space)			return m_DX8Input->IsKeyboardKeyDown(DIK_SPACE);
		if (key == CapsLock)		return m_DX8Input->IsKeyboardKeyDown(DIK_CAPSLOCK);
		if (key == Backspace)		return m_DX8Input->IsKeyboardKeyDown(DIK_BACKSPACE);
		if (key == Return)			return m_DX8Input->IsKeyboardKeyDown(DIK_RETURN);
		if (key == Delete)			return m_DX8Input->IsKeyboardKeyDown(DIK_DELETE);

		return false;
	}
}