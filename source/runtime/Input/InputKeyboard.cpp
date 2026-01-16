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
        GetKeys()[0]  = key_states[SDL_SCANCODE_F1];
        GetKeys()[1]  = key_states[SDL_SCANCODE_F2];
        GetKeys()[2]  = key_states[SDL_SCANCODE_F3];
        GetKeys()[3]  = key_states[SDL_SCANCODE_F4];
        GetKeys()[4]  = key_states[SDL_SCANCODE_F5];
        GetKeys()[5]  = key_states[SDL_SCANCODE_F6];
        GetKeys()[6]  = key_states[SDL_SCANCODE_F7];
        GetKeys()[7]  = key_states[SDL_SCANCODE_F8];
        GetKeys()[8]  = key_states[SDL_SCANCODE_F9];
        GetKeys()[9]  = key_states[SDL_SCANCODE_F10];
        GetKeys()[10] = key_states[SDL_SCANCODE_F11];
        GetKeys()[11] = key_states[SDL_SCANCODE_F12];
        GetKeys()[12] = key_states[SDL_SCANCODE_F13];
        GetKeys()[13] = key_states[SDL_SCANCODE_F14];
        GetKeys()[14] = key_states[SDL_SCANCODE_F15];
        // numbers
        GetKeys()[15] = key_states[SDL_SCANCODE_0];
        GetKeys()[16] = key_states[SDL_SCANCODE_1];
        GetKeys()[17] = key_states[SDL_SCANCODE_2];
        GetKeys()[18] = key_states[SDL_SCANCODE_3];
        GetKeys()[19] = key_states[SDL_SCANCODE_4];
        GetKeys()[20] = key_states[SDL_SCANCODE_5];
        GetKeys()[21] = key_states[SDL_SCANCODE_6];
        GetKeys()[22] = key_states[SDL_SCANCODE_7];
        GetKeys()[23] = key_states[SDL_SCANCODE_8];
        GetKeys()[24] = key_states[SDL_SCANCODE_9];
        // keypad
        GetKeys()[25] = key_states[SDL_SCANCODE_KP_0];
        GetKeys()[26] = key_states[SDL_SCANCODE_KP_1];
        GetKeys()[27] = key_states[SDL_SCANCODE_KP_2];
        GetKeys()[28] = key_states[SDL_SCANCODE_KP_3];
        GetKeys()[29] = key_states[SDL_SCANCODE_KP_4];
        GetKeys()[30] = key_states[SDL_SCANCODE_KP_5];
        GetKeys()[31] = key_states[SDL_SCANCODE_KP_6];
        GetKeys()[32] = key_states[SDL_SCANCODE_KP_7];
        GetKeys()[33] = key_states[SDL_SCANCODE_KP_8];
        GetKeys()[34] = key_states[SDL_SCANCODE_KP_9];
        // letters
        GetKeys()[35] = key_states[SDL_SCANCODE_Q];
        GetKeys()[36] = key_states[SDL_SCANCODE_W];
        GetKeys()[37] = key_states[SDL_SCANCODE_E];
        GetKeys()[38] = key_states[SDL_SCANCODE_R];
        GetKeys()[39] = key_states[SDL_SCANCODE_T];
        GetKeys()[40] = key_states[SDL_SCANCODE_Y];
        GetKeys()[41] = key_states[SDL_SCANCODE_U];
        GetKeys()[42] = key_states[SDL_SCANCODE_I];
        GetKeys()[43] = key_states[SDL_SCANCODE_O];
        GetKeys()[44] = key_states[SDL_SCANCODE_P];
        GetKeys()[45] = key_states[SDL_SCANCODE_A];
        GetKeys()[46] = key_states[SDL_SCANCODE_S];
        GetKeys()[47] = key_states[SDL_SCANCODE_D];
        GetKeys()[48] = key_states[SDL_SCANCODE_F];
        GetKeys()[49] = key_states[SDL_SCANCODE_G];
        GetKeys()[50] = key_states[SDL_SCANCODE_H];
        GetKeys()[51] = key_states[SDL_SCANCODE_J];
        GetKeys()[52] = key_states[SDL_SCANCODE_K];
        GetKeys()[53] = key_states[SDL_SCANCODE_L];
        GetKeys()[54] = key_states[SDL_SCANCODE_Z];
        GetKeys()[55] = key_states[SDL_SCANCODE_X];
        GetKeys()[56] = key_states[SDL_SCANCODE_C];
        GetKeys()[57] = key_states[SDL_SCANCODE_V];
        GetKeys()[58] = key_states[SDL_SCANCODE_B];
        GetKeys()[59] = key_states[SDL_SCANCODE_N];
        GetKeys()[60] = key_states[SDL_SCANCODE_M];
        // controls
        GetKeys()[61] = key_states[SDL_SCANCODE_ESCAPE];
        GetKeys()[62] = key_states[SDL_SCANCODE_TAB];
        GetKeys()[63] = key_states[SDL_SCANCODE_LSHIFT];
        GetKeys()[64] = key_states[SDL_SCANCODE_RSHIFT];
        GetKeys()[65] = key_states[SDL_SCANCODE_LCTRL];
        GetKeys()[66] = key_states[SDL_SCANCODE_RCTRL];
        GetKeys()[67] = key_states[SDL_SCANCODE_LALT];
        GetKeys()[68] = key_states[SDL_SCANCODE_RALT];
        GetKeys()[69] = key_states[SDL_SCANCODE_SPACE];
        GetKeys()[70] = key_states[SDL_SCANCODE_CAPSLOCK];
        GetKeys()[71] = key_states[SDL_SCANCODE_BACKSPACE];
        GetKeys()[72] = key_states[SDL_SCANCODE_RETURN];
        GetKeys()[73] = key_states[SDL_SCANCODE_DELETE];
        GetKeys()[74] = key_states[SDL_SCANCODE_LEFT];
        GetKeys()[75] = key_states[SDL_SCANCODE_RIGHT];
        GetKeys()[76] = key_states[SDL_SCANCODE_UP];
        GetKeys()[77] = key_states[SDL_SCANCODE_DOWN];
        GetKeys()[78] = key_states[SDL_SCANCODE_PAGEUP];
        GetKeys()[79] = key_states[SDL_SCANCODE_PAGEDOWN];
        GetKeys()[80] = key_states[SDL_SCANCODE_HOME];
        GetKeys()[81] = key_states[SDL_SCANCODE_END];
        GetKeys()[82] = key_states[SDL_SCANCODE_INSERT];
    }
}
