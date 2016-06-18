/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ================
#include "Input.h"
#include "../Misc/Settings.h"
//===========================

//= NAMESPACES ================
using namespace Directus::Math;

//=============================

Input::Input()
{
	m_DX8Input = nullptr;
}

Input::~Input()
{
	m_DX8Input->Release();
	m_DX8Input = nullptr;
}

void Input::Initialize(HINSTANCE instance, HWND handle)
{
	m_DX8Input = new DX8Input();
	m_DX8Input->Initialize(instance, handle);
}

void Input::Update()
{
	m_DX8Input->Update();

	// get mouse delta position
	m_mousePosDelta = Vector2(m_DX8Input->GetMouseDelta().x, m_DX8Input->GetMouseDelta().y);

	// Update the location of the mouse cursor based on the change of the mouse location during the frame.
	m_mousePos.x += m_mousePosDelta.x;
	m_mousePos.y += m_mousePosDelta.y;

	// keep mouse position in screen
	if (m_mousePos.x < 0) m_mousePos.x = 0;
	if (m_mousePos.y < 0) m_mousePos.y = 0;
	if (m_mousePos.x > RESOLUTION_WIDTH) m_mousePos.x = RESOLUTION_WIDTH;
	if (m_mousePos.y > RESOLUTION_HEIGHT) m_mousePos.y = RESOLUTION_HEIGHT;
}

bool Input::GetKey(KeyCode key)
{
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
	if (key == Space) return m_DX8Input->IsKeyboardKeyDown(DIK_SPACE);

	return false;
}

bool Input::GetMouseButton(int button)
{
	return m_DX8Input->IsMouseKeyDown(button);
}

Vector2 Input::GetMousePosition()
{
	return m_mousePos;
}

Vector2 Input::GetMousePositionDelta()
{
	return m_mousePosDelta;
}
