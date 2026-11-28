/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <optional>
#include <string>
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

    // runs an ad-hoc lua snippet and formats any non-nil return value with lua tostring
    bool execute_snippet(const std::string& code, std::optional<std::string>& out_result, std::string& out_error);

    // the component as its lua usertype, nil for components lua has no binding for
    sol::object to_lua(sol::state_view state, Component* component);

    // world companion script lifecycle (keeps sol/sol.hpp out of WorldSystems.cpp)
    void companion_load(const std::string& script_path);
    void companion_tick(float dt, bool is_playing);
    void companion_stop();
    void companion_clear();

    // reads the lua CarDriver table for PlayerBody without pulling sol/sol.hpp into world headers
    bool read_car_driver_motion(uint64_t car_id, float& out_seated);
}
