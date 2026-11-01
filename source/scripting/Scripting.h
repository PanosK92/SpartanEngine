/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <sol/forward.hpp>

namespace spartan
{
    class Component;
}

namespace spartan::scripting
{
    // creates the lua state and registers the whole engine api with it
    void initialize();

    // the state every script, world companion script and console command runs in
    sol::state_view get_state();

    // the component as its lua usertype, nil for components lua has no binding for
    sol::object to_lua(sol::state_view state, Component* component);
}
