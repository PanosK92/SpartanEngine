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

//= INCLUDES ===================================
#include "pch.h"
#include "McpCommands.h"
#include "../Commands/Console/ConsoleCommands.h"
#include "../Core/ProgressTracker.h"
#include "../Profiling/Profiler.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Component.h"
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
//==============================================

namespace spartan
{
    namespace
    {
        std::string json_escape(const std::string& value)
        {
            std::ostringstream stream;
            for (const unsigned char c : value)
            {
                switch (c)
                {
                case '\\':
                    stream << "\\\\";
                    break;
                case '"':
                    stream << "\\\"";
                    break;
                case '\n':
                    stream << "\\n";
                    break;
                case '\r':
                    stream << "\\r";
                    break;
                case '\t':
                    stream << "\\t";
                    break;
                default:
                    if (c < 0x20)
                    {
                        stream << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
                    }
                    else
                    {
                        stream << c;
                    }
                    break;
                }
            }

            return stream.str();
        }

        std::string json_string(const std::string& value)
        {
            return "\"" + json_escape(value) + "\"";
        }

        std::string json_bool(bool value)
        {
            return value ? "true" : "false";
        }

        std::string json_error(const std::string& error)
        {
            return "{\"ok\":false,\"error\":" + json_string(error) + "}";
        }

        std::string json_vector3(const math::Vector3& value)
        {
            return "{\"x\":" + std::to_string(value.x) + ",\"y\":" + std::to_string(value.y) + ",\"z\":" + std::to_string(value.z) + "}";
        }

        std::string json_quaternion(const math::Quaternion& value)
        {
            return "{\"x\":" + std::to_string(value.x) + ",\"y\":" + std::to_string(value.y) + ",\"z\":" + std::to_string(value.z) + ",\"w\":" + std::to_string(value.w) + "}";
        }

        std::string json_bounding_box(const math::BoundingBox& value)
        {
            return "{\"min\":" + json_vector3(value.GetMin()) + ",\"max\":" + json_vector3(value.GetMax()) + ",\"center\":" + json_vector3(value.GetCenter()) + ",\"size\":" + json_vector3(value.GetSize()) + "}";
        }

        std::optional<std::string> get_argument(const McpRequest& request, const std::string& name)
        {
            const auto it = request.arguments.find(name);
            if (it == request.arguments.end())
            {
                return std::nullopt;
            }

            return it->second;
        }

        bool parse_bool(const std::string& value, bool& result)
        {
            if (value == "true" || value == "1")
            {
                result = true;
                return true;
            }
            if (value == "false" || value == "0")
            {
                result = false;
                return true;
            }

            return false;
        }

        bool parse_uint64(const std::string& value, uint64_t& result)
        {
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
            if (end == value.c_str() || *end != '\0')
            {
                return false;
            }

            result = static_cast<uint64_t>(parsed);
            return true;
        }

        bool parse_float(const std::string& value, float& result)
        {
            char* end = nullptr;
            const float parsed = std::strtof(value.c_str(), &end);
            if (end == value.c_str() || *end != '\0')
            {
                return false;
            }

            result = parsed;
            return std::isfinite(result);
        }

        bool parse_float_list(const std::string& value, std::vector<float>& values, uint32_t expected_count)
        {
            std::stringstream stream(value);
            std::string part;

            while (std::getline(stream, part, ','))
            {
                float parsed = 0.0f;
                if (!parse_float(part, parsed))
                {
                    return false;
                }
                values.emplace_back(parsed);
            }

            return values.size() == expected_count;
        }

        bool parse_vector3(const std::string& value, math::Vector3& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 3))
            {
                return false;
            }

