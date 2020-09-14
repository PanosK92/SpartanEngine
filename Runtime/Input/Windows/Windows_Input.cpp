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

//= INCLUDES =========================
#include "Spartan.h"
#include "../Input.h"
#ifdef API_INPUT_WINDOWS
    #pragma comment(lib, "XInput.lib")
    #include <windows.h>
    #include <xinput.h>
#endif
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    XINPUT_STATE    g_gamepad;
    uint32_t        g_gamepad_num = 0;

    Input::Input(Context* context) : ISubsystem(context)
    {
        const WindowData& window_data   = context->m_engine->GetWindowData();
        const auto window_handle        = static_cast<HWND>(window_data.handle);

        if (!window_handle)
        {
            LOG_ERROR("Invalid window handle");
            return;
        }

        // Register mouse
        {
            #ifndef HID_USAGE_PAGE_GENERIC
            #define HID_USAGE_PAGE_GENERIC         ((USHORT) 0x01)
            #endif
            #ifndef HID_USAGE_GENERIC_MOUSE
            #define HID_USAGE_GENERIC_MOUSE        ((USHORT) 0x02)
            #endif
            RAWINPUTDEVICE Rid[1];
            Rid[0].usUsagePage  = HID_USAGE_PAGE_GENERIC;
            Rid[0].usUsage      = HID_USAGE_GENERIC_MOUSE;
            Rid[0].dwFlags      = RIDEV_INPUTSINK;
            Rid[0].hwndTarget   = window_handle;
            RegisterRawInputDevices(Rid, 1, sizeof(Rid[0]));
        }

        SUBSCRIBE_TO_EVENT(EventType::WindowData, EVENT_HANDLER(OnWindowData));
    }

    void Input::OnWindowData()
    {
        // OnWindowData can run multiple times per frame (for each window message)
        // So only code within the if statement scope will run once per frame
        if (m_is_new_frame)
        {
            m_mouse_delta           = Vector2::Zero;
            m_keys_previous_frame   = m_keys;
            m_check_for_new_device  = false;
        }

        const WindowData& window_data   = m_context->m_engine->GetWindowData();
        const HWND window_handle        = static_cast<HWND>(window_data.handle);

        // Mouse
        {
            // Keys
            m_keys[start_index_mouse]       = (::GetKeyState(VK_LBUTTON) & 0x8000) != 0; // Left button pressed
            m_keys[start_index_mouse + 1]   = (::GetKeyState(VK_MBUTTON) & 0x8000) != 0; // Middle button pressed
            m_keys[start_index_mouse + 2]   = (::GetKeyState(VK_RBUTTON) & 0x8000) != 0; // Right button pressed

            // Delta
            if (window_data.message == WM_INPUT)
            {
                UINT dwSize = 48;
                static BYTE lpb[48];
                GetRawInputData((HRAWINPUT)window_data.lparam, RID_INPUT, lpb, &dwSize, sizeof(RAWINPUTHEADER));
                RAWINPUT* raw = (RAWINPUT*)lpb;
                if (raw->header.dwType == RIM_TYPEMOUSE)
                {
                    m_mouse_delta.x = static_cast<float>(raw->data.mouse.lLastX);
                    m_mouse_delta.y = static_cast<float>(raw->data.mouse.lLastY);
                }
            }

            // Position
            if (window_handle == ::GetActiveWindow())
            {
                POINT mouse_screen_pos;
                if (::GetCursorPos(&mouse_screen_pos))
                {
                    ::ScreenToClient(window_handle, &mouse_screen_pos);
                    m_mouse_position = Vector2(static_cast<float>(mouse_screen_pos.x), static_cast<float>(mouse_screen_pos.y));
                }
            }

            // Wheel
            {
                m_mouse_wheel_delta = (float)GET_WHEEL_DELTA_WPARAM(window_data.wparam) / (float)WHEEL_DELTA;
            }
        }

        // KEYBOARD
        {
            #define is_pressed(key_code) (::GetKeyState(key_code) & 0x8000) != 0

            // FUNCTION
            m_keys[0]   = is_pressed(VK_F1);
            m_keys[1]   = is_pressed(VK_F2);
            m_keys[2]   = is_pressed(VK_F3);
            m_keys[3]   = is_pressed(VK_F4);
            m_keys[4]   = is_pressed(VK_F5);
            m_keys[5]   = is_pressed(VK_F6);
            m_keys[6]   = is_pressed(VK_F7);
            m_keys[7]   = is_pressed(VK_F8);
            m_keys[8]   = is_pressed(VK_F9);
            m_keys[9]   = is_pressed(VK_F10);
            m_keys[10]  = is_pressed(VK_F11);
            m_keys[11]  = is_pressed(VK_F12);
            m_keys[12]  = is_pressed(VK_F13);
            m_keys[13]  = is_pressed(VK_F14);
            m_keys[14]  = is_pressed(VK_F15);
            // NUMBERS
            m_keys[15]  = is_pressed('0');
            m_keys[16]  = is_pressed('1');
            m_keys[17]  = is_pressed('2');
            m_keys[18]  = is_pressed('3');
            m_keys[19]  = is_pressed('4');
            m_keys[20]  = is_pressed('5');
            m_keys[21]  = is_pressed('6');
            m_keys[22]  = is_pressed('7');
            m_keys[23]  = is_pressed('8');
            m_keys[24]  = is_pressed('9');
            // KEYPAD
            m_keys[25]  = is_pressed(VK_NUMPAD0);
            m_keys[26]  = is_pressed(VK_NUMPAD1);
            m_keys[27]  = is_pressed(VK_NUMPAD2);
            m_keys[28]  = is_pressed(VK_NUMPAD3);
            m_keys[29]  = is_pressed(VK_NUMPAD4);
            m_keys[30]  = is_pressed(VK_NUMPAD5);
            m_keys[31]  = is_pressed(VK_NUMPAD6);
            m_keys[32]  = is_pressed(VK_NUMPAD7);
            m_keys[33]  = is_pressed(VK_NUMPAD8);
            m_keys[34]  = is_pressed(VK_NUMPAD9);
            // LETTERS
            m_keys[35]  = is_pressed('Q');
            m_keys[36]  = is_pressed('W');
            m_keys[37]  = is_pressed('E');
            m_keys[38]  = is_pressed('R');
            m_keys[39]  = is_pressed('T');
            m_keys[40]  = is_pressed('Y');
            m_keys[41]  = is_pressed('U');
            m_keys[42]  = is_pressed('I');
            m_keys[43]  = is_pressed('O');
            m_keys[44]  = is_pressed('P');
            m_keys[45]  = is_pressed('A');
            m_keys[46]  = is_pressed('S');
            m_keys[47]  = is_pressed('D');
            m_keys[48]  = is_pressed('F');
            m_keys[49]  = is_pressed('G');
            m_keys[50]  = is_pressed('H');
            m_keys[51]  = is_pressed('J');
            m_keys[52]  = is_pressed('K');
            m_keys[53]  = is_pressed('L');
            m_keys[54]  = is_pressed('Z');
            m_keys[55]  = is_pressed('X');
            m_keys[56]  = is_pressed('C');
            m_keys[57]  = is_pressed('V');
            m_keys[58]  = is_pressed('B');
            m_keys[59]  = is_pressed('N');
            m_keys[60]  = is_pressed('M');
            // CONTROLS
            m_keys[61]  = is_pressed(VK_ESCAPE);
            m_keys[62]  = is_pressed(VK_TAB);
            m_keys[63]  = is_pressed(VK_LSHIFT);
            m_keys[64]  = is_pressed(VK_RSHIFT);
            m_keys[65]  = is_pressed(VK_LCONTROL);
            m_keys[66]  = is_pressed(VK_RCONTROL);
            m_keys[67]  = is_pressed(VK_LMENU);
            m_keys[68]  = is_pressed(VK_RMENU);
            m_keys[69]  = is_pressed(VK_SPACE);
            m_keys[70]  = is_pressed(VK_CAPITAL);
            m_keys[71]  = is_pressed(VK_BACK);
            m_keys[72]  = is_pressed(VK_RETURN);
            m_keys[73]  = is_pressed(VK_DELETE);
            m_keys[74]  = is_pressed(VK_LEFT);
            m_keys[75]  = is_pressed(VK_RIGHT);
            m_keys[76]  = is_pressed(VK_UP);
            m_keys[77]  = is_pressed(VK_DOWN);
            m_keys[78]  = is_pressed(VK_PRIOR);
            m_keys[79]  = is_pressed(VK_NEXT);
            m_keys[80]  = is_pressed(VK_HOME);
            m_keys[81]  = is_pressed(VK_END);
            m_keys[82]  = is_pressed(VK_INSERT);
        }

        // Check for any device changes (e.g. gamepad connected)
        if (window_data.message == WM_DEVICECHANGE)
        {
            m_check_for_new_device = true;
        }

        m_is_new_frame = false;
    }

    void Input::SetMousePosition(const Math::Vector2& position)
    {
        const WindowData& window_data   = m_context->m_engine->GetWindowData();
        const HWND window_handle        = static_cast<HWND>(window_data.handle);

        if (window_handle == ::GetActiveWindow())
        {
            POINT mouse_screen_pos = POINT{ static_cast<LONG>(position.x), static_cast<LONG>(position.y) };
            if (::ClientToScreen(window_handle, &mouse_screen_pos))
            {
                ::SetCursorPos(mouse_screen_pos.x, mouse_screen_pos.y);
            }
        }
    }

    void Input::Tick(float delta_time)
    {
        // Check for new device
        if (m_check_for_new_device)
        {
            // Check for an XBOX controller
            ZeroMemory(&g_gamepad, sizeof(XINPUT_STATE));
            m_gamepad_connected = XInputGetState(g_gamepad_num, &g_gamepad) == ERROR_SUCCESS;

            if (m_gamepad_connected)
            {
                LOG_INFO("Gamepad connected");
            }
            else
            {
                LOG_INFO("Gamepad disconnected");
            }

            m_check_for_new_device = false;
        }

        if (m_gamepad_connected)
        {
            m_keys[start_index_gamepad]         = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP;
            m_keys[start_index_gamepad + 1]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN;
            m_keys[start_index_gamepad + 2]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT;
            m_keys[start_index_gamepad + 3]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT;
            m_keys[start_index_gamepad + 4]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_A;
            m_keys[start_index_gamepad + 5]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_B;
            m_keys[start_index_gamepad + 6]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_X;
            m_keys[start_index_gamepad + 7]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_Y;
            m_keys[start_index_gamepad + 6]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_START;
            m_keys[start_index_gamepad + 7]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_BACK;
            m_keys[start_index_gamepad + 8]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB;
            m_keys[start_index_gamepad + 9]     = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB;
            m_keys[start_index_gamepad + 10]    = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER;
            m_keys[start_index_gamepad + 11]    = g_gamepad.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER;

            m_gamepad_trigger_left  = static_cast<float>(g_gamepad.Gamepad.bLeftTrigger);
            m_gamepad_trigger_right = static_cast<float>(g_gamepad.Gamepad.bRightTrigger);
            m_gamepad_thumb_left.x  = static_cast<float>(g_gamepad.Gamepad.sThumbLX);
            m_gamepad_thumb_left.y  = static_cast<float>(g_gamepad.Gamepad.sThumbLY);
            m_gamepad_thumb_right.x = static_cast<float>(g_gamepad.Gamepad.sThumbRX);
            m_gamepad_thumb_right.y = static_cast<float>(g_gamepad.Gamepad.sThumbRY);

            if (m_gamepad_trigger_left != 0)    m_gamepad_trigger_left  /= 255.0f;    // Convert [0, 255] to [0, 1]
            if (m_gamepad_trigger_right != 0)   m_gamepad_trigger_right /= 255.0f;    // Convert [0, 255] to [0, 1]
            if (m_gamepad_thumb_left.x != 0)    m_gamepad_thumb_left.x  = m_gamepad_thumb_left.x    < 0 ? m_gamepad_thumb_left.x    / 32768.0f : m_gamepad_thumb_left.x     / 32767.0f; // Convert [-32768, 32767] to [-1, 1]
            if (m_gamepad_thumb_left.y != 0)    m_gamepad_thumb_left.y  = m_gamepad_thumb_left.y    < 0 ? m_gamepad_thumb_left.y    / 32768.0f : m_gamepad_thumb_left.y     / 32767.0f; // Convert [-32768, 32767] to [-1, 1]
            if (m_gamepad_thumb_right.x != 0)   m_gamepad_thumb_right.x = m_gamepad_thumb_right.x   < 0 ? m_gamepad_thumb_right.x   / 32768.0f : m_gamepad_thumb_right.x    / 32767.0f; // Convert [-32768, 32767] to [-1, 1]
            if (m_gamepad_thumb_right.y != 0)   m_gamepad_thumb_right.y = m_gamepad_thumb_right.y   < 0 ? m_gamepad_thumb_right.y   / 32768.0f : m_gamepad_thumb_right.y    / 32767.0f; // Convert [-32768, 32767] to [-1, 1]
        }
        else
        {
            for (auto i = start_index_gamepad; i <= start_index_gamepad + 11; i++)
            {
                m_keys[i] = false;
            }
        }

        m_is_new_frame = true;
    }

    bool Input::GamepadVibrate(const float left_motor_speed, const float right_motor_speed) const
    {
        if (!m_gamepad_connected)
            return false;

        XINPUT_VIBRATION vibration;
        ZeroMemory(&vibration, sizeof(XINPUT_VIBRATION));
        
        vibration.wLeftMotorSpeed   = static_cast<int>(Helper::Clamp(left_motor_speed, 0.0f, 1.0f) * 65535);    // Convert [0, 1] to [0, 65535]
        vibration.wRightMotorSpeed  = static_cast<int>(Helper::Clamp(right_motor_speed, 0.0f, 1.0f) * 65535);   // Convert [0, 1] to [0, 65535]

        return XInputSetState(g_gamepad_num, &vibration) == ERROR_SUCCESS;
    }
}

