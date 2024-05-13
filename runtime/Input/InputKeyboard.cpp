/*
Copyright(c] 2016-2023 Panos Karabelas

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
#include "pch.h"
#include "Input.h"
#include <SDL/SDL.h>
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
        GetKeys()[0]  = keys_states[SDL_SCANCODE_F1];
        GetKeys()[1]  = keys_states[SDL_SCANCODE_F2];
        GetKeys()[2]  = keys_states[SDL_SCANCODE_F3];
        GetKeys()[3]  = keys_states[SDL_SCANCODE_F4];
        GetKeys()[4]  = keys_states[SDL_SCANCODE_F5];
        GetKeys()[5]  = keys_states[SDL_SCANCODE_F6];
        GetKeys()[6]  = keys_states[SDL_SCANCODE_F7];
        GetKeys()[7]  = keys_states[SDL_SCANCODE_F8];
        GetKeys()[8]  = keys_states[SDL_SCANCODE_F9];
        GetKeys()[9]  = keys_states[SDL_SCANCODE_F10];
        GetKeys()[10] = keys_states[SDL_SCANCODE_F11];
        GetKeys()[11] = keys_states[SDL_SCANCODE_F12];
        GetKeys()[12] = keys_states[SDL_SCANCODE_F13];
        GetKeys()[13] = keys_states[SDL_SCANCODE_F14];
        GetKeys()[14] = keys_states[SDL_SCANCODE_F15];
        // Numbers
        GetKeys()[15] = keys_states[SDL_SCANCODE_0];
        GetKeys()[16] = keys_states[SDL_SCANCODE_1];
        GetKeys()[17] = keys_states[SDL_SCANCODE_2];
        GetKeys()[18] = keys_states[SDL_SCANCODE_3];
        GetKeys()[19] = keys_states[SDL_SCANCODE_4];
        GetKeys()[20] = keys_states[SDL_SCANCODE_5];
        GetKeys()[21] = keys_states[SDL_SCANCODE_6];
        GetKeys()[22] = keys_states[SDL_SCANCODE_7];
        GetKeys()[23] = keys_states[SDL_SCANCODE_8];
        GetKeys()[24] = keys_states[SDL_SCANCODE_9];
        // Keypad
        GetKeys()[25] = keys_states[SDL_SCANCODE_KP_0];
        GetKeys()[26] = keys_states[SDL_SCANCODE_KP_1];
        GetKeys()[27] = keys_states[SDL_SCANCODE_KP_2];
        GetKeys()[28] = keys_states[SDL_SCANCODE_KP_3];
        GetKeys()[29] = keys_states[SDL_SCANCODE_KP_4];
        GetKeys()[30] = keys_states[SDL_SCANCODE_KP_5];
        GetKeys()[31] = keys_states[SDL_SCANCODE_KP_6];
        GetKeys()[32] = keys_states[SDL_SCANCODE_KP_7];
        GetKeys()[33] = keys_states[SDL_SCANCODE_KP_8];
        GetKeys()[34] = keys_states[SDL_SCANCODE_KP_9];
        // Letters
        GetKeys()[35] = keys_states[SDL_SCANCODE_Q];
        GetKeys()[36] = keys_states[SDL_SCANCODE_W];
        GetKeys()[37] = keys_states[SDL_SCANCODE_E];
        GetKeys()[38] = keys_states[SDL_SCANCODE_R];
        GetKeys()[39] = keys_states[SDL_SCANCODE_T];
        GetKeys()[40] = keys_states[SDL_SCANCODE_Y];
        GetKeys()[41] = keys_states[SDL_SCANCODE_U];
        GetKeys()[42] = keys_states[SDL_SCANCODE_I];
        GetKeys()[43] = keys_states[SDL_SCANCODE_O];
        GetKeys()[44] = keys_states[SDL_SCANCODE_P];
        GetKeys()[45] = keys_states[SDL_SCANCODE_A];
        GetKeys()[46] = keys_states[SDL_SCANCODE_S];
        GetKeys()[47] = keys_states[SDL_SCANCODE_D];
        GetKeys()[48] = keys_states[SDL_SCANCODE_F];
        GetKeys()[49] = keys_states[SDL_SCANCODE_G];
        GetKeys()[50] = keys_states[SDL_SCANCODE_H];
        GetKeys()[51] = keys_states[SDL_SCANCODE_J];
        GetKeys()[52] = keys_states[SDL_SCANCODE_K];
        GetKeys()[53] = keys_states[SDL_SCANCODE_L];
        GetKeys()[54] = keys_states[SDL_SCANCODE_Z];
        GetKeys()[55] = keys_states[SDL_SCANCODE_X];
        GetKeys()[56] = keys_states[SDL_SCANCODE_C];
        GetKeys()[57] = keys_states[SDL_SCANCODE_V];
        GetKeys()[58] = keys_states[SDL_SCANCODE_B];
        GetKeys()[59] = keys_states[SDL_SCANCODE_N];
        GetKeys()[60] = keys_states[SDL_SCANCODE_M];
        // Controls
        GetKeys()[61] = keys_states[SDL_SCANCODE_ESCAPE];
        GetKeys()[62] = keys_states[SDL_SCANCODE_TAB];
        GetKeys()[63] = keys_states[SDL_SCANCODE_LSHIFT];
        GetKeys()[64] = keys_states[SDL_SCANCODE_RSHIFT];
        GetKeys()[65] = keys_states[SDL_SCANCODE_LCTRL];
        GetKeys()[66] = keys_states[SDL_SCANCODE_RCTRL];
        GetKeys()[67] = keys_states[SDL_SCANCODE_LALT];
        GetKeys()[68] = keys_states[SDL_SCANCODE_RALT];
        GetKeys()[69] = keys_states[SDL_SCANCODE_SPACE];
        GetKeys()[70] = keys_states[SDL_SCANCODE_CAPSLOCK];
        GetKeys()[71] = keys_states[SDL_SCANCODE_BACKSPACE];
        GetKeys()[72] = keys_states[SDL_SCANCODE_RETURN];
        GetKeys()[73] = keys_states[SDL_SCANCODE_DELETE];
        GetKeys()[74] = keys_states[SDL_SCANCODE_LEFT];
        GetKeys()[75] = keys_states[SDL_SCANCODE_RIGHT];
        GetKeys()[76] = keys_states[SDL_SCANCODE_UP];
        GetKeys()[77] = keys_states[SDL_SCANCODE_DOWN];
        GetKeys()[78] = keys_states[SDL_SCANCODE_PAGEUP];
        GetKeys()[79] = keys_states[SDL_SCANCODE_PAGEDOWN];
        GetKeys()[80] = keys_states[SDL_SCANCODE_HOME];
        GetKeys()[81] = keys_states[SDL_SCANCODE_END];
        GetKeys()[82] = keys_states[SDL_SCANCODE_INSERT];
    }
}
