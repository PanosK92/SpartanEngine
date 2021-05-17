/*
Copyright(c] 2016-2021 Panos Karabelas

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

//= INCLUDES =======
#include "Spartan.h"
#include "Input.h"
#include "SDL.h"
//==================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    void Input::PollKeyboard()
    {
        // Get keyboard state
        const Uint8* keys_states = SDL_GetKeyboardState(nullptr);

        // Function
        m_keys[0] = keys_states[SDL_SCANCODE_F1];
        m_keys[1] = keys_states[SDL_SCANCODE_F2];
        m_keys[2] = keys_states[SDL_SCANCODE_F3];
        m_keys[3] = keys_states[SDL_SCANCODE_F4];
        m_keys[4] = keys_states[SDL_SCANCODE_F5];
        m_keys[5] = keys_states[SDL_SCANCODE_F6];
        m_keys[6] = keys_states[SDL_SCANCODE_F7];
        m_keys[7] = keys_states[SDL_SCANCODE_F8];
        m_keys[8] = keys_states[SDL_SCANCODE_F9];
        m_keys[9] = keys_states[SDL_SCANCODE_F10];
        m_keys[10] = keys_states[SDL_SCANCODE_F11];
        m_keys[11] = keys_states[SDL_SCANCODE_F12];
        m_keys[12] = keys_states[SDL_SCANCODE_F13];
        m_keys[13] = keys_states[SDL_SCANCODE_F14];
        m_keys[14] = keys_states[SDL_SCANCODE_F15];
        // Numbers
        m_keys[15] = keys_states[SDL_SCANCODE_0];
        m_keys[16] = keys_states[SDL_SCANCODE_1];
        m_keys[17] = keys_states[SDL_SCANCODE_2];
        m_keys[18] = keys_states[SDL_SCANCODE_3];
        m_keys[19] = keys_states[SDL_SCANCODE_4];
        m_keys[20] = keys_states[SDL_SCANCODE_5];
        m_keys[21] = keys_states[SDL_SCANCODE_6];
        m_keys[22] = keys_states[SDL_SCANCODE_7];
        m_keys[23] = keys_states[SDL_SCANCODE_8];
        m_keys[24] = keys_states[SDL_SCANCODE_9];
        // Keypad
        m_keys[25] = keys_states[SDL_SCANCODE_KP_0];
        m_keys[26] = keys_states[SDL_SCANCODE_KP_1];
        m_keys[27] = keys_states[SDL_SCANCODE_KP_2];
        m_keys[28] = keys_states[SDL_SCANCODE_KP_3];
        m_keys[29] = keys_states[SDL_SCANCODE_KP_4];
        m_keys[30] = keys_states[SDL_SCANCODE_KP_5];
        m_keys[31] = keys_states[SDL_SCANCODE_KP_6];
        m_keys[32] = keys_states[SDL_SCANCODE_KP_7];
        m_keys[33] = keys_states[SDL_SCANCODE_KP_8];
        m_keys[34] = keys_states[SDL_SCANCODE_KP_9];
        // Letters
        m_keys[35] = keys_states[SDL_SCANCODE_Q];
        m_keys[36] = keys_states[SDL_SCANCODE_W];
        m_keys[37] = keys_states[SDL_SCANCODE_E];
        m_keys[38] = keys_states[SDL_SCANCODE_R];
        m_keys[39] = keys_states[SDL_SCANCODE_T];
        m_keys[40] = keys_states[SDL_SCANCODE_Y];
        m_keys[41] = keys_states[SDL_SCANCODE_U];
        m_keys[42] = keys_states[SDL_SCANCODE_I];
        m_keys[43] = keys_states[SDL_SCANCODE_O];
        m_keys[44] = keys_states[SDL_SCANCODE_P];
        m_keys[45] = keys_states[SDL_SCANCODE_A];
        m_keys[46] = keys_states[SDL_SCANCODE_S];
        m_keys[47] = keys_states[SDL_SCANCODE_D];
        m_keys[48] = keys_states[SDL_SCANCODE_F];
        m_keys[49] = keys_states[SDL_SCANCODE_G];
        m_keys[50] = keys_states[SDL_SCANCODE_H];
        m_keys[51] = keys_states[SDL_SCANCODE_J];
        m_keys[52] = keys_states[SDL_SCANCODE_K];
        m_keys[53] = keys_states[SDL_SCANCODE_L];
        m_keys[54] = keys_states[SDL_SCANCODE_Z];
        m_keys[55] = keys_states[SDL_SCANCODE_X];
        m_keys[56] = keys_states[SDL_SCANCODE_C];
        m_keys[57] = keys_states[SDL_SCANCODE_V];
        m_keys[58] = keys_states[SDL_SCANCODE_B];
        m_keys[59] = keys_states[SDL_SCANCODE_N];
        m_keys[60] = keys_states[SDL_SCANCODE_M];
        // Controls
        m_keys[61] = keys_states[SDL_SCANCODE_ESCAPE];
        m_keys[62] = keys_states[SDL_SCANCODE_TAB];
        m_keys[63] = keys_states[SDL_SCANCODE_LSHIFT];
        m_keys[64] = keys_states[SDL_SCANCODE_RSHIFT];
        m_keys[65] = keys_states[SDL_SCANCODE_LCTRL];
        m_keys[66] = keys_states[SDL_SCANCODE_RCTRL];
        m_keys[67] = keys_states[SDL_SCANCODE_LALT];
        m_keys[68] = keys_states[SDL_SCANCODE_RALT];
        m_keys[69] = keys_states[SDL_SCANCODE_SPACE];
        m_keys[70] = keys_states[SDL_SCANCODE_CAPSLOCK];
        m_keys[71] = keys_states[SDL_SCANCODE_BACKSPACE];
        m_keys[72] = keys_states[SDL_SCANCODE_RETURN];
        m_keys[73] = keys_states[SDL_SCANCODE_DELETE];
        m_keys[74] = keys_states[SDL_SCANCODE_LEFT];
        m_keys[75] = keys_states[SDL_SCANCODE_RIGHT];
        m_keys[76] = keys_states[SDL_SCANCODE_UP];
        m_keys[77] = keys_states[SDL_SCANCODE_DOWN];
        m_keys[78] = keys_states[SDL_SCANCODE_PAGEUP];
        m_keys[79] = keys_states[SDL_SCANCODE_PAGEDOWN];
        m_keys[80] = keys_states[SDL_SCANCODE_HOME];
        m_keys[81] = keys_states[SDL_SCANCODE_END];
        m_keys[82] = keys_states[SDL_SCANCODE_INSERT];
    }
}
