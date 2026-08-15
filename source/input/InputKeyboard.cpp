/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"], to deal
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

//= INCLUDES ========
#include "pch.h"
#include "Input.h"
SP_WARNINGS_OFF
#include <SDL3/SDL.h>
SP_WARNINGS_ON
//===================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Input::PollKeyboard()
    {
        // get state
        int num_keys = 0;
        const bool* key_states = SDL_GetKeyboardState(&num_keys);
        SP_ASSERT_MSG(key_states != nullptr, "Failed to get keyboard state");

        // function
        m_keys[0]  = key_states[SDL_SCANCODE_F1];
        m_keys[1]  = key_states[SDL_SCANCODE_F2];
        m_keys[2]  = key_states[SDL_SCANCODE_F3];
        m_keys[3]  = key_states[SDL_SCANCODE_F4];
        m_keys[4]  = key_states[SDL_SCANCODE_F5];
        m_keys[5]  = key_states[SDL_SCANCODE_F6];
        m_keys[6]  = key_states[SDL_SCANCODE_F7];
        m_keys[7]  = key_states[SDL_SCANCODE_F8];
        m_keys[8]  = key_states[SDL_SCANCODE_F9];
        m_keys[9]  = key_states[SDL_SCANCODE_F10];
        m_keys[10] = key_states[SDL_SCANCODE_F11];
        m_keys[11] = key_states[SDL_SCANCODE_F12];
        m_keys[12] = key_states[SDL_SCANCODE_F13];
        m_keys[13] = key_states[SDL_SCANCODE_F14];
        m_keys[14] = key_states[SDL_SCANCODE_F15];
        // numbers
        m_keys[15] = key_states[SDL_SCANCODE_0];
        m_keys[16] = key_states[SDL_SCANCODE_1];
        m_keys[17] = key_states[SDL_SCANCODE_2];
        m_keys[18] = key_states[SDL_SCANCODE_3];
        m_keys[19] = key_states[SDL_SCANCODE_4];
        m_keys[20] = key_states[SDL_SCANCODE_5];
        m_keys[21] = key_states[SDL_SCANCODE_6];
        m_keys[22] = key_states[SDL_SCANCODE_7];
        m_keys[23] = key_states[SDL_SCANCODE_8];
        m_keys[24] = key_states[SDL_SCANCODE_9];
        // keypad
        m_keys[25] = key_states[SDL_SCANCODE_KP_0];
        m_keys[26] = key_states[SDL_SCANCODE_KP_1];
        m_keys[27] = key_states[SDL_SCANCODE_KP_2];
        m_keys[28] = key_states[SDL_SCANCODE_KP_3];
        m_keys[29] = key_states[SDL_SCANCODE_KP_4];
        m_keys[30] = key_states[SDL_SCANCODE_KP_5];
        m_keys[31] = key_states[SDL_SCANCODE_KP_6];
        m_keys[32] = key_states[SDL_SCANCODE_KP_7];
        m_keys[33] = key_states[SDL_SCANCODE_KP_8];
        m_keys[34] = key_states[SDL_SCANCODE_KP_9];
        // letters
        m_keys[35] = key_states[SDL_SCANCODE_Q];
        m_keys[36] = key_states[SDL_SCANCODE_W];
        m_keys[37] = key_states[SDL_SCANCODE_E];
        m_keys[38] = key_states[SDL_SCANCODE_R];
        m_keys[39] = key_states[SDL_SCANCODE_T];
        m_keys[40] = key_states[SDL_SCANCODE_Y];
        m_keys[41] = key_states[SDL_SCANCODE_U];
        m_keys[42] = key_states[SDL_SCANCODE_I];
        m_keys[43] = key_states[SDL_SCANCODE_O];
        m_keys[44] = key_states[SDL_SCANCODE_P];
        m_keys[45] = key_states[SDL_SCANCODE_A];
        m_keys[46] = key_states[SDL_SCANCODE_S];
        m_keys[47] = key_states[SDL_SCANCODE_D];
        m_keys[48] = key_states[SDL_SCANCODE_F];
        m_keys[49] = key_states[SDL_SCANCODE_G];
        m_keys[50] = key_states[SDL_SCANCODE_H];
        m_keys[51] = key_states[SDL_SCANCODE_J];
        m_keys[52] = key_states[SDL_SCANCODE_K];
        m_keys[53] = key_states[SDL_SCANCODE_L];
        m_keys[54] = key_states[SDL_SCANCODE_Z];
        m_keys[55] = key_states[SDL_SCANCODE_X];
        m_keys[56] = key_states[SDL_SCANCODE_C];
        m_keys[57] = key_states[SDL_SCANCODE_V];
        m_keys[58] = key_states[SDL_SCANCODE_B];
        m_keys[59] = key_states[SDL_SCANCODE_N];
        m_keys[60] = key_states[SDL_SCANCODE_M];
        // controls
        m_keys[61] = key_states[SDL_SCANCODE_ESCAPE];
        m_keys[62] = key_states[SDL_SCANCODE_TAB];
        m_keys[63] = key_states[SDL_SCANCODE_LSHIFT];
        m_keys[64] = key_states[SDL_SCANCODE_RSHIFT];
        m_keys[65] = key_states[SDL_SCANCODE_LCTRL];
        m_keys[66] = key_states[SDL_SCANCODE_RCTRL];
        m_keys[67] = key_states[SDL_SCANCODE_LALT];
        m_keys[68] = key_states[SDL_SCANCODE_RALT];
        m_keys[69] = key_states[SDL_SCANCODE_SPACE];
        m_keys[70] = key_states[SDL_SCANCODE_CAPSLOCK];
        m_keys[71] = key_states[SDL_SCANCODE_BACKSPACE];
        m_keys[72] = key_states[SDL_SCANCODE_RETURN];
        m_keys[73] = key_states[SDL_SCANCODE_DELETE];
        m_keys[74] = key_states[SDL_SCANCODE_LEFT];
        m_keys[75] = key_states[SDL_SCANCODE_RIGHT];
        m_keys[76] = key_states[SDL_SCANCODE_UP];
        m_keys[77] = key_states[SDL_SCANCODE_DOWN];
        m_keys[78] = key_states[SDL_SCANCODE_PAGEUP];
        m_keys[79] = key_states[SDL_SCANCODE_PAGEDOWN];
        m_keys[80] = key_states[SDL_SCANCODE_HOME];
        m_keys[81] = key_states[SDL_SCANCODE_END];
        m_keys[82] = key_states[SDL_SCANCODE_INSERT];
    }
}