// Constant          Note
// VK_ESCAPE   
// VK_1             On main keyboard 
// VK_2             On main keyboard 
// VK_3             On main keyboard 
// VK_4             On main keyboard 
// VK_5             On main keyboard 
// VK_6             On main keyboard 
// VK_7             On main keyboard 
// VK_8             On main keyboard 
// VK_9             On main keyboard 
// VK_0             On main keyboard 
// VK_MINUS         On main keyboard 
// VK_EQUALS        On main keyboard 
// VK_BACK          The backspace key  
// VK_TAB   
// VK_Q   
// VK_W   
// VK_E   
// VK_R   
// VK_T   
// VK_Y   
// VK_U   
// VK_I   
// VK_O   
// VK_P   
// VK_LBRACKET      The [ key  
// VK_RBRACKET      The ] key  
// VK_RETURN        enter key on main keyboard  
// VK_LCONTROL      Left ctrl key  
// VK_A   
// VK_S   
// VK_D   
// VK_F   
// VK_G   
// VK_H   
// VK_J   
// VK_K   
// VK_L   
// VK_SEMICOLON   
// VK_APOSTROPHE   
// VK_GRAVE          Grave accent (`) key  
// VK_LSHIFT         Left shift key  
// VK_BACKSLASH   
// VK_Z   
// VK_X   
// VK_C   
// VK_V   
// VK_B   
// VK_N   
// VK_M   
// VK_COMMA   
// VK_PERIOD         On main keyboard  
// VK_SLASH          Forward slash on main keyboard 
// VK_RSHIFT         Right shift key  
// VK_MULTIPLY       The * key on numeric keypad  
// VK_LMENU          Left alt key  
// VK_SPACE          spacebar  
// VK_CAPITAL        caps lock key  
// VK_F1   
// VK_F2   
// VK_F3   
// VK_F4   
// VK_F5   
// VK_F6   
// VK_F7   
// VK_F8   
// VK_F9   
// VK_F10   
// VK_NUMLOCK   
// VK_SCROLL         scroll lock 
// VK_NUMPAD7   
// VK_NUMPAD8   
// VK_NUMPAD9   
// VK_SUBTRACT       minus sign on numeric keypad  
// VK_NUMPAD4   
// VK_NUMPAD5   
// VK_NUMPAD6   
// VK_ADD            plus sign on numeric keypad  
// VK_NUMPAD1   
// VK_NUMPAD2   
// VK_NUMPAD3   
// VK_NUMPAD0   
// VK_DECIMAL        period (decimal point) on numeric keypad  
// VK_F11   
// VK_F12   
// VK_F13  
// VK_F14  
// VK_F15  
// VK_KANA           On Japanese keyboard 
// VK_CONVERT        On Japanese keyboard 
// VK_NOCONVERT      On Japanese keyboard 
// VK_YEN            On Japanese keyboard 
// VK_NUMPADEQUALS   On numeric keypad (NEC PC98) 
// VK_CIRCUMFLEX     On Japanese keyboard 
// VK_AT             On Japanese keyboard 
// VK_COLON          On Japanese keyboard 
// VK_UNDERLINE      On Japanese keyboard 
// VK_KANJI          On Japanese keyboard 
// VK_STOP           On Japanese keyboard 
// VK_AX             On Japanese keyboard 
// VK_UNLABELED      On Japanese keyboard 
// VK_NUMPADENTER   
// VK_RCONTROL       Right ctrl key  
// VK_NUMPADCOMMA    comma on NEC PC98 numeric keypad 
// VK_DIVIDE         Forward slash on numeric keypad  
// VK_SYSRQ   
// VK_RMENU          Right alt key  
// VK_HOME   
// VK_UP             up arrow  
// VK_PRIOR          page up  
// VK_LEFT           left arrow  
// VK_RIGHT          right arrow  
// VK_END   
// VK_DOWN           down arrow  
// VK_NEXT           page down  
// VK_INSERT   
// VK_DELETE   
// VK_LWIN           Left Windows key  
// VK_RWIN           Right Windows key  
// VK_APPS           Application key  
// VK_PAUSE
