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
#include <unordered_set>
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
#include "Components/Spline.h"
#include "Components/Light.h"
#include "Components/AudioSource.h"
#include "Components/ParticleSystem.h"
#include "Components/Terrain.h"
#include "Components/Text3D.h"
#include "Components/Animator.h"
#include "Components/Ragdoll.h"
#include "../Resource/ResourceCache.h"
#include "../RHI/RHI_Texture.h"
#include "../Rendering/Material.h"
#include "../Rendering/Renderer.h"
#include "Components/Physics.h"
#include "Components/Traffic.h"
#include "Components/Pedestrians.h"
#include "Components/Script.h"
#include "Components/CarReset.h"
#include "../Physics/PhysicsWorld.h"
#include "../Input/Input.h"
#include "../Core/Timer.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>
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
        vector<Entity*> entities_with_render;  // entities subset that contains only active render components
        vector<Entity*> entities_with_ragdoll; // active ragdolls, late-ticked after scripts
        vector<Entity*> entities_with_pretick; // physics, script, or ragdoll, only these need Entity::PreTick
        vector<Entity*> entities_with_logic;   // active non-render entities that still have components to tick
        vector<Entity*> entities_with_icon;    // active entities that show an editor gizmo icon
        vector<Entity*> entities_with_particles; // active particle emitters
        string file_path;
        string world_name; // cached to avoid per-frame allocation
        string world_description;
        vector<string> last_resource_cleanup;
        vector<string> last_resource_cleanup_failures;
        // mcp ai blockout output (project/mcp/blockout), empty means none registered
        string generated_resource_directory;
        // asset viewer curated library (project/mcp/library)
        string library_resource_directory;
        vector<string> world_console_variables; // cvar names overridden by this world (preserved across save/load)
        mutex entity_access_mutex;
        // entities created by workers but not yet drained into the live entities vector, the main thread drains this every tick
        // workers may still be configuring components for these, the renderer tolerates partial state via skip checks
        vector<Entity*> entities_pending;
        // deferred script initialization, lua is single threaded so script init is collected during the parallel
        // entity load and executed on the main thread after entities are published
        atomic<bool> defer_script_init = false;
        mutex script_init_mutex;
        vector<pair<int, function<void()>>> script_inits_pending;
        // keeps the world xml alive until main thread finishes deferred script init
        shared_ptr<pugi::xml_document> deferred_load_document;
        // load worker finished entity build, main thread must publish and run scripts before clearing loading
        atomic<bool> load_ready_for_main_commit = false;
        set<uint64_t> pending_remove;
        uint32_t audio_source_count = 0;
        atomic<bool> resolve        = false;
        bool was_in_editor_mode     = false;
        // tracks observed loading transition so the WorldLoaded event fires exactly once per load
        bool was_loading            = false;
        // rejects re-entrant loads, a second load would run Shutdown on the main thread while the first load's workers are still building the world
        enum class WorldIoState : uint8_t
        {
            Idle,
            Saving,
            Loading
        };
        atomic<WorldIoState> world_io_state = WorldIoState::Idle;
        BoundingBox bounding_box    = BoundingBox::Unit;
        Entity* camera              = nullptr;
        Entity* camera_override     = nullptr; // set by the sequencer or gameplay, takes precedence over the default camera
        Entity* light               = nullptr;

        struct SaveStateReset
        {
            ~SaveStateReset()
            {
                world_io_state.store(
                    WorldIoState::Idle,
                    memory_order_release
                );
            }
        };

        // prefer the player flycam (camera under a controller body) over cinematic sequence cameras
        Entity* pick_default_camera(Entity* current, Entity* candidate)
        {
            if (!candidate || !candidate->GetComponent<Camera>())
            {
                return current;
            }

            auto is_player_camera = [](Entity* entity) -> bool
            {
                Entity* parent = entity->GetParent();
                if (!parent)
                {
                    return false;
                }

                Physics* physics = parent->GetComponent<Physics>();
                return physics && physics->GetBodyType() == BodyType::Controller;
            };

            if (!current)
            {
                return candidate;
            }

            if (!is_player_camera(current) && is_player_camera(candidate))
            {
                return candidate;
            }

            return current;
        }

        void untrack_entity(Entity* entity)
        {
            if (!entity)
            {
                return;
            }

            if (entity == camera)
            {
                camera = nullptr;
            }
            if (entity == camera_override)
            {
                camera_override = nullptr;
            }
            if (entity == light)
            {
                light = nullptr;
            }

            auto erase_from = [entity](vector<Entity*>& list)
            {
                list.erase(remove(list.begin(), list.end(), entity), list.end());
            };

            erase_from(entities_with_render);
            erase_from(entities_with_ragdoll);
            erase_from(entities_with_pretick);
            erase_from(entities_with_logic);
            erase_from(entities_with_icon);
            erase_from(entities_with_particles);
            erase_from(entities_lights);
            erase_from(entities_pending);
        }

        // snapshot for play/stop state restoration (like unity's play mode)
        struct EntitySnapshot
        {
            Vector3 position;
            Quaternion rotation;
            Vector3 scale;
        };
        unordered_map<uint64_t, EntitySnapshot> play_mode_snapshot;
        float play_mode_time_of_day = 0.0f;

        // ids of entities created while playing, they are removed when play stops so spawned objects never leak into the world
        set<uint64_t> play_mode_spawned_ids;

        // play boot spreads Entity::Start over frames so thousands of entities do not freeze the first tick
        enum class play_boot_phase : uint8_t
        {
            idle,
            starting,
            ready
        };
        play_boot_phase play_boot = play_boot_phase::idle;
        vector<Entity*> play_start_queue;
        size_t play_start_cursor = 0;
        constexpr double play_start_budget_ms = 4.0;

        bool entity_has_play_priority(Entity* entity)
        {
            if (!entity)
            {
                return false;
            }
            return entity->GetComponent<Traffic>()
                || entity->GetComponent<Pedestrians>()
                || entity->GetComponent<CarReset>()
                || entity->GetComponent<Physics>()
                || entity->GetComponent<AudioSource>()
                || entity->GetComponent<Script>()
                || entity->GetComponent<Ragdoll>();
        }

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
            // revision covers property/texture pointer edits, resource states catch async prep
            size_t hash = 17;
            hash = (hash * 31) ^ static_cast<size_t>(material->GetRevision());
            hash = (hash * 31) ^ static_cast<size_t>(material->GetResourceState());

            for (const auto* texture : material->GetTextures())
            {
                hash = (hash * 31) ^ reinterpret_cast<size_t>(texture);
                if (texture)
                {
                    hash = (hash * 31) ^ static_cast<size_t>(texture->GetResourceState());
                }
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
                const Matrix& view_projection = light->GetViewProjectionMatrix(i);
                const float* elements         = view_projection.Data();
                for (uint32_t j = 0; j < 16; j++)
                {
                    hash = (hash * 31) ^ std::hash<float>{}(elements[j]);
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
                    if (Render* render = entity->GetComponent<Render>())
                    {
                        bounding_box.Merge(render->GetBoundingBox());
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

        bool path_is_within(
            const string& path,
            const string& directory
        )
        {
            filesystem::path normalized_path =
                filesystem::absolute(path).lexically_normal();
            filesystem::path normalized_directory =
                filesystem::absolute(directory).lexically_normal();

            auto path_it = normalized_path.begin();
            auto directory_it = normalized_directory.begin();
            for (
                ;
                directory_it != normalized_directory.end();
                ++directory_it, ++path_it
            )
            {
                if (path_it == normalized_path.end())
                {
                    return false;
                }

                string path_part = path_it->string();
                string directory_part = directory_it->string();
                transform(
                    path_part.begin(),
                    path_part.end(),
                    path_part.begin(),
                    [](unsigned char character)
                    {
                        return static_cast<char>(
                            tolower(character)
                        );
                    }
                );
                transform(
                    directory_part.begin(),
                    directory_part.end(),
                    directory_part.begin(),
                    [](unsigned char character)
                    {
                        return static_cast<char>(
                            tolower(character)
                        );
                    }
                );
                if (path_part != directory_part)
                {
                    return false;
                }
            }

            return true;
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

        constexpr int frequency_curl_base = 4;
        constexpr int frequency_gust      = 2;
        constexpr int frequency_micro     = 32;
        constexpr int curl_octaves        = 4;
        constexpr float curl_drift        = 0.03f;
        constexpr float gust_speed        = 0.07f;
        constexpr float micro_speed       = 0.9f;
        constexpr float life_rate         = 0.55f;
        constexpr float world_period      = 80.0f;

        uint32_t hash(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        Vector2 hash(int x, int y)
        {
            uint32_t hash_0 = hash(static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u);
            uint32_t hash_1 = hash(hash_0 ^ 0x9e3779b9u);
            constexpr float scale = 2.0f / 4294967295.0f;
            return Vector2(static_cast<float>(hash_0) * scale - 1.0f, static_cast<float>(hash_1) * scale - 1.0f);
        }

        int wrap(int value, int period)
        {
            return ((value % period) + period) % period;
        }

        float interpolate(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        float gradient_noise(const Vector2& position, int period)
        {
            int cell_x = static_cast<int>(floorf(position.x));
            int cell_y = static_cast<int>(floorf(position.y));
            float x = position.x - static_cast<float>(cell_x);
            float y = position.y - static_cast<float>(cell_y);
            float smooth_x = x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
            float smooth_y = y * y * y * (y * (y * 6.0f - 15.0f) + 10.0f);

            Vector2 gradient_a = hash(wrap(cell_x, period), wrap(cell_y, period));
            Vector2 gradient_b = hash(wrap(cell_x + 1, period), wrap(cell_y, period));
            Vector2 gradient_c = hash(wrap(cell_x, period), wrap(cell_y + 1, period));
            Vector2 gradient_d = hash(wrap(cell_x + 1, period), wrap(cell_y + 1, period));

            float a = gradient_a.x * x + gradient_a.y * y;
            float b = gradient_b.x * (x - 1.0f) + gradient_b.y * y;
            float c = gradient_c.x * x + gradient_c.y * (y - 1.0f);
            float d = gradient_d.x * (x - 1.0f) + gradient_d.y * (y - 1.0f);
            return interpolate(interpolate(a, b, smooth_x), interpolate(c, d, smooth_x), smooth_y);
        }

        float fbm_evolving(const Vector2& uv, float time, float phase)
        {
            static const Vector2 directions[curl_octaves] =
            {
                Vector2(0.80f, 0.60f),
                Vector2(-0.60f, 0.80f),
                Vector2(-0.80f, -0.60f),
                Vector2(0.60f, -0.80f)
            };

            float noise           = 0.0f;
            float amplitude       = 1.0f;
            float amplitude_total = 0.0f;
            int frequency         = frequency_curl_base;

            for (int i = 0; i < curl_octaves; i++)
            {
                Vector2 drift = directions[i] * (time * curl_drift * static_cast<float>(frequency));
                float life = 0.6f + 0.4f * sinf(time * life_rate + static_cast<float>(i) * 1.7f + phase);
                noise += amplitude * life * gradient_noise(uv * static_cast<float>(frequency) + drift, frequency);
                amplitude_total += amplitude;
                amplitude *= 0.55f;
                frequency *= 2;
            }

            return noise / amplitude_total;
        }

        Vector3 sample(const Vector3& position, float time)
        {
            float wind_magnitude = sqrtf(wind.x * wind.x + wind.z * wind.z);
            if (wind_magnitude <= 0.0001f)
            {
                return Vector3(0.0f, wind.y, 0.0f);
            }

            Vector2 wind_direction(wind.x / wind_magnitude, wind.z / wind_magnitude);
            Vector2 uv(position.x / world_period, position.z / world_period);
            Vector2 warp_drift_a = Vector2(0.06f, -0.09f) * time;
            Vector2 warp_drift_b = Vector2(-0.07f, 0.05f) * time;
            Vector2 warp(
                gradient_noise(uv * 2.0f + warp_drift_a, 2),
                gradient_noise(uv * 2.0f + warp_drift_b + Vector2(5.1f, 3.7f), 2)
            );
            warp *= 0.18f;

            float flow_x = fbm_evolving(uv + warp, time, 0.0f);
            float flow_y = fbm_evolving(uv - Vector2(warp.y, warp.x), time * 1.07f, 2.71f);
            Vector2 direction = wind_direction + Vector2(clamp(flow_x * 1.6f, -1.0f, 1.0f), clamp(flow_y * 1.6f, -1.0f, 1.0f)) * 0.55f;
            direction.Normalize();

            Vector2 gust_advection = wind_direction * (-time * gust_speed * static_cast<float>(frequency_gust));
            float gust_low = gradient_noise(uv * static_cast<float>(frequency_gust) + gust_advection, frequency_gust) * 0.5f + 0.5f;
            float gust_high = gradient_noise(uv * static_cast<float>(frequency_gust * 2) + gust_advection * 2.0f, frequency_gust * 2) * 0.5f + 0.5f;
            float gust = clamp(interpolate(gust_low, gust_high, 0.35f), 0.0f, 1.0f);
            float gust_t = clamp((gust - 0.42f) / (0.78f - 0.42f), 0.0f, 1.0f);
            gust = gust_t * gust_t * (3.0f - 2.0f * gust_t);

            Vector2 micro_advection = Vector2(0.31f, -0.27f) * (time * micro_speed * static_cast<float>(frequency_micro));
            float micro = gradient_noise(uv * static_cast<float>(frequency_micro) + micro_advection, frequency_micro) * 0.5f + 0.5f;
            float strength = (0.2f + 0.8f * gust) * wind_magnitude;
            return Vector3(direction.x * strength, wind.y * (0.2f + 0.8f * gust) + (micro - 0.5f) * wind_magnitude * 0.2f, direction.y * strength);
        }

        void initialize()
        {
            float rotation_y      = 120.0f * math::deg_to_rad;
            const float intensity = 8.0f;
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

        // unlink doomed entities from survivors first, everything is still alive
        // here so no surviving entity is left holding a freed parent or child
        for (Entity* entity : entities)
        {
            if (!entity || pending_remove.count(entity->GetObjectId()) == 0)
            {
                continue;
            }

            if (Entity* parent = entity->GetParent())
            {
                if (pending_remove.count(parent->GetObjectId()) == 0)
                {
                    parent->RemoveChild(entity, false);
                }
            }

            const vector<Entity*> children = entity->GetChildren();
            for (Entity* child : children)
            {
                if (!child)
                {
                    continue;
                }

                if (pending_remove.count(child->GetObjectId()) == 0)
                {
                    child->ClearParent();
                }
            }
        }

        for (auto it = entities.begin(); it != entities.end(); )
        {
            uint64_t id = (*it)->GetObjectId();
            if (pending_remove.count(id) > 0)
            {
                // strip cache lists before delete, pretick still runs this frame before resolve
                untrack_entity(*it);

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

        // the tracked lists still point at the entities that were just freed, RemoveEntity
        // only flagged a resolve for the frame that queued the removal, not for this one
        resolve = true;
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

        // stop background world jobs before tearing down resources they may still touch
        // collect first, never wait for preload while holding entity_access_mutex
        {
            vector<Traffic*> traffics;
            vector<Pedestrians*> pedestrians;
            {
                lock_guard<mutex> lock(entity_access_mutex);
                for (Entity* entity : entities)
                {
                    if (entity)
                    {
                        if (Traffic* traffic = entity->GetComponent<Traffic>())
                        {
                            traffics.push_back(traffic);
                        }
                        if (Pedestrians* peds = entity->GetComponent<Pedestrians>())
                        {
                            pedestrians.push_back(peds);
                        }
                    }
                }
                for (Entity* entity : entities_pending)
                {
                    if (entity)
                    {
                        if (Traffic* traffic = entity->GetComponent<Traffic>())
                        {
                            traffics.push_back(traffic);
                        }
                        if (Pedestrians* peds = entity->GetComponent<Pedestrians>())
                        {
                            pedestrians.push_back(peds);
                        }
                    }
                }
            }
            for (Traffic* traffic : traffics)
            {
                traffic->Stop();
            }
            // stop before entity delete, ~Pedestrians must not RemoveEntity under the mutex
            for (Pedestrians* peds : pedestrians)
            {
                peds->Stop();
            }
        }

        defer_script_init.store(false, memory_order_release);
        {
            lock_guard lock(script_init_mutex);
            script_inits_pending.clear();
        }
        deferred_load_document.reset();
        load_ready_for_main_commit.store(false, memory_order_release);

        // drop queued work and wait for in-flight material/resource/load tasks
        ThreadPool::Flush(true);

        // a load worker may have signaled commit as it finished, drop that after the wait
        {
            lock_guard lock(script_init_mutex);
            script_inits_pending.clear();
        }
        deferred_load_document.reset();
        load_ready_for_main_commit.store(false, memory_order_release);
        defer_script_init.store(false, memory_order_release);

        Renderer::DisableProceduralGrass();          // drop renderer references to builder owned grass mesh/material
        Renderer::DestroyAccelerationStructures();   // destroy tlas/blas before clearing resources

        // cars hold entity pointers, drop them before entity delete
        Car::ShutdownAll();

        // entities before resources, component destructors still need live meshes/materials
        {
            lock_guard<mutex> lock(entity_access_mutex);
            camera          = nullptr;
            camera_override = nullptr;
            light           = nullptr;
            for (Entity* entity : entities)
            {
                delete entity;
            }
            entities.clear();
            entities_lights.clear();
            entities_with_render.clear();
            entities_with_ragdoll.clear();
            entities_with_pretick.clear();
            entities_with_logic.clear();
            entities_with_icon.clear();
            entities_with_particles.clear();
            // also clear any entities the loader had queued, otherwise we'd leak partially-built objects when a load is aborted
            for (Entity* entity : entities_pending)
            {
                delete entity;
            }
            entities_pending.clear();
            pending_remove.clear();
        }

        WorldHelpers::Clear();                        // release long lived builder meshes and materials
        ResourceCache::Shutdown();                   // release all resources (textures, materials, meshes, etc)
        play_mode_spawned_ids.clear();
        play_mode_snapshot.clear();
        play_boot = play_boot_phase::idle;
        play_start_queue.clear();
        play_start_cursor = 0;
        was_in_editor_mode = true;
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
        // only world file loads park the tick, model importer progress from warm preloads must not freeze play
        if (world_io_state.load(memory_order_acquire) == WorldIoState::Loading)
        {
            SP_PROFILE_CPU();
            was_loading = true;

            // only the main thread may publish into entities, the load worker stages into entities_pending
            if (load_ready_for_main_commit.exchange(false, memory_order_acq_rel))
            {
                ProcessPendingAdditions();

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

                // builder scripts may have spawned more entities
                ProcessPendingAdditions();
                deferred_load_document.reset();

                ProgressTracker::GetProgress(ProgressType::World).Complete();
                ProgressTracker::SetGlobalLoadingState(false);
                world_io_state.store(WorldIoState::Idle, memory_order_release);

                // fall through so resolve rebuilds entities_with_render before Renderer::Tick
                // returning here left that list empty while HaveMaterialsChangedThisFrame still
                // recorded hashes, so bindless materials never uploaded until a later entity spawn
            }
            else
            {
                return;
            }
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
            // drop hashes recorded against an empty entities_with_render during the commit frame gap
            material_state_hashes.clear();
            light_state_hashes.clear();
            SP_FIRE_EVENT(EventType::WorldLoaded);
        }

        // detect game toggling
        const bool started = Engine::IsFlagSet(EngineMode::Playing) && was_in_editor_mode;
        const bool stopped = !Engine::IsFlagSet(EngineMode::Playing) && !was_in_editor_mode;
        was_in_editor_mode = !Engine::IsFlagSet(EngineMode::Playing);

        // start, transform snapshot and Entity::Start are both time budgeted across frames
        if (started)
        {
            play_mode_snapshot.clear();
            play_mode_snapshot.reserve(entities.size());
            play_mode_time_of_day = world_time::time_of_day;
            play_mode_spawned_ids.clear();

            play_start_queue.clear();
            play_start_queue.reserve(entities.size());
            for (Entity* entity : entities)
            {
                play_start_queue.push_back(entity);
            }
            // traffic pedestrians cars scripts first so their async work starts immediately
            stable_partition(
                play_start_queue.begin(),
                play_start_queue.end(),
                [](Entity* entity) { return entity_has_play_priority(entity); });
            play_start_cursor = 0;
            play_boot = play_boot_phase::starting;
        }

        // stop
        if (stopped)
        {
            play_boot = play_boot_phase::idle;
            play_start_queue.clear();
            play_start_cursor = 0;

            // copy the list, Stop can queue removals and must not walk a mutating vector
            const vector<Entity*> entities_to_stop = entities;
            for (Entity* entity : entities_to_stop)
            {
                if (entity)
                {
                    entity->Stop();
                }
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

            // snapshot restores bone entities to mid-play values after Animator::Stop
            // re-bind so skinned meshes leave play in a standing rest pose
            for (Entity* entity : entities)
            {
                if (!entity)
                {
                    continue;
                }

                if (Animator* animator = entity->GetComponent<Animator>())
                {
                    animator->ApplyBindPose();
                }
            }

            // restore skeleton body visibility before play spawned meshes are destroyed
            for (Car* car : Car::GetAll())
            {
                if (car)
                {
                    car->PrepareForPlayStop();
                }
            }

            // remove anything spawned during play so it never leaks into the world or gets saved by accident
            // the removal is deferred and handled by ProcessPendingRemovals right below
            vector<Entity*> spawned;
            for (Entity* entity : entities)
            {
                if (play_mode_spawned_ids.count(entity->GetObjectId()) == 0)
                {
                    continue;
                }

                // transient entities like skid mark trails are owned and managed by their component, removing them here would dangle that pointer
                if (entity->IsTransient())
                {
                    continue;
                }

                // prefab entities are built at load and must survive a stop
                if (entity->HasPrefabData() || entity->IsPrefabOwned())
                {
                    continue;
                }

                spawned.push_back(entity);
            }
            for (Entity* entity : spawned)
            {
                RemoveEntity(entity);
            }
            play_mode_spawned_ids.clear();
        }

        ProcessPendingRemovals();

        // drain a slice of snapshot + Entity::Start each frame until the scene is ready
        if (play_boot == play_boot_phase::starting)
        {
            const double budget_start = Timer::GetTimeMs();
            while (play_start_cursor < play_start_queue.size())
            {
                Entity* entity = play_start_queue[play_start_cursor++];
                if (entity)
                {
                    if (!entity->IsTransient())
                    {
                        EntitySnapshot snapshot;
                        snapshot.position = entity->GetPositionLocal();
                        snapshot.rotation = entity->GetRotationLocal();
                        snapshot.scale    = entity->GetScaleLocal();
                        play_mode_snapshot[entity->GetObjectId()] = snapshot;
                    }
                    entity->Start();
                }

                if ((Timer::GetTimeMs() - budget_start) >= play_start_budget_ms)
                {
                    break;
                }
            }

            if (play_start_cursor >= play_start_queue.size())
            {
                play_start_queue.clear();
                play_start_cursor = 0;
                play_boot = play_boot_phase::ready;
                SP_LOG_INFO(
                    "play boot complete, %zu entities started",
                    entities.size());
            }
        }

        // during boot keep rendering, but skip sim ticks and the per entity change scan
        if (play_boot != play_boot_phase::starting)
        {
            for (Entity* entity : entities_with_pretick)
            {
                if (entity->GetActive())
                {
                    entity->PreTick();
                }
            }

            // renderables cover most of the scene, cull/lod in parallel then finish other components
            const uint32_t render_count = static_cast<uint32_t>(entities_with_render.size());
            if (render_count > 0)
            {
                if (render_count >= 64)
                {
                    ThreadPool::ParallelLoop([&](uint32_t start, uint32_t end)
                    {
                        for (uint32_t i = start; i < end; i++)
                        {
                            Entity* entity = entities_with_render[i];
                            if (!entity->GetActive())
                            {
                                continue;
                            }

                            if (Render* render = entity->GetComponent<Render>())
                            {
                                render->Tick();
                            }
                        }
                    }, render_count);

                    for (Entity* entity : entities_with_render)
                    {
                        if (entity->GetActive())
                        {
                            entity->TickAfterParallelRender();
                        }
                    }
                }
                else
                {
                    for (Entity* entity : entities_with_render)
                    {
                        if (entity->GetActive())
                        {
                            entity->Tick();
                        }
                    }
                }
            }
            for (Entity* entity : entities_with_logic)
            {
                if (entity->GetActive())
                {
                    entity->Tick();
                }
            }

            // ragdoll hit capsules after scripts/pedestrians moved the bodies
            for (Entity* entity : entities_with_ragdoll)
            {
                if (entity->GetActive())
                {
                    if (Ragdoll* ragdoll = entity->GetComponent<Ragdoll>())
                    {
                        ragdoll->LateTick();
                    }
                }
            }

            // only entities marked dirty need the change scan, empty most frames
            if (!entity_states.empty())
            {
                for (Entity* entity : entities)
                {
                    if (!entity->GetActive())
                    {
                        continue;
                    }

                    uint64_t id = entity->GetObjectId();
                    auto it = entity_states.find(id);
                    if (it == entity_states.end())
                    {
                        continue;
                    }

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
                    if (Render* render = entity->GetComponent<Render>())
                    {
                        if (Material* material = render->GetMaterial())
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
                entities_with_render.clear();
                entities_with_ragdoll.clear();
                entities_with_pretick.clear();
                entities_with_logic.clear();
                entities_with_icon.clear();
                entities_with_particles.clear();
                for (Entity* entity : entities)
                {
                    if (entity->GetActive())
                    {
                        camera = pick_default_camera(camera, entity);

                        if (Light* light_comp = entity->GetComponent<Light>())
                        {
                            if (!light && light_comp->GetLightType() == LightType::Directional)
                            {
                                light = entity;
                            }
                            entities_lights.push_back(entity);
                        }

                        const bool has_render = entity->GetComponent<Render>() != nullptr;
                        if (has_render)
                        {
                            entities_with_render.push_back(entity);
                        }
                        else if (entity->GetComponentCount() > 0)
                        {
                            // lights, scripts, audio, etc without a mesh still need Entity::Tick
                            entities_with_logic.push_back(entity);
                        }

                        if (entity->GetComponent<Ragdoll>())
                        {
                            entities_with_ragdoll.push_back(entity);
                        }

                        if (
                            entity->GetComponent<Physics>() ||
                            entity->GetComponent<Script>() ||
                            entity->GetComponent<Ragdoll>()
                        )
                        {
                            entities_with_pretick.push_back(entity);
                        }

                        if (entity->GetComponent<AudioSource>())
                        {
                            audio_source_count++;
                        }

                        if (entity->GetComponent<ParticleSystem>())
                        {
                            entities_with_particles.push_back(entity);
                        }

                        // editor icons, skip empty and render-only props
                        const uint32_t component_count = entity->GetComponentCount();
                        if (component_count > 0 && !(component_count == 1 && has_render))
                        {
                            static const ComponentType icon_types[] =
                            {
                                ComponentType::Light,
                                ComponentType::Camera,
                                ComponentType::AudioSource,
                                ComponentType::ParticleSystem,
                                ComponentType::Volume,
                                ComponentType::SpawnPoint,
                                ComponentType::Terrain,
                                ComponentType::Water,
                                ComponentType::Physics,
                                ComponentType::Spline,
                                ComponentType::SplineFollower,
                                ComponentType::Traffic,
                                ComponentType::Pedestrians,
                                ComponentType::Animator,
                                ComponentType::Ragdoll,
                                ComponentType::SkidMarks,
                                ComponentType::CarReset,
                                ComponentType::Text3D,
                                ComponentType::Script,
                            };

                            for (ComponentType type : icon_types)
                            {
                                if (entity->GetComponentByType(type))
                                {
                                    entities_with_icon.push_back(entity);
                                    break;
                                }
                            }
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

    string World::GetResourceDirectory(
        const string& world_file_path
    )
    {
        return world_file_path_to_resource_directory(
            world_file_path
        );
    }

    void World::SetGeneratedResourceDirectory(
        const string& directory
    )
    {
        generated_resource_directory = directory;
        replace(
            generated_resource_directory.begin(),
            generated_resource_directory.end(),
            '\\',
            '/'
        );
        if (
            !generated_resource_directory.empty() &&
            generated_resource_directory.back() != '/'
        )
        {
            generated_resource_directory += '/';
        }
    }

    const string& World::GetGeneratedResourceDirectory()
    {
        return generated_resource_directory;
    }

    void World::SetLibraryResourceDirectory(
        const string& directory
    )
    {
        library_resource_directory = directory;
        replace(
            library_resource_directory.begin(),
            library_resource_directory.end(),
            '\\',
            '/'
        );
        if (
            !library_resource_directory.empty() &&
            library_resource_directory.back() != '/'
        )
        {
            library_resource_directory += '/';
        }
    }

    const string& World::GetLibraryResourceDirectory()
    {
        return library_resource_directory;
    }

    const vector<string>& World::GetLastResourceCleanup()
    {
        return last_resource_cleanup;
    }

    const vector<string>&
        World::GetLastResourceCleanupFailures()
    {
        return last_resource_cleanup_failures;
    }

    bool World::SaveToFile(string file_path)
    {
        WorldIoState expected = WorldIoState::Idle;
        if (
            !world_io_state.compare_exchange_strong(
                expected,
                WorldIoState::Saving
            )
        )
        {
            SP_LOG_WARNING("A world save is already in progress");
            return false;
        }

        SaveStateReset reset;
        return SaveToFileInternal(move(file_path), false);
    }

    bool World::SaveToFileAsync(string file_path)
    {
        WorldIoState expected = WorldIoState::Idle;
        if (
            !world_io_state.compare_exchange_strong(
                expected,
                WorldIoState::Saving
            )
        )
        {
            SP_LOG_WARNING("A world save is already in progress");
            return false;
        }

        // snapshot live world state on the caller, only the xml write runs on a worker
        if (!SaveToFileInternal(move(file_path), true))
        {
            world_io_state.store(WorldIoState::Idle, memory_order_release);
            return false;
        }

        return true;
    }

    bool World::IsSaving()
    {
        return
            world_io_state.load(memory_order_acquire) ==
            WorldIoState::Saving;
    }

    bool World::SaveToFileInternal(string file_path, bool defer_xml_write)
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
            // mcp raw blockout and curated library live outside the world, leave them alone
            const string generated_directory =
                World::GetGeneratedResourceDirectory();
            const string library_directory =
                World::GetLibraryResourceDirectory();
            auto is_mcp_owned = [&](const string& path) -> bool
            {
                if (path.empty())
                {
                    return false;
                }
                if (
                    !generated_directory.empty() &&
                    path_is_within(path, generated_directory)
                )
                {
                    return true;
                }
                if (
                    !library_directory.empty() &&
                    path_is_within(path, library_directory)
                )
                {
                    return true;
                }
                return false;
            };

            // caches are written by the engine, not by an entity, so nothing in the world points at
            // them and the prune below would wipe them on every save, forcing a full regeneration
            auto is_engine_cache = [](const string& path) -> bool
            {
                const string name = FileSystem::GetFileNameFromFilePath(path);
                return
                    name.find("terrain_cache")      == 0 ||
                    name.find("terrain_mesh_cache") == 0;
            };

            vector<shared_ptr<IResource>> resources = ResourceCache::GetResourcesSnapshot();
            set<IResource*> referenced_resources;
            auto reference_material =
                [&referenced_resources](Material* material)
            {
                if (material == nullptr)
                {
                    return;
                }
                referenced_resources.insert(material);
                for (RHI_Texture* texture : material->GetTextures())
                {
                    if (texture != nullptr)
                    {
                        referenced_resources.insert(texture);
                    }
                }
            };
            {
                lock_guard<mutex> lock(entity_access_mutex);
                for (Entity* entity : entities)
                {
                    if (entity == nullptr)
                    {
                        continue;
                    }
                    if (Render* render = entity->GetComponent<Render>())
                    {
                        if (Mesh* mesh = render->GetMesh())
                        {
                            referenced_resources.insert(mesh);
                        }
                        if (!render->IsUsingDefaultMaterial())
                        {
                            reference_material(
                                render->GetMaterial()
                            );
                        }
                    }
                    if (
                        ParticleSystem* particles =
                            entity->GetComponent<ParticleSystem>()
                    )
                    {
                        if (RHI_Texture* texture = particles->GetTexture())
                        {
                            referenced_resources.insert(texture);
                        }
                    }
                    if (
                        Terrain* terrain =
                            entity->GetComponent<Terrain>()
                    )
                    {
                        if (
                            RHI_Texture* height_map =
                                terrain->GetHeightMapSeed()
                        )
                        {
                            referenced_resources.insert(height_map);
                        }
                        reference_material(
                            terrain->GetMaterial().get()
                        );
                    }
                    if (
                        Spline* spline =
                            entity->GetComponent<Spline>()
                    )
                    {
                        const string& mesh_path =
                            spline->GetInstanceMeshPath();
                        if (
                            !mesh_path.empty()
                        )
                        {
                            if (
                                shared_ptr<Mesh> mesh =
                                    ResourceCache::GetByPath<Mesh>(
                                        mesh_path
                                    )
                            )
                            {
                                referenced_resources.insert(
                                    mesh.get()
                                );
                            }
                        }
                    }
                }
            }

            // the windows file system is case insensitive so the uniqueness check has to be too
            auto to_file_key = [](const string& file_name)
            {
                string key = file_name;
                transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
                return key;
            };

            // give every resource a unique file, duplicate object names used to overwrite each other and break Render::Load
            struct PendingResourceSave
            {
                IResource* resource;
                string target_path;
                bool path_changed;
            };

            vector<PendingResourceSave> pending_saves;
            set<string> used_file_names;
            for (shared_ptr<IResource>& resource : resources)
            {
                if (
                    referenced_resources.find(resource.get()) ==
                    referenced_resources.end()
                )
                {
                    continue;
                }
                // runtime generated resources are rebuilt by code on load, serializing them only accumulates orphans
                if (!resource->IsPersistent())
                {
                    continue;
                }

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

                const string current_path =
                    resource->GetResourceFilePath();
                if (is_mcp_owned(current_path))
                {
                    continue;
                }

                // strip an embedded extension so a name that already carries one does not save with it doubled
                string name = resource->GetObjectName();
                if (name.size() > ext.size() && name.compare(name.size() - ext.size(), ext.size(), ext) == 0)
                {
                    name = name.substr(0, name.size() - ext.size());
                }

                string unique_name = name;
                uint32_t suffix    = 2;
                while (!used_file_names.insert(to_file_key(unique_name + ext)).second)
                {
                    unique_name = name + "_" + to_string(suffix++);
                }

                // repoint before the entity xml below serializes any reference to this resource by name
                const string target_path = directory + unique_name + ext;
                const bool path_changed  = resource->GetResourceFilePath() != FileSystem::GetRelativePath(target_path);
                resource->SetResourceFilePath(target_path);
                pending_saves.push_back({ resource.get(), target_path, path_changed });
            }

            // pass 2, resources already at their target path are current, material setters persist changes immediately
            for (const PendingResourceSave& pending : pending_saves)
            {
                const bool immutable = pending.resource->GetResourceType() != ResourceType::Material;
                if (FileSystem::Exists(pending.target_path) && (!pending.path_changed || immutable))
                {
                    continue;
                }
                pending.resource->SaveToFile(pending.target_path);
            }

            // prefabs on disk reference meshes and materials that no live entity owns,
            // without protecting them a save turns every saved prefab into dangling references
            {
                auto protect_prefab_references =
                    [&used_file_names, &to_file_key](
                        const string& prefab_path
                    )
                {
                    pugi::xml_document prefab_document;
                    if (!prefab_document.load_file(prefab_path.c_str()))
                    {
                        return;
                    }

                    vector<pugi::xml_node> pending =
                    {
                        prefab_document.document_element()
                    };
                    while (!pending.empty())
                    {
                        const pugi::xml_node node = pending.back();
                        pending.pop_back();
                        for (
                            pugi::xml_node child = node.first_child();
                            child;
                            child = child.next_sibling()
                        )
                        {
                            pending.push_back(child);
                        }

                        for (
                            const char* attribute :
                            {
                                "mesh_path",
                                "material_path"
                            }
                        )
                        {
                            const string reference =
                                node.attribute(attribute).as_string();
                            if (!reference.empty())
                            {
                                used_file_names.insert(
                                    to_file_key(
                                        FileSystem::GetFileNameFromFilePath(
                                            reference
                                        )
                                    )
                                );
                            }
                        }
                    }
                };

                try
                {
                    for (
                        const filesystem::directory_entry& entry :
                        filesystem::recursive_directory_iterator(directory)
                    )
                    {
                        if (
                            entry.is_regular_file() &&
                            FileSystem::IsEnginePrefabFile(
                                entry.path().string()
                            )
                        )
                        {
                            protect_prefab_references(
                                entry.path().string()
                            );
                        }
                    }
                }
                catch (const exception& e)
                {
                    SP_LOG_WARNING(
                        "Failed to scan prefabs for referenced resources: %s",
                        e.what()
                    );
                }
            }

            // prune files that no longer belong to this save, loading picks up every file in this directory
            // so stale duplicates from older saves would otherwise come back and shadow the right resources
            last_resource_cleanup.clear();
            last_resource_cleanup_failures.clear();
            for (const string& existing_file : FileSystem::GetFilesInDirectory(directory))
            {
                if (is_mcp_owned(existing_file) || is_engine_cache(existing_file))
                {
                    continue;
                }

                if (used_file_names.find(to_file_key(FileSystem::GetFileNameFromFilePath(existing_file))) == used_file_names.end())
                {
                    if (FileSystem::Delete(existing_file))
                    {
                        last_resource_cleanup.push_back(
                            existing_file
                        );
                    }
                    else
                    {
                        last_resource_cleanup_failures.push_back(
                            existing_file
                        );
                    }
                }
            }

            // pruning used to be silent, which made deleted resources look like
            // files that never existed
            for (
                size_t index = 0;
                index < last_resource_cleanup.size();
                index++
            )
            {
                if (index == 10)
                {
                    SP_LOG_INFO(
                        "Pruned %zu more unreferenced resource files",
                        last_resource_cleanup.size() - index
                    );
                    break;
                }

                SP_LOG_INFO(
                    "Pruned unreferenced resource: %s",
                    last_resource_cleanup[index].c_str()
                );
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
            vector<Entity*> root_entities;
            World::GetRootEntities(root_entities);
            const uint32_t root_entity_count = static_cast<uint32_t>(root_entities.size());

            // progress tracking
            ProgressTracker::GetProgress(ProgressType::World).Start(root_entity_count, "Saving world...");

            // write entities to node while the world is still owned by this thread
            for (Entity* root : root_entities)
            {
                // transient entities are runtime only, such as skid mark trails, they must never be serialized
                if (root->IsTransient())
                {
                    ProgressTracker::GetProgress(ProgressType::World).JobDone();
                    continue;
                }

                pugi::xml_node entity_node = entities_node.append_child("Entity");
                root->Save(entity_node);
                ProgressTracker::GetProgress(ProgressType::World).JobDone();
            }

            // an empty world starts the tracker in continuous mode where it can never reach one on its own
            ProgressTracker::GetProgress(ProgressType::World).Complete();
        }

        if (defer_xml_write)
        {
            // snapshot is complete, only the file write leaves the main thread
            ostringstream xml_stream;
            doc.save(xml_stream, " ", pugi::format_indent);
            string xml_content = xml_stream.str();
            const float elapsed_ms = timer.GetElapsedTimeMs();

            ThreadPool::AddTask(
                [file_path = move(file_path), xml_content = move(xml_content), elapsed_ms]()
                {
                    SaveStateReset reset;

                    ofstream out(file_path, ios::binary | ios::trunc);
                    if (!out)
                    {
                        SP_LOG_ERROR("Failed to save XML file.");
                        return;
                    }

                    out.write(xml_content.data(), static_cast<streamsize>(xml_content.size()));
                    if (!out)
                    {
                        SP_LOG_ERROR("Failed to save XML file.");
                        return;
                    }

                    SP_LOG_INFO("World \"%s\" has been saved. Duration %.2f ms", file_path.c_str(), elapsed_ms);
                }
            );

            return true;
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
        // reject re-entrant loads, the second Shutdown below would tear down the world while the first load's workers are still building it
        WorldIoState expected = WorldIoState::Idle;
        if (
            !world_io_state.compare_exchange_strong(
                expected,
                WorldIoState::Loading
            )
        )
        {
            return false;
        }

        // ensure prefabs are registered before loading
        Car::RegisterPrefabs();

        // shutdown synchronously before async loading
        Shutdown();

        // publish the loading state now so the progress ui shows this frame instead of only once the worker task starts
        ProgressTracker::SetGlobalLoadingState(true);

        // copy path for the lambda capture
        string path_copy = file_path_;

        // load asynchronously
        ThreadPool::AddTask([path_copy]()
        {
            // clears the loading state and releases the guard, must run on every exit path
            auto finish = []()
            {
                // the tracker never resets while it is below one, so an early return or a miscount
                // would keep the loading screen up for this load and poison every load after it
                ProgressTracker::GetProgress(ProgressType::World).Complete();
                ProgressTracker::SetGlobalLoadingState(false);
                world_io_state.store(
                    WorldIoState::Idle,
                    memory_order_release
                );
            };

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
                        const string file_name =
                            FileSystem::GetFileNameFromFilePath(path);
                        if (
                            file_name.rfind("car_", 0) == 0 &&
                            file_name.find("_packed_slot") != string::npos
                        )
                        {
                            continue;
                        }

                        // the terrain reads its own caches while it generates, loading them here as
                        // ordinary resources would build the whole terrain mesh a second time
                        if (
                            file_name.rfind("terrain_cache", 0)      == 0 ||
                            file_name.rfind("terrain_mesh_cache", 0) == 0
                        )
                        {
                            continue;
                        }

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

                    // progress counts only what is actually loaded below, counting every file on disk
                    // left the unclassified ones permanently outstanding and pinned the bar under 100 percent
                    uint32_t resource_count = static_cast<uint32_t>(
                        texture_paths.size() + mesh_paths.size() + material_paths.size()
                    );
                    if (resource_count > 0)
                    {
                        ProgressTracker::GetProgress(ProgressType::World).Start(resource_count, "Loading resources...");
                    }

                    // pass 1, textures and meshes are independent, fan them out together
                    // ResourceCache::Load uses a per-path in-flight lock so concurrent loads of the same path are deduplicated,
                    // RHI_Texture::PrepareForGpu transitions state via compare_exchange_strong so it is safe across threads
                    {
                        struct ResourceJob
                        {
                            enum class Type : uint8_t { Texture, Mesh } type;
                            string path;
                        };

                        vector<ResourceJob> jobs;
                        jobs.reserve(texture_paths.size() + mesh_paths.size());
                        for (const string& path : texture_paths)
                        {
                            jobs.push_back({ ResourceJob::Type::Texture, path });
                        }
                        for (const string& path : mesh_paths)
                        {
                            jobs.push_back({ ResourceJob::Type::Mesh, path });
                        }

                        if (!jobs.empty())
                        {
                            ThreadPool::ParallelLoop([&jobs, resource_count](uint32_t start, uint32_t end)
                            {
                                for (uint32_t i = start; i < end; i++)
                                {
                                    if (jobs[i].type == ResourceJob::Type::Texture)
                                    {
                                        if (shared_ptr<RHI_Texture> texture = ResourceCache::Load<RHI_Texture>(jobs[i].path))
                                        {
                                            texture->PrepareForGpu();
                                        }
                                    }
                                    else
                                    {
                                        ResourceCache::Load<Mesh>(jobs[i].path);
                                    }

                                    if (resource_count > 0)
                                    {
                                        ProgressTracker::GetProgress(ProgressType::World).JobDone();
                                    }
                                }
                            }, static_cast<uint32_t>(jobs.size()));
                        }
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

            // load xml document, kept alive until main thread finishes deferred script init
            shared_ptr<pugi::xml_document> doc = make_shared<pugi::xml_document>();
            pugi::xml_parse_result result = doc->load_file(file_path.c_str());
            if (!result)
            {
                SP_LOG_ERROR("Failed to load XML file: %s", result.description());
                finish();
                return;
            }
            deferred_load_document = doc;

            // get world node
            pugi::xml_node world_node = doc->child("World");
            if (!world_node)
            {
                SP_LOG_ERROR("No 'World' node found.");
                deferred_load_document.reset();
                finish();
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
                    deferred_load_document.reset();
                    finish();
                    return;
                }

                // flatten the entity tree so every node can load in parallel
                // parent_index is into this same vector, UINT32_MAX means root
                struct FlatEntity
                {
                    pugi::xml_node node;
                    uint32_t parent_index = UINT32_MAX;
                };

                vector<FlatEntity> flat_entities;
                {
                    function<void(pugi::xml_node, uint32_t)> collect = [&](pugi::xml_node node, uint32_t parent_index)
                    {
                        const uint32_t index = static_cast<uint32_t>(flat_entities.size());
                        flat_entities.push_back({ node, parent_index });

                        for (pugi::xml_node child = node.child("Entity"); child; child = child.next_sibling("Entity"))
                        {
                            collect(child, index);
                        }
                    };

                    for (pugi::xml_node entity_node = entities_node.child("Entity"); entity_node; entity_node = entity_node.next_sibling("Entity"))
                    {
                        collect(entity_node, UINT32_MAX);
                    }
                }

                // progress tracking
                uint32_t entity_count = static_cast<uint32_t>(flat_entities.size());
                // close the resource phase first, a missed resource JobDone would accumulate into
                // this start and leave the bar stuck under 100 after every entity has loaded
                ProgressTracker::GetProgress(ProgressType::World).Complete();
                ProgressTracker::GetProgress(ProgressType::World).Start(entity_count, "Loading entities...");

                // defer script lua execution, lua is single threaded and cannot run across the worker threads below
                {
                    lock_guard lock(script_init_mutex);
                    script_inits_pending.clear();
                }
                defer_script_init.store(true, memory_order_release);

                // create and load every entity without hierarchy, children are wired after
                // keep this sequential on the load worker, component Initialize/Load is not safe
                // across the pool (audio cache, water gpu buffers, renderer ocean state)
                vector<Entity*> loaded_entities(entity_count, nullptr);
                if (entity_count > 0)
                {
                    for (uint32_t i = 0; i < entity_count; i++)
                    {
                        Entity* entity = World::CreateEntity();
                        entity->Load(flat_entities[i].node, false);
                        loaded_entities[i] = entity;
                        ProgressTracker::GetProgress(ProgressType::World).JobDone();
                    }

                    // wire parents in document order so each parent exists before its children attach
                    for (uint32_t i = 0; i < entity_count; i++)
                    {
                        const uint32_t parent_index = flat_entities[i].parent_index;
                        if (parent_index == UINT32_MAX)
                        {
                            continue;
                        }

                        SP_ASSERT(parent_index < loaded_entities.size());
                        SP_ASSERT(loaded_entities[i] != nullptr);
                        SP_ASSERT(loaded_entities[parent_index] != nullptr);
                        loaded_entities[i]->SetParent(loaded_entities[parent_index]);
                    }
                }

                // leave entities in entities_pending, only the main thread may publish into the live vector
            }

            // report time
            SP_LOG_INFO("World \"%s\" has been loaded. Duration %.2f ms", file_path.c_str(), timer.GetElapsedTimeMs());

            // hand off publish + deferred script init to World::Tick, loading stays up until that finishes
            load_ready_for_main_commit.store(true, memory_order_release);
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

        // entities spawned during play are tracked so they can be removed when play stops
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
        // main thread only, escaped Entity* holders are not notified
        SP_ASSERT_MSG(entity_to_remove != nullptr, "Entity is null");
        SP_ASSERT_MSG(!ProgressTracker::IsLoading(), "Immediate delete is unsafe during world loading");

        lock_guard<mutex> lock(entity_access_mutex);

        // get the entity and all of its descendants
        vector<Entity*> entities_to_remove;
        entities_to_remove.push_back(entity_to_remove);
        entity_to_remove->GetDescendants(&entities_to_remove);

        // detach from the parent before deleting, re-acquiring here would keep the
        // doomed entity in the list because it is still part of the world
        if (Entity* parent = entity_to_remove->GetParent())
        {
            parent->RemoveChild(entity_to_remove, false);
        }

        // remove and delete immediately
        for (Entity* entity : entities_to_remove)
        {
            uint64_t id = entity->GetObjectId();

            // any child that outlives this entity must not keep a freed parent
            const vector<Entity*> children = entity->GetChildren();
            for (Entity* child : children)
            {
                const bool child_survives =
                    child &&
                    find(
                        entities_to_remove.begin(),
                        entities_to_remove.end(),
                        child
                    ) == entities_to_remove.end();
                if (child_survives)
                {
                    child->ClearParent();
                }
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

            untrack_entity(entity);

            pending_remove.erase(id);

            delete entity;
        }

        resolve = true;
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

        // entities created this frame are not drained yet, a lookup that misses them
        // makes callers think their own freshly created entity died
        for (const auto& entity : entities_pending)
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

    const vector<Entity*>& World::GetEntitiesWithRender()
    {
        return entities_with_render;
    }

    const vector<Entity*>& World::GetEntitiesWithIcon()
    {
        return entities_with_icon;
    }

    const vector<Entity*>& World::GetEntitiesWithParticles()
    {
        return entities_with_particles;
    }

    bool World::IsPlayBooting()
    {
        return play_boot == play_boot_phase::starting;
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
        if (camera_override && camera_override->GetActive())
        {
            if (Camera* component = camera_override->GetComponent<Camera>())
            {
                return component;
            }
        }

        return camera ? camera->GetComponent<Camera>() : nullptr;
    }

    void World::SetActiveCamera(Entity* entity)
    {
        camera_override = entity;
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

        static uint32_t last_global_revision = 0;
        static uint64_t resource_poll_frame = 0;
        const uint32_t global_revision = Material::GetGlobalRevision();
        const bool props_changed = global_revision != last_global_revision;
        last_global_revision = global_revision;

        // property/texture pointer edits are covered by the global revision, async resource
        // states still need a periodic poll so bindless updates when gpu prep finishes
        resource_poll_frame++;
        const bool poll_resources = (resource_poll_frame % 8) == 0;
        const bool hashes_empty = material_state_hashes.empty() && !entities_with_render.empty();
        if (!props_changed && !poll_resources && !hashes_empty)
        {
            return false;
        }

        bool changed = false;
        unordered_set<uint64_t> seen;
        seen.reserve(entities_with_render.size());

        for (Entity* entity : entities_with_render)
        {
            if (!entity)
            {
                continue;
            }

            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            Material* material = render->GetMaterial();
            if (!material)
            {
                continue;
            }

            const uint64_t id = material->GetObjectId();
            if (!seen.insert(id).second)
            {
                continue;
            }

            size_t current_hash = compute_material_hash(material);
            auto it = material_state_hashes.find(id);
            if (it == material_state_hashes.end())
            {
                material_state_hashes[id] = current_hash;
                changed = true;
            }
            else if (it->second != current_hash)
            {
                it->second = current_hash;
                changed = true;
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

    Vector3 World::SampleWind(const Vector3& position, float time)
    {
        return world_wind::sample(position, time);
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
