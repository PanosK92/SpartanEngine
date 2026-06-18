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
#include "../Logging/Log.h"
#include "../Profiling/Profiler.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Component.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Light.h"
#include "../World/Components/Physics.h"
#include "../World/Components/Render.h"
#include "../World/Components/Script.h"
#include <algorithm>
#include <cctype>
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

        std::string to_lower_copy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c)
            {
                return static_cast<char>(std::tolower(c));
            });
            return value;
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
            return "[" + std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "]";
        }

        std::string json_color(const Color& value)
        {
            return "[" + std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b) + "," + std::to_string(value.a) + "]";
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

        bool parse_color(const std::string& value, Color& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 3))
            {
                values.clear();
                if (!parse_float_list(value, values, 4))
                {
                    return false;
                }
            }

            result = values.size() == 3 ? Color(values[0], values[1], values[2], 1.0f) : Color(values[0], values[1], values[2], values[3]);
            return std::isfinite(result.r) && std::isfinite(result.g) && std::isfinite(result.b) && std::isfinite(result.a);
        }

        std::optional<MeshType> mesh_type_from_name(const std::string& name)
        {
            if (name == "cube")
            {
                return MeshType::Cube;
            }
            if (name == "quad" || name == "plane")
            {
                return MeshType::Quad;
            }
            if (name == "sphere")
            {
                return MeshType::Sphere;
            }
            if (name == "cylinder")
            {
                return MeshType::Cylinder;
            }
            if (name == "cone")
            {
                return MeshType::Cone;
            }

            return std::nullopt;
        }

        std::string primitive_types_json()
        {
            return "["
                "{\"name\":\"cube\",\"aliases\":[\"box\"],\"default_body_type\":\"box\"},"
                "{\"name\":\"quad\",\"aliases\":[\"plane\"],\"default_body_type\":\"plane\"},"
                "{\"name\":\"sphere\",\"aliases\":[\"ball\"],\"default_body_type\":\"sphere\"},"
                "{\"name\":\"cylinder\",\"aliases\":[],\"default_body_type\":\"capsule\"},"
                "{\"name\":\"cone\",\"aliases\":[],\"default_body_type\":\"box\"}"
            "]";
        }

        std::optional<BodyType> body_type_from_name(const std::string& name)
        {
            if (name == "box")
            {
                return BodyType::Box;
            }
            if (name == "sphere")
            {
                return BodyType::Sphere;
            }
            if (name == "plane")
            {
                return BodyType::Plane;
            }
            if (name == "capsule")
            {
                return BodyType::Capsule;
            }
            if (name == "mesh")
            {
                return BodyType::Mesh;
            }
            if (name == "mesh_convex")
            {
                return BodyType::MeshConvex;
            }
            if (name == "controller")
            {
                return BodyType::Controller;
            }
            if (name == "vehicle")
            {
                return BodyType::Vehicle;
            }
            if (name == "cloth")
            {
                return BodyType::Cloth;
            }

            return std::nullopt;
        }

        std::string body_type_to_name(BodyType type)
        {
            switch (type)
            {
            case BodyType::Box:
                return "box";
            case BodyType::Sphere:
                return "sphere";
            case BodyType::Plane:
                return "plane";
            case BodyType::Capsule:
                return "capsule";
            case BodyType::Mesh:
                return "mesh";
            case BodyType::MeshConvex:
                return "mesh_convex";
            case BodyType::Controller:
                return "controller";
            case BodyType::Vehicle:
                return "vehicle";
            case BodyType::Cloth:
                return "cloth";
            default:
                return "unknown";
            }
        }

        std::optional<LightType> light_type_from_name(const std::string& name)
        {
            if (name == "directional")
            {
                return LightType::Directional;
            }
            if (name == "point")
            {
                return LightType::Point;
            }
            if (name == "spot")
            {
                return LightType::Spot;
            }
            if (name == "area")
            {
                return LightType::Area;
            }

            return std::nullopt;
        }

        std::string light_type_to_name(LightType type)
        {
            switch (type)
            {
            case LightType::Directional:
                return "directional";
            case LightType::Point:
                return "point";
            case LightType::Spot:
                return "spot";
            case LightType::Area:
                return "area";
            default:
                return "unknown";
            }
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

        Entity* find_entity_by_name_unique(const std::string& name, bool exact, std::string& error)
        {
            const std::string query = to_lower_copy(name);
            Entity* match = nullptr;

            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr)
                {
                    continue;
                }

                const std::string entity_name = to_lower_copy(entity->GetObjectName());
                const bool matches = exact ? entity_name == query : entity_name.find(query) != std::string::npos;
                if (!matches)
                {
                    continue;
                }

                if (match != nullptr)
                {
                    error = "multiple entities match name";
                    return nullptr;
                }

                match = entity;
            }

            if (match == nullptr)
            {
                error = exact ? "entity name not found" : "entity name match not found";
            }

            return match;
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
                Entity* entity = find_entity_by_name_unique(*id_arg, true, error);
                if (entity != nullptr)
                {
                    return entity;
                }

                entity = find_entity_by_name_unique(*id_arg, false, error);
                if (entity != nullptr)
                {
                    return entity;
                }

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

        std::string log_type_to_string(LogType type)
        {
            if (type == LogType::Warning)
            {
                return "warning";
            }
            if (type == LogType::Error)
            {
                return "error";
            }

            return "info";
        }

        std::optional<LogType> log_type_from_name(const std::string& name)
        {
            if (name == "info")
            {
                return LogType::Info;
            }
            if (name == "warning")
            {
                return LogType::Warning;
            }
            if (name == "error")
            {
                return LogType::Error;
            }

            return std::nullopt;
        }

        bool log_type_passes_filter(LogType type, LogType minimum_type)
        {
            return static_cast<uint32_t>(type) >= static_cast<uint32_t>(minimum_type);
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

        std::optional<ComponentType> component_type_from_name(const std::string& name)
        {
            #define X(type, str) \
                if (name == #str) \
                { \
                    return ComponentType::type; \
                }
            SP_COMPONENT_LIST
            #undef X

            return std::nullopt;
        }

        std::string component_types_json()
        {
            std::string json = "[";
            bool first = true;

            #define X(type, str) \
                if (!first) \
                { \
                    json += ","; \
                } \
                first = false; \
                json += json_string(#str);
            SP_COMPONENT_LIST
            #undef X

            json += "]";
            return json;
        }

        std::string entity_components_json(Entity* entity)
        {
            std::string json = "[";
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
            return json;
        }

        std::string entity_to_json_compact(Entity* entity)
        {
            std::string json = "{";
            json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"active\":" + json_bool(entity->IsActive());

            Entity* parent = entity->GetParent();
            json += ",\"parent_id\":";
            json += parent ? json_string(std::to_string(parent->GetObjectId())) : "null";

            json += ",\"components\":" + entity_components_json(entity);
            json += ",\"position\":" + json_vector3(entity->GetPosition());
            json += ",\"rotation_euler\":" + json_vector3(entity->GetRotation().ToEulerAngles());
            json += ",\"scale\":" + json_vector3(entity->GetScale());
            json += "}";
            return json;
        }

        std::string entity_to_json_list_item(Entity* entity)
        {
            std::string json = "{";
            json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());

            Entity* parent = entity->GetParent();
            json += ",\"parent_id\":";
            json += parent ? json_string(std::to_string(parent->GetObjectId())) : "null";

            json += ",\"components\":" + entity_components_json(entity);
            json += "}";
            return json;
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

            json += ",\"components\":" + entity_components_json(entity);

            json += ",\"position\":" + json_vector3(entity->GetPosition());
            json += ",\"position_local\":" + json_vector3(entity->GetPositionLocal());
            json += ",\"rotation_euler\":" + json_vector3(entity->GetRotation().ToEulerAngles());
            json += ",\"rotation_euler_local\":" + json_vector3(entity->GetRotationLocal().ToEulerAngles());
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

        std::string command_console_read(const McpRequest& request)
        {
            uint32_t limit = 100;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 500)
                {
                    return json_error("limit must be between 1 and 500");
                }

                limit = static_cast<uint32_t>(parsed);
            }

            LogType minimum_type = LogType::Info;
            if (const std::optional<std::string> minimum_type_arg = get_argument(request, "minimum_type"))
            {
                const std::optional<LogType> parsed = log_type_from_name(*minimum_type_arg);
                if (!parsed)
                {
                    return json_error("minimum_type must be info, warning, or error");
                }

                minimum_type = *parsed;
            }

            std::vector<LogCmd> entries = Log::GetRecentEntries(500);
            std::vector<LogCmd> filtered_entries;
            filtered_entries.reserve(limit);
            for (auto it = entries.rbegin(); it != entries.rend(); ++it)
            {
                if (!log_type_passes_filter(it->type, minimum_type))
                {
                    continue;
                }

                filtered_entries.emplace_back(*it);
                if (filtered_entries.size() >= limit)
                {
                    break;
                }
            }
            std::reverse(filtered_entries.begin(), filtered_entries.end());

            std::string json = "{\"ok\":true,\"entries\":[";
            bool first = true;
            for (const LogCmd& entry : filtered_entries)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;

                json += "{";
                json += "\"type\":" + json_string(log_type_to_string(entry.type));
                json += ",\"text\":" + json_string(entry.text);
                json += "}";
            }
            json += "]}";
            return json;
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

        std::string command_world_set_environment(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("world environment edits require edit mode");
            }

            bool changed = false;
            if (const std::optional<std::string> time_of_day = get_argument(request, "time_of_day"))
            {
                float parsed = 0.0f;
                if (!parse_float(*time_of_day, parsed) || parsed < 0.0f || parsed > 1.0f)
                {
                    return json_error("invalid time_of_day");
                }

                World::SetTimeOfDay(parsed);
                changed = true;
            }

            if (const std::optional<std::string> wind = get_argument(request, "wind"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*wind, parsed))
                {
                    return json_error("invalid wind");
                }

                World::SetWind(parsed);
                changed = true;
            }

            if (const std::optional<std::string> description = get_argument(request, "description"))
            {
                World::SetDescription(*description);
                changed = true;
            }

            if (!changed)
            {
                return json_error("no environment values provided");
            }

            return command_world_summary();
        }

        std::string command_entity_list(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            uint32_t limit = 200;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 1000)
                {
                    return json_error("limit must be between 1 and 1000");
                }

                limit = static_cast<uint32_t>(parsed);
            }

            uint32_t offset = 0;
            if (const std::optional<std::string> offset_arg = get_argument(request, "offset"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*offset_arg, parsed))
                {
                    return json_error("invalid offset");
                }

                offset = static_cast<uint32_t>(parsed);
            }

            std::vector<Entity*> entities;
            for (Entity* entity : World::GetEntities())
            {
                if (entity != nullptr)
                {
                    entities.emplace_back(entity);
                }
            }

            const uint32_t total = static_cast<uint32_t>(entities.size());
            std::string json = "{\"ok\":true,\"total\":" + std::to_string(total);
            json += ",\"offset\":" + std::to_string(offset);
            json += ",\"entities\":[";
            uint32_t count = 0;
            bool first = true;
            for (uint32_t i = offset; i < total && count < limit; i++)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += entity_to_json_list_item(entities[i]);
                count++;
            }
            json += "],\"count\":" + std::to_string(count);
            json += ",\"truncated\":" + json_bool(offset + count < total);
            json += "}";
            return json;
        }

        std::string command_entity_find(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> name = get_argument(request, "name");
            if (!name || name->empty())
            {
                return json_error("missing name");
            }

            std::string match = get_argument(request, "match").value_or("contains");
            if (match != "exact" && match != "contains")
            {
                return json_error("match must be exact or contains");
            }

            uint32_t limit = 20;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 100)
                {
                    return json_error("limit must be between 1 and 100");
                }

                limit = static_cast<uint32_t>(parsed);
            }

            const std::string query = to_lower_copy(*name);
            std::string json = "{\"ok\":true,\"matches\":[";
            bool first = true;
            uint32_t count = 0;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr)
                {
                    continue;
                }

                const std::string entity_name = to_lower_copy(entity->GetObjectName());
                const bool is_match = match == "exact" ? entity_name == query : entity_name.find(query) != std::string::npos;
                if (!is_match)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += entity_to_json_compact(entity);

                count++;
                if (count >= limit)
                {
                    break;
                }
            }

            json += "],\"truncated\":";
            json += json_bool(count >= limit);
            json += "}";
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

        std::string command_entity_update(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity updates require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            bool changed = false;
            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                if (name->empty())
                {
                    return json_error("name cannot be empty");
                }

                entity->SetObjectName(*name);
                changed = true;
            }

            if (const std::optional<std::string> active = get_argument(request, "active"))
            {
                bool parsed = false;
                if (!parse_bool(*active, parsed))
                {
                    return json_error("invalid active");
                }

                entity->SetActive(parsed);
                changed = true;
            }

            if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
            {
                Entity* parent = nullptr;
                if (!parent_id->empty() && *parent_id != "null" && *parent_id != "none" && *parent_id != "root" && *parent_id != "0")
                {
                    uint64_t parsed_parent_id = 0;
                    if (!parse_uint64(*parent_id, parsed_parent_id))
                    {
                        return json_error("invalid parent_id");
                    }

                    parent = World::GetEntityById(parsed_parent_id);
                    if (parent == nullptr)
                    {
                        return json_error("parent entity not found");
                    }
                    if (parent == entity || parent->IsDescendantOf(entity))
                    {
                        return json_error("parent cannot be self or descendant");
                    }
                }

                entity->SetParent(parent);
                changed = true;
            }

            if (!changed)
            {
                return json_error("no entity values provided");
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_entity_delete(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity deletion requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::string deleted_id = std::to_string(entity->GetObjectId());
            World::RemoveEntity(entity);
            return "{\"ok\":true,\"deleted_id\":" + json_string(deleted_id) + "}";
        }

        std::string command_entity_delete_children(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity child deletion requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            uint32_t deleted_count = 0;
            for (uint32_t pass = 0; pass < 64; pass++)
            {
                entity->AcquireChildren();
                std::vector<Entity*> children = entity->GetChildren();
                if (children.empty())
                {
                    break;
                }

                bool deleted_any = false;
                for (Entity* child : children)
                {
                    if (child == nullptr || !World::EntityExists(child))
                    {
                        continue;
                    }

                    World::RemoveEntityImmediate(child);
                    deleted_count++;
                    deleted_any = true;
                }

                if (!deleted_any)
                {
                    break;
                }
            }

            entity->AcquireChildren();

            std::string json = "{\"ok\":true,\"deleted_count\":" + std::to_string(deleted_count);
            json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"remaining_count\":" + std::to_string(entity->GetChildrenCount());
            json += ",\"remaining_children\":[";
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
                json += json_string(child->GetObjectName());
            }
            json += "]";
            json += "}";
            return json;
        }

        std::string command_component_types()
        {
            return "{\"ok\":true,\"component_types\":" + component_types_json() + "}";
        }

        std::string command_primitive_types()
        {
            return "{\"ok\":true,\"primitive_types\":" + primitive_types_json() + "}";
        }

        std::string command_entity_add_component(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("component edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            if (!type_name)
            {
                return json_error("missing type");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            Component* component = entity->AddComponent(*type);
            if (component == nullptr)
            {
                return json_error("failed to add component");
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_entity_remove_component(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("component edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            if (!type_name)
            {
                return json_error("missing type");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }
            if (entity->GetComponentByType(*type) == nullptr)
            {
                return json_error("entity does not have component");
            }

            entity->RemoveComponentByType(*type);
            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string component_properties_to_json(Component* component)
        {
            if (component == nullptr)
            {
                return "{}";
            }

            const ComponentType type = component->GetType();
            std::string json = "{";

            if (type == ComponentType::Render)
            {
                Render* renderable = static_cast<Render*>(component);
                json += "\"mesh\":" + json_string(renderable->GetMeshName());
                json += ",\"material\":" + json_string(renderable->GetMaterialName());
                json += ",\"default_material\":" + json_bool(renderable->IsUsingDefaultMaterial());
                json += ",\"visible\":" + json_bool(renderable->IsVisible());
                json += ",\"casts_shadows\":" + json_bool(renderable->HasFlag(RenderableFlags::CastsShadows));
                json += ",\"exclude_from_ray_tracing\":" + json_bool(renderable->HasFlag(RenderableFlags::ExcludeFromRayTracing));
                json += ",\"max_render_distance\":" + std::to_string(renderable->GetMaxRenderDistance());
                json += ",\"max_shadow_distance\":" + std::to_string(renderable->GetMaxShadowDistance());
            }
            else if (type == ComponentType::Physics)
            {
                Physics* physics = static_cast<Physics*>(component);
                json += "\"body_type\":" + json_string(body_type_to_name(physics->GetBodyType()));
                json += ",\"static\":" + json_bool(physics->IsStatic());
                json += ",\"kinematic\":" + json_bool(physics->IsKinematic());
                json += ",\"enabled\":" + json_bool(physics->IsEnabled());
                json += ",\"mass\":" + std::to_string(physics->GetMass());
                json += ",\"friction\":" + std::to_string(physics->GetFriction());
                json += ",\"friction_rolling\":" + std::to_string(physics->GetFrictionRolling());
                json += ",\"restitution\":" + std::to_string(physics->GetRestitution());
                json += ",\"center_of_mass\":" + json_vector3(physics->GetCenterOfMass());
            }
            else if (type == ComponentType::Light)
            {
                Light* light = static_cast<Light*>(component);
                json += "\"light_type\":" + json_string(light_type_to_name(light->GetLightType()));
                json += ",\"color\":" + json_color(light->GetColor());
                json += ",\"temperature\":" + std::to_string(light->GetTemperature());
                json += ",\"intensity\":" + std::to_string(light->GetIntensityPhotometric());
                json += ",\"range\":" + std::to_string(light->GetRange());
                json += ",\"angle_degrees\":" + std::to_string(light->GetAngle() / math::deg_to_rad);
                json += ",\"area_width\":" + std::to_string(light->GetAreaWidth());
                json += ",\"area_height\":" + std::to_string(light->GetAreaHeight());
                json += ",\"shadows\":" + json_bool(light->GetFlag(LightFlags::Shadows));
                json += ",\"volumetric\":" + json_bool(light->GetFlag(LightFlags::Volumetric));
                json += ",\"draw_distance\":" + std::to_string(light->GetDrawDistance());
                json += ",\"shadow_distance\":" + std::to_string(light->GetShadowDistance());
                json += ",\"volumetric_distance\":" + std::to_string(light->GetVolumetricDistance());
            }
            else if (type == ComponentType::Camera)
            {
                Camera* camera = static_cast<Camera*>(component);
                json += "\"fov_degrees\":" + std::to_string(camera->GetFovHorizontalDeg());
                json += ",\"aperture\":" + std::to_string(camera->GetAperture());
                json += ",\"shutter_speed\":" + std::to_string(camera->GetShutterSpeed());
                json += ",\"iso\":" + std::to_string(camera->GetIso());
                json += ",\"projection\":" + json_string(camera->GetProjectionType() == Projection_Perspective ? "perspective" : "orthographic");
                json += ",\"controllable\":" + json_bool(camera->GetFlag(CameraFlags::CanBeControlled));
                json += ",\"flashlight\":" + json_bool(camera->GetFlag(CameraFlags::Flashlight));
            }
            else if (type == ComponentType::AudioSource)
            {
                AudioSource* audio_source = static_cast<AudioSource*>(component);
                json += "\"clip\":" + json_string(audio_source->GetAudioClipName());
                json += ",\"mute\":" + json_bool(audio_source->GetMute());
                json += ",\"play_on_start\":" + json_bool(audio_source->GetPlayOnStart());
                json += ",\"loop\":" + json_bool(audio_source->GetLoop());
                json += ",\"is_3d\":" + json_bool(audio_source->GetIs3d());
                json += ",\"volume\":" + std::to_string(audio_source->GetVolume());
                json += ",\"pitch\":" + std::to_string(audio_source->GetPitch());
                json += ",\"reverb_enabled\":" + json_bool(audio_source->GetReverbEnabled());
                json += ",\"reverb_room_size\":" + std::to_string(audio_source->GetReverbRoomSize());
                json += ",\"reverb_decay\":" + std::to_string(audio_source->GetReverbDecay());
                json += ",\"reverb_wet\":" + std::to_string(audio_source->GetReverbWet());
            }
            else if (type == ComponentType::Script)
            {
                Script* script = static_cast<Script*>(component);
                json += "\"file_path\":" + json_string(script->file_path);
            }

            json += "}";
            return json;
        }

        std::string editable_properties_json(ComponentType type)
        {
            if (type == ComponentType::Render)
            {
                return "[\"mesh\",\"material\",\"default_material\",\"visible\",\"casts_shadows\",\"exclude_from_ray_tracing\",\"max_render_distance\",\"max_shadow_distance\"]";
            }
            if (type == ComponentType::Physics)
            {
                return "[\"body_type\",\"static\",\"kinematic\",\"enabled\",\"mass\",\"friction\",\"friction_rolling\",\"restitution\",\"center_of_mass\",\"linear_velocity\",\"angular_velocity\"]";
            }
            if (type == ComponentType::Light)
            {
                return "[\"light_type\",\"color\",\"temperature\",\"intensity\",\"range\",\"angle_degrees\",\"area_width\",\"area_height\",\"shadows\",\"volumetric\",\"draw_distance\",\"shadow_distance\",\"volumetric_distance\"]";
            }
            if (type == ComponentType::Camera)
            {
                return "[\"fov_degrees\",\"aperture\",\"shutter_speed\",\"iso\",\"projection\",\"controllable\",\"flashlight\"]";
            }
            if (type == ComponentType::AudioSource)
            {
                return "[\"clip\",\"mute\",\"play_on_start\",\"loop\",\"is_3d\",\"volume\",\"pitch\",\"reverb_enabled\",\"reverb_room_size\",\"reverb_decay\",\"reverb_wet\"]";
            }
            if (type == ComponentType::Script)
            {
                return "[\"file_path\"]";
            }

            return "[]";
        }

        std::string command_component_get(const McpRequest& request)
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

            const std::optional<std::string> type_name = get_argument(request, "type");
            if (!type_name)
            {
                return json_error("missing type");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            Component* component = entity->GetComponentByType(*type);
            if (component == nullptr)
            {
                return json_error("entity does not have component");
            }

            std::string json = "{\"ok\":true,\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"editable_properties\":" + editable_properties_json(*type);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += "}}";
            return json;
        }

        bool set_render_property(Render* renderable, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "mesh")
            {
                const std::optional<MeshType> parsed = mesh_type_from_name(value);
                if (!parsed)
                {
                    error = "invalid mesh";
                    return false;
                }
                renderable->SetMesh(*parsed);
                return true;
            }
            if (property == "material")
            {
                if (value == "default")
                {
                    renderable->SetDefaultMaterial();
                }
                else
                {
                    renderable->SetMaterial(value);
                }
                return true;
            }
            if (property == "default_material")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid default_material";
                    return false;
                }
                if (parsed)
                {
                    renderable->SetDefaultMaterial();
                }
                return true;
            }
            if (property == "visible")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid visible";
                    return false;
                }
                renderable->SetVisible(parsed);
                return true;
            }
            if (property == "casts_shadows" || property == "exclude_from_ray_tracing")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid render flag";
                    return false;
                }
                renderable->SetFlag(property == "casts_shadows" ? RenderableFlags::CastsShadows : RenderableFlags::ExcludeFromRayTracing, parsed);
                return true;
            }
            if (property == "max_render_distance" || property == "max_shadow_distance")
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid render distance";
                    return false;
                }
                if (property == "max_render_distance")
                {
                    renderable->SetMaxRenderDistance(parsed);
                }
                else
                {
                    renderable->SetMaxShadowDistance(parsed);
                }
                return true;
            }

            error = "unsupported render property";
            return false;
        }

        bool set_physics_property(Physics* physics, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "body_type")
            {
                const std::optional<BodyType> parsed = body_type_from_name(value);
                if (!parsed)
                {
                    error = "invalid body_type";
                    return false;
                }
                physics->SetBodyType(*parsed);
                return true;
            }
            if (property == "static" || property == "kinematic" || property == "enabled")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid physics boolean";
                    return false;
                }
                if (property == "static")
                {
                    physics->SetStatic(parsed);
                }
                else if (property == "kinematic")
                {
                    physics->SetKinematic(parsed);
                }
                else
                {
                    physics->SetEnabled(parsed);
                }
                return true;
            }
            if (property == "mass" || property == "friction" || property == "friction_rolling" || property == "restitution")
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid physics float";
                    return false;
                }
                if (property == "mass")
                {
                    physics->SetMass(parsed);
                }
                else if (property == "friction")
                {
                    physics->SetFriction(parsed);
                }
                else if (property == "friction_rolling")
                {
                    physics->SetFrictionRolling(parsed);
                }
                else
                {
                    physics->SetRestitution(parsed);
                }
                return true;
            }
            if (property == "center_of_mass" || property == "linear_velocity" || property == "angular_velocity")
            {
                math::Vector3 parsed;
                if (!parse_vector3(value, parsed))
                {
                    error = "invalid physics vector";
                    return false;
                }
                if (property == "center_of_mass")
                {
                    physics->SetCenterOfMass(parsed);
                }
                else if (property == "linear_velocity")
                {
                    physics->SetLinearVelocity(parsed);
                }
                else
                {
                    physics->SetAngularVelocity(parsed);
                }
                return true;
            }

            error = "unsupported physics property";
            return false;
        }

        bool set_light_property(Light* light, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "light_type")
            {
                const std::optional<LightType> parsed = light_type_from_name(value);
                if (!parsed)
                {
                    error = "invalid light_type";
                    return false;
                }
                light->SetLightType(*parsed);
                return true;
            }
            if (property == "color")
            {
                Color parsed;
                if (!parse_color(value, parsed))
                {
                    error = "invalid color";
                    return false;
                }
                light->SetColor(parsed);
                return true;
            }
            if (property == "shadows" || property == "volumetric")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid light flag";
                    return false;
                }
                light->SetFlag(property == "shadows" ? LightFlags::Shadows : LightFlags::Volumetric, parsed);
                return true;
            }
            if (
                property == "temperature" || property == "intensity" || property == "range" ||
                property == "angle_degrees" || property == "area_width" || property == "area_height" ||
                property == "draw_distance" || property == "shadow_distance" || property == "volumetric_distance"
            )
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid light float";
                    return false;
                }
                if (property == "temperature")
                {
                    light->SetTemperature(parsed);
                }
                else if (property == "intensity")
                {
                    light->SetIntensity(parsed);
                }
                else if (property == "range")
                {
                    light->SetRange(parsed);
                }
                else if (property == "angle_degrees")
                {
                    light->SetAngle(parsed * math::deg_to_rad);
                }
                else if (property == "area_width")
                {
                    light->SetAreaWidth(parsed);
                }
                else if (property == "area_height")
                {
                    light->SetAreaHeight(parsed);
                }
                else if (property == "draw_distance")
                {
                    light->SetDrawDistance(parsed);
                }
                else if (property == "shadow_distance")
                {
                    light->SetShadowDistance(parsed);
                }
                else
                {
                    light->SetVolumetricDistance(parsed);
                }
                return true;
            }

            error = "unsupported light property";
            return false;
        }

        bool set_camera_property(Camera* camera, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "projection")
            {
                if (value == "perspective")
                {
                    camera->SetProjection(Projection_Perspective);
                    return true;
                }
                if (value == "orthographic")
                {
                    camera->SetProjection(Projection_Orthographic);
                    return true;
                }

                error = "invalid projection";
                return false;
            }
            if (property == "controllable" || property == "flashlight")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid camera flag";
                    return false;
                }
                camera->SetFlag(property == "controllable" ? CameraFlags::CanBeControlled : CameraFlags::Flashlight, parsed);
                return true;
            }
            if (property == "fov_degrees" || property == "aperture" || property == "shutter_speed" || property == "iso")
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid camera float";
                    return false;
                }
                if (property == "fov_degrees")
                {
                    camera->SetFovHorizontalDeg(parsed);
                }
                else if (property == "aperture")
                {
                    camera->SetAperture(parsed);
                }
                else if (property == "shutter_speed")
                {
                    camera->SetShutterSpeed(parsed);
                }
                else
                {
                    camera->SetIso(parsed);
                }
                return true;
            }

            error = "unsupported camera property";
            return false;
        }

        bool set_audio_source_property(AudioSource* audio_source, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "clip")
            {
                audio_source->SetAudioClip(value);
                return true;
            }
            if (property == "mute" || property == "play_on_start" || property == "loop" || property == "is_3d" || property == "reverb_enabled")
            {
                bool parsed = false;
                if (!parse_bool(value, parsed))
                {
                    error = "invalid audio boolean";
                    return false;
                }
                if (property == "mute")
                {
                    audio_source->SetMute(parsed);
                }
                else if (property == "play_on_start")
                {
                    audio_source->SetPlayOnStart(parsed);
                }
                else if (property == "loop")
                {
                    audio_source->SetLoop(parsed);
                }
                else if (property == "is_3d")
                {
                    audio_source->SetIs3d(parsed);
                }
                else
                {
                    audio_source->SetReverbEnabled(parsed);
                }
                return true;
            }
            if (property == "volume" || property == "pitch" || property == "reverb_room_size" || property == "reverb_decay" || property == "reverb_wet")
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid audio float";
                    return false;
                }
                if (property == "volume")
                {
                    audio_source->SetVolume(parsed);
                }
                else if (property == "pitch")
                {
                    audio_source->SetPitch(parsed);
                }
                else if (property == "reverb_room_size")
                {
                    audio_source->SetReverbRoomSize(parsed);
                }
                else if (property == "reverb_decay")
                {
                    audio_source->SetReverbDecay(parsed);
                }
                else
                {
                    audio_source->SetReverbWet(parsed);
                }
                return true;
            }

            error = "unsupported audio_source property";
            return false;
        }

        std::string command_component_set(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("component edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> type_name = get_argument(request, "type");
            const std::optional<std::string> property = get_argument(request, "property");
            const std::optional<std::string> value = get_argument(request, "value");
            if (!type_name || !property || !value)
            {
                return json_error("missing type, property, or value");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            Component* component = entity->GetComponentByType(*type);
            if (component == nullptr)
            {
                return json_error("entity does not have component");
            }

            bool changed = false;
            if (*type == ComponentType::Render)
            {
                changed = set_render_property(static_cast<Render*>(component), *property, *value, error);
            }
            else if (*type == ComponentType::Physics)
            {
                changed = set_physics_property(static_cast<Physics*>(component), *property, *value, error);
            }
            else if (*type == ComponentType::Light)
            {
                changed = set_light_property(static_cast<Light*>(component), *property, *value, error);
            }
            else if (*type == ComponentType::Camera)
            {
                changed = set_camera_property(static_cast<Camera*>(component), *property, *value, error);
            }
            else if (*type == ComponentType::AudioSource)
            {
                changed = set_audio_source_property(static_cast<AudioSource*>(component), *property, *value, error);
            }
            else if (*type == ComponentType::Script && *property == "file_path")
            {
                static_cast<Script*>(component)->LoadScriptFile(*value);
                changed = true;
            }
            else
            {
                error = "unsupported component property";
            }

            if (!changed)
            {
                return json_error(error.empty() ? "failed to set component property" : error);
            }

            std::string json = "{\"ok\":true,\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += "}}";
            return json;
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

        std::string command_context_snapshot()
        {
            std::string json = "{\"ok\":true";
            json += ",\"status\":" + command_engine_status();
            json += ",\"world\":" + command_world_summary();
            json += ",\"selection\":" + command_selection_get();
            json += "}";
            return json;
        }

        std::string command_entity_resolve(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            bool selected = false;
            if (const std::optional<std::string> selected_arg = get_argument(request, "selected"))
            {
                if (!parse_bool(*selected_arg, selected))
                {
                    return json_error("invalid selected");
                }
            }

            if (selected)
            {
                Camera* camera = World::GetCamera();
                if (camera == nullptr)
                {
                    return json_error("camera not found");
                }

                std::vector<Entity*> selected_entities;
                for (Entity* entity : camera->GetSelectedEntities())
                {
                    if (entity != nullptr)
                    {
                        selected_entities.emplace_back(entity);
                    }
                }

                if (selected_entities.empty())
                {
                    return json_error("nothing selected");
                }
                if (selected_entities.size() > 1)
                {
                    return json_error("multiple entities selected");
                }

                return "{\"ok\":true,\"entity\":" + entity_to_json_compact(selected_entities.front()) + ",\"source\":\"selection\"}";
            }

            if (const std::optional<std::string> id = get_argument(request, "id"))
            {
                std::string error;
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return json_error(error);
                }

                return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + ",\"source\":\"id\"}";
            }

            const std::optional<std::string> name = get_argument(request, "name");
            if (!name || name->empty())
            {
                return json_error("missing id, name, or selected");
            }

            std::string error;
            Entity* entity = find_entity_by_name_unique(*name, true, error);
            if (entity != nullptr)
            {
                return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + ",\"source\":\"name_exact\"}";
            }

            entity = find_entity_by_name_unique(*name, false, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + ",\"source\":\"name_contains\"}";
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

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_entity_create_primitive(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("primitive creation requires edit mode");
            }

            MeshType mesh_type = MeshType::Cube;
            if (const std::optional<std::string> mesh = get_argument(request, "mesh"))
            {
                const std::optional<MeshType> parsed = mesh_type_from_name(*mesh);
                if (!parsed)
                {
                    return json_error("invalid mesh");
                }

                mesh_type = *parsed;
            }

            Entity* parent = nullptr;
            if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
            {
                uint64_t parsed_parent_id = 0;
                if (!parse_uint64(*parent_id, parsed_parent_id))
                {
                    return json_error("invalid parent_id");
                }

                parent = World::GetEntityById(parsed_parent_id);
                if (parent == nullptr)
                {
                    return json_error("parent entity not found");
                }
            }

            std::optional<math::Vector3> parsed_position;
            if (const std::optional<std::string> position = get_argument(request, "position"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                parsed_position = parsed;
            }

            std::optional<math::Vector3> parsed_rotation_euler;
            if (const std::optional<std::string> rotation_euler = get_argument(request, "rotation_euler"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                parsed_rotation_euler = parsed;
            }

            std::optional<math::Vector3> parsed_scale;
            if (const std::optional<std::string> scale = get_argument(request, "scale"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*scale, parsed))
                {
                    return json_error("invalid scale");
                }
                parsed_scale = parsed;
            }

            bool with_physics = false;
            if (const std::optional<std::string> with_physics_arg = get_argument(request, "with_physics"))
            {
                if (!parse_bool(*with_physics_arg, with_physics))
                {
                    return json_error("invalid with_physics");
                }
            }

            BodyType body_type = BodyType::Box;
            if (const std::optional<std::string> body_type_arg = get_argument(request, "body_type"))
            {
                const std::optional<BodyType> parsed = body_type_from_name(*body_type_arg);
                if (!parsed)
                {
                    return json_error("invalid body_type");
                }

                body_type = *parsed;
                with_physics = true;
            }
            else if (mesh_type == MeshType::Sphere)
            {
                body_type = BodyType::Sphere;
            }
            else if (mesh_type == MeshType::Quad)
            {
                body_type = BodyType::Plane;
            }
            else if (mesh_type == MeshType::Cylinder)
            {
                body_type = BodyType::Capsule;
            }

            std::optional<bool> physics_static;
            if (const std::optional<std::string> value = get_argument(request, "physics_static"))
            {
                bool parsed = false;
                if (!parse_bool(*value, parsed))
                {
                    return json_error("invalid physics_static");
                }

                physics_static = parsed;
                with_physics = true;
            }

            std::optional<bool> physics_kinematic;
            if (const std::optional<std::string> value = get_argument(request, "physics_kinematic"))
            {
                bool parsed = false;
                if (!parse_bool(*value, parsed))
                {
                    return json_error("invalid physics_kinematic");
                }

                physics_kinematic = parsed;
                with_physics = true;
            }

            std::optional<float> physics_mass;
            if (const std::optional<std::string> value = get_argument(request, "physics_mass"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid physics_mass");
                }

                physics_mass = parsed;
                with_physics = true;
            }

            std::optional<float> physics_friction;
            if (const std::optional<std::string> value = get_argument(request, "physics_friction"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid physics_friction");
                }

                physics_friction = parsed;
                with_physics = true;
            }

            std::optional<float> physics_restitution;
            if (const std::optional<std::string> value = get_argument(request, "physics_restitution"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid physics_restitution");
                }

                physics_restitution = parsed;
                with_physics = true;
            }

            Entity* entity = World::CreateEntity();
            if (entity == nullptr)
            {
                return json_error("failed to create entity");
            }

            entity->SetObjectName(get_argument(request, "name").value_or("primitive"));
            if (parent != nullptr)
            {
                entity->SetParent(parent);
            }
            if (parsed_position)
            {
                entity->SetPositionLocal(*parsed_position);
            }
            if (parsed_rotation_euler)
            {
                entity->SetRotationLocal(math::Quaternion::FromEulerAngles(*parsed_rotation_euler));
            }
            if (parsed_scale)
            {
                entity->SetScaleLocal(*parsed_scale);
            }

            Render* renderable = entity->AddComponent<Render>();
            if (renderable == nullptr)
            {
                return json_error("failed to add render component");
            }
            renderable->SetMesh(mesh_type);
            renderable->SetDefaultMaterial();

            if (with_physics)
            {
                Physics* physics = entity->AddComponent<Physics>();
                if (physics == nullptr)
                {
                    return json_error("failed to add physics component");
                }

                physics->SetBodyType(body_type);
                if (physics_static)
                {
                    physics->SetStatic(*physics_static);
                }
                if (physics_kinematic)
                {
                    physics->SetKinematic(*physics_kinematic);
                }
                if (physics_mass)
                {
                    physics->SetMass(*physics_mass);
                }
                if (physics_friction)
                {
                    physics->SetFriction(*physics_friction);
                }
                if (physics_restitution)
                {
                    physics->SetRestitution(*physics_restitution);
                }
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_entity_create_primitive_batch(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("primitive creation requires edit mode");
            }

            const std::optional<std::string> count_arg = get_argument(request, "count");
            uint64_t count = 0;
            if (!count_arg || !parse_uint64(*count_arg, count) || count == 0 || count > 64)
            {
                return json_error("count must be between 1 and 64");
            }

            const std::vector<std::string> keys =
            {
                "mesh",
                "name",
                "parent_id",
                "position",
                "rotation_euler",
                "scale",
                "with_physics",
                "body_type",
                "physics_static",
                "physics_kinematic",
                "physics_mass",
                "physics_friction",
                "physics_restitution"
            };

            std::string created_json = "[";
            uint32_t created_count = 0;
            for (uint64_t i = 0; i < count; i++)
            {
                McpRequest item_request;
                item_request.command = "entity_create_primitive";
                for (const std::string& key : keys)
                {
                    const std::string batch_key = "item_" + std::to_string(i) + "_" + key;
                    const auto it = request.arguments.find(batch_key);
                    if (it != request.arguments.end())
                    {
                        item_request.arguments[key] = it->second;
                    }
                }

                const std::string item_result = command_entity_create_primitive(item_request);
                if (item_result.find("\"ok\":true") == std::string::npos)
                {
                    created_json += "]";
                    std::string json = "{\"ok\":false,\"error\":\"failed to create primitive batch item\"";
                    json += ",\"created\":" + created_json;
                    json += ",\"created_count\":" + std::to_string(created_count);
                    json += ",\"failed_index\":" + std::to_string(i);
                    json += ",\"failure\":" + item_result;
                    json += "}";
                    return json;
                }

                if (created_count > 0)
                {
                    created_json += ",";
                }
                created_json += item_result;
                created_count++;
            }

            std::string json = "{\"ok\":true,\"created\":" + created_json + "]";
            json += ",\"created_count\":" + std::to_string(created_count);
            json += "}";
            return json;
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

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_execute_lua(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("lua execution requires edit mode");
            }

            const std::optional<std::string> code = get_argument(request, "code");
            if (!code || code->empty())
            {
                return json_error("missing code");
            }

            sol::state_view lua = World::GetLuaState();
            sol::protected_function_result result = lua.safe_script(*code, sol::script_pass_on_error);
            if (!result.valid())
            {
                const sol::error error = result;
                return json_error(std::string("lua error, ") + error.what());
            }

            std::string json = "{\"ok\":true";
            const sol::object return_value = result;
            if (return_value.valid() && return_value.get_type() != sol::type::nil)
            {
                const sol::protected_function to_string = lua["tostring"];
                const sol::protected_function_result to_string_result = to_string(return_value);
                if (to_string_result.valid())
                {
                    const sol::optional<std::string> as_string = to_string_result;
                    if (as_string)
                    {
                        json += ",\"result\":" + json_string(*as_string);
                    }
                }
            }
            json += "}";
            return json;
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
        if (request.command == "console_read")
        {
            return command_console_read(request);
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
        if (request.command == "world_set_environment")
        {
            return command_world_set_environment(request);
        }
        if (request.command == "entity_list")
        {
            return command_entity_list(request);
        }
        if (request.command == "entity_find")
        {
            return command_entity_find(request);
        }
        if (request.command == "entity_get")
        {
            return command_entity_get(request);
        }
        if (request.command == "selection_get")
        {
            return command_selection_get();
        }
        if (request.command == "context_snapshot")
        {
            return command_context_snapshot();
        }
        if (request.command == "entity_resolve")
        {
            return command_entity_resolve(request);
        }
        if (request.command == "component_types")
        {
            return command_component_types();
        }
        if (request.command == "primitive_types")
        {
            return command_primitive_types();
        }
        if (request.command == "entity_create_empty")
        {
            return command_entity_create_empty(request);
        }
        if (request.command == "entity_create_primitive")
        {
            return command_entity_create_primitive(request);
        }
        if (request.command == "entity_create_primitive_batch")
        {
            return command_entity_create_primitive_batch(request);
        }
        if (request.command == "entity_update")
        {
            return command_entity_update(request);
        }
        if (request.command == "entity_delete")
        {
            return command_entity_delete(request);
        }
        if (request.command == "entity_delete_children")
        {
            return command_entity_delete_children(request);
        }
        if (request.command == "entity_select")
        {
            return command_entity_select(request);
        }
        if (request.command == "entity_set_transform")
        {
            return command_entity_set_transform(request);
        }
        if (request.command == "entity_add_component")
        {
            return command_entity_add_component(request);
        }
        if (request.command == "entity_remove_component")
        {
            return command_entity_remove_component(request);
        }
        if (request.command == "component_get")
        {
            return command_component_get(request);
        }
        if (request.command == "component_set")
        {
            return command_component_set(request);
        }
        if (request.command == "execute_lua")
        {
            return command_execute_lua(request);
        }

        return json_error("unknown command");
    }
}
