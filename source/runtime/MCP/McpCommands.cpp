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
#include "../Commands/CommandStack.h"
#include "../Core/ProgressTracker.h"
#include "../Logging/Log.h"
#include "../Physics/PhysicsWorld.h"
#include "../Profiling/Profiler.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Component.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Light.h"
#include "../World/Components/ParticleSystem.h"
#include "../World/Components/Physics.h"
#include "../World/Components/Render.h"
#include "../World/Components/Script.h"
#include "../World/Components/Spline.h"
#include "../World/Components/Terrain.h"
#include "../World/Prefab.h"
#include "../Resource/ResourceCache.h"
#include "../Animation/Animation.h"
#include "../Geometry/Mesh.h"
#include "../RHI/RHI_Texture.h"
#include "../Rendering/Material.h"
#include "../Rendering/Renderer.h"
#include "../Math/Vector2.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <typeinfo>
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

        std::string json_string_array(const std::vector<std::string>& values)
        {
            std::string json = "[";
            for (size_t i = 0; i < values.size(); i++)
            {
                if (i != 0)
                {
                    json += ",";
                }
                json += json_string(values[i]);
            }
            json += "]";
            return json;
        }

        std::string json_error(const std::string& error)
        {
            return "{\"ok\":false,\"error\":" + json_string(error) + "}";
        }

        std::string json_vector3(const math::Vector3& value)
        {
            return "[" + std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "]";
        }

        std::string json_vector2(const math::Vector2& value)
        {
            return "[" + std::to_string(value.x) + "," + std::to_string(value.y) + "]";
        }

        std::string json_quaternion(const math::Quaternion& value)
        {
            return "[" + std::to_string(value.x) + "," + std::to_string(value.y) + "," + std::to_string(value.z) + "," + std::to_string(value.w) + "]";
        }

        std::string json_color(const Color& value)
        {
            return "[" + std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b) + "," + std::to_string(value.a) + "]";
        }

        std::string json_matrix(const math::Matrix& value)
        {
            return "["
                + std::to_string(value.m00) + "," + std::to_string(value.m01) + "," + std::to_string(value.m02) + "," + std::to_string(value.m03) + ","
                + std::to_string(value.m10) + "," + std::to_string(value.m11) + "," + std::to_string(value.m12) + "," + std::to_string(value.m13) + ","
                + std::to_string(value.m20) + "," + std::to_string(value.m21) + "," + std::to_string(value.m22) + "," + std::to_string(value.m23) + ","
                + std::to_string(value.m30) + "," + std::to_string(value.m31) + "," + std::to_string(value.m32) + "," + std::to_string(value.m33) + "]";
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

        bool parse_int32(const std::string& value, int32_t& result)
        {
            char* end = nullptr;
            const long parsed = std::strtol(value.c_str(), &end, 10);
            if (end == value.c_str() || *end != '\0' || parsed < std::numeric_limits<int32_t>::min() || parsed > std::numeric_limits<int32_t>::max())
            {
                return false;
            }

            result = static_cast<int32_t>(parsed);
            return true;
        }

        bool parse_uint32(const std::string& value, uint32_t& result)
        {
            uint64_t parsed = 0;
            if (!parse_uint64(value, parsed) || parsed > std::numeric_limits<uint32_t>::max())
            {
                return false;
            }

            result = static_cast<uint32_t>(parsed);
            return true;
        }

        bool parse_vector2(const std::string& value, math::Vector2& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 2))
            {
                return false;
            }

            result = math::Vector2(values[0], values[1]);
            return result.IsFinite();
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

        bool parse_matrix(const std::string& value, math::Matrix& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 16))
            {
                return false;
            }

            result = math::Matrix(values.data());
            return true;
        }

        bool parse_bounding_box(const std::string& value, math::BoundingBox& result)
        {
            std::vector<float> values;
            if (!parse_float_list(value, values, 6))
            {
                return false;
            }

            const math::Vector3 min(values[0], values[1], values[2]);
            const math::Vector3 max(values[3], values[4], values[5]);
            if (!min.IsFinite() || !max.IsFinite())
            {
                return false;
            }

            result = math::BoundingBox(min, max);
            return true;
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

        std::string normalize_screenshot_path(const std::optional<std::string>& path)
        {
            std::filesystem::path file_path;
            if (path && !path->empty())
            {
                file_path = std::filesystem::path(*path);
                if (file_path.extension().empty())
                {
                    file_path.replace_extension(".png");
                }
            }
            else
            {
                file_path = std::filesystem::path("screenshots") / ("mcp_screenshot_" + std::to_string(Renderer::GetFrameNumber()) + ".png");
            }

            if (file_path.is_relative())
            {
                file_path = std::filesystem::absolute(file_path);
            }

            return file_path.generic_string();
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

        std::string camel_to_snake(const std::string& value)
        {
            std::string result;
            for (size_t i = 0; i < value.size(); i++)
            {
                const unsigned char c = static_cast<unsigned char>(value[i]);
                if (std::isupper(c))
                {
                    if (i != 0 && !result.empty() && result.back() != '_' && (std::islower(static_cast<unsigned char>(value[i - 1])) || std::isdigit(static_cast<unsigned char>(value[i - 1]))))
                    {
                        result.push_back('_');
                    }
                    result.push_back(static_cast<char>(std::tolower(c)));
                }
                else
                {
                    result.push_back(static_cast<char>(c));
                }
            }

            return result;
        }

        std::string attribute_property_name(const Attribute& attribute)
        {
            if (attribute.name.rfind("m_", 0) == 0)
            {
                return attribute.name.substr(2);
            }
            if (attribute.name.rfind("Get", 0) == 0 && attribute.name.size() > 3)
            {
                return camel_to_snake(attribute.name.substr(3));
            }

            return camel_to_snake(attribute.name);
        }

        bool attribute_matches_property(const Attribute& attribute, const std::string& property)
        {
            const std::string query = to_lower_copy(property);
            return query == to_lower_copy(attribute.name) || query == to_lower_copy(attribute_property_name(attribute));
        }

        std::string projection_type_to_name(ProjectionType type)
        {
            return type == Projection_Orthographic ? "orthographic" : "perspective";
        }

        std::optional<ProjectionType> projection_type_from_name(const std::string& name)
        {
            if (name == "perspective" || name == "0")
            {
                return Projection_Perspective;
            }
            if (name == "orthographic" || name == "1")
            {
                return Projection_Orthographic;
            }

            return std::nullopt;
        }

        std::string spline_profile_to_name(SplineProfile profile)
        {
            switch (profile)
            {
            case SplineProfile::Road:
                return "road";
            case SplineProfile::Wall:
                return "wall";
            case SplineProfile::Tube:
                return "tube";
            case SplineProfile::Fence:
                return "fence";
            case SplineProfile::Channel:
                return "channel";
            default:
                return "unknown";
            }
        }

        std::optional<SplineProfile> spline_profile_from_name(const std::string& name)
        {
            if (name == "road" || name == "0")
            {
                return SplineProfile::Road;
            }
            if (name == "wall" || name == "1")
            {
                return SplineProfile::Wall;
            }
            if (name == "tube" || name == "2")
            {
                return SplineProfile::Tube;
            }
            if (name == "fence" || name == "3")
            {
                return SplineProfile::Fence;
            }
            if (name == "channel" || name == "4")
            {
                return SplineProfile::Channel;
            }

            return std::nullopt;
        }

        std::string spline_attach_mode_to_name(SplineAttachMode mode)
        {
            switch (mode)
            {
            case SplineAttachMode::None:
                return "none";
            case SplineAttachMode::Centerline:
                return "centerline";
            case SplineAttachMode::LeftEdge:
                return "left_edge";
            case SplineAttachMode::RightEdge:
                return "right_edge";
            case SplineAttachMode::LeftOuter:
                return "left_outer";
            case SplineAttachMode::RightOuter:
                return "right_outer";
            default:
                return "unknown";
            }
        }

        std::optional<SplineAttachMode> spline_attach_mode_from_name(const std::string& name)
        {
            if (name == "none" || name == "0")
            {
                return SplineAttachMode::None;
            }
            if (name == "centerline" || name == "1")
            {
                return SplineAttachMode::Centerline;
            }
            if (name == "left_edge" || name == "2")
            {
                return SplineAttachMode::LeftEdge;
            }
            if (name == "right_edge" || name == "3")
            {
                return SplineAttachMode::RightEdge;
            }
            if (name == "left_outer" || name == "4")
            {
                return SplineAttachMode::LeftOuter;
            }
            if (name == "right_outer" || name == "5")
            {
                return SplineAttachMode::RightOuter;
            }

            return std::nullopt;
        }

        std::optional<ParticlePreset> particle_preset_from_name(const std::string& name)
        {
            if (name == "custom" || name == "0")
            {
                return ParticlePreset::Custom;
            }
            if (name == "fire" || name == "1")
            {
                return ParticlePreset::Fire;
            }
            if (name == "smoke" || name == "2")
            {
                return ParticlePreset::Smoke;
            }
            if (name == "steam" || name == "3")
            {
                return ParticlePreset::Steam;
            }
            if (name == "sparks" || name == "4")
            {
                return ParticlePreset::Sparks;
            }
            if (name == "dust" || name == "5")
            {
                return ParticlePreset::Dust;
            }
            if (name == "snow" || name == "6")
            {
                return ParticlePreset::Snow;
            }
            if (name == "rain" || name == "7")
            {
                return ParticlePreset::Rain;
            }
            if (name == "confetti" || name == "8")
            {
                return ParticlePreset::Confetti;
            }
            if (name == "fireflies" || name == "9")
            {
                return ParticlePreset::Fireflies;
            }
            if (name == "blood" || name == "10")
            {
                return ParticlePreset::Blood;
            }
            if (name == "magic" || name == "11")
            {
                return ParticlePreset::Magic;
            }
            if (name == "explosion" || name == "12")
            {
                return ParticlePreset::Explosion;
            }
            if (name == "waterfall" || name == "13")
            {
                return ParticlePreset::Waterfall;
            }
            if (name == "embers" || name == "14")
            {
                return ParticlePreset::Embers;
            }
            if (name == "tire_smoke" || name == "tiresmoke" || name == "15")
            {
                return ParticlePreset::TireSmoke;
            }
            if (name == "exhaust" || name == "16")
            {
                return ParticlePreset::Exhaust;
            }

            return std::nullopt;
        }

        std::optional<PhysicsForce> physics_force_from_name(const std::string& name)
        {
            if (name == "constant" || name == "force" || name == "0")
            {
                return PhysicsForce::Constant;
            }
            if (name == "impulse" || name == "1")
            {
                return PhysicsForce::Impulse;
            }

            return std::nullopt;
        }

        std::string resource_type_to_name(ResourceType type)
        {
            switch (type)
            {
            case ResourceType::Texture:
                return "texture";
            case ResourceType::Audio:
                return "audio";
            case ResourceType::Material:
                return "material";
            case ResourceType::Mesh:
                return "mesh";
            case ResourceType::Cubemap:
                return "cubemap";
            case ResourceType::Animation:
                return "animation";
            case ResourceType::Font:
                return "font";
            case ResourceType::Shader:
                return "shader";
            case ResourceType::Unknown:
                return "unknown";
            default:
                return "all";
            }
        }

        std::optional<ResourceType> resource_type_from_name(const std::string& name)
        {
            if (name == "all" || name == "max" || name.empty())
            {
                return ResourceType::Max;
            }
            if (name == "texture")
            {
                return ResourceType::Texture;
            }
            if (name == "audio")
            {
                return ResourceType::Audio;
            }
            if (name == "material")
            {
                return ResourceType::Material;
            }
            if (name == "mesh")
            {
                return ResourceType::Mesh;
            }
            if (name == "cubemap")
            {
                return ResourceType::Cubemap;
            }
            if (name == "animation")
            {
                return ResourceType::Animation;
            }
            if (name == "font")
            {
                return ResourceType::Font;
            }
            if (name == "shader")
            {
                return ResourceType::Shader;
            }
            if (name == "unknown")
            {
                return ResourceType::Unknown;
            }

            return std::nullopt;
        }

        std::string material_texture_type_to_name(MaterialTextureType type)
        {
            switch (type)
            {
            case MaterialTextureType::Color:
                return "color";
            case MaterialTextureType::Roughness:
                return "roughness";
            case MaterialTextureType::Metalness:
                return "metalness";
            case MaterialTextureType::Normal:
                return "normal";
            case MaterialTextureType::Occlusion:
                return "occlusion";
            case MaterialTextureType::Emission:
                return "emission";
            case MaterialTextureType::Height:
                return "height";
            case MaterialTextureType::AlphaMask:
                return "alpha_mask";
            case MaterialTextureType::Packed:
                return "packed";
            default:
                return "unknown";
            }
        }

        std::optional<MaterialTextureType> material_texture_type_from_name(const std::string& name)
        {
            if (name == "color" || name == "albedo" || name == "base_color")
            {
                return MaterialTextureType::Color;
            }
            if (name == "roughness")
            {
                return MaterialTextureType::Roughness;
            }
            if (name == "metalness" || name == "metallic")
            {
                return MaterialTextureType::Metalness;
            }
            if (name == "normal")
            {
                return MaterialTextureType::Normal;
            }
            if (name == "occlusion" || name == "ao")
            {
                return MaterialTextureType::Occlusion;
            }
            if (name == "emission" || name == "emissive")
            {
                return MaterialTextureType::Emission;
            }
            if (name == "height")
            {
                return MaterialTextureType::Height;
            }
            if (name == "alpha_mask" || name == "alpha")
            {
                return MaterialTextureType::AlphaMask;
            }
            if (name == "packed")
            {
                return MaterialTextureType::Packed;
            }

            return std::nullopt;
        }

        std::string material_property_to_name(MaterialProperty property)
        {
            switch (property)
            {
            case MaterialProperty::Gltf:
                return "gltf";
            case MaterialProperty::WorldHeight:
                return "world_space_height";
            case MaterialProperty::WorldWidth:
                return "world_space_width";
            case MaterialProperty::WorldSpaceUv:
                return "world_space_uv";
            case MaterialProperty::Tessellation:
                return "tessellation";
            case MaterialProperty::ColorR:
                return "color_r";
            case MaterialProperty::ColorG:
                return "color_g";
            case MaterialProperty::ColorB:
                return "color_b";
            case MaterialProperty::ColorA:
                return "color_a";
            case MaterialProperty::Roughness:
                return "roughness";
            case MaterialProperty::Metalness:
                return "metalness";
            case MaterialProperty::Normal:
                return "normal";
            case MaterialProperty::Height:
                return "height";
            case MaterialProperty::Clearcoat:
                return "clearcoat";
            case MaterialProperty::Clearcoat_Roughness:
                return "clearcoat_roughness";
            case MaterialProperty::Anisotropic:
                return "anisotropic";
            case MaterialProperty::AnisotropicRotation:
                return "anisotropic_rotation";
            case MaterialProperty::Sheen:
                return "sheen";
            case MaterialProperty::SubsurfaceScattering:
                return "subsurface_scattering";
            case MaterialProperty::NormalFromAlbedo:
                return "normal_from_albedo";
            case MaterialProperty::EmissiveFromAlbedo:
                return "emissive_from_albedo";
            case MaterialProperty::TextureTilingX:
                return "texture_tiling_x";
            case MaterialProperty::TextureTilingY:
                return "texture_tiling_y";
            case MaterialProperty::TextureOffsetX:
                return "texture_offset_x";
            case MaterialProperty::TextureOffsetY:
                return "texture_offset_y";
            case MaterialProperty::TextureInvertX:
                return "texture_invert_x";
            case MaterialProperty::TextureInvertY:
                return "texture_invert_y";
            case MaterialProperty::TextureRotation:
                return "texture_rotation";
            case MaterialProperty::IsTerrain:
                return "texture_slope_based";
            case MaterialProperty::IsGrassBlade:
                return "is_grass_blade";
            case MaterialProperty::IsFlower:
                return "is_flower";
            case MaterialProperty::WindAnimation:
                return "wind_animation";
            case MaterialProperty::ColorVariationFromInstance:
                return "color_variation_from_instance";
            case MaterialProperty::IsWater:
                return "vertex_animate_water";
            case MaterialProperty::CullMode:
                return "cull_mode";
            default:
                return "unknown";
            }
        }

        std::optional<MaterialProperty> material_property_from_name(const std::string& name)
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); i++)
            {
                const MaterialProperty property = static_cast<MaterialProperty>(i);
                if (name == material_property_to_name(property))
                {
                    return property;
                }
            }

            if (name == "world_height")
            {
                return MaterialProperty::WorldHeight;
            }
            if (name == "world_width")
            {
                return MaterialProperty::WorldWidth;
            }
            if (name == "world_uv")
            {
                return MaterialProperty::WorldSpaceUv;
            }
            if (name == "base_color_r")
            {
                return MaterialProperty::ColorR;
            }
            if (name == "base_color_g")
            {
                return MaterialProperty::ColorG;
            }
            if (name == "base_color_b")
            {
                return MaterialProperty::ColorB;
            }
            if (name == "base_color_a")
            {
                return MaterialProperty::ColorA;
            }

            return std::nullopt;
        }

        IResource* get_resource_by_name_or_path(const std::string& name_or_path, ResourceType type)
        {
            std::lock_guard<std::recursive_mutex> guard(ResourceCache::GetMutex());
            for (std::shared_ptr<IResource>& resource : ResourceCache::GetResources())
            {
                if (!resource || (type != ResourceType::Max && resource->GetResourceType() != type))
                {
                    continue;
                }

                if (resource->GetObjectName() == name_or_path || resource->GetResourceFilePath() == name_or_path)
                {
                    return resource.get();
                }
            }

            return nullptr;
        }

        std::shared_ptr<IResource> get_resource_shared_by_name_or_path(const std::string& name_or_path, ResourceType type)
        {
            std::lock_guard<std::recursive_mutex> guard(ResourceCache::GetMutex());
            for (std::shared_ptr<IResource>& resource : ResourceCache::GetResources())
            {
                if (!resource || (type != ResourceType::Max && resource->GetResourceType() != type))
                {
                    continue;
                }

                if (resource->GetObjectName() == name_or_path || resource->GetResourceFilePath() == name_or_path)
                {
                    return resource;
                }
            }

            return nullptr;
        }

        std::optional<std::string> renderer_debug_cvar_from_name(const std::string& name)
        {
            if (name == "aabb")
            {
                return "r.aabb";
            }
            if (name == "picking_ray")
            {
                return "r.picking_ray";
            }
            if (name == "grid")
            {
                return "r.grid";
            }
            if (name == "transform_handle")
            {
                return "r.transform_handle";
            }
            if (name == "selection_outline")
            {
                return "r.selection_outline";
            }
            if (name == "lights")
            {
                return "r.lights";
            }
            if (name == "audio_sources")
            {
                return "r.audio_sources";
            }
            if (name == "performance_metrics")
            {
                return "r.performance_metrics";
            }
            if (name == "physics")
            {
                return "r.physics";
            }
            if (name == "wireframe")
            {
                return "r.wireframe";
            }
            if (name == "meshlet_visualize")
            {
                return "r.meshlet_visualize";
            }
            if (name == "cluster_visualize")
            {
                return "r.cluster_visualize";
            }

            return std::nullopt;
        }

        std::string renderer_debug_options_json()
        {
            return "[\"aabb\",\"picking_ray\",\"grid\",\"transform_handle\",\"selection_outline\",\"lights\",\"audio_sources\",\"performance_metrics\",\"physics\",\"wireframe\",\"meshlet_visualize\",\"cluster_visualize\"]";
        }

        Material* get_material_from_request(const McpRequest& request, std::string& error)
        {
            const std::optional<std::string> name = get_argument(request, "name");
            const std::optional<std::string> path = get_argument(request, "path");
            const std::string key = name ? *name : (path ? *path : "");
            if (key.empty())
            {
                error = "missing material name or path";
                return nullptr;
            }

            Material* material = static_cast<Material*>(get_resource_by_name_or_path(key, ResourceType::Material));
            if (material == nullptr)
            {
                error = "material not found";
            }

            return material;
        }

        std::string resource_to_json(IResource* resource)
        {
            if (resource == nullptr)
            {
                return "null";
            }

            std::string json = "{";
            json += "\"id\":" + json_string(std::to_string(resource->GetObjectId()));
            json += ",\"name\":" + json_string(resource->GetObjectName());
            json += ",\"type\":" + json_string(resource_type_to_name(resource->GetResourceType()));
            json += ",\"path\":" + json_string(resource->GetResourceFilePath());
            json += ",\"state\":" + std::to_string(static_cast<uint32_t>(resource->GetResourceState()));
            json += ",\"flags\":" + std::to_string(resource->GetFlags());
            json += "}";
            return json;
        }

        std::string material_to_json(Material* material)
        {
            if (material == nullptr)
            {
                return "null";
            }

            std::string json = "{";
            json += "\"resource\":" + resource_to_json(material);
            json += ",\"properties\":{";
            bool first_property = true;
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); i++)
            {
                const MaterialProperty property = static_cast<MaterialProperty>(i);
                const std::string name = material_property_to_name(property);
                if (name == "unknown")
                {
                    continue;
                }

                if (!first_property)
                {
                    json += ",";
                }
                first_property = false;
                json += json_string(name) + ":" + std::to_string(material->GetProperty(property));
            }
            json += "}";

            json += ",\"textures\":{";
            bool first_texture = true;
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialTextureType::Max); i++)
            {
                const MaterialTextureType texture_type = static_cast<MaterialTextureType>(i);
                if (!first_texture)
                {
                    json += ",";
                }
                first_texture = false;
                json += json_string(material_texture_type_to_name(texture_type)) + ":[";
                for (uint32_t slot = 0; slot < Material::slots_per_texture; slot++)
                {
                    if (slot != 0)
                    {
                        json += ",";
                    }
                    json += json_string(material->GetTexturePathByType(texture_type, static_cast<uint8_t>(slot)));
                }
                json += "]";
            }
            json += "}}";
            return json;
        }

        struct ComponentMetadata
        {
            std::string property;
            std::string member;
            std::string type;
            bool writable = true;
            std::string unit;
            std::string range;
            std::string enum_values;
            std::vector<std::string> side_effects;
            std::string read_only_reason;
            std::string recommended_default;
            std::string note;
        };

        std::string range_json(std::optional<float> min, std::optional<float> max)
        {
            std::string json = "{";
            bool first = true;
            if (min)
            {
                json += "\"min\":" + std::to_string(*min);
                first = false;
            }
            if (max)
            {
                if (!first)
                {
                    json += ",";
                }
                json += "\"max\":" + std::to_string(*max);
            }
            json += "}";
            return json;
        }

        std::string enum_values_json(std::initializer_list<std::pair<std::string, std::string>> values)
        {
            std::string json = "[";
            size_t index = 0;
            for (const std::pair<std::string, std::string>& value : values)
            {
                if (index != 0)
                {
                    json += ",";
                }
                json += "{\"name\":" + json_string(value.first) + ",\"value\":" + value.second + "}";
                index++;
            }
            json += "]";
            return json;
        }

        std::string component_metadata_to_json(const ComponentMetadata& metadata)
        {
            std::string json = "{";
            json += "\"property\":" + json_string(metadata.property);
            if (!metadata.member.empty())
            {
                json += ",\"member\":" + json_string(metadata.member);
            }
            json += ",\"type\":" + json_string(metadata.type);
            json += ",\"writable\":" + json_bool(metadata.writable);
            if (!metadata.unit.empty())
            {
                json += ",\"unit\":" + json_string(metadata.unit);
            }
            if (!metadata.range.empty())
            {
                json += ",\"range\":" + metadata.range;
            }
            if (!metadata.enum_values.empty())
            {
                json += ",\"enum_values\":" + metadata.enum_values;
            }
            if (!metadata.side_effects.empty())
            {
                json += ",\"side_effects\":" + json_string_array(metadata.side_effects);
            }
            if (!metadata.read_only_reason.empty())
            {
                json += ",\"read_only_reason\":" + json_string(metadata.read_only_reason);
            }
            if (!metadata.recommended_default.empty())
            {
                json += ",\"recommended_default\":" + metadata.recommended_default;
            }
            if (!metadata.note.empty())
            {
                json += ",\"note\":" + json_string(metadata.note);
            }
            json += "}";
            return json;
        }

        std::string projection_enum_values_json()
        {
            return enum_values_json({ { "perspective", json_string("perspective") }, { "orthographic", json_string("orthographic") } });
        }

        std::string body_type_enum_values_json()
        {
            return enum_values_json({
                { "box", json_string("box") },
                { "sphere", json_string("sphere") },
                { "plane", json_string("plane") },
                { "capsule", json_string("capsule") },
                { "mesh", json_string("mesh") },
                { "mesh_convex", json_string("mesh_convex") },
                { "controller", json_string("controller") },
                { "vehicle", json_string("vehicle") },
                { "cloth", json_string("cloth") }
            });
        }

        std::string light_type_enum_values_json()
        {
            return enum_values_json({
                { "directional", json_string("directional") },
                { "point", json_string("point") },
                { "spot", json_string("spot") },
                { "area", json_string("area") }
            });
        }

        std::string spline_profile_enum_values_json()
        {
            return enum_values_json({
                { "road", json_string("road") },
                { "wall", json_string("wall") },
                { "tube", json_string("tube") },
                { "fence", json_string("fence") },
                { "channel", json_string("channel") }
            });
        }

        std::string spline_attach_mode_enum_values_json()
        {
            return enum_values_json({
                { "none", json_string("none") },
                { "centerline", json_string("centerline") },
                { "left_edge", json_string("left_edge") },
                { "right_edge", json_string("right_edge") },
                { "left_outer", json_string("left_outer") },
                { "right_outer", json_string("right_outer") }
            });
        }

        std::string particle_preset_enum_values_json()
        {
            return enum_values_json({
                { "custom", "0" },
                { "fire", "1" },
                { "smoke", "2" },
                { "steam", "3" },
                { "sparks", "4" },
                { "dust", "5" },
                { "snow", "6" },
                { "rain", "7" },
                { "confetti", "8" },
                { "fireflies", "9" },
                { "blood", "10" },
                { "magic", "11" },
                { "explosion", "12" },
                { "waterfall", "13" },
                { "embers", "14" },
                { "tire_smoke", "15" },
                { "exhaust", "16" }
            });
        }

        std::string particle_blend_mode_enum_values_json()
        {
            return enum_values_json({ { "alpha", "0" }, { "premultiplied", "1" }, { "additive", "2" } });
        }

        std::string particle_lighting_mode_enum_values_json()
        {
            return enum_values_json({ { "lit", "0" }, { "unlit", "1" }, { "emissive", "2" } });
        }

        std::string attribute_value_to_json(const std::any& value, const std::string& type_name)
        {
            const std::type_info& type = value.type();

            if (type == typeid(bool))
            {
                return json_bool(std::any_cast<bool>(value));
            }
            if (type == typeid(float))
            {
                return std::to_string(std::any_cast<float>(value));
            }
            if (type == typeid(double))
            {
                return std::to_string(std::any_cast<double>(value));
            }
            if (type == typeid(int32_t))
            {
                return std::to_string(std::any_cast<int32_t>(value));
            }
            if (type == typeid(uint32_t))
            {
                return std::to_string(std::any_cast<uint32_t>(value));
            }
            if (type == typeid(uint64_t))
            {
                return std::to_string(std::any_cast<uint64_t>(value));
            }
            if (type == typeid(std::string))
            {
                return json_string(std::any_cast<std::string>(value));
            }
            if (type == typeid(math::Vector2))
            {
                return json_vector2(std::any_cast<math::Vector2>(value));
            }
            if (type == typeid(math::Vector3))
            {
                return json_vector3(std::any_cast<math::Vector3>(value));
            }
            if (type == typeid(math::Quaternion))
            {
                return json_quaternion(std::any_cast<math::Quaternion>(value));
            }
            if (type == typeid(Color))
            {
                return json_color(std::any_cast<Color>(value));
            }
            if (type == typeid(math::BoundingBox))
            {
                return json_bounding_box(std::any_cast<math::BoundingBox>(value));
            }
            if (type == typeid(math::Matrix))
            {
                return json_matrix(std::any_cast<math::Matrix>(value));
            }
            if (type == typeid(ProjectionType))
            {
                return json_string(projection_type_to_name(std::any_cast<ProjectionType>(value)));
            }
            if (type == typeid(BodyType))
            {
                return json_string(body_type_to_name(std::any_cast<BodyType>(value)));
            }
            if (type == typeid(LightType))
            {
                return json_string(light_type_to_name(std::any_cast<LightType>(value)));
            }
            if (type == typeid(SplineProfile))
            {
                return json_string(spline_profile_to_name(std::any_cast<SplineProfile>(value)));
            }
            if (type == typeid(SplineAttachMode))
            {
                return json_string(spline_attach_mode_to_name(std::any_cast<SplineAttachMode>(value)));
            }
            if (type == typeid(ParticlePreset))
            {
                return std::to_string(static_cast<uint32_t>(std::any_cast<ParticlePreset>(value)));
            }
            if (type == typeid(ParticleBlendMode))
            {
                return std::to_string(static_cast<uint32_t>(std::any_cast<ParticleBlendMode>(value)));
            }
            if (type == typeid(ParticleLightingMode))
            {
                return std::to_string(static_cast<uint32_t>(std::any_cast<ParticleLightingMode>(value)));
            }
            if (type == typeid(Material*))
            {
                Material* material = std::any_cast<Material*>(value);
                return material ? json_string(material->GetObjectName()) : "null";
            }
            if (type == typeid(Mesh*))
            {
                Mesh* mesh = std::any_cast<Mesh*>(value);
                return mesh ? json_string(mesh->GetObjectName()) : "null";
            }
            if (type == typeid(std::vector<Instance>))
            {
                const std::vector<Instance>& instances = std::any_cast<const std::vector<Instance>&>(value);
                return "{\"count\":" + std::to_string(instances.size()) + "}";
            }

            return "{\"unsupported_type\":" + json_string(type_name) + "}";
        }

        bool attribute_type_is_writable(const std::any& value)
        {
            const std::type_info& type = value.type();
            return type == typeid(bool) ||
                type == typeid(float) ||
                type == typeid(double) ||
                type == typeid(int32_t) ||
                type == typeid(uint32_t) ||
                type == typeid(uint64_t) ||
                type == typeid(std::string) ||
                type == typeid(math::Vector2) ||
                type == typeid(math::Vector3) ||
                type == typeid(math::Quaternion) ||
                type == typeid(Color) ||
                type == typeid(math::BoundingBox) ||
                type == typeid(math::Matrix) ||
                type == typeid(ProjectionType) ||
                type == typeid(BodyType) ||
                type == typeid(LightType) ||
                type == typeid(SplineProfile) ||
                type == typeid(SplineAttachMode) ||
                type == typeid(ParticlePreset) ||
                type == typeid(ParticleBlendMode) ||
                type == typeid(ParticleLightingMode);
        }

        bool parse_attribute_value(const Attribute& attribute, const std::string& value, std::any& parsed, std::string& error)
        {
            const std::type_info& type = attribute.getter().type();

            if (type == typeid(bool))
            {
                bool result = false;
                if (!parse_bool(value, result))
                {
                    error = "invalid bool";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(float))
            {
                float result = 0.0f;
                if (!parse_float(value, result))
                {
                    error = "invalid float";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(double))
            {
                float result = 0.0f;
                if (!parse_float(value, result))
                {
                    error = "invalid double";
                    return false;
                }
                parsed = static_cast<double>(result);
                return true;
            }
            if (type == typeid(int32_t))
            {
                int32_t result = 0;
                if (!parse_int32(value, result))
                {
                    error = "invalid int32";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(uint32_t))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result))
                {
                    error = "invalid uint32";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(uint64_t))
            {
                uint64_t result = 0;
                if (!parse_uint64(value, result))
                {
                    error = "invalid uint64";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(std::string))
            {
                parsed = value;
                return true;
            }
            if (type == typeid(math::Vector2))
            {
                math::Vector2 result;
                if (!parse_vector2(value, result))
                {
                    error = "invalid vector2";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::Vector3))
            {
                math::Vector3 result;
                if (!parse_vector3(value, result))
                {
                    error = "invalid vector3";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::Quaternion))
            {
                math::Quaternion result;
                if (!parse_quaternion(value, result))
                {
                    error = "invalid quaternion";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(Color))
            {
                Color result;
                if (!parse_color(value, result))
                {
                    error = "invalid color";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::Matrix))
            {
                math::Matrix result;
                if (!parse_matrix(value, result))
                {
                    error = "invalid matrix";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(math::BoundingBox))
            {
                math::BoundingBox result;
                if (!parse_bounding_box(value, result))
                {
                    error = "invalid bounding_box";
                    return false;
                }
                parsed = result;
                return true;
            }
            if (type == typeid(ProjectionType))
            {
                const std::optional<ProjectionType> result = projection_type_from_name(value);
                if (!result)
                {
                    error = "invalid projection";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(BodyType))
            {
                const std::optional<BodyType> result = body_type_from_name(value);
                if (!result)
                {
                    error = "invalid body_type";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(LightType))
            {
                const std::optional<LightType> result = light_type_from_name(value);
                if (!result)
                {
                    error = "invalid light_type";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(SplineProfile))
            {
                const std::optional<SplineProfile> result = spline_profile_from_name(value);
                if (!result)
                {
                    error = "invalid spline_profile";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(SplineAttachMode))
            {
                const std::optional<SplineAttachMode> result = spline_attach_mode_from_name(value);
                if (!result)
                {
                    error = "invalid spline_attach_mode";
                    return false;
                }
                parsed = *result;
                return true;
            }
            if (type == typeid(ParticlePreset))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result) || result >= static_cast<uint32_t>(ParticlePreset::Count))
                {
                    error = "invalid particle_preset";
                    return false;
                }
                parsed = static_cast<ParticlePreset>(result);
                return true;
            }
            if (type == typeid(ParticleBlendMode))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result) || result >= static_cast<uint32_t>(ParticleBlendMode::Count))
                {
                    error = "invalid particle_blend_mode";
                    return false;
                }
                parsed = static_cast<ParticleBlendMode>(result);
                return true;
            }
            if (type == typeid(ParticleLightingMode))
            {
                uint32_t result = 0;
                if (!parse_uint32(value, result) || result >= static_cast<uint32_t>(ParticleLightingMode::Count))
                {
                    error = "invalid particle_lighting_mode";
                    return false;
                }
                parsed = static_cast<ParticleLightingMode>(result);
                return true;
            }

            error = "member is read-only through MCP";
            return false;
        }

        std::string component_member_names_json(Component* component)
        {
            std::string json = "[";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(attribute_property_name(attribute));
            }

            json += "]";
            return json;
        }

        std::string component_members_to_json(Component* component)
        {
            std::string json = "[";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                const std::any value = attribute.getter();
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += "{";
                json += "\"property\":" + json_string(attribute_property_name(attribute));
                json += ",\"member\":" + json_string(attribute.name);
                json += ",\"type\":" + json_string(attribute.type);
                json += ",\"writable\":" + json_bool(attribute_type_is_writable(value));
                json += ",\"value\":" + attribute_value_to_json(value, attribute.type);
                json += "}";
            }

            json += "]";
            return json;
        }

        void apply_common_member_metadata(ComponentMetadata& metadata)
        {
            const std::string property = metadata.property;
            if (property == "projection_type")
            {
                metadata.type = "enum";
                metadata.enum_values = projection_enum_values_json();
            }
            else if (property == "body_type")
            {
                metadata.type = "enum";
                metadata.enum_values = body_type_enum_values_json();
                metadata.side_effects.emplace_back("recreates physics body and shapes");
            }
            else if (property == "light_type")
            {
                metadata.type = "enum";
                metadata.enum_values = light_type_enum_values_json();
                metadata.side_effects.emplace_back("resets sensible range and shadow mode compatibility");
            }
            else if (property == "profile")
            {
                metadata.type = "enum";
                metadata.enum_values = spline_profile_enum_values_json();
                metadata.side_effects.emplace_back("changes generated spline cross section");
            }
            else if (property == "attach_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = spline_attach_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes how the spline samples its source spline");
            }
            else if (property == "preset")
            {
                metadata.type = "enum";
                metadata.enum_values = particle_preset_enum_values_json();
                metadata.side_effects.emplace_back("overwrites multiple particle properties");
            }
            else if (property == "blend_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = particle_blend_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes particle material blending");
            }
            else if (property == "lighting_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = particle_lighting_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes particle lighting path");
            }

            if (property.find("path") != std::string::npos || property.find("mesh") != std::string::npos || property.find("material") != std::string::npos)
            {
                if (metadata.unit.empty())
                {
                    metadata.unit = "path or resource name";
                }
            }
            if (property.find("distance") != std::string::npos || property.find("width") != std::string::npos || property.find("height") != std::string::npos || property.find("radius") != std::string::npos || property.find("offset") != std::string::npos)
            {
                if (metadata.unit.empty())
                {
                    metadata.unit = "meters";
                }
                if (metadata.range.empty())
                {
                    metadata.range = range_json(0.0f, std::nullopt);
                }
            }
            if (property.find("angle") != std::string::npos || property.find("yaw") != std::string::npos)
            {
                if (metadata.unit.empty())
                {
                    metadata.unit = "radians";
                }
            }
            if (property.find("fps") != std::string::npos)
            {
                metadata.unit = "frames per second";
                metadata.range = range_json(0.0f, std::nullopt);
            }
            if (property.find("rate") != std::string::npos)
            {
                metadata.unit = "per second";
                metadata.range = range_json(0.0f, std::nullopt);
            }
            if (property.find("count") != std::string::npos || property.find("resolution") != std::string::npos || property.find("segments") != std::string::npos || property.find("iterations") != std::string::npos)
            {
                metadata.range = range_json(0.0f, std::nullopt);
            }
            if (property.find("opacity") != std::string::npos || property.find("wet") != std::string::npos || property.find("blend") != std::string::npos || property.find("influence") != std::string::npos || property.find("stiffness") != std::string::npos || property.find("damping") != std::string::npos)
            {
                metadata.unit = "normalized";
                metadata.range = range_json(0.0f, 1.0f);
            }
            if (property == "mass")
            {
                metadata.unit = "kilograms";
                metadata.range = range_json(0.0f, std::nullopt);
                metadata.side_effects.emplace_back("updates rigid body mass");
            }
            if (property == "friction" || property == "friction_rolling" || property == "restitution")
            {
                metadata.unit = "coefficient";
                metadata.range = property == "restitution" ? range_json(0.0f, 1.0f) : range_json(0.0f, std::nullopt);
                metadata.side_effects.emplace_back("updates physics material");
            }
            if (property == "bounding_box" || property == "bounding_box_mesh" || property == "distance_squared" || property == "is_visible" || property == "lod_index" || property == "previous_lights" || property == "area_km2" || property == "height_samples" || property == "vertex_count" || property == "index_count" || property == "triangle_count")
            {
                metadata.note = "derived runtime state, edits can be overwritten by the component";
            }
            if (property == "spawn_burst")
            {
                metadata.side_effects.emplace_back("emits a particle burst");
            }
            if (property == "needs_road_regeneration")
            {
                metadata.note = "internal dirty flag, prefer component_action generate_road_mesh";
            }
            if (property == "source_spline_entity_id" || property == "instance_template_id")
            {
                metadata.unit = "entity id";
            }
        }

        ComponentMetadata component_member_metadata(const Attribute& attribute)
        {
            const std::any value = attribute.getter();
            ComponentMetadata metadata;
            metadata.property = attribute_property_name(attribute);
            metadata.member   = attribute.name;
            metadata.type     = attribute.type;
            metadata.writable = attribute_type_is_writable(value);
            if (!metadata.writable)
            {
                metadata.read_only_reason = "unsupported member type for component_set";
            }

            apply_common_member_metadata(metadata);
            return metadata;
        }

        std::string component_member_metadata_json(Component* component)
        {
            std::string json = "[";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += component_metadata_to_json(component_member_metadata(attribute));
            }

            json += "]";
            return json;
        }

        std::string component_member_properties_to_json(Component* component)
        {
            std::string json = "{";
            bool first = true;

            for (const Attribute& attribute : component->GetAttributes())
            {
                const std::any value = attribute.getter();
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(attribute_property_name(attribute)) + ":" + attribute_value_to_json(value, attribute.type);
            }

            json += "}";
            return json;
        }

        bool set_component_member(Component* component, const std::string& property, const std::string& value, std::string& error)
        {
            for (const Attribute& attribute : component->GetAttributes())
            {
                if (!attribute_matches_property(attribute, property))
                {
                    continue;
                }

                std::any parsed;
                if (!parse_attribute_value(attribute, value, parsed, error))
                {
                    return false;
                }

                try
                {
                    attribute.setter(parsed);
                }
                catch (const std::bad_any_cast&)
                {
                    error = "member type mismatch";
                    return false;
                }

                return true;
            }

            error = "unknown component property";
            return false;
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

        std::string command_camera_snapshot()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr || camera->GetEntity() == nullptr)
            {
                return json_error("camera not found");
            }

            Entity* entity = camera->GetEntity();
            std::string json = "{\"ok\":true";
            json += ",\"entity_id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"entity_name\":" + json_string(entity->GetObjectName());
            json += ",\"position\":" + json_vector3(entity->GetPosition());
            json += ",\"forward\":" + json_vector3(entity->GetForward());
            json += ",\"right\":" + json_vector3(entity->GetRight());
            json += ",\"up\":" + json_vector3(entity->GetUp());
            json += "}";
            return json;
        }

        std::string command_screenshot_take(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::string path = normalize_screenshot_path(get_argument(request, "path"));
            std::string extension = std::filesystem::path(path).extension().generic_string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".png")
            {
                return json_error("screenshot path must be a .png file");
            }

            const bool accepted = Renderer::Screenshot(path);
            if (!accepted)
            {
                return json_error("screenshot already pending");
            }

            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(path);
            json += ",\"ready\":false";
            json += ",\"async\":true";
            json += ",\"note\":" + json_string("screenshot will be written after the next rendered frame");
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

        std::string command_world_raycast(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> origin_arg = get_argument(request, "origin");
            const std::optional<std::string> direction_arg = get_argument(request, "direction");
            if (!origin_arg || !direction_arg)
            {
                return json_error("missing origin or direction");
            }

            math::Vector3 origin;
            math::Vector3 direction;
            if (!parse_vector3(*origin_arg, origin))
            {
                return json_error("invalid origin");
            }
            if (!parse_vector3(*direction_arg, direction) || direction == math::Vector3::Zero)
            {
                return json_error("invalid direction");
            }

            float max_distance = 1000.0f;
            if (const std::optional<std::string> max_distance_arg = get_argument(request, "max_distance"))
            {
                if (!parse_float(*max_distance_arg, max_distance) || max_distance <= 0.0f)
                {
                    return json_error("invalid max_distance");
                }
            }

            math::Vector3 hit_position;
            Entity* hit_entity = nullptr;
            const bool hit = PhysicsWorld::RaycastStatic(origin, direction, max_distance, hit_position, hit_entity);

            std::string json = "{\"ok\":true";
            json += ",\"hit\":" + json_bool(hit);
            if (hit)
            {
                json += ",\"position\":" + json_vector3(hit_position);
                if (hit_entity != nullptr)
                {
                    json += ",\"entity_id\":" + json_string(std::to_string(hit_entity->GetObjectId()));
                    json += ",\"entity_name\":" + json_string(hit_entity->GetObjectName());
                }
            }
            json += "}";
            return json;
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
            else
            {
                return component_member_properties_to_json(component);
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

        std::string component_property_metadata_json(ComponentType type)
        {
            std::vector<ComponentMetadata> entries;
            const auto add = [&](ComponentMetadata metadata)
            {
                entries.emplace_back(std::move(metadata));
            };

            if (type == ComponentType::Render)
            {
                add({ "mesh", "", "string", true, "", "", "", { "loads or resolves render mesh", "updates render bounds and acceleration structure state" }, "", json_string("standard_cube") });
                add({ "material", "", "string", true, "", "", "", { "loads or resolves material resource", "changes rendered surface appearance" }, "", json_string("standard") });
                add({ "default_material", "", "bool", true, "", "", "", { "replaces the assigned material with the renderer default material" }, "", "false" });
                add({ "visible", "", "bool", true, "", "", "", {}, "", "true" });
                add({ "casts_shadows", "", "bool", true, "", "", "", { "affects shadow map participation" }, "", "true" });
                add({ "exclude_from_ray_tracing", "", "bool", true, "", "", "", { "affects blas and tlas participation" }, "", "false" });
                add({ "max_render_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects distance culling" }, "", "0" });
                add({ "max_shadow_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects shadow culling" }, "", "0" });
            }
            else if (type == ComponentType::Physics)
            {
                add({ "body_type", "", "enum", true, "", "", body_type_enum_values_json(), { "recreates physics body and shapes" }, "", json_string("box") });
                add({ "static", "", "bool", true, "", "", "", { "recreates or retags physics body" }, "", "false" });
                add({ "kinematic", "", "bool", true, "", "", "", { "changes simulation ownership of the body" }, "", "false" });
                add({ "enabled", "", "bool", true, "", "", "", { "enables or disables physics processing for the component" }, "", "true" });
                add({ "mass", "", "float", true, "kilograms", range_json(0.0f, std::nullopt), "", { "updates rigid body mass" }, "", "1" });
                add({ "friction", "", "float", true, "coefficient", range_json(0.0f, std::nullopt), "", { "updates physics material" }, "", "0.4" });
                add({ "friction_rolling", "", "float", true, "coefficient", range_json(0.0f, std::nullopt), "", { "updates rolling friction" }, "", "0.4" });
                add({ "restitution", "", "float", true, "coefficient", range_json(0.0f, 1.0f), "", { "updates physics material bounce" }, "", "0.2" });
                add({ "center_of_mass", "", "vector3", true, "meters local", "", "", { "changes rigid body center of mass" }, "", "[0,0,0]" });
                add({ "linear_velocity", "", "vector3", true, "meters per second", "", "", { "changes runtime rigid body velocity" }, "", "[0,0,0]" });
                add({ "angular_velocity", "", "vector3", true, "radians per second", "", "", { "changes runtime rigid body angular velocity" }, "", "[0,0,0]" });
            }
            else if (type == ComponentType::Light)
            {
                add({ "light_type", "", "enum", true, "", "", light_type_enum_values_json(), { "resets sensible range and shadow mode compatibility" }, "", json_string("point") });
                add({ "color", "", "color", true, "linear rgba", range_json(0.0f, std::nullopt), "", { "updates light color and marks renderer lighting data dirty" }, "", "[1,1,1,1]" });
                add({ "temperature", "", "float", true, "kelvin", range_json(1000.0f, 40000.0f), "", { "updates light color from blackbody temperature" }, "", "6500" });
                add({ "intensity", "", "float", true, "lux for directional, lumens otherwise", range_json(0.0f, std::nullopt), "", { "updates photometric and radiometric light intensity" }, "", "1000" });
                add({ "range", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects light culling and shadow coverage" }, "", "10" });
                add({ "angle_degrees", "", "float", true, "degrees", range_json(0.1f, 179.0f), "", { "affects spot light cone and shadow projection" }, "", "45" });
                add({ "area_width", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects area light emitter size" }, "", "1" });
                add({ "area_height", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects area light emitter size" }, "", "1" });
                add({ "shadows", "", "bool", true, "", "", "", { "allocates and renders shadow maps when enabled" }, "", "true" });
                add({ "volumetric", "", "bool", true, "", "", "", { "enables volumetric lighting contribution" }, "", "false" });
                add({ "draw_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects light icon and debug drawing visibility" }, "", "0" });
                add({ "shadow_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects shadow rendering distance" }, "", "0" });
                add({ "volumetric_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects volumetric contribution distance" }, "", "0" });
            }
            else if (type == ComponentType::Camera)
            {
                add({ "fov_degrees", "", "float", true, "degrees", range_json(1.0f, 179.0f), "", { "updates camera projection" }, "", "90" });
                add({ "aperture", "", "float", true, "f stop", range_json(0.01f, std::nullopt), "", { "changes exposure and depth of field behavior" }, "", "5.6" });
                add({ "shutter_speed", "", "float", true, "seconds", range_json(0.0001f, std::nullopt), "", { "changes exposure and motion blur behavior" }, "", "0.008" });
                add({ "iso", "", "float", true, "iso", range_json(1.0f, std::nullopt), "", { "changes exposure" }, "", "200" });
                add({ "projection", "", "enum", true, "", "", projection_enum_values_json(), { "updates camera projection" }, "", json_string("perspective") });
                add({ "controllable", "", "bool", true, "", "", "", { "enables editor fps camera controls" }, "", "true" });
                add({ "flashlight", "", "bool", true, "", "", "", { "creates or toggles transient camera flashlight entity" }, "", "false" });
            }
            else if (type == ComponentType::AudioSource)
            {
                add({ "clip", "", "string", true, "path or cached clip name", "", "", { "loads or resolves audio clip resource" }, "", json_string("") });
                add({ "mute", "", "bool", true, "", "", "", {}, "", "false" });
                add({ "play_on_start", "", "bool", true, "", "", "", { "changes behavior on component start" }, "", "false" });
                add({ "loop", "", "bool", true, "", "", "", { "changes playback looping behavior" }, "", "false" });
                add({ "is_3d", "", "bool", true, "", "", "", { "changes spatialization behavior" }, "", "true" });
                add({ "volume", "", "float", true, "linear gain", range_json(0.0f, 1.0f), "", { "updates playback gain" }, "", "1" });
                add({ "pitch", "", "float", true, "multiplier", range_json(0.0f, 4.0f), "", { "updates playback rate and pitch" }, "", "1" });
                add({ "reverb_enabled", "", "bool", true, "", "", "", { "enables reverb processing" }, "", "false" });
                add({ "reverb_room_size", "", "float", true, "normalized", range_json(0.0f, 1.0f), "", { "updates reverb parameters" }, "", "0.5" });
                add({ "reverb_decay", "", "float", true, "seconds", range_json(0.0f, std::nullopt), "", { "updates reverb parameters" }, "", "1" });
                add({ "reverb_wet", "", "float", true, "normalized", range_json(0.0f, 1.0f), "", { "updates reverb wet mix" }, "", "0.3" });
            }
            else if (type == ComponentType::Script)
            {
                add({ "file_path", "", "string", true, "path", "", "", { "changes script file loaded by the component" }, "", json_string("") });
            }

            std::string json = "[";
            for (size_t i = 0; i < entries.size(); i++)
            {
                if (i != 0)
                {
                    json += ",";
                }
                json += component_metadata_to_json(entries[i]);
            }
            json += "]";
            return json;
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
            json += ",\"editable_members\":" + component_member_names_json(component);
            json += ",\"property_metadata\":" + component_property_metadata_json(*type);
            json += ",\"member_metadata\":" + component_member_metadata_json(component);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
            json += "}}";
            return json;
        }

        void append_render_material_snapshot(std::string& json, Entity* entity, bool include_descendants, bool& first)
        {
            if (entity == nullptr)
            {
                return;
            }

            if (Render* renderable = entity->GetComponent<Render>())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;

                Entity* parent = entity->GetParent();
                json += "{";
                json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
                json += ",\"name\":" + json_string(entity->GetObjectName());
                json += ",\"parent_id\":";
                json += parent ? json_string(std::to_string(parent->GetObjectId())) : "null";
                json += ",\"mesh\":" + json_string(renderable->GetMeshName());
                json += ",\"material\":" + json_string(renderable->GetMaterialName());
                json += ",\"default_material\":" + json_bool(renderable->IsUsingDefaultMaterial());
                json += "}";
            }

            if (!include_descendants)
            {
                return;
            }

            for (Entity* child : entity->GetChildren())
            {
                append_render_material_snapshot(json, child, include_descendants, first);
            }
        }

        std::string command_entity_render_materials(const McpRequest& request)
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

            bool include_descendants = true;
            if (const std::optional<std::string> value = get_argument(request, "include_descendants"))
            {
                if (!parse_bool(*value, include_descendants))
                {
                    return json_error("invalid include_descendants");
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"materials\":[";
            bool first = true;
            append_render_material_snapshot(json, entity, include_descendants, first);
            json += "]}";
            return json;
        }

        std::string command_resource_list(const McpRequest& request)
        {
            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            uint32_t limit = 500;
            if (const std::optional<std::string> limit_arg = get_argument(request, "limit"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*limit_arg, parsed) || parsed == 0 || parsed > 5000)
                {
                    return json_error("limit must be between 1 and 5000");
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

            uint32_t total = 0;
            uint32_t emitted = 0;
            std::string json = "{\"ok\":true";
            json += ",\"type\":" + json_string(resource_type_to_name(type));
            json += ",\"offset\":" + std::to_string(offset);
            json += ",\"resources\":[";
            bool first = true;
            std::lock_guard<std::recursive_mutex> guard(ResourceCache::GetMutex());
            for (std::shared_ptr<IResource>& resource : ResourceCache::GetResources())
            {
                if (!resource || (type != ResourceType::Max && resource->GetResourceType() != type))
                {
                    continue;
                }

                if (total++ < offset)
                {
                    continue;
                }

                if (emitted >= limit)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                emitted++;
                json += resource_to_json(resource.get());
            }

            json += "],\"total\":" + std::to_string(total);
            json += ",\"count\":" + std::to_string(emitted);
            json += ",\"truncated\":" + json_bool(total > offset + emitted);
            json += "}";
            return json;
        }

        std::string command_material_get(const McpRequest& request)
        {
            std::string error;
            Material* material = get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            return "{\"ok\":true,\"material\":" + material_to_json(material) + "}";
        }

        std::string command_material_set_property(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material edits require edit mode");
            }

            std::string error;
            Material* material = get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> property_arg = get_argument(request, "property");
            const std::optional<std::string> value_arg = get_argument(request, "value");
            if (!property_arg || !value_arg)
            {
                return json_error("missing property or value");
            }

            const std::optional<MaterialProperty> property = material_property_from_name(to_lower_copy(*property_arg));
            if (!property)
            {
                return json_error("invalid material property");
            }

            float value = 0.0f;
            if (!parse_float(*value_arg, value))
            {
                return json_error("invalid material property value");
            }

            material->SetProperty(*property, value);
            return "{\"ok\":true,\"material\":" + material_to_json(material) + "}";
        }

        std::string command_material_set_texture(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material edits require edit mode");
            }

            std::string error;
            Material* material = get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> texture_type_arg = get_argument(request, "texture_type");
            const std::optional<std::string> texture_path = get_argument(request, "texture_path");
            if (!texture_type_arg || !texture_path)
            {
                return json_error("missing texture_type or texture_path");
            }

            const std::optional<MaterialTextureType> texture_type = material_texture_type_from_name(to_lower_copy(*texture_type_arg));
            if (!texture_type)
            {
                return json_error("invalid material texture type");
            }

            uint8_t slot = 0;
            if (const std::optional<std::string> slot_arg = get_argument(request, "slot"))
            {
                uint32_t parsed = 0;
                if (!parse_uint32(*slot_arg, parsed) || parsed >= Material::slots_per_texture)
                {
                    return json_error("invalid texture slot");
                }
                slot = static_cast<uint8_t>(parsed);
            }

            material->SetTexture(*texture_type, *texture_path, slot);
            return "{\"ok\":true,\"material\":" + material_to_json(material) + "}";
        }

        std::string command_undo_redo(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("undo and redo require edit mode");
            }

            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!action_arg)
            {
                return json_error("missing action");
            }

            const std::string action = to_lower_copy(*action_arg);
            if (action == "undo")
            {
                CommandStack::Undo();
            }
            else if (action == "redo")
            {
                CommandStack::Redo();
            }
            else
            {
                return json_error("unknown undo action");
            }

            return "{\"ok\":true,\"action\":" + json_string(action) + "}";
        }

        std::string command_resource_load(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> type_arg = get_argument(request, "type");
            const std::optional<std::string> path_arg = get_argument(request, "path");
            if (!type_arg || !path_arg || path_arg->empty())
            {
                return json_error("missing type or path");
            }

            const std::optional<ResourceType> type = resource_type_from_name(to_lower_copy(*type_arg));
            if (!type || *type == ResourceType::Max || *type == ResourceType::Unknown)
            {
                return json_error("invalid resource type");
            }

            uint32_t flags = 0;
            if (const std::optional<std::string> flags_arg = get_argument(request, "flags"))
            {
                if (!parse_uint32(*flags_arg, flags))
                {
                    return json_error("invalid flags");
                }
            }

            std::shared_ptr<IResource> resource;
            if (*type == ResourceType::Material)
            {
                resource = ResourceCache::Load<Material>(*path_arg, flags);
            }
            else if (*type == ResourceType::Texture)
            {
                resource = ResourceCache::Load<RHI_Texture>(*path_arg, flags != 0 ? flags : RHI_Texture_Srv);
            }
            else if (*type == ResourceType::Mesh)
            {
                resource = ResourceCache::Load<Mesh>(*path_arg, flags);
            }
            else if (*type == ResourceType::Animation)
            {
                resource = ResourceCache::Load<Animation>(*path_arg, flags);
            }
            else
            {
                return json_error("resource type is not loadable by MCP");
            }

            if (!resource)
            {
                return json_error("failed to load resource");
            }

            return "{\"ok\":true,\"resource\":" + resource_to_json(resource.get()) + "}";
        }

        std::string command_resource_reload(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            const std::optional<std::string> key_arg = get_argument(request, "name") ? get_argument(request, "name") : get_argument(request, "path");
            if (!key_arg || key_arg->empty())
            {
                return json_error("missing resource name or path");
            }

            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            std::shared_ptr<IResource> resource = get_resource_shared_by_name_or_path(*key_arg, type);
            if (!resource)
            {
                return json_error("resource not found");
            }
            if (resource->GetResourceFilePath().empty())
            {
                return json_error("resource has no file path");
            }

            resource->LoadFromFile(resource->GetResourceFilePath());
            return "{\"ok\":true,\"resource\":" + resource_to_json(resource.get()) + "}";
        }

        std::string command_resource_save(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("resource save requires edit mode");
            }

            const std::optional<std::string> key_arg = get_argument(request, "name") ? get_argument(request, "name") : get_argument(request, "path");
            if (!key_arg || key_arg->empty())
            {
                return json_error("missing resource name or path");
            }

            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            std::shared_ptr<IResource> resource = get_resource_shared_by_name_or_path(*key_arg, type);
            if (!resource)
            {
                return json_error("resource not found");
            }

            const std::optional<std::string> save_path = get_argument(request, "save_path");
            const std::string path = save_path && !save_path->empty() ? *save_path : resource->GetResourceFilePath();
            if (path.empty())
            {
                return json_error("resource has no save path");
            }

            const std::filesystem::path file_path(path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(file_path.parent_path());
            }
            resource->SetResourceFilePath(path);
            resource->SaveToFile(path);
            return "{\"ok\":true,\"path\":" + json_string(path) + ",\"resource\":" + resource_to_json(resource.get()) + "}";
        }

        std::string command_resource_remove(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("resource removal requires edit mode");
            }

            const std::optional<std::string> key_arg = get_argument(request, "name") ? get_argument(request, "name") : get_argument(request, "path");
            if (!key_arg || key_arg->empty())
            {
                return json_error("missing resource name or path");
            }

            ResourceType type = ResourceType::Max;
            if (const std::optional<std::string> type_arg = get_argument(request, "type"))
            {
                const std::optional<ResourceType> parsed = resource_type_from_name(to_lower_copy(*type_arg));
                if (!parsed)
                {
                    return json_error("invalid resource type");
                }
                type = *parsed;
            }

            std::lock_guard<std::recursive_mutex> guard(ResourceCache::GetMutex());
            std::vector<std::shared_ptr<IResource>>& resources = ResourceCache::GetResources();
            const auto it = std::find_if(resources.begin(), resources.end(), [&](const std::shared_ptr<IResource>& resource)
            {
                return resource && (type == ResourceType::Max || resource->GetResourceType() == type) && (resource->GetObjectName() == *key_arg || resource->GetResourceFilePath() == *key_arg);
            });

            if (it == resources.end())
            {
                return json_error("resource not found");
            }

            const std::string removed = resource_to_json(it->get());
            resources.erase(it);
            return "{\"ok\":true,\"removed\":" + removed + "}";
        }

        std::string command_material_create(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material creation requires edit mode");
            }

            const std::optional<std::string> path_arg = get_argument(request, "path");
            if (!path_arg || path_arg->empty())
            {
                return json_error("missing path");
            }

            std::shared_ptr<Material> material = std::make_shared<Material>();
            const std::filesystem::path file_path(*path_arg);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(file_path.parent_path());
            }
            material->SetResourceFilePath(*path_arg);
            material->SaveToFile(*path_arg);
            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                material->SetObjectName(*name);
            }

            material = ResourceCache::Cache(material);
            return "{\"ok\":true,\"material\":" + material_to_json(material.get()) + "}";
        }

        std::string command_viewport_frame(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("viewport frame requires edit mode");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            if (get_argument(request, "id"))
            {
                std::string error;
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return json_error(error);
                }
                camera->ClearSelection();
                camera->AddToSelection(entity);
            }

            camera->FocusOnSelectedEntity();
            return command_camera_snapshot();
        }

        std::string command_camera_set_view(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("camera view changes require edit mode");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr || camera->GetEntity() == nullptr)
            {
                return json_error("camera not found");
            }

            Entity* entity = camera->GetEntity();
            if (const std::optional<std::string> position = get_argument(request, "position"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                entity->SetPosition(parsed);
            }

            if (const std::optional<std::string> rotation_euler = get_argument(request, "rotation_euler"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                entity->SetRotation(math::Quaternion::FromEulerAngles(parsed));
            }
            else if (const std::optional<std::string> target = get_argument(request, "target"))
            {
                math::Vector3 parsed;
                if (!parse_vector3(*target, parsed))
                {
                    return json_error("invalid target");
                }
                const math::Vector3 direction = parsed - entity->GetPosition();
                if (direction.LengthSquared() <= std::numeric_limits<float>::epsilon())
                {
                    return json_error("target must differ from camera position");
                }
                entity->SetRotation(math::Quaternion::FromLookRotation(direction));
            }

            return command_camera_snapshot();
        }

        std::string command_renderer_debug_get()
        {
            std::string json = "{\"ok\":true,\"options\":" + renderer_debug_options_json() + ",\"values\":{";
            bool first = true;
            const std::vector<std::string> options =
            {
                "aabb", "picking_ray", "grid", "transform_handle", "selection_outline", "lights", "audio_sources", "performance_metrics", "physics", "wireframe", "meshlet_visualize", "cluster_visualize"
            };

            for (const std::string& option : options)
            {
                const std::optional<std::string> cvar = renderer_debug_cvar_from_name(option);
                if (!cvar)
                {
                    continue;
                }
                const std::optional<std::string> value = ConsoleRegistry::Get().GetValueAsString(*cvar);
                if (!value)
                {
                    continue;
                }
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(option) + ":" + json_string(*value);
            }

            json += "}}";
            return json;
        }

        std::string command_renderer_debug_set(const McpRequest& request)
        {
            const std::optional<std::string> option_arg = get_argument(request, "option");
            const std::optional<std::string> value_arg = get_argument(request, "value");
            if (!option_arg || !value_arg)
            {
                return json_error("missing option or value");
            }

            const std::optional<std::string> cvar = renderer_debug_cvar_from_name(to_lower_copy(*option_arg));
            if (!cvar)
            {
                return json_error("unknown renderer debug option");
            }

            std::string value = to_lower_copy(*value_arg);
            if (value == "true")
            {
                value = "1";
            }
            else if (value == "false")
            {
                value = "0";
            }

            if (!ConsoleRegistry::Get().SetValueFromString(*cvar, value))
            {
                return json_error("failed to set renderer debug option");
            }

            return command_renderer_debug_get();
        }

        std::string command_physics_state(const McpRequest& request)
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

            Physics* physics = entity->GetComponent<Physics>();
            if (physics == nullptr)
            {
                return json_error("physics component not found");
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += ",\"body_type\":" + json_string(body_type_to_name(physics->GetBodyType()));
            json += ",\"enabled\":" + json_bool(physics->IsEnabled());
            json += ",\"static\":" + json_bool(physics->IsStatic());
            json += ",\"kinematic\":" + json_bool(physics->IsKinematic());
            json += ",\"mass\":" + std::to_string(physics->GetMass());
            json += ",\"friction\":" + std::to_string(physics->GetFriction());
            json += ",\"friction_rolling\":" + std::to_string(physics->GetFrictionRolling());
            json += ",\"restitution\":" + std::to_string(physics->GetRestitution());
            json += ",\"center_of_mass\":" + json_vector3(physics->GetCenterOfMass());
            json += ",\"linear_velocity\":" + json_vector3(physics->GetLinearVelocity());
            json += ",\"grounded\":" + json_bool(physics->IsGrounded());
            if (Entity* ground = physics->GetGroundEntity())
            {
                json += ",\"ground_entity\":" + entity_to_json_compact(ground);
            }

            if (physics->GetBodyType() == BodyType::Vehicle)
            {
                json += ",\"vehicle\":{";
                json += "\"throttle\":" + std::to_string(physics->GetVehicleThrottle());
                json += ",\"brake\":" + std::to_string(physics->GetVehicleBrake());
                json += ",\"steering\":" + std::to_string(physics->GetVehicleSteering());
                json += ",\"handbrake\":" + std::to_string(physics->GetVehicleHandbrake());
                json += ",\"gear\":" + json_string(physics->GetCurrentGearString());
                json += ",\"engine_rpm\":" + std::to_string(physics->GetEngineRPM());
                json += ",\"boost_pressure\":" + std::to_string(physics->GetBoostPressure());
                json += ",\"abs_active\":" + json_bool(physics->IsAbsActiveAny());
                json += ",\"tc_active\":" + json_bool(physics->IsTcActive());
                json += ",\"wheels\":[";
                for (uint32_t i = 0; i < static_cast<uint32_t>(WheelIndex::Count); i++)
                {
                    if (i != 0)
                    {
                        json += ",";
                    }
                    const WheelIndex wheel = static_cast<WheelIndex>(i);
                    json += "{";
                    json += "\"index\":" + std::to_string(i);
                    json += ",\"grounded\":" + json_bool(physics->IsWheelGrounded(wheel));
                    json += ",\"compression\":" + std::to_string(physics->GetWheelCompression(wheel));
                    json += ",\"slip_angle\":" + std::to_string(physics->GetWheelSlipAngle(wheel));
                    json += ",\"slip_ratio\":" + std::to_string(physics->GetWheelSlipRatio(wheel));
                    json += ",\"rpm\":" + std::to_string(physics->GetWheelRPM(wheel));
                    json += ",\"temperature\":" + std::to_string(physics->GetWheelTemperature(wheel));
                    json += ",\"wear\":" + std::to_string(physics->GetWheelWear(wheel));
                    json += ",\"contact_point\":" + json_vector3(physics->GetWheelContactPoint(wheel));
                    json += ",\"contact_normal\":" + json_vector3(physics->GetWheelContactNormal(wheel));
                    json += "}";
                }
                json += "]}";
            }

            json += "}";
            return json;
        }

        std::string command_selection_update(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("selection update requires edit mode");
            }

            Camera* camera = World::GetCamera();
            if (camera == nullptr)
            {
                return json_error("camera not found");
            }

            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!action_arg)
            {
                return json_error("missing action");
            }

            const std::string action = to_lower_copy(*action_arg);
            if (action == "clear")
            {
                camera->ClearSelection();
            }
            else if (action == "set_by_component")
            {
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

                camera->ClearSelection();
                for (Entity* entity : World::GetEntities())
                {
                    if (entity != nullptr && entity->GetComponentByType(*type) != nullptr)
                    {
                        camera->AddToSelection(entity);
                    }
                }
            }
            else
            {
                std::string error;
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return json_error(error);
                }

                if (action == "set")
                {
                    camera->ClearSelection();
                    camera->AddToSelection(entity);
                }
                else if (action == "add")
                {
                    camera->AddToSelection(entity);
                }
                else if (action == "remove")
                {
                    camera->RemoveFromSelection(entity);
                }
                else if (action == "toggle")
                {
                    camera->ToggleSelection(entity);
                }
                else
                {
                    return json_error("unknown selection action");
                }
            }

            std::string json = "{\"ok\":true,\"selected_ids\":[";
            bool first = true;
            for (Entity* selected_entity : camera->GetSelectedEntities())
            {
                if (selected_entity == nullptr)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(std::to_string(selected_entity->GetObjectId()));
            }
            json += "]}";
            return json;
        }

        std::string command_entity_clone(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity clone requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            Entity* parent = nullptr;
            if (const std::optional<std::string> parent_id = get_argument(request, "parent_id"))
            {
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
            }

            Entity* clone = entity->Clone();
            if (clone == nullptr)
            {
                return json_error("failed to clone entity");
            }

            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                clone->SetObjectName(*name);
            }

            if (get_argument(request, "parent_id"))
            {
                clone->SetParent(parent);
            }

            bool select = false;
            if (const std::optional<std::string> select_arg = get_argument(request, "select"))
            {
                if (!parse_bool(*select_arg, select))
                {
                    return json_error("invalid select");
                }
            }
            if (select)
            {
                if (Camera* camera = World::GetCamera())
                {
                    camera->ClearSelection();
                    camera->AddToSelection(clone);
                }
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json(clone, true) + "}";
        }

        std::string command_entity_move_index(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity move requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> index_arg = get_argument(request, "index");
            if (!index_arg)
            {
                return json_error("missing index");
            }

            uint32_t index = 0;
            if (!parse_uint32(*index_arg, index))
            {
                return json_error("invalid index");
            }

            if (Entity* parent = entity->GetParent())
            {
                parent->MoveChildToIndex(entity, index);
            }
            else
            {
                World::MoveEntityToIndex(entity, index);
            }

            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_prefab_types()
        {
            std::vector<std::string> types = Prefab::GetRegisteredTypes();
            std::string json = "{\"ok\":true,\"types\":[";
            bool first = true;
            for (const std::string& type : types)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(type);
            }
            json += "]}";
            return json;
        }

        std::string command_prefab_save(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("prefab save requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> path = get_argument(request, "path");
            if (!path || path->empty())
            {
                return json_error("missing path");
            }

            const bool saved = Prefab::SaveToFile(entity, *path);
            if (!saved)
            {
                return json_error("failed to save prefab");
            }

            return "{\"ok\":true,\"path\":" + json_string(*path) + ",\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        std::string command_prefab_load(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("prefab load requires edit mode");
            }

            const std::optional<std::string> path = get_argument(request, "path");
            if (!path || path->empty())
            {
                return json_error("missing path");
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

            if (parent == nullptr)
            {
                parent = World::CreateEntity();
                if (parent == nullptr)
                {
                    return json_error("failed to create prefab root");
                }
                parent->SetObjectName("prefab");
            }

            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                parent->SetObjectName(*name);
            }

            const bool loaded = Prefab::LoadFromFile(*path, parent);
            if (!loaded)
            {
                return json_error("failed to load prefab");
            }

            parent->SetPrefabFilePath(*path);
            parent->MarkPrefabBaseline();
            return "{\"ok\":true,\"path\":" + json_string(*path) + ",\"entity\":" + entity_to_json(parent, true) + "}";
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

        bool set_component_property(ComponentType type, Component* component, const std::string& property, const std::string& value, std::string& error)
        {
            bool changed = false;
            if (type == ComponentType::Render)
            {
                changed = set_render_property(static_cast<Render*>(component), property, value, error);
            }
            else if (type == ComponentType::Physics)
            {
                changed = set_physics_property(static_cast<Physics*>(component), property, value, error);
            }
            else if (type == ComponentType::Light)
            {
                changed = set_light_property(static_cast<Light*>(component), property, value, error);
            }
            else if (type == ComponentType::Camera)
            {
                changed = set_camera_property(static_cast<Camera*>(component), property, value, error);
            }
            else if (type == ComponentType::AudioSource)
            {
                changed = set_audio_source_property(static_cast<AudioSource*>(component), property, value, error);
            }
            else if (type == ComponentType::Script && property == "file_path")
            {
                static_cast<Script*>(component)->LoadScriptFile(value);
                changed = true;
            }
            else
            {
                changed = set_component_member(component, property, value, error);
            }

            if (!changed && (error.empty() || error.rfind("unsupported", 0) == 0))
            {
                std::string member_error;
                changed = set_component_member(component, property, value, member_error);
                if (!changed)
                {
                    error = member_error;
                }
            }

            return changed;
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

            if (!set_component_property(*type, component, *property, *value, error))
            {
                return json_error(error.empty() ? "failed to set component property" : error);
            }

            std::string json = "{\"ok\":true,\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
            json += "}}";
            return json;
        }

        std::string command_component_set_batch(const McpRequest& request)
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
            const std::optional<std::string> count_arg = get_argument(request, "count");
            if (!type_name || !count_arg)
            {
                return json_error("missing type or count");
            }

            const std::optional<ComponentType> type = component_type_from_name(*type_name);
            if (!type)
            {
                return json_error("unknown component type");
            }

            uint64_t count = 0;
            if (!parse_uint64(*count_arg, count) || count == 0 || count > 128)
            {
                return json_error("count must be between 1 and 128");
            }

            Component* component = entity->GetComponentByType(*type);
            if (component == nullptr)
            {
                return json_error("entity does not have component");
            }

            for (uint64_t i = 0; i < count; i++)
            {
                const std::optional<std::string> property = get_argument(request, "property_" + std::to_string(i));
                const std::optional<std::string> value = get_argument(request, "value_" + std::to_string(i));
                if (!property || !value)
                {
                    return json_error("missing batch property or value at index " + std::to_string(i));
                }

                if (!set_component_property(*type, component, *property, *value, error))
                {
                    std::string json = "{\"ok\":false";
                    json += ",\"failed_index\":" + std::to_string(i);
                    json += ",\"error\":" + json_string(error.empty() ? "failed to set component property" : error);
                    json += "}";
                    return json;
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"updated_count\":" + std::to_string(count);
            json += ",\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
            json += "}}";
            return json;
        }

        std::string command_entity_find_by_component(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
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

            uint32_t limit = 100;
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

            uint32_t total = 0;
            uint32_t emitted = 0;
            std::string json = "{\"ok\":true";
            json += ",\"type\":" + json_string(*type_name);
            json += ",\"offset\":" + std::to_string(offset);
            json += ",\"entities\":[";
            bool first = true;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr || entity->GetComponentByType(*type) == nullptr)
                {
                    continue;
                }

                if (total++ < offset)
                {
                    continue;
                }

                if (emitted >= limit)
                {
                    continue;
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                emitted++;
                json += entity_to_json_compact(entity);
            }

            json += "],\"total\":" + std::to_string(total);
            json += ",\"count\":" + std::to_string(emitted);
            json += ",\"truncated\":" + json_bool(total > offset + emitted);
            json += "}";
            return json;
        }

        std::string command_component_action(const McpRequest& request)
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
            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!type_name || !action_arg)
            {
                return json_error("missing type or action");
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

            const std::string action = to_lower_copy(*action_arg);
            const bool runtime_action =
                (*type == ComponentType::Physics && (action == "apply_force" || action == "sync_wheel_offsets" || action == "reset_tire_wear" || action == "shift_up" || action == "shift_down" || action == "shift_to_neutral" || action == "draw_debug_visualization")) ||
                (*type == ComponentType::AudioSource && (action == "play" || action == "stop"));

            if (!runtime_action && !is_edit_mode())
            {
                return json_error("component action requires edit mode");
            }

            std::string result_json = "{}";
            if (*type == ComponentType::Terrain && action == "generate")
            {
                static_cast<Terrain*>(component)->Generate();
            }
            else if (*type == ComponentType::Spline && action == "generate_road_mesh")
            {
                static_cast<Spline*>(component)->GenerateRoadMesh();
            }
            else if (*type == ComponentType::Spline && action == "clear_road_mesh")
            {
                static_cast<Spline*>(component)->ClearRoadMesh();
            }
            else if (*type == ComponentType::Spline && action == "spawn_instances")
            {
                static_cast<Spline*>(component)->SpawnInstances();
            }
            else if (*type == ComponentType::Spline && action == "clear_instances")
            {
                static_cast<Spline*>(component)->ClearInstances();
            }
            else if (*type == ComponentType::ParticleSystem && action == "apply_preset")
            {
                const std::optional<std::string> preset_arg = get_argument(request, "preset");
                const std::optional<std::string> value_arg = get_argument(request, "value");
                const std::string preset_name = preset_arg ? *preset_arg : (value_arg ? *value_arg : "");
                const std::optional<ParticlePreset> preset = particle_preset_from_name(to_lower_copy(preset_name));
                if (!preset)
                {
                    return json_error("invalid particle preset");
                }

                static_cast<ParticleSystem*>(component)->ApplyPreset(*preset);
            }
            else if (*type == ComponentType::ParticleSystem && action == "trigger_burst")
            {
                const std::optional<std::string> count_arg = get_argument(request, "count");
                const std::optional<std::string> value_arg = get_argument(request, "value");
                float count = 0.0f;
                if (!parse_float(count_arg ? *count_arg : (value_arg ? *value_arg : ""), count) || count <= 0.0f)
                {
                    return json_error("invalid burst count");
                }

                static_cast<ParticleSystem*>(component)->TriggerBurst(count);
            }
            else if (*type == ComponentType::Physics && action == "apply_force")
            {
                const std::optional<std::string> force_arg = get_argument(request, "force");
                if (!force_arg)
                {
                    return json_error("missing force");
                }

                math::Vector3 force;
                if (!parse_vector3(*force_arg, force))
                {
                    return json_error("invalid force");
                }

                PhysicsForce mode = PhysicsForce::Impulse;
                if (const std::optional<std::string> mode_arg = get_argument(request, "mode"))
                {
                    const std::optional<PhysicsForce> parsed = physics_force_from_name(to_lower_copy(*mode_arg));
                    if (!parsed)
                    {
                        return json_error("invalid force mode");
                    }
                    mode = *parsed;
                }

                static_cast<Physics*>(component)->ApplyForce(force, mode);
            }
            else if (*type == ComponentType::Physics && action == "sync_wheel_offsets")
            {
                static_cast<Physics*>(component)->SyncWheelOffsetsFromEntities();
            }
            else if (*type == ComponentType::Physics && action == "reset_tire_wear")
            {
                static_cast<Physics*>(component)->ResetTireWear();
            }
            else if (*type == ComponentType::Physics && action == "shift_up")
            {
                static_cast<Physics*>(component)->ShiftUp();
            }
            else if (*type == ComponentType::Physics && action == "shift_down")
            {
                static_cast<Physics*>(component)->ShiftDown();
            }
            else if (*type == ComponentType::Physics && action == "shift_to_neutral")
            {
                static_cast<Physics*>(component)->ShiftToNeutral();
            }
            else if (*type == ComponentType::Physics && action == "draw_debug_visualization")
            {
                static_cast<Physics*>(component)->DrawDebugVisualization();
            }
            else if (*type == ComponentType::AudioSource && action == "play")
            {
                static_cast<AudioSource*>(component)->PlayClip();
            }
            else if (*type == ComponentType::AudioSource && action == "stop")
            {
                static_cast<AudioSource*>(component)->StopClip();
            }
            else if (*type == ComponentType::Light && action == "fit_to_mesh")
            {
                const bool fitted = static_cast<Light*>(component)->FitToMesh();
                result_json = "{\"fitted\":" + json_bool(fitted) + "}";
            }
            else if (*type == ComponentType::Camera && action == "focus_selected")
            {
                static_cast<Camera*>(component)->FocusOnSelectedEntity();
            }
            else
            {
                return json_error("unsupported component action");
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += ",\"type\":" + json_string(*type_name);
            json += ",\"action\":" + json_string(action);
            json += ",\"result\":" + result_json;
            json += ",\"component\":{";
            json += "\"type\":" + json_string(*type_name);
            json += ",\"properties\":" + component_properties_to_json(component);
            json += ",\"members\":" + component_members_to_json(component);
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
            json += ",\"camera\":" + command_camera_snapshot();
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

            Stopwatch step_timer;
            Entity* entity = World::CreateEntity();
            float step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: World::CreateEntity took %.1f ms", step_ms);
            }
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

            step_timer.Start();
            Render* renderable = entity->AddComponent<Render>();
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: AddComponent<Render> took %.1f ms", step_ms);
            }
            if (renderable == nullptr)
            {
                return json_error("failed to add render component");
            }
            step_timer.Start();
            renderable->SetMesh(mesh_type);
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: Render::SetMesh took %.1f ms", step_ms);
            }
            step_timer.Start();
            renderable->SetDefaultMaterial();
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: Render::SetDefaultMaterial took %.1f ms", step_ms);
            }
            if (const std::optional<std::string> material = get_argument(request, "material"))
            {
                if (*material == "default")
                {
                    renderable->SetDefaultMaterial();
                }
                else
                {
                    renderable->SetMaterial(*material);
                }
            }

            if (with_physics)
            {
                step_timer.Start();
                Physics* physics = entity->AddComponent<Physics>();
                step_ms = step_timer.GetElapsedTimeMs();
                if (step_ms > 500.0f)
                {
                    SP_LOG_WARNING("MCP entity_create_primitive: AddComponent<Physics> took %.1f ms", step_ms);
                }
                if (physics == nullptr)
                {
                    return json_error("failed to add physics component");
                }

                step_timer.Start();
                physics->SetBodyType(body_type);
                step_ms = step_timer.GetElapsedTimeMs();
                if (step_ms > 500.0f)
                {
                    SP_LOG_WARNING("MCP entity_create_primitive: Physics::SetBodyType(%s) took %.1f ms", body_type_to_name(body_type).c_str(), step_ms);
                }
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
                "material",
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
        if (request.command == "undo_redo")
        {
            return command_undo_redo(request);
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
        if (request.command == "world_raycast")
        {
            return command_world_raycast(request);
        }
        if (request.command == "entity_list")
        {
            return command_entity_list(request);
        }
        if (request.command == "entity_find")
        {
            return command_entity_find(request);
        }
        if (request.command == "entity_find_by_component")
        {
            return command_entity_find_by_component(request);
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
        if (request.command == "camera_snapshot")
        {
            return command_camera_snapshot();
        }
        if (request.command == "camera_set_view")
        {
            return command_camera_set_view(request);
        }
        if (request.command == "screenshot_take")
        {
            return command_screenshot_take(request);
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
        if (request.command == "selection_update")
        {
            return command_selection_update(request);
        }
        if (request.command == "entity_clone")
        {
            return command_entity_clone(request);
        }
        if (request.command == "entity_move_index")
        {
            return command_entity_move_index(request);
        }
        if (request.command == "viewport_frame")
        {
            return command_viewport_frame(request);
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
        if (request.command == "entity_render_materials")
        {
            return command_entity_render_materials(request);
        }
        if (request.command == "resource_list")
        {
            return command_resource_list(request);
        }
        if (request.command == "resource_load")
        {
            return command_resource_load(request);
        }
        if (request.command == "resource_reload")
        {
            return command_resource_reload(request);
        }
        if (request.command == "resource_save")
        {
            return command_resource_save(request);
        }
        if (request.command == "resource_remove")
        {
            return command_resource_remove(request);
        }
        if (request.command == "material_get")
        {
            return command_material_get(request);
        }
        if (request.command == "material_create")
        {
            return command_material_create(request);
        }
        if (request.command == "material_set_property")
        {
            return command_material_set_property(request);
        }
        if (request.command == "material_set_texture")
        {
            return command_material_set_texture(request);
        }
        if (request.command == "component_set")
        {
            return command_component_set(request);
        }
        if (request.command == "component_set_batch")
        {
            return command_component_set_batch(request);
        }
        if (request.command == "component_action")
        {
            return command_component_action(request);
        }
        if (request.command == "renderer_debug_get")
        {
            return command_renderer_debug_get();
        }
        if (request.command == "renderer_debug_set")
        {
            return command_renderer_debug_set(request);
        }
        if (request.command == "physics_state")
        {
            return command_physics_state(request);
        }
        if (request.command == "prefab_types")
        {
            return command_prefab_types();
        }
        if (request.command == "prefab_save")
        {
            return command_prefab_save(request);
        }
        if (request.command == "prefab_load")
        {
            return command_prefab_load(request);
        }
        if (request.command == "execute_lua")
        {
            return command_execute_lua(request);
        }

        return json_error("unknown command");
    }
}
