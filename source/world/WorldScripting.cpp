/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "World.h"
#include "Entity.h"
#include "WorldHelpers.h"
#include "components/AudioSource.h"
#include "components/Camera.h"
#include "components/Physics.h"
#include "components/Render.h"
#include "components/Light.h"
#include "components/Spline.h"
#include "components/ParticleSystem.h"
#include "components/Text3D.h"
#include "components/Animator.h"
#include "components/Ragdoll.h"
#include "../geometry/Mesh.h"
#include "../input/Input.h"
#include "../physics/PhysicsWorld.h"
#include <sol/sol.hpp>
using namespace std;
using namespace spartan::math;
namespace spartan
{
    namespace { sol::state lua_state; }
    void World::InitializeScripting()
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
        Timer["GetDeltaTimeSmoothedMs"]         = &Timer::GetDeltaTimeSmoothedMs;
        Timer["GetDeltaTimeSmoothedSec"]        = &Timer::GetDeltaTimeSmoothedSec;

        Entity          ::RegisterForScripting(state_view);
        Mesh            ::RegisterForScripting(state_view);
        AudioSource     ::RegisterForScripting(state_view);
        Render      ::RegisterForScripting(state_view);
        Physics         ::RegisterForScripting(state_view);
        Light           ::RegisterForScripting(state_view);
        ParticleSystem  ::RegisterForScripting(state_view);
        Spline          ::RegisterForScripting(state_view);
        Text3D          ::RegisterForScripting(state_view);
        Animator        ::RegisterForScripting(state_view);
        Ragdoll         ::RegisterForScripting(state_view);
        Camera          ::RegisterForScripting(state_view);
        WorldHelpers    ::RegisterForScripting(state_view);

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