            result = math::Vector3(values[0], values[1], values[2]);
            return result.IsFinite();
        }

        bool parse_quaternion(const std::string& value, math::Quaternion& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 4))
            {
                return false;
            }

            result = math::Quaternion(values[0], values[1], values[2], values[3]);
            return result.IsFinite();
        }

        bool is_edit_mode()
        {
            return !Engine::IsFlagSet(EngineMode::Playing);
        }

        bool is_world_path_valid(const std::string& path)
        {
            std::filesystem::path file_path(path);
            return file_path.is_absolute() && file_path.extension() == ".world";
        }

        Entity* get_entity_from_request(const McpRequest& request, std::string& error)
        {
            const std::optional<std::string> id_arg = get_argument(request, "id");
            if (!id_arg)
            {
                error = "missing id";
                return nullptr;
            }

            uint64_t id = 0;
            if (!parse_uint64(*id_arg, id))
            {
                error = "invalid id";
                return nullptr;
            }

            Entity* entity = World::GetEntityById(id);
            if (entity == nullptr)
            {
                error = "entity not found";
                return nullptr;
            }

            return entity;
        }

        std::string cvar_type(const CVarVariant& value)
        {
            return std::visit([]<typename T>(const T&) -> std::string
            {
                if constexpr (std::is_same_v<T, int32_t>)
                {
                    return "int";
                }
                else if constexpr (std::is_same_v<T, float>)
                {
                    return "float";
                }
                else if constexpr (std::is_same_v<T, bool>)
                {
                    return "bool";
                }
                else
                {
                    return "string";
                }
            }, value);
        }

        bool is_blocked_cvar(const std::string& name)
        {
            static const std::set<std::string> blocked_cvars =
            {
                "r.resolution_scale",
                "r.hdr",
                "r.ray_traced_reflections",
                "r.ray_traced_shadows",
                "r.variable_rate_shading",
                "r.antialiasing_upsampling"
            };

            return blocked_cvars.contains(name);
        }

        std::string entity_to_json(Entity* entity, bool include_children)
        {
            std::string json = "{";
            json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"active\":" + json_bool(entity->IsActive());

            Entity* parent = entity->GetParent();
            json += ",\"parent_id\":";
            json += parent ? json_string(std::to_string(parent->GetObjectId())) : "null";

            json += ",\"components\":[";
            bool first_component = true;
            for (const std::shared_ptr<Component>& component : entity->GetAllComponents())
            {
                if (component == nullptr)
                {
                    continue;
                }

                if (!first_component)
                {
                    json += ",";
                }
                first_component = false;
                json += json_string(Component::TypeToString(component->GetType()));
            }
            json += "]";

            json += ",\"position\":" + json_vector3(entity->GetPosition());
            json += ",\"position_local\":" + json_vector3(entity->GetPositionLocal());
            json += ",\"rotation\":" + json_quaternion(entity->GetRotation());
            json += ",\"rotation_local\":" + json_quaternion(entity->GetRotationLocal());
            json += ",\"scale\":" + json_vector3(entity->GetScale());
            json += ",\"scale_local\":" + json_vector3(entity->GetScaleLocal());

            if (include_children)
            {
                json += ",\"children\":[";
                bool first_child = true;
                for (Entity* child : entity->GetChildren())
                {
                    if (child == nullptr)
                    {
                        continue;
                    }

                    if (!first_child)
                    {
                        json += ",";
                    }
                    first_child = false;
                    json += json_string(std::to_string(child->GetObjectId()));
                }
                json += "]";
            }

            json += "}";
            return json;
        }

        std::string command_ping()
        {
            return "{\"ok\":true,\"version\":" + json_string(version::c_str()) + "}";
        }

        std::string command_engine_status()
        {
            std::string json = "{\"ok\":true";
            json += ",\"version\":" + json_string(version::c_str());
            json += ",\"editor_visible\":" + json_bool(Engine::IsFlagSet(EngineMode::EditorVisible));
            json += ",\"playing\":" + json_bool(Engine::IsFlagSet(EngineMode::Playing));
            json += ",\"paused\":" + json_bool(Engine::IsFlagSet(EngineMode::Paused));
            json += ",\"loading\":" + json_bool(ProgressTracker::IsLoading());
            json += ",\"fps\":" + std::to_string(Profiler::GetFps());
            json += ",\"frame_ms\":" + std::to_string(Profiler::GetFrameDurationMs());
            json += ",\"time_seconds\":" + std::to_string(Timer::GetTimeSec());
            json += "}";
            return json;
        }

        std::string command_engine_set_mode(const McpRequest& request)
        {
            if (const std::optional<std::string> mode = get_argument(request, "mode"))
            {
                if (*mode == "edit")
                {
                    Engine::SetFlag(EngineMode::Playing, false);
                    Engine::SetFlag(EngineMode::Paused, false);
                }
                else if (*mode == "play")
                {
                    Engine::SetFlag(EngineMode::Playing, true);
                    Engine::SetFlag(EngineMode::Paused, false);
                }
                else if (*mode == "pause")
                {
                    Engine::SetFlag(EngineMode::Paused, true);
                }
                else if (*mode == "resume")
                {
                    Engine::SetFlag(EngineMode::Playing, true);
                    Engine::SetFlag(EngineMode::Paused, false);
                }
                else
                {
                    return json_error("invalid mode");
                }
            }

            const std::pair<const char*, EngineMode> flags[] =
            {
                { "playing", EngineMode::Playing },
                { "paused", EngineMode::Paused },
                { "editor_visible", EngineMode::EditorVisible }
            };

            for (const auto& [name, flag] : flags)
            {
                if (const std::optional<std::string> value = get_argument(request, name))
                {
                    bool parsed = false;
                    if (!parse_bool(*value, parsed))
                    {
                        return json_error(std::string("invalid ") + name);
                    }
                    Engine::SetFlag(flag, parsed);
                }
            }

            return command_engine_status();
        }

        std::string command_cvar_list()
        {
            std::string json = "{\"ok\":true,\"cvars\":[";
            bool first = true;
            for (const auto& [name, cvar] : ConsoleRegistry::Get().GetAll())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;

                std::optional<std::string> value = ConsoleRegistry::Get().GetValueAsString(name);
                json += "{";
                json += "\"name\":" + json_string(std::string(name));
                json += ",\"type\":" + json_string(cvar_type(*cvar.m_value_ptr));
                json += ",\"hint\":" + json_string(std::string(cvar.m_hint));
                json += ",\"value\":" + json_string(value.value_or(""));
                json += "}";
            }
            json += "]}";
            return json;
        }

        std::string command_cvar_get(const McpRequest& request)
        {
            const std::optional<std::string> name = get_argument(request, "name");
            if (!name)
            {
                return json_error("missing name");
            }

            ConsoleVariable* cvar = ConsoleRegistry::Get().Find(*name);
            if (cvar == nullptr)
            {
                return json_error("cvar not found");
            }

            std::optional<std::string> value = ConsoleRegistry::Get().GetValueAsString(*name);
            if (!value)
            {
                return json_error("cvar value is unsupported");
            }

            std::string json = "{\"ok\":true";
            json += ",\"name\":" + json_string(*name);
            json += ",\"type\":" + json_string(cvar_type(*cvar->m_value_ptr));
            json += ",\"hint\":" + json_string(std::string(cvar->m_hint));
            json += ",\"value\":" + json_string(*value);
            json += "}";
            return json;
        }

        std::string command_cvar_set(const McpRequest& request)
        {
            const std::optional<std::string> name  = get_argument(request, "name");
            const std::optional<std::string> value = get_argument(request, "value");
            if (!name || !value)
            {
                return json_error("missing name or value");
            }

            if (is_blocked_cvar(*name))
            {
                return json_error("cvar is blocked by MCP");
            }

            if (ConsoleRegistry::Get().Find(*name) == nullptr)
            {
                return json_error("cvar not found");
            }

            if (!ConsoleRegistry::Get().SetValueFromString(*name, *value))
            {
                return json_error("failed to set cvar");
            }

            return command_cvar_get(request);
        }

        std::string command_world_summary()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const math::Vector3& wind = World::GetWind();
            std::string json = "{\"ok\":true";
            json += ",\"name\":" + json_string(World::GetName());
            json += ",\"file_path\":" + json_string(World::GetFilePath());
            json += ",\"description\":" + json_string(World::GetDescription());
            json += ",\"entity_count\":" + std::to_string(World::GetEntities().size());
            json += ",\"light_count\":" + std::to_string(World::GetLightCount());
            json += ",\"audio_source_count\":" + std::to_string(World::GetAudioSourceCount());
            json += ",\"time_of_day\":" + std::to_string(World::GetTimeOfDay(false));
            json += ",\"wind\":" + json_vector3(wind);
            json += ",\"bounding_box\":" + json_bounding_box(World::GetBoundingBox());
            json += "}";
            return json;
        }

        std::string command_world_load(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> path = get_argument(request, "path");
            if (!path)
            {
                return json_error("missing path");
            }

            if (!is_world_path_valid(*path))
            {
                return json_error("path must be an absolute .world file");
            }

            if (!World::LoadFromFile(*path))
            {
                return json_error("failed to queue world load");
            }

            return "{\"ok\":true,\"queued\":true}";
        }

        std::string command_world_save(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string path = World::GetFilePath();
            if (const std::optional<std::string> path_arg = get_argument(request, "path"))
            {
                path = *path_arg;
            }

            if (path.empty())
            {
                return json_error("missing path");
            }

            if (!is_world_path_valid(path))
            {
                return json_error("path must be an absolute .world file");
            }

            if (!World::SaveToFile(path))
            {
                return json_error("failed to save world");
            }

            return "{\"ok\":true,\"path\":" + json_string(path) + "}";
        }

        std::string command_entity_list()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string json = "{\"ok\":true,\"entities\":[";
            bool first = true;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += entity_to_json(entity, false);
            }
            json += "]}";
            return json;
        }

        std::string command_entity_get(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json(entity, true) + "}";
        }

        std::string command_selection_get()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            std::string json = "{\"ok\":true,\"selected_ids\":[";
            bool first = true;
            for (Entity* entity : camera->GetSelectedEntities())
            {
                if (entity == nullptr)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(std::to_string(entity->GetObjectId()));
            }
            json += "]}";
            return json;
        }

        std::string command_entity_create_empty(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity creation requires edit mode");
            }

            Entity* entity = World::CreateEntity();
            if (entity == nullptr)
            {
                return json_error("failed to create entity");
            }

            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                entity->SetObjectName(*name);
            }

            if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
            {
                uint64_t parsed_parent_id = 0;
                if (!parse_uint64(*parent_id, parsed_parent_id))
                {
                    return json_error("invalid parent_id");
                }

                Entity* parent = World::GetEntityById(parsed_parent_id);
                if (parent == nullptr)
                {
                    return json_error("parent entity not found");
                }

                entity->SetParent(parent);
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json(entity, true) + "}";
        }

        std::string command_entity_select(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("selection requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            camera->SetSelectedEntity(entity);
            return command_selection_get();
        }

        std::string command_entity_set_transform(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("transform requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            bool changed = false;

            if (const std::optional<std::string> position = get_argument(request, "position"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                entity->SetPositionLocal(parsed);
                changed = true;
            }

            if (const std::optional<std::string> rotation = get_argument(request, "rotation"))
            {
                math::Quaternion parsed;
                if (!parse_quaternion(*rotation, parsed))
                {
                    return json_error("invalid rotation");
                }
                entity->SetRotationLocal(parsed);
                changed = true;
            }

            if (const std::optional<std::string> rotation_euler = get_argument(request, "rotation_euler"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                entity->SetRotationLocal(math::Quaternion::FromEulerAngles(parsed));
                changed = true;
            }

            if (const std::optional<std::string> scale = get_argument(request, "scale"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*scale, parsed))
                {
                    return json_error("invalid scale");
                }
                entity->SetScaleLocal(parsed);
                changed = true;
            }

            if (!changed)
            {
                return json_error("no transform values provided");
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json(entity, true) + "}";
        }
    }

    std::string ExecuteMcpCommand(const McpRequest& request)
    {
        if (request.command == "ping")
        {
            return command_ping();
        }
        if (request.command == "engine_status")
        {
            return command_engine_status();
        }
        if (request.command == "engine_set_mode")
        {
            return command_engine_set_mode(request);
        }
        if (request.command == "cvar_list")
        {
            return command_cvar_list();
        }
        if (request.command == "cvar_get")
        {
            return command_cvar_get(request);
        }
        if (request.command == "cvar_set")
        {
            return command_cvar_set(request);
        }
        if (request.command == "world_summary")
        {
            return command_world_summary();
        }
        if (request.command == "world_load")
        {
            return command_world_load(request);
        }
        if (request.command == "world_save")
        {
            return command_world_save(request);
        }
        if (request.command == "entity_list")
        {
            return command_entity_list();
        }
        if (request.command == "entity_get")
        {
            return command_entity_get(request);
        }
        if (request.command == "selection_get")
        {
            return command_selection_get();
        }
        if (request.command == "entity_create_empty")
        {
            return command_entity_create_empty(request);
        }
        if (request.command == "entity_select")
        {
            return command_entity_select(request);
        }
        if (request.command == "entity_set_transform")
        {
            return command_entity_set_transform(request);
        }

        return json_error("unknown command");
    }
}
