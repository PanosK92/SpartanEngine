/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
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

//= INCLUDES =========================
#include "pch.h"
#include "World.h"
#include "Entity.h"
#include "Prefab.h"
#include "WorldHelpers.h"
#include "../Car/Car.h"
#include "../Profiling/Profiler.h"
#include "../Core/ProgressTracker.h"
#include "../Core/ThreadPool.h"
#include "../Core/Event.h"
#include "Components/Render.h"
#include "Components/Camera.h"
#include "Components/Light.h"
#include "Components/AudioSource.h"
#include "Components/ParticleSystem.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture.h"
#include "../Rendering/Renderer.h"
#include "Components/Physics.h"
#include "../Physics/PhysicsWorld.h"
#include "../Input/Input.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        sol::state lua_state;
        vector<Entity*> entities;
        vector<Entity*> entities_lights;       // entities subset that contains only lights
        vector<Entity*> entities_renderables;  // entities subset that contains only active renderables
        string file_path;
        string world_name; // cached to avoid per-frame allocation
        string world_description;
        vector<string> world_console_variables; // cvar names overridden by this world (preserved across save/load)
        mutex entity_access_mutex;
        // entities created by workers but not yet drained into the live entities vector, the main thread drains this every tick
        // workers may still be configuring components for these, the renderer tolerates partial state via skip checks
        vector<Entity*> entities_pending;
        // deferred script initialization, lua is single threaded so script init is collected during the parallel
        // entity load and executed sequentially on the load thread once every entity exists
        atomic<bool> defer_script_init = false;
        mutex script_init_mutex;
        vector<pair<int, function<void()>>> script_inits_pending;
        set<uint64_t> pending_remove;
        uint32_t audio_source_count = 0;
        atomic<bool> resolve        = false;
        bool was_in_editor_mode     = false;
        // tracks observed loading transition so the WorldLoaded event fires exactly once per load
        bool was_loading            = false;
        BoundingBox bounding_box    = BoundingBox::Unit;
        Entity* camera              = nullptr;
        Entity* light               = nullptr;

        // snapshot for play/stop state restoration (like unity's play mode)
        struct EntitySnapshot
        {
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
        };
        unordered_map<uint64_t, EntitySnapshot> play_mode_snapshot;
        float play_mode_time_of_day = 0.0f;

        // ids of entities created while playing, these are removed when play stops so they never leak into the world
        set<uint64_t> play_mode_spawned_ids;

        // entity state tracking - things that change the nature of the entity for rendering
        enum class EntityChange : uint8_t
        {
            None       = 0,
            Active     = 1 << 0,
            Components = 1 << 1,
            CullMode   = 1 << 2,
            LightType  = 1 << 3
        };
        unordered_map<uint64_t, uint32_t> entity_states; // stores: low 8 bits for flags, next 8 for component count, next 8 for cull mode, next 8 for light type

        // material change tracking - things that change the nature of the material for rendering
        unordered_map<uint64_t, size_t> material_state_hashes;

        // light change tracking - things that change the nature of the light for rendering
        unordered_map<uint64_t, size_t> light_state_hashes;

        void mark_entity_changed(uint64_t id, EntityChange change)
        {
            entity_states[id] |= static_cast<uint32_t>(change);
            resolve            = true;
        }

        size_t compute_material_hash(Material* material)
        {
            size_t hash = 17; // FNV-1a seed

            // include resource state so async preparation completion triggers an update
            hash = (hash * 31) ^ static_cast<size_t>(material->GetResourceState());

            for (const auto* texture : material->GetTextures())
            {
                hash = (hash * 31) ^ reinterpret_cast<size_t>(texture);

                // include texture's resource state so async texture preparation triggers an update
                if (texture)
                {
                    hash = (hash * 31) ^ static_cast<size_t>(texture->GetResourceState());
                }
            }
            for (const float prop : material->GetProperties())
            {
                hash = (hash * 31) ^ std::hash<float>{}(prop);
            }
            return hash;
        }

        size_t compute_light_hash(Light* light, Entity* entity)
        {
            size_t hash = 17;

            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().r);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().g);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().b);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().a);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetIntensityRadiometric());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetRange());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetAngle());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetAreaWidth());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetAreaHeight());
            hash = (hash * 31) ^ static_cast<size_t>(light->GetLightType());
            hash = (hash * 31) ^ static_cast<size_t>(light->GetFlags());
            hash = (hash * 31) ^ static_cast<size_t>(entity->GetActive());

            const Vector3& pos = entity->GetPosition();
            hash = (hash * 31) ^ std::hash<float>{}(pos.x);
            hash = (hash * 31) ^ std::hash<float>{}(pos.y);
            hash = (hash * 31) ^ std::hash<float>{}(pos.z);
            const Vector3& fwd = entity->GetForward();
            hash = (hash * 31) ^ std::hash<float>{}(fwd.x);
            hash = (hash * 31) ^ std::hash<float>{}(fwd.y);
            hash = (hash * 31) ^ std::hash<float>{}(fwd.z);

            for (uint32_t i = 0; i < light->GetSliceCount(); i++)
            {
                const Matrix& vp       = light->GetViewProjectionMatrix(i);
                const float* vp_data   = vp.Data();
                for (uint32_t j = 0; j < 16; j++)
                {
                    hash = (hash * 31) ^ std::hash<float>{}(vp_data[j]);
                }
            }

            return hash;
        }

        void compute_bounding_box()
        {
            bounding_box = BoundingBox::Unit;

            for (Entity* entity : entities)
            {
                if (entity->GetActive())
                {
                    if (Render* renderable = entity->GetComponent<Render>())
                    {
                        bounding_box.Merge(renderable->GetBoundingBox());
                    }
                }
            }
        }

        bool is_world_in_project_directory(const string& world_file_path)
        {
            // check if the world is in the project directory (has local assets alongside it)
            string normalized_path = world_file_path;
            replace(normalized_path.begin(), normalized_path.end(), '\\', '/');

            string project_dir = ResourceCache::GetProjectDirectory();
            replace(project_dir.begin(), project_dir.end(), '\\', '/');

            // world is in project if path starts with project directory or contains /project/
            return normalized_path.find(project_dir) != string::npos ||
                   normalized_path.find("/project/") != string::npos ||
                   normalized_path.rfind("project/", 0) == 0;
        }

        string world_file_path_to_resource_directory(const string& world_file_path)
        {
            const string world_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(world_file_path);
            string result;

            // if the world is in the project directory, resources are alongside the world file
            if (is_world_in_project_directory(world_file_path))
            {
                result = FileSystem::GetDirectoryFromFilePath(world_file_path) + "/" + world_name + "_resources/";
            }
            else
            {
                // otherwise (worlds/, repo root, etc.), resources go to ./project/
                result = "./" + string(ResourceCache::GetProjectDirectory()) + world_name + "_resources/";
            }

            // normalize to forward slashes
            replace(result.begin(), result.end(), '\\', '/');

            SP_LOG_INFO("World resource directory: %s (from world: %s)", result.c_str(), world_file_path.c_str());
            return result;
        }


        void InitializeCoreLua()
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
                sol::protected_function LuaStringFunc = lua["tostring"];

                std::string Output;
                Output.reserve(256);
                for (size_t i = 0; i < args.size(); ++i)
                {
                    sol::object Obj = args[i];

                    sol::protected_function_result Result = LuaStringFunc(Obj);

                    if (Result.valid())
                    {
                        if (sol::optional<const char*> str = Result)
                        {
                            Output += *str;
                        }
                        else
                        {
                            Output += "[tostring error]";
                        }
                    }
                    else
                    {
                        sol::error err = Result;
                        Output += "[error: ";
                        Output += err.what();
                        Output += "]";
                    }

                    if (i < args.size() - 1)
                    {
                        Output += "\t";
                    }
                }

                SP_LOG_INFO("[Lua] %s", Output.c_str())
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
            WorldHelpers    ::RegisterForScripting(state_view);

            lua_state.new_enum("ComponentType",
                "AudioSource",              ComponentType::AudioSource,
                "Camera",                   ComponentType::Camera,
                "Light",                    ComponentType::Light,
                "Physics",                  ComponentType::Physics,
                "Renderable",               ComponentType::Render,
                "Terrain",                  ComponentType::Terrain,
                "Volume",                   ComponentType::Volume,
                "Script",                   ComponentType::Script,
                "ParticleSystem",           ComponentType::ParticleSystem
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
            InputTable["GetKey"]     = &Input::GetKey;
            InputTable["GetKeyDown"] = &Input::GetKeyDown;
            InputTable["GetKeyUp"]   = &Input::GetKeyUp;

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
            WorldTable["GetEntities"]               = &World::GetEntities;
            WorldTable["GetEntitiesLights"]         = &World::GetEntitiesLights;
            WorldTable["CreateEntity"]              = &World::CreateEntity;
            WorldTable["RemoveEntity"]              = &World::RemoveEntity;
            WorldTable["GetLightCount"]             = &World::GetLightCount;
            WorldTable["GetAudioSourceCount"]       = &World::GetAudioSourceCount;
            WorldTable["GetTimeOfDay"]              = &World::GetTimeOfDay;
            WorldTable["SetTimeOfDay"]              = &World::SetTimeOfDay;
            WorldTable["GetWind"]                   = &World::GetWind;
            WorldTable["SetWind"]                   = &World::SetWind;
            WorldTable["GetDirectionalLight"]       = &World::GetDirectionalLight;
            WorldTable["GetCameraEntity"]           = []() -> Entity*
            {
                Camera* camera = World::GetCamera();
                return camera ? camera->GetEntity() : nullptr;
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
                sol::meta_function::length, [](const Vector3& V) { return 2; },

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
                    throw std::out_of_range("Vector2 index out of range (1-2)");
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
                        throw std::out_of_range("Vector3 index out of range (1-2)");
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
                //@TODO

                // Equality
                sol::meta_function::equal_to, [](const Vector4& LHS, const Vector4& RHS) { return LHS == RHS; },

                // To string
                sol::meta_function::to_string, [](const Vector4& V)
                {
                    return "Vector4(" + std::to_string(V.x) + ", " + std::to_string(V.y) + std::to_string(V.z) + ", " + std::to_string(V.w) + ")";
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
                    throw std::out_of_range("Vector4 index out of range (1-2-3-4)");
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
                        throw std::out_of_range("Vector3 index out of range (1-2)");
                    }
                },

                // Utility methods
                "Length", [](const Vector4& V) { return V.Length(); },
                "LengthSquared", [](const Vector4& V) { return V.LengthSquared(); },
                "Normalize", [](Vector4& V) { return V.Normalize(); },
                "Normalized", [](const Vector4& V) { return V.Normalized(); },
                "Distance", [](const Vector4& V, const Vector4& Other) { return Vector3::Distance(V, Other); },
                "DistanceSquared", [](const Vector4& V, const Vector4& Other) { return Vector3::DistanceSquared(V, Other); }
            );

            lua_state.new_usertype<Quaternion>("Quaternion",
                sol::call_constructor,
                sol::constructors<Quaternion()>(),

                "x", &Quaternion::x,
                "y", &Quaternion::y,
                "z", &Quaternion::z,
                "w", &Quaternion::w,

                "FromEulerAngles", [](float pitch, float yaw, float roll) { return Quaternion::FromEulerAngles(pitch, yaw, roll); },
                "Identity",        sol::var(Quaternion::Identity)
            );


        }

    }

    namespace world_time
    {
        // simulated time
        float time_of_day = 0.25f; // 6 AM
        float time_scale = 200.0f; // 200x real time

        // tick simulated time every frame
        void tick()
        {
            time_of_day += (static_cast<float>(Timer::GetDeltaTimeSec()) * time_scale) / 86400.0f;
            if (time_of_day >= 1.0f)
            {
                time_of_day -= 1.0f;
            }
            else if (time_of_day < 0.0f)
            {
                time_of_day = 0.0f;
            }
        }

        // get current time of day based on boolean
        float get_time_of_day(bool use_real_world_time)
        {
            if (use_real_world_time)
            {
                using namespace std::chrono;
                auto now = system_clock::now();
                time_t t = system_clock::to_time_t(now);
                tm local_time = {};
            #if defined(_WIN32)
                localtime_s(&local_time, &t);
            #else
                localtime_r(&t, &local_time);
            #endif
                float hours = static_cast<float>(local_time.tm_hour);
                float minutes = static_cast<float>(local_time.tm_min);
                float seconds = static_cast<float>(local_time.tm_sec);
                return (hours + minutes / 60.0f + seconds / 3600.0f) / 24.0f;
            }

            // return simulated time if not using real-world time
            return time_of_day;
        }
    }

    namespace world_wind
    {
        Vector3 wind = Vector3::Zero;

        void initialize()
        {
            float rotation_y      = 120.0f * math::deg_to_rad;
            const float intensity = 3.0f;
            wind = Vector3(sin(rotation_y), 0.0f, cos(rotation_y)) * intensity;
        }
    }

    void World::ProcessPendingRemovals()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        if (pending_remove.empty())
        {
            return;
        }

        // editor selection lives on the camera, drop deleted entities from it to avoid a dangling pointer
        Camera* selection_camera = GetCamera();

        for (auto it = entities.begin(); it != entities.end(); )
        {
            uint64_t id = (*it)->GetObjectId();
            if (pending_remove.count(id) > 0)
            {
                if (selection_camera)
                {
                    selection_camera->RemoveFromSelection(*it);
                }

                // clean up change tracking
                entity_states.erase(id);
                if (Material* mat = (*it)->GetComponent<Render>() ? (*it)->GetComponent<Render>()->GetMaterial() : nullptr)
                {
                    material_state_hashes.erase(mat->GetObjectId());
                }
                light_state_hashes.erase(id);
                delete *it;
                it = entities.erase(it);
            }
            else
            {
                ++it;
            }
        }

        pending_remove.clear();
    }

    void World::ProcessPendingAdditions()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        if (entities_pending.empty())
        {
            return;
        }

        // drain whatever workers have created so far, the renderer will skip entities whose components are still being set up
        entities.insert(entities.end(), entities_pending.begin(), entities_pending.end());
        entities_pending.clear();
        resolve = true;
    }

    void World::Initialize()
    {
        InitializeCoreLua();
        world_wind::initialize();
    }

    void World::Shutdown()
    {
        Engine::SetFlag(EngineMode::Playing, false); // stop simulation
        Renderer::DisableProceduralGrass();          // drop renderer references to builder owned grass mesh/material
        WorldHelpers::Clear();                        // release long lived builder meshes and materials
        Renderer::DestroyAccelerationStructures();   // destroy tlas/blas before clearing resources
        ResourceCache::Shutdown();                   // release all resources (textures, materials, meshes, etc)n

        // clear entities
        camera = nullptr;
        light  = nullptr;
        for (Entity* entity : entities)
        {
            delete entity;
        }
        entities.clear();
        entities_lights.clear();
        entities_renderables.clear();
        // also clear any entities the loader had queued, otherwise we'd leak partially-built objects when a load is aborted
        {
            lock_guard<mutex> lock(entity_access_mutex);
            for (Entity* entity : entities_pending)
            {
                delete entity;
            }
            entities_pending.clear();
        }
        camera = nullptr;
        light  = nullptr;
        file_path.clear();
        world_name.clear();
        world_description.clear();

        // clear change tracking
        entity_states.clear();
        material_state_hashes.clear();
        light_state_hashes.clear();

        // mark for resolve
        resolve = true;
    }

    void World::Tick()
    {
        // loading can happen in the background, but the renderer still wants to see whatever entities the loader has already published
        // do a load-time tick, drain published entities, run normal component ticks, rebuild caches; skip simulated world time
        if (ProgressTracker::IsLoading())
        {
            SP_PROFILE_CPU();

            was_loading = true;
            ProcessPendingRemovals();
            ProcessPendingAdditions();

            // entities created by workers are auto-drained each frame, components may still be partially configured but PreTick and Tick tolerate that via null checks
            for (Entity* entity : entities)
            {
                if (entity->GetActive())
                {
                    entity->PreTick();
                }
            }
            for (Entity* entity : entities)
            {
                if (entity->GetActive())
                {
                    entity->Tick();
                }
            }

            // force the renderable, lights, camera caches to rebuild every loading frame so newly drained entities show up immediately
            resolve = true;
            {
                camera             = nullptr;
                light              = nullptr;
                audio_source_count = 0;
                entities_lights.clear();
                entities_renderables.clear();
                for (Entity* entity : entities)
                {
                    if (entity->GetActive())
                    {
                        if (!camera && entity->GetComponent<Camera>())
                        {
                            camera = entity;
                        }

                        if (Light* light_comp = entity->GetComponent<Light>())
                        {
                            if (!light && light_comp->GetLightType() == LightType::Directional)
                            {
                                light = entity;
                            }
                            entities_lights.push_back(entity);
                        }

                        if (entity->GetComponent<Render>())
                        {
                            entities_renderables.push_back(entity);
                        }

                        if (entity->GetComponent<AudioSource>())
                        {
                            audio_source_count++;
                        }
                    }
                }
            }

            compute_bounding_box();
            resolve = false;
            // do not clear entity_states here, worker threads are still writing to it via mark_entity_changed under the lock, racing with an unlocked clear crashes the unordered_map
            // it gets drained safely in the regular non-loading Tick path once loading completes

            return;
        }

        SP_PROFILE_CPU();

        // notify listeners on the first tick after loading completes
        // any final pending entities are drained so subscribers see a fully populated scene
        if (was_loading)
        {
            was_loading = false;
            ProcessPendingAdditions();
            // force a resolve so the final entity state, deferred script setup like the sun, is rebuilt into the
            // renderer caches, a static world such as empty would otherwise stay on the last unlit loading frame
            resolve = true;
            SP_FIRE_EVENT(EventType::WorldLoaded);
        }

        // detect game toggling
        const bool started = Engine::IsFlagSet(EngineMode::Playing) && was_in_editor_mode;
        const bool stopped = !Engine::IsFlagSet(EngineMode::Playing) && !was_in_editor_mode;
        was_in_editor_mode = !Engine::IsFlagSet(EngineMode::Playing);

        // start
        if (started)
        {
            // snapshot all entity transforms before simulation begins
            play_mode_snapshot.clear();
            for (Entity* entity : entities)
            {
                if (!entity->IsTransient())
                {
                    EntitySnapshot snapshot;
                    snapshot.position = entity->GetPositionLocal();
                    snapshot.rotation = entity->GetRotationLocal();
                    snapshot.scale    = entity->GetScaleLocal();
                    play_mode_snapshot[entity->GetObjectId()] = snapshot;
                }
            }
            play_mode_time_of_day = world_time::time_of_day;

            // start a fresh record of entities spawned during this play session
            play_mode_spawned_ids.clear();

            for (Entity* entity : entities)
            {
                entity->Start();
            }
        }

        // stop
        if (stopped)
        {
            for (Entity* entity : entities)
            {
                entity->Stop();
            }

            // restore all entity transforms from snapshot
            for (Entity* entity : entities)
            {
                auto it = play_mode_snapshot.find(entity->GetObjectId());
                if (it != play_mode_snapshot.end())
                {
                    const EntitySnapshot& snapshot = it->second;
                    entity->SetPositionLocal(snapshot.position);
                    entity->SetRotationLocal(snapshot.rotation);
                    entity->SetScaleLocal(snapshot.scale);
                }
            }
            play_mode_snapshot.clear();
            world_time::time_of_day = play_mode_time_of_day;

            // delete anything spawned during play so it never leaks into the world or gets saved by accident
            // drain pending additions first so freshly spawned entities are visible to the removal pass
            ProcessPendingAdditions();
            vector<Entity*> spawned;
            for (Entity* entity : entities)
            {
                if (play_mode_spawned_ids.count(entity->GetObjectId()) > 0)
                {
                    spawned.push_back(entity);
                }
            }
            for (Entity* entity : spawned)
            {
                RemoveEntity(entity);
            }
            play_mode_spawned_ids.clear();
        }

        ProcessPendingRemovals();


        for (Entity* entity : entities)
        {
            if (entity->GetActive())
            {
                entity->PreTick();
            }
        }

        // tick
        for (Entity* entity : entities)
        {
            if (entity->GetActive())
            {
                entity->Tick();
            }
        }

        // check for entity changes
        for (Entity* entity : entities)
        {
            if (entity->GetActive())
            {
                uint64_t id = entity->GetObjectId();
                auto it = entity_states.find(id);
                if (it != entity_states.end())
                {
                    uint32_t& state = it->second;
                    uint32_t new_state = state;

                    // active state
                    bool was_active = (state & static_cast<uint32_t>(EntityChange::Active)) != 0;
                    if (entity->GetActive() != was_active)
                    {
                        new_state |= static_cast<uint32_t>(EntityChange::Active);
                        resolve = true;
                    }

                    // component count
                    uint8_t prev_component_count = (state >> 8) & 0xFF;
                    uint8_t curr_component_count = static_cast<uint8_t>(min(entity->GetComponentCount(), 255u));
                    if (curr_component_count != prev_component_count)
                    {
                        new_state = (new_state & ~0xFF00) | (curr_component_count << 8);
                        new_state |= static_cast<uint32_t>(EntityChange::Components);
                        resolve = true;
                    }

                    // cull mode
                    uint8_t prev_cull = (state >> 16) & 0xFF;
                    uint8_t curr_cull = static_cast<uint8_t>(RHI_CullMode::None);
                    if (Render* renderable = entity->GetComponent<Render>())
                    {
                        if (Material* material = renderable->GetMaterial())
                        {
                            curr_cull = static_cast<uint8_t>(material->GetProperty(MaterialProperty::CullMode));
                        }
                    }
                    if (curr_cull != prev_cull)
                    {
                        new_state = (new_state & ~0xFF0000) | (curr_cull << 16);
                        new_state |= static_cast<uint32_t>(EntityChange::CullMode);
                        resolve = true;
                    }

                    // light type
                    uint8_t prev_light_type = (state >> 24) & 0xFF;
                    uint8_t curr_light_type = static_cast<uint8_t>(LightType::Max);
                    if (Light* light_comp = entity->GetComponent<Light>())
                    {
                        curr_light_type = static_cast<uint8_t>(light_comp->GetLightType());
                    }
                    if (curr_light_type != prev_light_type)
                    {
                        new_state = (new_state & ~0xFF000000) | (curr_light_type << 24);
                        new_state |= static_cast<uint32_t>(EntityChange::LightType);
                        resolve = true;
                    }

                    state = new_state;
                }
            }
        }

        ProcessPendingAdditions();

        // resolve if needed
        if (resolve)
        {
            // track entities
            {
                camera             = nullptr;
                light              = nullptr;
                audio_source_count = 0;
                entities_lights.clear();
                entities_renderables.clear();
                for (Entity* entity : entities)
                {
                    if (entity->GetActive())
                    {
                        if (!camera && entity->GetComponent<Camera>())
                        {
                            camera = entity;
                        }

                        if (Light* light_comp = entity->GetComponent<Light>())
                        {
                            if (!light && light_comp->GetLightType() == LightType::Directional)
                            {
                                light = entity;
                            }
                            entities_lights.push_back(entity);
                        }

                        if (entity->GetComponent<Render>())
                        {
                            entities_renderables.push_back(entity);
                        }

                        if (entity->GetComponent<AudioSource>())
                        {
                            audio_source_count++;
                        }
                    }
                }
            }

            compute_bounding_box();
            resolve = false;
            entity_states.clear();
        }

        if (Engine::IsFlagSet(EngineMode::Playing) && !Engine::IsFlagSet(EngineMode::Paused))
        {
            world_time::tick();
        }
    }

    bool World::SaveToFile(string file_path)
    {
        if (FileSystem::GetExtensionFromFilePath(file_path) != EXTENSION_WORLD)
        {
            file_path += string(EXTENSION_WORLD);
        }

        // start timing
        const Stopwatch timer;

        // serialize the resources before saving the world (XML), as it references them
        {
            string directory = world_file_path_to_resource_directory(file_path);
            FileSystem::CreateDirectory_(directory);

            vector<shared_ptr<IResource>> resources = ResourceCache::GetResources();

            // save resources filtered by type
            for (shared_ptr<IResource>& resource : resources)
            {
                string ext;
                switch (resource->GetResourceType())
                {
                    case ResourceType::Texture:
                    {
                        // only save textures that can be saved (compressed with data)
                        // others will be re-imported from source path when material loads
                        RHI_Texture* texture = static_cast<RHI_Texture*>(resource.get());
                        if (!texture->CanSaveToFile())
                        {
                            continue;
                        }
                        ext = EXTENSION_TEXTURE;
                        break;
                    }
                    case ResourceType::Material: ext = EXTENSION_MATERIAL; break;
                    case ResourceType::Mesh:     ext = EXTENSION_MESH;     break;
                    default: continue;
                }
                resource->SaveToFile(directory + resource->GetObjectName() + ext);
            }
        }

        // create document
        pugi::xml_document doc;
        pugi::xml_node world_node = doc.append_child("World");
        world_node.append_attribute("name")        = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path).c_str();
        world_node.append_attribute("description") = world_description.c_str();

        // console variables (only those explicitly overridden by this world are persisted)
        if (!world_console_variables.empty())
        {
            pugi::xml_node cvars_node = world_node.append_child("ConsoleVariables");
            for (const string& cvar_name : world_console_variables)
            {
                optional<string> value = ConsoleRegistry::Get().GetValueAsString(cvar_name);
                if (!value.has_value())
                {
                    continue;
                }

                pugi::xml_node var_node = cvars_node.append_child("Variable");
                var_node.append_attribute("name")  = cvar_name.c_str();
                var_node.append_attribute("value") = value->c_str();
            }
        }

        // entities
        {
            // node
            pugi::xml_node entities_node = world_node.append_child("Entities");

            // get root entities, save them, and they will save their children recursively
            static vector<Entity*> root_entities;
            World::GetRootEntities(root_entities);
            const uint32_t root_entity_count = static_cast<uint32_t>(root_entities.size());

            // progress tracking
            ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Saving world...");

            // write entities to node
            for (Entity* root : root_entities)
            {
                pugi::xml_node entity_node = entities_node.append_child("Entity");
                root->Save(entity_node);
                ProgressTracker::GetProgress(ProgressType::World).JobDone();
            }
        }

        // save to file
        bool saved = doc.save_file(file_path.c_str(), " ", pugi::format_indent);
        if (!saved)
        {
            SP_LOG_ERROR("Failed to save XML file.");
            return false;
        }

        // log
        SP_LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

        return true;
    }

    bool World::LoadFromFile(const string& file_path_)
    {
        // ensure prefabs are registered before loading
        Car::RegisterPrefabs();

        // shutdown synchronously before async loading
        Shutdown();

        // copy path for the lambda capture
        string path_copy = file_path_;

        // load asynchronously
        ThreadPool::AddTask([path_copy]()
        {
            ProgressTracker::SetGlobalLoadingState(true);

            file_path  = path_copy;
            world_name = FileSystem::GetFileNameFromFilePath(file_path);

            // start timing
            const Stopwatch timer;

            // deserialize the resources before loading the world (XML), as it references them
            {
                string directory = world_file_path_to_resource_directory(file_path);

                // only load resources if the directory exists (worlds in "worlds/" folder may not have local resources yet)
                if (FileSystem::Exists(directory) && FileSystem::IsDirectory(directory))
                {
                    vector<string> files = FileSystem::GetFilesInDirectory(directory);

                    // progress for resource loading
                    uint32_t resource_count = static_cast<uint32_t>(files.size());
                    if (resource_count > 0)
                    {
                        ProgressTracker::GetProgress(ProgressType::World).Start(resource_count, "Loading resources...");
                    }

                    // bucket files by type so we can fan each bucket out across the thread pool
                    // sequential loads here used to dominate world load time on texture heavy scenes
                    vector<string> texture_paths;
                    vector<string> mesh_paths;
                    vector<string> material_paths;
                    texture_paths.reserve(files.size());
                    mesh_paths.reserve(files.size());
                    material_paths.reserve(files.size());

                    for (const string& path : files)
                    {
                        if (FileSystem::IsEngineTextureFile(path))
                        {
                            texture_paths.push_back(path);
                        }
                        else if (FileSystem::IsEngineMeshFile(path))
                        {
                            mesh_paths.push_back(path);
                        }
                        else if (FileSystem::IsEngineMaterialFile(path))
                        {
                            material_paths.push_back(path);
                        }
                    }

                    // pass 1, textures and meshes are independent, run them in parallel
                    // ResourceCache::Load uses a per-path in-flight lock so concurrent loads of the same path are deduplicated,
                    // RHI_Texture::PrepareForGpu transitions state via compare_exchange_strong so it is safe across threads
                    if (!texture_paths.empty())
                    {
                        ThreadPool::ParallelLoop([&texture_paths, resource_count](uint32_t start, uint32_t end)
                        {
                            for (uint32_t i = start; i < end; i++)
                            {
                                if (shared_ptr<RHI_Texture> texture = ResourceCache::Load<RHI_Texture>(texture_paths[i]))
                                {
                                    texture->PrepareForGpu();
                                }

                                if (resource_count > 0)
                                {
                                    ProgressTracker::GetProgress(ProgressType::World).JobDone();
                                }
                            }
                        }, static_cast<uint32_t>(texture_paths.size()));
                    }

                    if (!mesh_paths.empty())
                    {
                        ThreadPool::ParallelLoop([&mesh_paths, resource_count](uint32_t start, uint32_t end)
                        {
                            for (uint32_t i = start; i < end; i++)
                            {
                                ResourceCache::Load<Mesh>(mesh_paths[i]);

                                if (resource_count > 0)
                                {
                                    ProgressTracker::GetProgress(ProgressType::World).JobDone();
                                }
                            }
                        }, static_cast<uint32_t>(mesh_paths.size()));
                    }

                    // pass 2, materials reference textures by path so they must run after the texture pass completes
                    if (!material_paths.empty())
                    {
                        ThreadPool::ParallelLoop([&material_paths, resource_count](uint32_t start, uint32_t end)
                        {
                            for (uint32_t i = start; i < end; i++)
                            {
                                ResourceCache::Load<Material>(material_paths[i]);

                                if (resource_count > 0)
                                {
                                    ProgressTracker::GetProgress(ProgressType::World).JobDone();
                                }
                            }
                        }, static_cast<uint32_t>(material_paths.size()));
                    }
                }
            }

            // load xml document
            pugi::xml_document doc;
            pugi::xml_parse_result result = doc.load_file(file_path.c_str());
            if (!result)
            {
                SP_LOG_ERROR("Failed to load XML file: %s", result.description());
                ProgressTracker::SetGlobalLoadingState(false);
                return;
            }

            // get world node
            pugi::xml_node world_node = doc.child("World");
            if (!world_node)
            {
                SP_LOG_ERROR("No 'World' node found.");
                ProgressTracker::SetGlobalLoadingState(false);
                return;
            }

            // read metadata
            world_description = world_node.attribute("description").as_string();

            // console variables: apply any cvars defined by the world
            // format:
            //   <ConsoleVariables>
            //     <Variable name="r.restir_pt" value="1" />
            //   </ConsoleVariables>
            world_console_variables.clear();
            if (pugi::xml_node cvars_node = world_node.child("ConsoleVariables"))
            {
                for (pugi::xml_node var_node = cvars_node.child("Variable"); var_node; var_node = var_node.next_sibling("Variable"))
                {
                    const char* name  = var_node.attribute("name").as_string();
                    const char* value = var_node.attribute("value").as_string();

                    if (name && name[0] != '\0')
                    {
                        ConsoleRegistry::Get().SetValueFromString(name, value);
                        world_console_variables.emplace_back(name);
                    }
                }
            }

            // entities
            {
                // get node
                pugi::xml_node entities_node = world_node.child("Entities");
                if (!entities_node)
                {
                    SP_LOG_ERROR("No 'Entities' node found.");
                    ProgressTracker::SetGlobalLoadingState(false);
                    return;
                }

                // collect all root entity nodes
                vector<pugi::xml_node> entity_nodes;
                for (pugi::xml_node entity_node = entities_node.child("Entity"); entity_node; entity_node = entity_node.next_sibling("Entity"))
                {
                    entity_nodes.push_back(entity_node);
                }

                // progress tracking
                uint32_t entity_count = static_cast<uint32_t>(entity_nodes.size());
                ProgressTracker::GetProgress(ProgressType::World).Start(entity_count, "Loading entities...");

                // defer script lua execution, lua is single threaded and cannot run across the worker threads below
                {
                    lock_guard lock(script_init_mutex);
                    script_inits_pending.clear();
                }
                defer_script_init.store(true, memory_order_release);

                // load root entities in parallel, each created entity is auto-drained into the renderer on the next World::Tick
                ThreadPool::ParallelLoop([&entity_nodes](uint32_t start, uint32_t end)
                {
                    for (uint32_t i = start; i < end; i++)
                    {
                        Entity* entity = World::CreateEntity();
                        entity->Load(entity_nodes[i]);
                        ProgressTracker::GetProgress(ProgressType::World).JobDone();
                    }
                }, entity_count);

                // every entity now exists, run the queued script initialization sequentially on this thread
                // builder scripts get the full thread pool for their own parallel work and lua stays single threaded
                defer_script_init.store(false, memory_order_release);
                {
                    vector<pair<int, function<void()>>> inits;
                    {
                        lock_guard lock(script_init_mutex);
                        inits.swap(script_inits_pending);
                    }

                    // run lower order first so lights are configured before heavy world builders populate the scene
                    stable_sort(inits.begin(), inits.end(), [](const pair<int, function<void()>>& a, const pair<int, function<void()>>& b)
                    {
                        return a.first < b.first;
                    });

                    for (pair<int, function<void()>>& init : inits)
                    {
                        init.second();
                    }
                }
            }

            // report time
            SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

            ProgressTracker::SetGlobalLoadingState(false);
        });

        return true;
    }

    sol::state_view World::GetLuaState()
    {
        return sol::state_view(lua_state);
    }

    Entity* World::CreateEntity()
    {
        lock_guard lock(entity_access_mutex);

        Entity* entity = new Entity();
        // entity becomes visible to the renderer on the next World::Tick which auto-drains this list, partial component state is tolerated via skip checks
        entities_pending.push_back(entity);
        mark_entity_changed(entity->GetObjectId(), EntityChange::Components); // new entity requires resolve

        // entities created while playing are transient to the session and get removed when play stops
        if (Engine::IsFlagSet(EngineMode::Playing))
        {
            play_mode_spawned_ids.insert(entity->GetObjectId());
        }

        return entity;
    }

    bool World::IsDeferringScriptInit()
    {
        return defer_script_init.load(memory_order_acquire);
    }

    void World::AddDeferredScriptInit(int order, function<void()>&& init)
    {
        lock_guard lock(script_init_mutex);
        script_inits_pending.emplace_back(order, std::move(init));
    }

    bool World::EntityExists(Entity* entity)
    {
        SP_ASSERT_MSG(entity != nullptr, "Entity is null");

        return GetEntityById(entity->GetObjectId()) != nullptr;
    }

    void World::RemoveEntity(Entity* entity_to_remove)
    {
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");

        lock_guard<mutex> lock(entity_access_mutex);

        // keep track of the local camera pointer so we don't have a dangling pointer
        if (Camera* camera_ = entity_to_remove->GetComponent<Camera>())
        {
            camera = nullptr;
        }

        // remove the entity and all of its children
        {
            // get the root entity and its descendants
            vector<Entity*> entities_to_remove;
            entities_to_remove.push_back(entity_to_remove); // add the root entity
            entity_to_remove->GetDescendants(&entities_to_remove); // get descendants

            // create a set containing the object ids of entities to remove
            set<uint64_t> ids_to_remove;
            for (Entity* entity : entities_to_remove)
            {
                ids_to_remove.insert(entity->GetObjectId());
            }

            // defer removal
            pending_remove.insert(ids_to_remove.begin(), ids_to_remove.end());

            // detach from parent so it won't hold a dangling pointer after deferred deletion
            if (Entity* parent = entity_to_remove->GetParent())
            {
                parent->RemoveChild(entity_to_remove, false);
            }
        }

        resolve = true;
    }

    void World::RemoveEntityImmediate(Entity* entity_to_remove)
    {
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");

        lock_guard<mutex> lock(entity_access_mutex);

        // editor selection lives on the camera, drop deleted entities from it to avoid a dangling pointer
        Camera* selection_camera = GetCamera();

        // keep track of the local camera pointer so we don't have a dangling pointer
        if (Camera* camera_ = entity_to_remove->GetComponent<Camera>())
        {
            camera = nullptr;
        }

        // get the entity and all of its descendants
        vector<Entity*> entities_to_remove;
        entities_to_remove.push_back(entity_to_remove);
        entity_to_remove->GetDescendants(&entities_to_remove);

        // if there was a parent, update it
        if (Entity* parent = entity_to_remove->GetParent())
        {
            parent->AcquireChildren();
        }

        // remove and delete immediately
        for (Entity* entity : entities_to_remove)
        {
            uint64_t id = entity->GetObjectId();

            if (selection_camera)
            {
                selection_camera->RemoveFromSelection(entity);
            }

            // remove from entities vector
            auto it = find(entities.begin(), entities.end(), entity);
            if (it != entities.end())
            {
                // clean up change tracking
                entity_states.erase(id);
                if (Material* mat = entity->GetComponent<Render>() ? entity->GetComponent<Render>()->GetMaterial() : nullptr)
                {
                    material_state_hashes.erase(mat->GetObjectId());
                }
                light_state_hashes.erase(id);
                entities.erase(it);
            }

            // also remove from the pending additions list in case it was just created and not yet drained
            auto pending_it = find(entities_pending.begin(), entities_pending.end(), entity);
            if (pending_it != entities_pending.end())
            {
                entities_pending.erase(pending_it);
            }

            delete entity;
        }
    }

    void World::GetRootEntities(vector<Entity*>& entities_out)
    {
        lock_guard<mutex> lock(entity_access_mutex);

        entities_out.clear();
        entities_out.reserve(entities.size() + entities_pending.size());

        // include committed entities
        for (Entity* entity : entities)
        {
            if (!entity->GetParent())
            {
                entities_out.emplace_back(entity);
            }
        }

        // also include entities that are still pending, important during world loading when prefabs reference entities that haven't been drained yet
        for (Entity* entity : entities_pending)
        {
            if (!entity->GetParent())
            {
                entities_out.emplace_back(entity);
            }
        }
    }

    void World::MoveEntityToIndex(Entity* entity, uint32_t index)
    {
        if (!entity)
        {
            return;
        }

        lock_guard<mutex> lock(entity_access_mutex);

        // find the entity in the list
        auto it = find(entities.begin(), entities.end(), entity);
        if (it == entities.end())
        {
            return;
        } // entity not found

        // get current position before removing
        uint32_t current_index = static_cast<uint32_t>(distance(entities.begin(), it));

        // remove from current position
        entities.erase(it);

        // adjust target index if the entity was before the target position
        // (removing it shifts all subsequent indices down by 1)
        if (current_index < index && index > 0)
        {
            index--;
        }

        // clamp index to valid range
        if (index > entities.size())
        {
            index = static_cast<uint32_t>(entities.size());
        }

        // insert at new position
        entities.insert(entities.begin() + index, entity);
    }

    void World::MoveRootEntityNear(Entity* entity_to_move, Entity* target_entity, bool insert_after)
    {
        if (!entity_to_move || !target_entity)
        {
            return;
        }

        // both must be root entities (no parent)
        if (entity_to_move->GetParent() || target_entity->GetParent())
        {
            return;
        }

        lock_guard<mutex> lock(entity_access_mutex);

        // find and remove the entity to move
        auto move_it = find(entities.begin(), entities.end(), entity_to_move);
        if (move_it == entities.end())
        {
            return;
        }
        entities.erase(move_it);

        // find the target entity's position (after removal of entity_to_move)
        auto target_it = find(entities.begin(), entities.end(), target_entity);
        if (target_it == entities.end())
        {
            // target not found, put entity_to_move back at end
            entities.push_back(entity_to_move);
            return;
        }

        // insert before or after the target
        if (insert_after)
        {
            ++target_it;
        }

        entities.insert(target_it, entity_to_move);
    }

    Entity* World::GetEntityById(const uint64_t id)
    {
        lock_guard<mutex> lock(entity_access_mutex);

        for (const auto& entity : entities)
        {
            if (entity && entity->GetObjectId() == id)
            {
                return entity;
            }
        }

        return nullptr;
    }

    const vector<Entity*>& World::GetEntities()
    {
        return entities;
    }

    const vector<Entity*>& World::GetEntitiesLights()
    {
        return entities_lights;
    }

    const vector<Entity*>& World::GetEntitiesRenderables()
    {
        return entities_renderables;
    }

    const string& World::GetName()
    {
        return world_name;
    }

    const string& World::GetFilePath()
    {
        return file_path;
    }

    BoundingBox& World::GetBoundingBox()
    {
        return bounding_box;
    }

    Camera* World::GetCamera()
    {
        return camera ? camera->GetComponent<Camera>() : nullptr;
    }

    Light* World::GetDirectionalLight()
    {
        return light ? light->GetComponent<Light>() : nullptr;
    }

    uint32_t World::GetLightCount()
    {
        return static_cast<uint32_t>(entities_lights.size());
    }

    uint32_t World::GetAudioSourceCount()
    {
        return audio_source_count;
    }

    bool World::HaveMaterialsChangedThisFrame()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        bool changed = false;
        for (Entity* entity : entities)
        {
            if (Render* renderable = entity->GetComponent<Render>())
            {
                if (Material* material = renderable->GetMaterial())
                {
                    const uint64_t id   = material->GetObjectId();
                    size_t current_hash = compute_material_hash(material);
                    auto it = material_state_hashes.find(id);
                    if (it == material_state_hashes.end())
                    {
                        // new material
                        material_state_hashes[id] = current_hash;
                        changed = true;
                    }
                    else if (it->second != current_hash)
                    {
                        // material changed
                        it->second = current_hash;
                        changed = true;
                    }
                }
            }
        }

        return changed;
    }

    bool World::HaveLightsChanged()
    {
        lock_guard<mutex> lock(entity_access_mutex);

        bool changed = false;
        for (Entity* entity : entities_lights)
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                const uint64_t id   = entity->GetObjectId();
                size_t current_hash = compute_light_hash(light, entity);
                auto it = light_state_hashes.find(id);
                if (it == light_state_hashes.end())
                {
                    light_state_hashes[id] = current_hash;
                    changed = true;
                }
                else if (it->second != current_hash)
                {
                    it->second = current_hash;
                    changed = true;
                }
            }
        }

        return changed;
    }

    float World::GetTimeOfDay(bool use_real_world_time)
    {
        return world_time::get_time_of_day(use_real_world_time);
    }

    void World::SetTimeOfDay(float time_of_day)
    {
        if (time_of_day < 0.0f)
        {
            time_of_day = 0.0f;
        }
        else if (time_of_day > 1.0f)
        {
            time_of_day = 1.0f;
        }
        world_time::time_of_day = time_of_day;
    }

    const Vector3& World::GetWind()
    {
        return world_wind::wind;
    }

    void World::SetWind(const Vector3& wind)
    {
        world_wind::wind = wind;
    }

    const string& World::GetDescription()
    {
        return world_description;
    }

    void World::SetDescription(const string& description)
    {
        world_description = description;
    }

    bool World::ReadMetadata(const string& world_file_path, WorldMetadata& metadata)
    {
        // load xml document
        pugi::xml_document doc;
        pugi::xml_parse_result result = doc.load_file(world_file_path.c_str());
        if (!result)
        {
            SP_LOG_ERROR("Failed to load world file for metadata: %s", result.description());
            return false;
        }

        // get world node
        pugi::xml_node world_node = doc.child("World");
        if (!world_node)
        {
            SP_LOG_ERROR("No 'World' node found in: %s", world_file_path.c_str());
            return false;
        }

        // read metadata
        metadata.file_path   = world_file_path;
        metadata.name        = world_node.attribute("name").as_string();
        metadata.description = world_node.attribute("description").as_string();

        return true;
    }
}
