/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <sol/forward.hpp>

namespace spartan::hud
{
    // screen space drawing for lua (menus, titles, fades), in pixels of the game view: the editor viewport while the editor shows, the window otherwise
    void register_for_scripting(sol::state_view state);
}