        lua_state.new_enum("Intersection",
            "Outside", Intersection::Outside,
            "Inside",       Intersection::Inside,
            "Intersects",   Intersection::Intersects
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

        lua_state.new_usertype<BoundingBox>("BoundingBox",
            sol::call_constructor,      sol::constructors<BoundingBox(), BoundingBox(Vector3, Vector3)>(),

            "Intersects",               sol::overload(
                [](const BoundingBox& Self, const Vector3& Point) { return Self.Intersects(Point); },
                [](const BoundingBox& Self, const BoundingBox& Other) { return Self.Intersects(Other); }),

            "Contains",                 &BoundingBox::Contains,
            "Merge",                    &BoundingBox::Merge,
            "GetClosestPoint",          &BoundingBox::GetClosestPoint,
            "GetCenter",                &BoundingBox::GetCenter,
            "GetSize",                  &BoundingBox::GetSize,
            "GetExtents",               &BoundingBox::GetExtents,
            "GetVolume",                &BoundingBox::GetVolume,

            "GetMin",                   &BoundingBox::GetMin,
            "GetMax",                   &BoundingBox::GetMax

            );

        sol::table WorldTable = lua_state.create_named_table("World");
        WorldTable["GetName"]                   = &World::GetName;
        WorldTable["GetFilePath"]               = &World::GetFilePath;
        WorldTable["GetBoundingBox"]            = &World::GetBoundingBox;
        WorldTable["GetEntities"]               = []() -> sol::table
        {
            sol::state_view lua = World::GetLuaState();
            sol::table result = lua.create_table();
            const std::vector<Entity*>& entities = World::GetEntities();
            for (size_t i = 0; i < entities.size(); i++)
            {
                result[i + 1] = entities[i];
            }
            return result;
        };
        WorldTable["GetEntitiesLights"]         = []() -> sol::table
        {
            sol::state_view lua = World::GetLuaState();
            sol::table result = lua.create_table();
            const std::vector<Entity*>& entities = World::GetEntitiesLights();
            for (size_t i = 0; i < entities.size(); i++)
            {
                result[i + 1] = entities[i];
            }
            return result;
        };
        WorldTable["CreateEntity"]              = &World::CreateEntity;
        WorldTable["RemoveEntity"]              = &World::RemoveEntity;
        WorldTable["GetLightCount"]             = &World::GetLightCount;
        WorldTable["GetAudioSourceCount"]       = &World::GetAudioSourceCount;
        WorldTable["GetTimeOfDay"]              = &World::GetTimeOfDay;
        WorldTable["SetTimeOfDay"]              = &World::SetTimeOfDay;
        WorldTable["GetWind"]                   = &World::GetWind;
        WorldTable["SetWind"]                   = &World::SetWind;
        WorldTable["GetPuddliness"]             = &World::GetPuddliness;
        WorldTable["SetPuddliness"]             = &World::SetPuddliness;
        WorldTable["SetRain"] = &Environment::SetRain;
        WorldTable["GetRain"] = &Environment::GetRain;
        WorldTable["SetCloudCoverage"] = &Environment::SetCloudCoverage;
        WorldTable["GetCloudCoverage"] = &Environment::GetCloudCoverage;
        WorldTable["SetDateUtc"] = &Environment::SetDate;
        WorldTable["GetAirTemperature"] = []() { return World::GetEnvironment().air_temperature; };
        WorldTable["GetRoadTemperature"] = []() { return World::GetEnvironment().road_temperature; };
        WorldTable["GetDirectionalLight"]       = &World::GetDirectionalLight;
        WorldTable["GetCameraEntity"]           = []() -> Entity*
        {
            Camera* camera = World::GetCamera();
            return camera ? camera->GetEntity() : nullptr;
        };
        WorldTable["GetEntityByName"] = [](const std::string& name) -> Entity*
        {
            for (Entity* entity : World::GetEntities())
            {
                if (entity && entity->GetObjectName() == name)
                {
                    return entity;
                }
            }

            return nullptr;
        };
        // ids exceed lua number precision, so they pass as strings
        WorldTable["GetEntityById"] = [](const std::string& id) -> Entity*
        {
            return World::GetEntityById(std::strtoull(id.c_str(), nullptr, 10));
        };
        WorldTable["Raycast"] = [](const Vector3& origin, const Vector3& direction, float max_distance) -> sol::object
        {
            Vector3 hit_position;
            Entity* hit_entity = nullptr;
            if (PhysicsWorld::RaycastStatic(origin, direction, max_distance, hit_position, hit_entity) && hit_entity)
            {
                sol::state_view lua(lua_state);
                sol::table result = lua.create_table();
                result["entity"]   = hit_entity;
                result["position"] = hit_position;
                return result;
            }
            return sol::nil;
        };

        lua_state.new_usertype<Vector2>("Vector2",
            sol::call_constructor,
            sol::constructors<Vector2(), Vector2(const Vector2&), Vector2(int, int), Vector2(float, float)>(),

            "x", &Vector2::x,
            "y", &Vector2::y,

            // Addition
            sol::meta_function::addition, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS + RHS; },
                [](const Vector2& LHS, float RHS) { return LHS + RHS; }
            ),

