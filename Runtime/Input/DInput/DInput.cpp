/*
Copyright(c) 2016-2018 Panos Karabelas

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

#define DIRECTINPUT_VERSION 0x0800

//= INCLUDES ======================
#include "DInput.h"
#include <dinput.h>
#include <sstream>
#include "../../Logging/Log.h"
#include "../../Core/Engine.h"
#include "../../Core/Settings.h"
#include "../../Core/EventSystem.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	IDirectInput8* g_directInput;
	IDirectInputDevice8* g_keyboard;
	IDirectInputDevice8* g_mouse;
	DIMOUSESTATE g_mouseState;
	unsigned char g_keyboardState[256];

	DInput::DInput(Context* context) : IInput(context)
	{
		g_directInput	= nullptr;
		g_keyboard		= nullptr;
		g_mouse			= nullptr;

		SUBSCRIBE_TO_EVENT(EVENT_UPDATE, EVENT_HANDLER(Update));
	}

	DInput::~DInput()
	{
		// Release the mouse.
		if (g_mouse)
		{
			g_mouse->Unacquire();
			g_mouse->Release();
			g_mouse = nullptr;
		}

		// Release the keyboard.
		if (g_keyboard)
		{
			g_keyboard->Unacquire();
			g_keyboard->Release();
			g_keyboard = nullptr;
		}

		// Release the main interface to direct input.
		if (g_directInput)
		{
			g_directInput->Release();
			g_directInput = nullptr;
		}
	}

	bool DInput::Input_Initialize()
	{
		if (!Engine::GetWindowHandle() || !Engine::GetWindowInstance())
			return false;

		bool success		= true;
		auto windowHandle	= (HWND)Engine::GetWindowHandle();
		auto windowInstance = (HINSTANCE)Engine::GetWindowInstance();

		// Make sure the window has focus, otherwise the mouse and keyboard won't be able to be aquired.
		SetForegroundWindow(windowHandle);

		// Initialize the main direct input interface.
		auto result = DirectInput8Create(windowInstance, DIRECTINPUT_VERSION, IID_IDirectInput8, (void**)&g_directInput, nullptr);
		if (FAILED(result))
		{
			switch (result)
			{
				case DIERR_INVALIDPARAM:			LOG_ERROR("DInput: DirectInput8Create() Failed, invalid parameters.");			break;
				case DIERR_BETADIRECTINPUTVERSION:	LOG_ERROR("DInput: DirectInput8Create() Failed, beta direct input version.");	break;
				case DIERR_OLDDIRECTINPUTVERSION:	LOG_ERROR("DInput: DirectInput8Create() Failed, old direct input version.");	break;
				case DIERR_OUTOFMEMORY:				LOG_ERROR("DInput: DirectInput8Create() Failed, out of memory.");				break;
				default:							LOG_ERROR("DInput: Failed to initialize the DirectInput interface.");
			}
			return false;
		}

		// Initialize the direct input interface for the keyboard.
		result = g_directInput->CreateDevice(GUID_SysKeyboard, &g_keyboard, nullptr);
		if (SUCCEEDED(result))
		{
			// Set the data format. In this case since it is a keyboard we can use the predefined data format.
			result = g_keyboard->SetDataFormat(&c_dfDIKeyboard);
			if (FAILED(result))
			{
				LOG_ERROR("DInput: Failed to initialize DirectInput keyboard data format.");
			}

			// Set the cooperative level of the keyboard to share with other programs.
			result = g_keyboard->SetCooperativeLevel(windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
			if (FAILED(result))
			{
				LOG_ERROR("DInput: Failed to set DirectInput keyboard's cooperative level.");
			}

			// Acquire the keyboard.
			if (!SUCCEEDED(g_keyboard->Acquire()))
			{
				LOG_ERROR("DInput: Failed to aquire the keyboard.");
			}		
		}
		else
		{
			LOG_ERROR("DInput: Failed to initialize a DirectInput keyboard.");
			success = false;
		}

		// Initialize the direct input interface for the mouse.
		result = g_directInput->CreateDevice(GUID_SysMouse, &g_mouse, nullptr);
		if (SUCCEEDED(result))
		{
			// Set the data format for the mouse using the pre-defined mouse data format.
			result = g_mouse->SetDataFormat(&c_dfDIMouse);
			if (FAILED(result))
			{
				LOG_ERROR("DInput: Failed to initialize a DirectInput mouse.");
			}
		
			// Set the cooperative level of the mouse to share with other programs.
			result = g_mouse->SetCooperativeLevel(windowHandle, DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
			if (FAILED(result))
			{
				LOG_ERROR("DInput: Failed to set DirectInput mouse's cooperative level.");
			}
		
			// Acquire the mouse.
			if (!SUCCEEDED(g_mouse->Acquire()))
			{
				LOG_ERROR("DInput: Failed to aquire the mouse.");
			}
		}
		else
		{
			LOG_ERROR("DInput: Failed to initialize a DirectInput mouse.");
			success = false;
		}

		if (success)
		{
			stringstream ss;
			ss << hex << DIRECTINPUT_VERSION;
			auto major = ss.str().erase(1, 2);
			auto minor = ss.str().erase(0, 1);
			LOG_INFO("Input: DirectInput " + major + "." + minor);
		}

		return success;
	}

	void DInput::Update()
	{
		if (ReadMouse())
		{
			// COMPUTE DELTA
			m_mouseDelta.x		= (float)g_mouseState.lX; // lX = x
			m_mouseDelta.y		= (float)g_mouseState.lY; // lY = y
			m_mouseWheelDelta	= (float)g_mouseState.lZ; // lZ = wheel

			// COMPUTE POSITION
			m_mousePos.x = Clamp(m_mousePos.x + m_mouseDelta.x, 0.0f, (float)DISPLAY_WIDTH);
			m_mousePos.y = Clamp(m_mousePos.y + m_mouseDelta.y, 0.0f, (float)DISPLAY_HEIGHT);
			m_mouseWheel += m_mouseWheelDelta;

			// COMPUTE BUTTON STATE
			m_mouseState[0] = g_mouseState.rgbButtons[0] & 0x80; // Left Button
			m_mouseState[1] = g_mouseState.rgbButtons[3] & 0x80; // Middle Button
			m_mouseState[2] = g_mouseState.rgbButtons[1] & 0x80; // Right Button
			// DInput: [4,7] -> Side buttons
		}
		else
		{
			m_mouseDelta.x		= 0;
			m_mouseDelta.y		= 0;
			m_mouseWheelDelta	= 0;

			m_mouseState[0] = false;
			m_mouseState[1] = false;
			m_mouseState[2] = false;
		}	
			
		if(ReadKeyboard())
		{
			// FUNCTION
			m_keyboardState[0] = g_keyboardState[DIK_F1] & 0x80;
			m_keyboardState[1] = g_keyboardState[DIK_F2] & 0x80;
			m_keyboardState[2] = g_keyboardState[DIK_F3] & 0x80;
			m_keyboardState[3] = g_keyboardState[DIK_F4] & 0x80;
			m_keyboardState[4] = g_keyboardState[DIK_F5] & 0x80;
			m_keyboardState[5] = g_keyboardState[DIK_F6] & 0x80;
			m_keyboardState[6] = g_keyboardState[DIK_F7] & 0x80;
			m_keyboardState[7] = g_keyboardState[DIK_F8] & 0x80;
			m_keyboardState[8] = g_keyboardState[DIK_F9] & 0x80;
			m_keyboardState[9] = g_keyboardState[DIK_F10] & 0x80;
			m_keyboardState[10] = g_keyboardState[DIK_F11] & 0x80;
			m_keyboardState[11] = g_keyboardState[DIK_F12] & 0x80;
			m_keyboardState[12] = g_keyboardState[DIK_F13] & 0x80;
			m_keyboardState[13] = g_keyboardState[DIK_F14] & 0x80;
			m_keyboardState[14] = g_keyboardState[DIK_F15] & 0x80;
			// NUMBERS
			m_keyboardState[15] = g_keyboardState[DIK_0] & 0x80;
			m_keyboardState[16] = g_keyboardState[DIK_1] & 0x80;
			m_keyboardState[17] = g_keyboardState[DIK_2] & 0x80;
			m_keyboardState[18] = g_keyboardState[DIK_3] & 0x80;
			m_keyboardState[19] = g_keyboardState[DIK_4] & 0x80;
			m_keyboardState[20] = g_keyboardState[DIK_5] & 0x80;
			m_keyboardState[21] = g_keyboardState[DIK_6] & 0x80;
			m_keyboardState[22] = g_keyboardState[DIK_7] & 0x80;
			m_keyboardState[23] = g_keyboardState[DIK_8] & 0x80;
			m_keyboardState[24] = g_keyboardState[DIK_9] & 0x80;
			// KEYPAD
			m_keyboardState[25] = g_keyboardState[DIK_NUMPAD0] & 0x80;
			m_keyboardState[26] = g_keyboardState[DIK_NUMPAD1] & 0x80;
			m_keyboardState[27] = g_keyboardState[DIK_NUMPAD2] & 0x80;
			m_keyboardState[28] = g_keyboardState[DIK_NUMPAD3] & 0x80;
			m_keyboardState[29] = g_keyboardState[DIK_NUMPAD4] & 0x80;
			m_keyboardState[30] = g_keyboardState[DIK_NUMPAD5] & 0x80;
			m_keyboardState[31] = g_keyboardState[DIK_NUMPAD6] & 0x80;
			m_keyboardState[32] = g_keyboardState[DIK_NUMPAD7] & 0x80;
			m_keyboardState[33] = g_keyboardState[DIK_NUMPAD8] & 0x80;
			m_keyboardState[34] = g_keyboardState[DIK_NUMPAD9] & 0x80;
			// LETTERS
			m_keyboardState[35] = g_keyboardState[DIK_Q] & 0x80;
			m_keyboardState[36] = g_keyboardState[DIK_W] & 0x80;
			m_keyboardState[37] = g_keyboardState[DIK_E] & 0x80;
			m_keyboardState[38] = g_keyboardState[DIK_R] & 0x80;
			m_keyboardState[39] = g_keyboardState[DIK_T] & 0x80;
			m_keyboardState[40] = g_keyboardState[DIK_Y] & 0x80;
			m_keyboardState[41] = g_keyboardState[DIK_U] & 0x80;
			m_keyboardState[42] = g_keyboardState[DIK_I] & 0x80;
			m_keyboardState[43] = g_keyboardState[DIK_O] & 0x80;
			m_keyboardState[44] = g_keyboardState[DIK_P] & 0x80;
			m_keyboardState[45] = g_keyboardState[DIK_A] & 0x80;
			m_keyboardState[46] = g_keyboardState[DIK_S] & 0x80;
			m_keyboardState[47] = g_keyboardState[DIK_D] & 0x80;
			m_keyboardState[48] = g_keyboardState[DIK_F] & 0x80;
			m_keyboardState[49] = g_keyboardState[DIK_G] & 0x80;
			m_keyboardState[50] = g_keyboardState[DIK_H] & 0x80;
			m_keyboardState[51] = g_keyboardState[DIK_J] & 0x80;
			m_keyboardState[52] = g_keyboardState[DIK_K] & 0x80;
			m_keyboardState[53] = g_keyboardState[DIK_L] & 0x80;
			m_keyboardState[54] = g_keyboardState[DIK_Z] & 0x80;
			m_keyboardState[55] = g_keyboardState[DIK_X] & 0x80;
			m_keyboardState[56] = g_keyboardState[DIK_C] & 0x80;
			m_keyboardState[57] = g_keyboardState[DIK_V] & 0x80;
			m_keyboardState[58] = g_keyboardState[DIK_B] & 0x80;
			m_keyboardState[59] = g_keyboardState[DIK_N] & 0x80;
			m_keyboardState[60] = g_keyboardState[DIK_M] & 0x80;
			// CONTROLS
			m_keyboardState[61] = g_keyboardState[DIK_ESCAPE] & 0x80;
			m_keyboardState[62] = g_keyboardState[DIK_TAB] & 0x80;
			m_keyboardState[63] = g_keyboardState[DIK_LSHIFT] & 0x80;
			m_keyboardState[64] = g_keyboardState[DIK_RSHIFT] & 0x80;
			m_keyboardState[65] = g_keyboardState[DIK_LCONTROL] & 0x80;
			m_keyboardState[66] = g_keyboardState[DIK_RCONTROL] & 0x80;
			m_keyboardState[67] = g_keyboardState[DIK_LALT] & 0x80;
			m_keyboardState[68] = g_keyboardState[DIK_RALT] & 0x80;
			m_keyboardState[69] = g_keyboardState[DIK_SPACE] & 0x80;
			m_keyboardState[70] = g_keyboardState[DIK_CAPSLOCK] & 0x80;
			m_keyboardState[71] = g_keyboardState[DIK_BACKSPACE] & 0x80;
			m_keyboardState[72] = g_keyboardState[DIK_RETURN] & 0x80;
			m_keyboardState[73] = g_keyboardState[DIK_DELETE] & 0x80;
			m_keyboardState[74] = g_keyboardState[DIK_LEFTARROW] & 0x80;
			m_keyboardState[75] = g_keyboardState[DIK_RIGHTARROW] & 0x80;
			m_keyboardState[76] = g_keyboardState[DIK_UPARROW] & 0x80;
			m_keyboardState[77] = g_keyboardState[DIK_DOWNARROW] & 0x80;
			m_keyboardState[78] = g_keyboardState[DIK_PGUP] & 0x80;
			m_keyboardState[79] = g_keyboardState[DIK_PGDN] & 0x80;
			m_keyboardState[80] = g_keyboardState[DIK_HOME] & 0x80;
			m_keyboardState[81] = g_keyboardState[DIK_END] & 0x80;
			m_keyboardState[82] = g_keyboardState[DIK_INSERT] & 0x80;
		}
		else
		{
			for (bool& i : m_keyboardState)
			{
				i = false;
			}
		}
	}

	bool DInput::ReadMouse()
	{
		// Get mouse state
		auto result = g_mouse->GetDeviceState(sizeof(DIMOUSESTATE), (LPVOID)&g_mouseState);
		if (SUCCEEDED(result))
			return true;

		// If the mouse lost focus or was not acquired then try to get control back.
		if ((result == DIERR_INPUTLOST) || (result == DIERR_NOTACQUIRED) || (result == DIERR_OTHERAPPHASPRIO))
		{
			g_mouse->Acquire();
		}

		return false;
	}

	bool DInput::ReadKeyboard()
	{
		// Get keyboard state
		auto result = g_keyboard->GetDeviceState(sizeof(g_keyboardState), (LPVOID)&g_keyboardState);
		if (SUCCEEDED(result))
			return true;

		// If the keyboard lost focus or was not acquired then try to get control back.
		if ((result == DIERR_INPUTLOST) || (result == DIERR_NOTACQUIRED))
		{
			g_keyboard->Acquire();
		}

		return false;
	}
}

// Constant          Note
// DIK_ESCAPE   
// DIK_1             On main keyboard 
// DIK_2             On main keyboard 
// DIK_3             On main keyboard 
// DIK_4             On main keyboard 
// DIK_5             On main keyboard 
// DIK_6             On main keyboard 
// DIK_7             On main keyboard 
// DIK_8             On main keyboard 
// DIK_9             On main keyboard 
// DIK_0             On main keyboard 
// DIK_MINUS         On main keyboard 
// DIK_EQUALS        On main keyboard 
// DIK_BACK          The backspace key  
// DIK_TAB   
// DIK_Q   
// DIK_W   
// DIK_E   
// DIK_R   
// DIK_T   
// DIK_Y   
// DIK_U   
// DIK_I   
// DIK_O   
// DIK_P   
// DIK_LBRACKET      The [ key  
// DIK_RBRACKET      The ] key  
// DIK_RETURN        enter key on main keyboard  
// DIK_LCONTROL      Left ctrl key  
// DIK_A   
// DIK_S   
// DIK_D   
// DIK_F   
// DIK_G   
// DIK_H   
// DIK_J   
// DIK_K   
// DIK_L   
// DIK_SEMICOLON   
// DIK_APOSTROPHE   
// DIK_GRAVE          Grave accent (`) key  
// DIK_LSHIFT         Left shift key  
// DIK_BACKSLASH   
// DIK_Z   
// DIK_X   
// DIK_C   
// DIK_V   
// DIK_B   
// DIK_N   
// DIK_M   
// DIK_COMMA   
// DIK_PERIOD         On main keyboard  
// DIK_SLASH          Forward slash on main keyboard 
// DIK_RSHIFT         Right shift key  
// DIK_MULTIPLY       The * key on numeric keypad  
// DIK_LMENU          Left alt key  
// DIK_SPACE          spacebar  
// DIK_CAPITAL        caps lock key  
// DIK_F1   
// DIK_F2   
// DIK_F3   
// DIK_F4   
// DIK_F5   
// DIK_F6   
// DIK_F7   
// DIK_F8   
// DIK_F9   
// DIK_F10   
// DIK_NUMLOCK   
// DIK_SCROLL         scroll lock 
// DIK_NUMPAD7   
// DIK_NUMPAD8   
// DIK_NUMPAD9   
// DIK_SUBTRACT       minus sign on numeric keypad  
// DIK_NUMPAD4   
// DIK_NUMPAD5   
// DIK_NUMPAD6   
// DIK_ADD            plus sign on numeric keypad  
// DIK_NUMPAD1   
// DIK_NUMPAD2   
// DIK_NUMPAD3   
// DIK_NUMPAD0   
// DIK_DECIMAL        period (decimal point) on numeric keypad  
// DIK_F11   
// DIK_F12   
// DIK_F13  
// DIK_F14  
// DIK_F15  
// DIK_KANA           On Japanese keyboard 
// DIK_CONVERT        On Japanese keyboard 
// DIK_NOCONVERT      On Japanese keyboard 
// DIK_YEN            On Japanese keyboard 
// DIK_NUMPADEQUALS   On numeric keypad (NEC PC98) 
// DIK_CIRCUMFLEX     On Japanese keyboard 
// DIK_AT             On Japanese keyboard 
// DIK_COLON          On Japanese keyboard 
// DIK_UNDERLINE      On Japanese keyboard 
// DIK_KANJI          On Japanese keyboard 
// DIK_STOP           On Japanese keyboard 
// DIK_AX             On Japanese keyboard 
// DIK_UNLABELED      On Japanese keyboard 
// DIK_NUMPADENTER   
// DIK_RCONTROL       Right ctrl key  
// DIK_NUMPADCOMMA    comma on NEC PC98 numeric keypad 
// DIK_DIVIDE         Forward slash on numeric keypad  
// DIK_SYSRQ   
// DIK_RMENU          Right alt key  
// DIK_HOME   
// DIK_UP             up arrow  
// DIK_PRIOR          page up  
// DIK_LEFT           left arrow  
// DIK_RIGHT          right arrow  
// DIK_END   
// DIK_DOWN           down arrow  
// DIK_NEXT           page down  
// DIK_INSERT   
// DIK_DELETE   
// DIK_LWIN           Left Windows key  
// DIK_RWIN           Right Windows key  
// DIK_APPS           Application key  
// DIK_PAUSE