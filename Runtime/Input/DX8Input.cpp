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

//= INCLUDES =========
#include "DX8Input.h"
#include "../Logging/Log.h"
//====================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

namespace Directus
{
	DX8Input::DX8Input()
	{
		m_directInput = nullptr;
		m_keyboard = nullptr;
		m_mouse = nullptr;
	}

	DX8Input::~DX8Input()
	{
		Release();
	}

	bool DX8Input::Initialize(void* instance, void* handle)
	{
		HWND handleHWND = (HWND)handle;
		HINSTANCE instanceHandle = (HINSTANCE)instance;

		if (!instance || !handle)
			return false;

		// Make sure the window has focus, otherwise the mouse and keyboard won't be able to be aquired.
		SetForegroundWindow(handleHWND);

		// Initialize the main direct input interface.
		HRESULT result = DirectInput8Create(instanceHandle, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&m_directInput, nullptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to initialize the DirectInput interface.");
			return false;
		}

		// Initialize the direct input interface for the keyboard.
		result = m_directInput->CreateDevice(GUID_SysKeyboard, &m_keyboard, nullptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to initialize a DirectInput keyboard.");
			return false;
		}

		// Set the data format. In this case since it is a keyboard we can use the predefined data format.
		result = m_keyboard->SetDataFormat(&c_dfDIKeyboard);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to initialize DirectInput keyboard data format.");
		}

		// Set the cooperative level of the keyboard to share with other programs.
		result = m_keyboard->SetCooperativeLevel(handleHWND, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to set DirectInput keyboard's cooperative level.");
		}

		// Acquire the keyboard.
		if (!GetKeyboard())
		{
			LOG_ERROR("Failed to aquire the keyboard.");
		}

		// Initialize the direct input interface for the mouse.
		result = m_directInput->CreateDevice(GUID_SysMouse, &m_mouse, nullptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to set DirectInput keyboard's cooperative level.");
			return false;
		}

		// Set the data format for the mouse using the pre-defined mouse data format.
		result = m_mouse->SetDataFormat(&c_dfDIMouse);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to initialize a DirectInput mouse.");
		}

		// Set the cooperative level of the mouse to share with other programs.
		result = m_mouse->SetCooperativeLevel(handleHWND, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to set DirectInput mouse's cooperative level.");
		}

		// Acquire the mouse.
		if (!GetMouse())
		{
			LOG_ERROR("Failed to aquire the mouse.");
		}

		return true;
	}

	void DX8Input::Update()
	{
		if (!ReadKeyboard())
		{
			LOG_ERROR("Failed to read from keyboard.");
		}

		if (!ReadMouse())
		{
			LOG_ERROR("Failed to read from mouse.");
		}
	}

	void DX8Input::Release()
	{
		// Release the mouse.
		if (m_mouse)
		{
			m_mouse->Unacquire();
			m_mouse->Release();
			m_mouse = nullptr;
		}

		// Release the keyboard.
		if (m_keyboard)
		{
			m_keyboard->Unacquire();
			m_keyboard->Release();
			m_keyboard = nullptr;
		}

		// Release the main interface to direct input.
		if (m_directInput)
		{
			m_directInput->Release();
			m_directInput = nullptr;
		}
	}

	bool DX8Input::IsKeyboardKeyDown(byte key)
	{
		// & = bitwise AND
		// hexadecimal 0x80 = 10000000 binary
		if (m_keyboardState[key] & 0x80)
			return true;

		return false;
	}

	bool DX8Input::IsMouseKeyDown(int key)
	{
		//  0 = Left Button
		//  1 = Right Button
		//  3 = Middle Button (Scroll Wheel pressed)
		//  4-7 = Side buttons
		if (m_mouseState.rgbButtons[key] & 0x80)
			return true;

		return false;
	}

	Vector3 DX8Input::GetMouseDelta()
	{
		// lX = position x delta
		// lY = position y delta
		// lZ = wheel delta
		return Vector3(m_mouseState.lX, m_mouseState.lY, m_mouseState.lZ);
	}

	bool DX8Input::GetKeyboard()
	{
		HRESULT result = m_keyboard->Acquire();
		return SUCCEEDED(result);
	}

	bool DX8Input::GetMouse()
	{
		HRESULT result = m_mouse->Acquire();
		return SUCCEEDED(result);
	}

	bool DX8Input::ReadKeyboard()
	{
		// Read keyboard
		HRESULT result = m_keyboard->GetDeviceState(sizeof(m_keyboardState), static_cast<LPVOID>(&m_keyboardState));
		if (SUCCEEDED(result))
			return true;

		// If the keyboard lost focus or was not acquired then try to get control back.
		if ((result == DIERR_INPUTLOST) || (result == DIERR_NOTACQUIRED))
		{
			GetKeyboard();
		}
		return false;
	}

	bool DX8Input::ReadMouse()
	{
		// Read mouse
		HRESULT result = m_mouse->GetDeviceState(sizeof(DIMOUSESTATE), static_cast<LPVOID>(&m_mouseState));
		if (SUCCEEDED(result))
			return true;

		// If the mouse lost focus or was not acquired then try to get control back.
		if ((result == DIERR_INPUTLOST) || (result == DIERR_NOTACQUIRED))
		{
			GetMouse();
		}
		return false;
	}
}
