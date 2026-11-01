/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <sol/forward.hpp>

namespace spartan::scripting
{
    void register_math(sol::state_view state);
    void register_entity(sol::state_view state);
    void register_components(sol::state_view state);
    void register_world_helpers(sol::state_view state);
    void register_world(sol::state_view state);
    void register_world_systems(sol::state_view state);
    void register_hud(sol::state_view state);
    void register_car_tuning(sol::state_view state);
    // extends the Physics usertype and the BodyType enum, so it runs after register_components
    void register_car_physics(sol::state_view state);
}
