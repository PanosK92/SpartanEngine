/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==================================
#include "pch.h"
#include <sol/sol.hpp>
#include "Scripting.h"
#include "ScriptingInternal.h"
#include "../world/components/Component.h"
#include "../input/Input.h"
#include "../core/Window.h"
#include "../commands/console/ConsoleCommands.h"
//=============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::scripting
{
    namespace
    {
        sol::state lua_state;
    }

    void initialize()
    {
        lua_state.collect_gc();

        lua_state.open_libraries(
            sol::lib::base,
            sol::lib::package,
            sol::lib::coroutine,
            sol::lib::string,
            sol::lib::math,
            sol::lib::table,
            sol::lib::io);

        sol::state_view state_view(lua_state);

        lua_state.set_function("print", [&](sol::this_state s, const sol::variadic_args& args)
        {
            sol::state_view lua(s);
            sol::protected_function tostring_function =
                lua["tostring"];

            std::string line;
            line.reserve(256);
            for (size_t i = 0; i < args.size(); ++i)
            {
                sol::protected_function_result stringified =
                    tostring_function(args[i]);
                if (stringified.valid())
                {
                    if (sol::optional<const char*> text = stringified)
                    {
                        line += *text;
                    }
                    else
                    {
                        line += "[tostring error]";
                    }
                }
                else
                {
                    sol::error error = stringified;
                    line += "[error: ";
                    line += error.what();
                    line += "]";
                }

                if (i < args.size() - 1)
                {
                    line += "\t";
                }
            }

            SP_LOG_INFO("[Lua] %s", line.c_str());
        });

        sol::table Timer = lua_state.create_named_table("Timer");
        Timer["SetFPSLimit"]                    = &Timer::SetFpsLimit;
        Timer["GetFPSLimit"]                    = &Timer::GetFpsLimit;
        Timer["GetTimeMs"]                      = &Timer::GetTimeMs;
        Timer["GetTimeSec"]                     = &Timer::GetTimeSec;
        Timer["GetDeltaTimeMs"]                 = &Timer::GetDeltaTimeMs;
        Timer["GetDeltaTimeSec"]                = &Timer::GetDeltaTimeSec;
        Timer["GetDeltaTimeRealSec"]            = &Timer::GetDeltaTimeRealSec;
        Timer["GetSimTimeSec"]                  = &Timer::GetSimTimeSec;
        Timer["GetTimeScale"]                   = &Timer::GetTimeScale;
        Timer["SetTimeScale"]                   = &Timer::SetTimeScale;
        Timer["GetDeltaTimeSmoothedMs"]         = &Timer::GetDeltaTimeSmoothedMs;
        Timer["GetDeltaTimeSmoothedSec"]        = &Timer::GetDeltaTimeSmoothedSec;

        register_entity(state_view);
        register_components(state_view);
        register_world_helpers(state_view);
        register_hud(state_view);
        register_car_tuning(state_view);

        lua_state.new_enum("ComponentType",
            "AudioSource",              ComponentType::AudioSource,
            "Camera",                   ComponentType::Camera,
            "Light",                    ComponentType::Light,
            "Physics",                  ComponentType::Physics,
            "Render",               ComponentType::Render,
            "Terrain",                  ComponentType::Terrain,
            "Volume",                   ComponentType::Volume,
            "Script",                   ComponentType::Script,
            "ParticleSystem",           ComponentType::ParticleSystem,
            "Spline",                   ComponentType::Spline,
            "Text3D",                   ComponentType::Text3D,
            "Animator",                 ComponentType::Animator,
            "Ragdoll",                  ComponentType::Ragdoll
        );

        lua_state.new_enum("KeyCode",
            "F1", KeyCode::F1, "F2", KeyCode::F2, "F3", KeyCode::F3, "F4", KeyCode::F4, "F5", KeyCode::F5,
            "F6", KeyCode::F6, "F7", KeyCode::F7, "F8", KeyCode::F8, "F9", KeyCode::F9, "F10", KeyCode::F10,
            "F11", KeyCode::F11, "F12", KeyCode::F12,
            "Alpha0", KeyCode::Alpha0, "Alpha1", KeyCode::Alpha1, "Alpha2", KeyCode::Alpha2, "Alpha3", KeyCode::Alpha3,
            "Alpha4", KeyCode::Alpha4, "Alpha5", KeyCode::Alpha5, "Alpha6", KeyCode::Alpha6, "Alpha7", KeyCode::Alpha7,
            "Alpha8", KeyCode::Alpha8, "Alpha9", KeyCode::Alpha9,
            "Q", KeyCode::Q, "W", KeyCode::W, "E", KeyCode::E, "R", KeyCode::R, "T", KeyCode::T, "Y", KeyCode::Y,
            "U", KeyCode::U, "I", KeyCode::I, "O", KeyCode::O, "P", KeyCode::P, "A", KeyCode::A, "S", KeyCode::S,
            "D", KeyCode::D, "F", KeyCode::F, "G", KeyCode::G, "H", KeyCode::H, "J", KeyCode::J, "K", KeyCode::K,
            "L", KeyCode::L, "Z", KeyCode::Z, "X", KeyCode::X, "C", KeyCode::C, "V", KeyCode::V, "B", KeyCode::B,
            "N", KeyCode::N, "M", KeyCode::M,
            "Esc", KeyCode::Esc, "Tab", KeyCode::Tab,
            "Shift_Left", KeyCode::Shift_Left, "Shift_Right", KeyCode::Shift_Right,
            "Ctrl_Left", KeyCode::Ctrl_Left, "Ctrl_Right", KeyCode::Ctrl_Right,
            "Alt_Left", KeyCode::Alt_Left, "Alt_Right", KeyCode::Alt_Right,
            "Space", KeyCode::Space, "CapsLock", KeyCode::CapsLock, "Backspace", KeyCode::Backspace,
            "Enter", KeyCode::Enter, "Delete", KeyCode::Delete,
            "Arrow_Left", KeyCode::Arrow_Left, "Arrow_Right", KeyCode::Arrow_Right,
            "Arrow_Up", KeyCode::Arrow_Up, "Arrow_Down", KeyCode::Arrow_Down,
            "Page_Up", KeyCode::Page_Up, "Page_Down", KeyCode::Page_Down,
            "Home", KeyCode::Home, "End", KeyCode::End, "Insert", KeyCode::Insert,
            "Click_Left", KeyCode::Click_Left, "Click_Middle", KeyCode::Click_Middle, "Click_Right", KeyCode::Click_Right,
            "DPad_Up", KeyCode::DPad_Up, "DPad_Down", KeyCode::DPad_Down, "DPad_Left", KeyCode::DPad_Left, "DPad_Right", KeyCode::DPad_Right,
            "Button_South", KeyCode::Button_South, "Button_East", KeyCode::Button_East,
            "Button_West", KeyCode::Button_West, "Button_North", KeyCode::Button_North,
            "Back", KeyCode::Back, "Guide", KeyCode::Guide, "Start", KeyCode::Start,
            "Left_Stick", KeyCode::Left_Stick, "Right_Stick", KeyCode::Right_Stick,
            "Left_Shoulder", KeyCode::Left_Shoulder, "Right_Shoulder", KeyCode::Right_Shoulder
            );

        sol::table InputTable = lua_state.create_named_table("Input");
        InputTable["GetKey"]         = &Input::GetKey;
        InputTable["GetKeyDown"]     = &Input::GetKeyDown;
        InputTable["GetKeyUp"]       = &Input::GetKeyUp;
        InputTable["GetMouseDelta"]  = &Input::GetMouseDelta;
        InputTable["GetMouseWheelDelta"] = &Input::GetMouseWheelDelta;
        InputTable["GetGamepadThumbStickLeft"] = &Input::GetGamepadThumbStickLeft;

        sol::table EngineTable = lua_state.create_named_table("Engine");
        EngineTable["IsEditorVisible"] = []() { return Engine::IsFlagSet(EngineMode::EditorVisible); };
        EngineTable["IsPlaying"]       = []() { return Engine::IsFlagSet(EngineMode::Playing); };
        EngineTable["IsPaused"]        = []() { return Engine::IsFlagSet(EngineMode::Paused); };
        EngineTable["Quit"]            = []() { Window::Close(); };

        sol::table ConsoleTable = lua_state.create_named_table("Console");
        ConsoleTable["Set"] = [](const std::string& name, const std::string& value) -> bool
        {
            return ConsoleRegistry::Get().SetValueFromString(name, value);
        };
        ConsoleTable["Get"] = [](const std::string& name) -> sol::object
        {
            optional<string> value = ConsoleRegistry::Get().GetValueAsString(name);
            if (!value)
            {
                return sol::nil;
            }
            return sol::make_object(lua_state, *value);
        };

        register_math(state_view);
        register_world(state_view);
        register_car_physics(state_view);
        register_world_systems(state_view);
    }

    sol::state_view get_state()
    {
        return sol::state_view(lua_state);
    }
}