            // Subtraction
            sol::meta_function::subtraction, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS - RHS; },
                [](const Vector2& LHS, float RHS) { return LHS - RHS; }
            ),

            // Multiplication
            sol::meta_function::multiplication, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS * RHS; },
                [](const Vector2& LHS, float RHS) { return LHS * RHS; }
            ),

            // Division
            sol::meta_function::division, sol::overload(
                [](const Vector2& LHS, const Vector2& RHS) { return LHS / RHS; },
                [](const Vector2& LHS, float RHS) { return LHS / RHS; }
            ),

            // Unary minus
            sol::meta_function::unary_minus, [](const Vector2& V) { return -V; },

            // Equality
            sol::meta_function::equal_to, [](const Vector2& LHS, const Vector2& RHS) { return LHS == RHS; },

            // To string
            sol::meta_function::to_string, [](const Vector2& V)
            {
                return "Vector2(" + std::to_string(V.x) + ", " + std::to_string(V.y) + ")";
            },

            // Length
            sol::meta_function::length, [](const Vector2& V) { return 2; },

            // Index access
            sol::meta_function::index, [](const Vector2& V, int index) -> float {
                if (index == 1)
                {
                    return V.x;
                }
                if (index == 2)
                {
                    return V.y;
                }
                throw std::out_of_range("Vector2 index out of range (1-2)");
            },

            sol::meta_function::new_index, [](Vector2& V, int index, float value) {
                if (index == 1)
                {
                    V.x = value;
                }
                else if (index == 2)
                {
                    V.y = value;
                }
                else
                {
                    throw std::out_of_range("Vector2 index out of range (1-2)");
                }
            },

            // Utility methods
            "Length", [](const Vector2& V) { return V.Length(); },
            "LengthSquared", [](const Vector2& V) { return V.LengthSquared(); },
            "Normalize", [](Vector2& V) { return V.Normalize(); },
            "Normalized", [](const Vector2& V) { return V.Normalized(); },
            "Distance", [](const Vector2& V, const Vector2& Other) { return Vector2::Distance(V, Other); },
            "DistanceSquared", [](const Vector2& V, const Vector2& Other) { return Vector2::DistanceSquared(V, Other); }
        );



        lua_state.new_usertype<Vector3>("Vector3",
            sol::call_constructor,
            sol::constructors<Vector3(), Vector3(const Vector3&), Vector3(float, float, float)>(),

            "x", &Vector3::x,
            "y", &Vector3::y,
            "z", &Vector3::z,

            // Addition
            sol::meta_function::addition, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS + RHS; },
                [](const Vector3& LHS, float RHS) { return LHS + RHS; }
            ),

            // Subtraction
            sol::meta_function::subtraction, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS - RHS; },
                [](const Vector3& LHS, float RHS) { return LHS - RHS; }
            ),

            // Multiplication
            sol::meta_function::multiplication, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS * RHS; },
                [](const Vector3& LHS, float RHS) { return LHS * RHS; }
            ),

            // Division
            sol::meta_function::division, sol::overload(
                [](const Vector3& LHS, const Vector3& RHS) { return LHS / RHS; },
                [](const Vector3& LHS, float RHS) { return LHS / RHS; }
            ),

            // Unary minus
            sol::meta_function::unary_minus, [](const Vector3& V) { return -V; },

            // Equality
            sol::meta_function::equal_to, [](const Vector3& LHS, const Vector3& RHS) { return LHS == RHS; },

            // To string
            sol::meta_function::to_string, [](const Vector3& V)
            {
                return "Vector3(" + std::to_string(V.x) + ", " + std::to_string(V.y) + ", " + std::to_string(V.z) + ")";
            },

            // Length
            sol::meta_function::length, [](const Vector3& V) { return 3; },

            // Index access
            sol::meta_function::index, [](const Vector3& V, int index) -> float
            {
                if (index == 1)
                {
                    return V.x;
                }
                if (index == 2)
                {
                    return V.y;
                }
                if (index == 3)
                {
                    return V.z;
                }
                throw std::out_of_range("Vector3 index out of range (1-3)");
            },

            sol::meta_function::new_index, [](Vector3& V, int index, float value)
            {
                if (index == 1)
                {
                    V.x = value;
                }
                else if (index == 2)
                {
                    V.y = value;
                }
                else if (index == 3)
                {
                    V.z = value;
                }
                else
                {
                    throw std::out_of_range("Vector3 index out of range (1-3)");
                }
            },

            // Utility methods
            "Length", [](const Vector3& V) { return V.Length(); },
            "LengthSquared", [](const Vector3& V) { return V.LengthSquared(); },
            "Normalize", [](Vector3& V) { return V.Normalize(); },
            "Normalized", [](const Vector3& V) { return V.Normalized(); },
            "Distance", [](const Vector3& V, const Vector3& Other) { return Vector3::Distance(V, Other); },
            "DistanceSquared", [](const Vector3& V, const Vector3& Other) { return Vector3::DistanceSquared(V, Other); }
        );


        lua_state.new_usertype<Vector4>("Vector4",
            sol::call_constructor,
            sol::constructors<Vector4(), Vector4(const Vector4&), Vector4(float, float, float, float)>(),

            "x", &Vector4::x,
            "y", &Vector4::y,
            "z", &Vector4::z,
            "w", &Vector4::w,

            // Addition
            sol::meta_function::addition, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS + RHS; },
                [](const Vector4& LHS, float RHS) { return LHS + RHS; }
            ),

            // Subtraction
            sol::meta_function::subtraction, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS - RHS; },
                [](const Vector4& LHS, float RHS) { return LHS - RHS; }
            ),

            // Multiplication
            sol::meta_function::multiplication, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS * RHS; },
                [](const Vector4& LHS, float RHS) { return LHS * RHS; }
            ),

            // Division
            sol::meta_function::division, sol::overload(
                [](const Vector4& LHS, const Vector4& RHS) { return LHS / RHS; },
                [](const Vector4& LHS, float RHS) { return LHS / RHS; }
            ),

            // Unary minus
            sol::meta_function::unary_minus, [](const Vector4& V) { return -V; },

            // Equality
            sol::meta_function::equal_to, [](const Vector4& LHS, const Vector4& RHS) { return LHS == RHS; },

            // To string
            sol::meta_function::to_string, [](const Vector4& V)
            {
                return "Vector4(" + std::to_string(V.x) + ", " + std::to_string(V.y) + ", " + std::to_string(V.z) + ", " + std::to_string(V.w) + ")";
            },

            // Length
            sol::meta_function::length, [](const Vector4& V) { return 4; },

            // Index access
            sol::meta_function::index, [](const Vector4& V, int index) -> float {
                if (index == 1)
                {
                    return V.x;
                }
                if (index == 2)
                {
                    return V.y;
                }
                if (index == 3)
                {
                    return V.z;
                }
                if (index == 4)
                {
                    return V.w;
                }
                throw std::out_of_range("Vector4 index out of range (1-4)");
            },

            sol::meta_function::new_index, [](Vector4& V, int index, float value) {
                if (index == 1)
                {
                    V.x = value;
                }
                else if (index == 2)
                {
                    V.y = value;
                }
                else if (index == 3)
                {
                    V.z = value;
                }
                else if (index == 4)
                {
                    V.w = value;
                }
                else
                {
                    throw std::out_of_range("Vector4 index out of range (1-4)");
                }
            },

            // Utility methods
            "Length", [](const Vector4& V) { return V.Length(); },
            "LengthSquared", [](const Vector4& V) { return V.LengthSquared(); },
            "Normalize", [](Vector4& V) { return V.Normalize(); },
            "Normalized", [](const Vector4& V) { return V.Normalized(); },
            "Distance", [](const Vector4& V, const Vector4& Other) { return Vector4::Distance(V, Other); },
            "DistanceSquared", [](const Vector4& V, const Vector4& Other) { return Vector4::DistanceSquared(V, Other); }
        );

        lua_state.new_usertype<Quaternion>("Quaternion",
            sol::call_constructor,
            sol::constructors<Quaternion()>(),

            "x", &Quaternion::x,
            "y", &Quaternion::y,
            "z", &Quaternion::z,
            "w", &Quaternion::w,

            "FromEulerAngles", [](float pitch, float yaw, float roll) { return Quaternion::FromEulerAngles(pitch, yaw, roll); },
            "FromLookRotation", [](const Vector3& direction, const Vector3& up)
            {
                return Quaternion::FromLookRotation(direction, up);
            },
            "Lerp", [](const Quaternion& a, const Quaternion& b, float t)
            {
                return Quaternion::Lerp(a, b, t);
            },
            "Identity",        sol::var(Quaternion::Identity)
        );


    }

    sol::state_view World::GetLuaState() { return sol::state_view(lua_state); }
}
