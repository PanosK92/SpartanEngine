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
#include "McpGeometryKernel.h"
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
#include "../World/Components/SplineFollower.h"
#include "../World/Components/Terrain.h"
#include "../World/Prefab.h"
#include "../Car/Car.h"
#include "../Car/CarSimulation.h"
#include "../Car/CarState.h"
#include "../Resource/ResourceCache.h"
#include "../Animation/Animation.h"
#include "../Geometry/GeometryGeneration.h"
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

        std::string json_number(double value)
        {
            return std::isfinite(value)
                ? std::to_string(value)
                : "null";
        }

        std::string json_vector3(const math::Vector3& value)
        {
            return "[" + json_number(value.x) + "," + json_number(value.y) + "," + json_number(value.z) + "]";
        }

        std::string json_vector2(const math::Vector2& value)
        {
            return "[" + json_number(value.x) + "," + json_number(value.y) + "]";
        }

        std::string json_quaternion(const math::Quaternion& value)
        {
            return "[" + json_number(value.x) + "," + json_number(value.y) + "," + json_number(value.z) + "," + json_number(value.w) + "]";
        }

        std::string json_color(const Color& value)
        {
            return "[" + json_number(value.r) + "," + json_number(value.g) + "," + json_number(value.b) + "," + json_number(value.a) + "]";
        }

        std::string json_matrix(const math::Matrix& value)
        {
            return "["
                + json_number(value.m00) + "," + json_number(value.m01) + "," + json_number(value.m02) + "," + json_number(value.m03) + ","
                + json_number(value.m10) + "," + json_number(value.m11) + "," + json_number(value.m12) + "," + json_number(value.m13) + ","
                + json_number(value.m20) + "," + json_number(value.m21) + "," + json_number(value.m22) + "," + json_number(value.m23) + ","
                + json_number(value.m30) + "," + json_number(value.m31) + "," + json_number(value.m32) + "," + json_number(value.m33) + "]";
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

        bool parse_profile(
            const std::string& value,
            std::vector<math::Vector2>& profile
        )
        {
            std::stringstream stream(value);
            std::string part;
            std::vector<float> values;

            while (std::getline(stream, part, ','))
            {
                float parsed = 0.0f;
                if (!parse_float(part, parsed))
                {
                    return false;
                }
                values.push_back(parsed);
            }

            if (
                values.size() < 6 ||
                values.size() > 64 ||
                values.size() % 2 != 0
            )
            {
                return false;
            }

            for (size_t i = 0; i < values.size(); i += 2)
            {
                profile.emplace_back(values[i], values[i + 1]);
            }

            return true;
        }

        bool parse_profile_set(
            const std::string& value,
            uint32_t profile_count,
            uint32_t point_count,
            std::vector<std::vector<math::Vector2>>& profiles
        )
        {
            if (
                profile_count < 2 ||
                profile_count > 64 ||
                point_count < 3 ||
                point_count > 32
            )
            {
                return false;
            }
            std::vector<float> values;
            if (
                !parse_float_list(
                    value,
                    values,
                    profile_count * point_count * 2
                )
            )
            {
                return false;
            }

            profiles.clear();
            profiles.reserve(profile_count);
            size_t value_index = 0;
            for (
                uint32_t profile_index = 0;
                profile_index < profile_count;
                profile_index++
            )
            {
                std::vector<math::Vector2> profile;
                profile.reserve(point_count);
                for (
                    uint32_t point_index = 0;
                    point_index < point_count;
                    point_index++
                )
                {
                    profile.emplace_back(
                        values[value_index],
                        values[value_index + 1]
                    );
                    value_index += 2;
                }
                profiles.push_back(std::move(profile));
            }
            return true;
        }

        bool parse_path3(
            const std::string& value,
            std::vector<math::Vector3>& path
        )
        {
            std::stringstream stream(value);
            std::string part;
            std::vector<float> values;
            while (std::getline(stream, part, ','))
            {
                float parsed = 0.0f;
                if (!parse_float(part, parsed))
                {
                    return false;
                }
                values.push_back(parsed);
            }
            if (
                values.size() < 6 ||
                values.size() > 192 ||
                values.size() % 3 != 0
            )
            {
                return false;
            }
            for (size_t i = 0; i < values.size(); i += 3)
            {
                path.emplace_back(
                    values[i],
                    values[i + 1],
                    values[i + 2]
                );
            }

            for (size_t i = 1; i < path.size(); i++)
            {
                if (
                    (
                        path[i] -
                        path[i - 1]
                    ).LengthSquared() <= 0.0000001f
                )
                {
                    return false;
                }
            }
            for (size_t i = 1; i + 1 < path.size(); i++)
            {
                if (
                    (
                        path[i + 1] -
                        path[i - 1]
                    ).LengthSquared() <= 0.0000001f
                )
                {
                    return false;
                }
            }

            return true;
        }

        bool profile_has_distinct_neighbors(
            const std::vector<math::Vector2>& profile,
            bool closed
        )
        {
            const size_t edge_count =
                closed ? profile.size() : profile.size() - 1;
            for (size_t i = 0; i < edge_count; i++)
            {
                const size_t next = (i + 1) % profile.size();
                if (
                    (
                        profile[next] -
                        profile[i]
                    ).LengthSquared() <= 0.0000001f
                )
                {
                    return false;
                }
            }

            return true;
        }

        bool profile_is_convex_counter_clockwise(
            const std::vector<math::Vector2>& profile
        )
        {
            if (!profile_has_distinct_neighbors(profile, true))
            {
                return false;
            }

            float signed_area_twice = 0.0f;
            bool has_positive_turn = false;
            for (size_t i = 0; i < profile.size(); i++)
            {
                const math::Vector2& a = profile[i];
                const math::Vector2& b =
                    profile[(i + 1) % profile.size()];
                const math::Vector2& c =
                    profile[(i + 2) % profile.size()];
                signed_area_twice +=
                    a.x * b.y -
                    b.x * a.y;

                const math::Vector2 edge_a = b - a;
                const math::Vector2 edge_b = c - b;
                const float turn =
                    edge_a.x * edge_b.y -
                    edge_a.y * edge_b.x;
                if (turn < -0.00001f)
                {
                    return false;
                }
                has_positive_turn |= turn > 0.00001f;
            }

            return signed_area_twice > 0.00001f &&
                has_positive_turn;
        }

        bool profile_is_simple(
            const std::vector<math::Vector2>& profile
        )
        {
            auto orientation = [](
                const math::Vector2& a,
                const math::Vector2& b,
                const math::Vector2& c
            )
            {
                return
                    (b.x - a.x) * (c.y - a.y) -
                    (b.y - a.y) * (c.x - a.x);
            };
            auto on_segment = [](
                const math::Vector2& a,
                const math::Vector2& b,
                const math::Vector2& point
            )
            {
                return
                    point.x >= std::min(a.x, b.x) - 0.00001f &&
                    point.x <= std::max(a.x, b.x) + 0.00001f &&
                    point.y >= std::min(a.y, b.y) - 0.00001f &&
                    point.y <= std::max(a.y, b.y) + 0.00001f;
            };
            auto intersects = [&](
                const math::Vector2& a,
                const math::Vector2& b,
                const math::Vector2& c,
                const math::Vector2& d
            )
            {
                const float o1 = orientation(a, b, c);
                const float o2 = orientation(a, b, d);
                const float o3 = orientation(c, d, a);
                const float o4 = orientation(c, d, b);
                if (
                    (
                        (o1 > 0.00001f && o2 < -0.00001f) ||
                        (o1 < -0.00001f && o2 > 0.00001f)
                    ) &&
                    (
                        (o3 > 0.00001f && o4 < -0.00001f) ||
                        (o3 < -0.00001f && o4 > 0.00001f)
                    )
                )
                {
                    return true;
                }
                if (
                    std::abs(o1) <= 0.00001f &&
                    on_segment(a, b, c)
                )
                {
                    return true;
                }
                if (
                    std::abs(o2) <= 0.00001f &&
                    on_segment(a, b, d)
                )
                {
                    return true;
                }
                if (
                    std::abs(o3) <= 0.00001f &&
                    on_segment(c, d, a)
                )
                {
                    return true;
                }
                return
                    std::abs(o4) <= 0.00001f &&
                    on_segment(c, d, b);
            };

            for (size_t i = 0; i < profile.size(); i++)
            {
                const size_t next_i = (i + 1) % profile.size();
                for (size_t j = i + 1; j < profile.size(); j++)
                {
                    const size_t next_j =
                        (j + 1) % profile.size();
                    if (
                        i == j ||
                        next_i == j ||
                        next_j == i
                    )
                    {
                        continue;
                    }
                    if (
                        intersects(
                            profile[i],
                            profile[next_i],
                            profile[j],
                            profile[next_j]
                        )
                    )
                    {
                        return false;
                    }
                }
            }

            return true;
        }

        bool profile_is_counter_clockwise(
            const std::vector<math::Vector2>& profile
        )
        {
            if (
                !profile_has_distinct_neighbors(profile, true) ||
                !profile_is_simple(profile)
            )
            {
                return false;
            }

            float signed_area_twice = 0.0f;
            for (size_t i = 0; i < profile.size(); i++)
            {
                const math::Vector2& a = profile[i];
                const math::Vector2& b =
                    profile[(i + 1) % profile.size()];
                signed_area_twice +=
                    a.x * b.y -
                    b.x * a.y;
            }
            return signed_area_twice > 0.00001f;
        }

        bool profile_has_valid_revolve_tangents(
            const std::vector<math::Vector2>& profile
        )
        {
            if (!profile_has_distinct_neighbors(profile, false))
            {
                return false;
            }

            for (size_t i = 1; i + 1 < profile.size(); i++)
            {
                if (
                    (
                        profile[i + 1] -
                        profile[i - 1]
                    ).LengthSquared() <= 0.0000001f
                )
                {
                    return false;
                }
            }

            return true;
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

        std::optional<mcp_geometry_kernel::axis>
        geometry_axis_from_name(const std::string& name)
        {
            if (name == "x")
            {
                return mcp_geometry_kernel::axis::x;
            }
            if (name == "y")
            {
                return mcp_geometry_kernel::axis::y;
            }
            if (name == "z")
            {
                return mcp_geometry_kernel::axis::z;
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

        bool is_screenshot_path_valid(const std::string& path)
        {
            const std::filesystem::path screenshot_root =
                std::filesystem::absolute("screenshots").lexically_normal();
            const std::filesystem::path file_path =
                std::filesystem::path(path).lexically_normal();
            const std::filesystem::path relative =
                file_path.lexically_relative(screenshot_root);
            if (relative.empty())
            {
                return false;
            }
            const auto first = relative.begin();
            return (
                first != relative.end() &&
                (*first).generic_string() != ".."
            );
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

        std::string spline_follow_mode_to_name(SplineFollowMode mode)
        {
            switch (mode)
            {
            case SplineFollowMode::Clamp:
                return "clamp";
            case SplineFollowMode::Loop:
                return "loop";
            case SplineFollowMode::PingPong:
                return "ping_pong";
            default:
                return "unknown";
            }
        }

        std::optional<SplineFollowMode> spline_follow_mode_from_name(const std::string& name)
        {
            if (name == "clamp" || name == "0")
            {
                return SplineFollowMode::Clamp;
            }
            if (name == "loop" || name == "1")
            {
                return SplineFollowMode::Loop;
            }
            if (name == "ping_pong" || name == "2")
            {
                return SplineFollowMode::PingPong;
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
            case MaterialProperty::FlakeStrength:
                return "flake_strength";
            case MaterialProperty::FlakeScale:
                return "flake_scale";
            case MaterialProperty::PearlStrength:
                return "pearl_strength";
            case MaterialProperty::PearlColorR:
                return "pearl_color_r";
            case MaterialProperty::PearlColorG:
                return "pearl_color_g";
            case MaterialProperty::PearlColorB:
                return "pearl_color_b";
            case MaterialProperty::CoatTintR:
                return "coat_tint_r";
            case MaterialProperty::CoatTintG:
                return "coat_tint_g";
            case MaterialProperty::CoatTintB:
                return "coat_tint_b";
            case MaterialProperty::CoatTintStrength:
                return "coat_tint_strength";
            case MaterialProperty::Ior:
                return "ior";
            case MaterialProperty::Absorption:
                return "absorption";
            case MaterialProperty::Thickness:
                return "thickness";
            case MaterialProperty::PaintPreset:
                return "paint_preset";
            case MaterialProperty::SurfacePreset:
                return "surface_preset";
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
            case MaterialProperty::MotionBlurRadial:
                return "motion_blur_radial";
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

        std::optional<MaterialPaintPreset> material_paint_preset_from_name(
            const std::string& name
        )
        {
            if (name == "gloss_solid")
            {
                return MaterialPaintPreset::GlossSolid;
            }
            if (name == "metallic")
            {
                return MaterialPaintPreset::Metallic;
            }
            if (name == "satin")
            {
                return MaterialPaintPreset::Satin;
            }
            if (name == "matte")
            {
                return MaterialPaintPreset::Matte;
            }
            if (name == "pearl")
            {
                return MaterialPaintPreset::Pearl;
            }
            if (name == "candy")
            {
                return MaterialPaintPreset::Candy;
            }
            if (name == "chameleon")
            {
                return MaterialPaintPreset::Chameleon;
            }

            return std::nullopt;
        }

        std::optional<MaterialSurfacePreset>
        material_surface_preset_from_name(
            const std::string& name
        )
        {
            if (name == "glass_clear")
            {
                return MaterialSurfacePreset::GlassClear;
            }
            if (name == "glass_tinted")
            {
                return MaterialSurfacePreset::GlassTinted;
            }
            if (name == "headlight_lens")
            {
                return MaterialSurfacePreset::HeadlightLens;
            }
            if (name == "taillight_lens")
            {
                return MaterialSurfacePreset::TaillightLens;
            }
            if (name == "rubber")
            {
                return MaterialSurfacePreset::RubberTire;
            }
            if (name == "carbon_fiber")
            {
                return MaterialSurfacePreset::CarbonFiber;
            }
            if (name == "chrome")
            {
                return MaterialSurfacePreset::Chrome;
            }
            if (name == "polished_metal")
            {
                return MaterialSurfacePreset::PolishedMetal;
            }
            if (name == "brake_disc")
            {
                return MaterialSurfacePreset::BrakeDisc;
            }
            if (name == "leather")
            {
                return MaterialSurfacePreset::Leather;
            }
            if (name == "black_plastic")
            {
                return MaterialSurfacePreset::BlackPlastic;
            }
            if (name == "emissive_red")
            {
                return MaterialSurfacePreset::EmissiveRedLight;
            }
            if (name == "emissive_white")
            {
                return MaterialSurfacePreset::EmissiveWhiteLight;
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
            if (name == "entity_icons")
            {
                return "r.entity_icons";
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
            return "[\"aabb\",\"picking_ray\",\"grid\",\"transform_handle\",\"selection_outline\",\"entity_icons\",\"performance_metrics\",\"physics\",\"wireframe\",\"meshlet_visualize\",\"cluster_visualize\"]";
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
                const float value =
                    material->GetProperty(property);
                json += json_string(name) + ":";
                json += std::isfinite(value)
                    ? std::to_string(value)
                    : "null";
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

        std::string spline_follow_mode_enum_values_json()
        {
            return enum_values_json({ { "clamp", "0" }, { "loop", "1" }, { "ping_pong", "2" } });
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
                return json_number(std::any_cast<float>(value));
            }
            if (type == typeid(double))
            {
                return json_number(std::any_cast<double>(value));
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
                // as a string, uint64 entity ids survive json parsers that use doubles
                return json_string(std::to_string(std::any_cast<uint64_t>(value)));
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
            if (type == typeid(SplineFollowMode))
            {
                return json_string(spline_follow_mode_to_name(std::any_cast<SplineFollowMode>(value)));
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
                type == typeid(SplineFollowMode) ||
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
            if (type == typeid(SplineFollowMode))
            {
                const std::optional<SplineFollowMode> result = spline_follow_mode_from_name(value);
                if (!result)
                {
                    error = "invalid spline_follow_mode";
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
            else if (property == "follow_mode")
            {
                metadata.type = "enum";
                metadata.enum_values = spline_follow_mode_enum_values_json();
                metadata.side_effects.emplace_back("changes what happens when the follower reaches the end of the spline");
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
            if (property == "source_spline_entity_id" || property == "instance_template_id" || property == "spline_entity_id")
            {
                metadata.unit = "entity id";
            }
            if (property == "progress")
            {
                metadata.unit = "normalized";
                metadata.range = range_json(0.0f, 1.0f);
            }
            if (property == "flip_forward")
            {
                metadata.note = "set true when the mesh drives backwards along the spline, rotates it 180 degrees";
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

        std::string entity_tags_json(Entity* entity)
        {
            std::string json = "[";
            bool first = true;
            for (const std::string& tag : entity->GetTags())
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(tag);
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
            if (!entity->GetTags().empty())
            {
                json += ",\"tags\":" + entity_tags_json(entity);
            }
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
            if (!entity->GetTags().empty())
            {
                json += ",\"tags\":" + entity_tags_json(entity);
            }
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
            if (!entity->GetTags().empty())
            {
                json += ",\"tags\":" + entity_tags_json(entity);
            }

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

        const char* queue_type_to_name(RHI_Queue_Type type)
        {
            switch (type)
            {
            case RHI_Queue_Type::Graphics:
                return "graphics";
            case RHI_Queue_Type::Compute:
                return "compute";
            case RHI_Queue_Type::Copy:
                return "copy";
            case RHI_Queue_Type::Present:
                return "present";
            default:
                return "none";
            }
        }

        std::string command_profiler_snapshot(const McpRequest& request)
        {
            // optional filters, type is cpu, gpu or all, sort is duration or timeline
            std::string type_filter = "all";
            if (const std::optional<std::string> value = get_argument(request, "type"))
            {
                type_filter = to_lower_copy(*value);
            }

            bool sort_by_duration = true;
            if (const std::optional<std::string> value = get_argument(request, "sort"))
            {
                sort_by_duration = to_lower_copy(*value) != "timeline";
            }

            uint32_t top = 0;
            if (const std::optional<std::string> value = get_argument(request, "top"))
            {
                uint64_t parsed = 0;
                if (parse_uint64(*value, parsed))
                {
                    top = static_cast<uint32_t>(parsed);
                }
            }

            std::vector<const TimeBlock*> blocks;
            for (const TimeBlock& block : Profiler::GetTimeBlocks())
            {
                if (!block.IsComplete())
                {
                    continue;
                }

                const bool is_cpu = block.GetType() == TimeBlockType::Cpu;
                if (type_filter == "cpu" && !is_cpu)
                {
                    continue;
                }
                if (type_filter == "gpu" && is_cpu)
                {
                    continue;
                }

                blocks.push_back(&block);
            }

            if (sort_by_duration)
            {
                std::sort(blocks.begin(), blocks.end(), [](const TimeBlock* a, const TimeBlock* b)
                {
                    return a->GetDuration() > b->GetDuration();
                });
            }

            std::string json = "{\"ok\":true";
            json += ",\"fps\":" + std::to_string(Profiler::GetFps());
            json += ",\"frame_ms\":" + std::to_string(Profiler::GetFrameDurationMs());
            json += ",\"cpu_ms\":" + std::to_string(Profiler::GetTimeCpuLast());
            json += ",\"gpu_ms\":" + std::to_string(Profiler::GetTimeGpuLast());
            json += ",\"frame_ms_last\":" + std::to_string(Profiler::GetTimeFrameLast());
            json += ",\"cpu_stuttering\":" + json_bool(Profiler::IsCpuStuttering());
            json += ",\"gpu_stuttering\":" + json_bool(Profiler::IsGpuStuttering());
            json += ",\"update_interval_sec\":" + std::to_string(Profiler::GetUpdateInterval());
            json += ",\"visualized\":" + json_bool(Profiler::IsVisualized());

            json += ",\"rhi\":{";
            json += "\"draw_calls\":" + std::to_string(Profiler::m_rhi_draw);
            json += ",\"instance_count\":" + std::to_string(Profiler::m_rhi_instance_count);
            json += ",\"timeblock_count\":" + std::to_string(Profiler::m_rhi_timeblock_count);
            json += ",\"pipeline_barriers\":" + std::to_string(Profiler::m_rhi_pipeline_barriers);
            json += ",\"pipeline_bindings\":" + std::to_string(Profiler::m_rhi_bindings_pipeline);
            json += ",\"descriptor_set_count\":" + std::to_string(Profiler::m_rhi_descriptor_set_count);
            json += "}";

            const size_t limit = (top > 0 && top < blocks.size()) ? top : blocks.size();
            json += ",\"time_block_count\":" + std::to_string(blocks.size());
            json += ",\"time_blocks\":[";
            for (size_t i = 0; i < limit; i++)
            {
                const TimeBlock* block = blocks[i];
                if (i != 0)
                {
                    json += ",";
                }

                json += "{\"name\":" + json_string(block->GetName() ? block->GetName() : "");
                json += ",\"type\":" + json_string(block->GetType() == TimeBlockType::Cpu ? "cpu" : "gpu");
                json += ",\"queue\":" + json_string(queue_type_to_name(block->GetQueueType()));
                json += ",\"duration_ms\":" + std::to_string(block->GetDuration());
                json += ",\"start_ms\":" + std::to_string(block->GetStartMs());
                json += ",\"end_ms\":" + std::to_string(block->GetEndMs());
                json += ",\"tree_depth\":" + std::to_string(block->GetTreeDepth());
                json += "}";
            }
            json += "]";
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
            if (!is_screenshot_path_valid(path))
            {
                return json_error(
                    "screenshot path must be inside the screenshots directory"
                );
            }
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
            json += "],\"resource_cleanup_failures\":[";
            first = true;
            for (
                const std::string& file :
                World::GetLastResourceCleanupFailures()
            )
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
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

            const std::vector<std::string>& failures =
                World::GetLastResourceCleanupFailures();
            std::string json = "{\"ok\":" +
                json_bool(failures.empty());
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
            if (std::filesystem::path(path).is_relative())
            {
                path = std::filesystem::absolute(path)
                    .lexically_normal()
                    .string();
            }

            if (!is_world_path_valid(path))
            {
                return json_error("path must be an absolute .world file");
            }

            if (!World::SaveToFile(path))
            {
                return json_error("failed to save world");
            }

            std::string json = "{\"ok\":true,\"path\":" +
                json_string(path);
            json += ",\"resources_removed\":[";
            bool first = true;
            for (
                const std::string& file :
                World::GetLastResourceCleanup()
            )
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
            }
            json += "]}";
            return json;
        }

        std::string command_world_resources_clean()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error(
                    "world resource cleanup requires edit mode"
                );
            }

            std::string path = World::GetFilePath();
            if (path.empty())
            {
                return json_error("world has no file path");
            }
            if (std::filesystem::path(path).is_relative())
            {
                path = std::filesystem::absolute(path)
                    .lexically_normal()
                    .string();
            }
            if (!is_world_path_valid(path))
            {
                return json_error(
                    "world path must be an absolute .world file"
                );
            }

            const std::string directory =
                World::GetResourceDirectory(path);
            const std::vector<std::string> previous_cleanup =
                World::GetLastResourceCleanup();
            const std::vector<std::string> before =
                FileSystem::GetFilesInDirectory(directory);
            if (!World::SaveToFile(path))
            {
                return json_error(
                    "failed to save world and clean resources"
                );
            }
            const std::vector<std::string> after =
                FileSystem::GetFilesInDirectory(directory);
            const std::vector<std::string>& failures =
                World::GetLastResourceCleanupFailures();
            std::set<std::string> retained(
                after.begin(),
                after.end()
            );
            std::set<std::string> removed(
                previous_cleanup.begin(),
                previous_cleanup.end()
            );
            removed.insert(
                World::GetLastResourceCleanup().begin(),
                World::GetLastResourceCleanup().end()
            );
            for (const std::string& file : before)
            {
                if (retained.find(file) == retained.end())
                {
                    removed.insert(file);
                }
            }
            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(path);
            json += ",\"directory\":" + json_string(directory);
            json += ",\"removed\":[";
            bool first = true;
            for (const std::string& file : removed)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
            }
            json += "]";
            json += ",\"failed\":[";
            first = true;
            for (const std::string& file : failures)
            {
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += json_string(file);
            }
            json += "]";
            json += ",\"orphan_count\":" +
                std::to_string(failures.size());
            json += ",\"retained_count\":" +
                std::to_string(after.size());
            json += "}";
            return json;
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

            PhysicsRaycastHit hit_result;
            const bool hit = PhysicsWorld::RaycastStatic(
                origin,
                direction,
                max_distance,
                hit_result
            );

            std::string json = "{\"ok\":true";
            json += ",\"hit\":" + json_bool(hit);
            if (hit)
            {
                json += ",\"position\":" +
                    json_vector3(hit_result.position);
                json += ",\"normal\":" +
                    json_vector3(hit_result.normal);
                json += ",\"distance\":" +
                    std::to_string(hit_result.distance);
                if (hit_result.entity != nullptr)
                {
                    json += ",\"entity_id\":" +
                        json_string(
                            std::to_string(
                                hit_result.entity->GetObjectId()
                            )
                        );
                    json += ",\"entity_name\":" +
                        json_string(
                            hit_result.entity->GetObjectName()
                        );
                }
            }
            json += "}";
            return json;
        }

        std::string command_entity_snap(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("entity snapping requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::string mode = to_lower_copy(
                get_argument(request, "mode").value_or("floor")
            );
            if (
                mode != "floor" &&
                mode != "ceiling" &&
                mode != "wall" &&
                mode != "surface"
            )
            {
                return json_error("invalid snap mode");
            }

            float max_distance = 1000.0f;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "max_distance")
            )
            {
                if (
                    !parse_float(*value, max_distance) ||
                    max_distance <= 0.0f
                )
                {
                    return json_error("invalid max_distance");
                }
            }

            float offset = 0.0f;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "offset")
            )
            {
                if (!parse_float(*value, offset))
                {
                    return json_error("invalid offset");
                }
            }

            bool align_to_surface =
                mode == "wall" || mode == "surface";
            if (
                const std::optional<std::string> value =
                    get_argument(request, "align_to_surface")
            )
            {
                if (!parse_bool(*value, align_to_surface))
                {
                    return json_error("invalid align_to_surface");
                }
            }

            math::Vector3 target = entity->GetPosition();
            if (
                const std::optional<std::string> value =
                    get_argument(request, "target")
            )
            {
                if (!parse_vector3(*value, target))
                {
                    return json_error("invalid target");
                }
            }

            math::Vector3 origin = entity->GetPosition();
            math::Vector3 direction = math::Vector3::Down;
            if (mode == "floor")
            {
                origin = target +
                    math::Vector3::Up * (max_distance * 0.5f);
                direction = math::Vector3::Down;
            }
            else if (mode == "ceiling")
            {
                origin = target +
                    math::Vector3::Down * (max_distance * 0.5f);
                direction = math::Vector3::Up;
            }
            else
            {
                origin = target;
                direction = mode == "wall"
                    ? entity->GetForward()
                    : math::Vector3::Down;
            }

            if (
                const std::optional<std::string> value =
                    get_argument(request, "origin")
            )
            {
                if (!parse_vector3(*value, origin))
                {
                    return json_error("invalid origin");
                }
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "direction")
            )
            {
                if (
                    !parse_vector3(*value, direction) ||
                    direction == math::Vector3::Zero
                )
                {
                    return json_error("invalid direction");
                }
            }

            PhysicsRaycastHit hit;
            if (
                !PhysicsWorld::RaycastStatic(
                    origin,
                    direction,
                    max_distance,
                    hit,
                    entity
                )
            )
            {
                return json_error("snap ray did not hit static geometry");
            }

            if (align_to_surface)
            {
                if (mode == "wall")
                {
                    entity->SetRotation(
                        math::Quaternion::FromLookRotation(
                            hit.normal,
                            math::Vector3::Up
                        )
                    );
                }
                else
                {
                    entity->SetRotation(
                        math::Quaternion::FromRotation(
                            math::Vector3::Up,
                            hit.normal
                        )
                    );
                }
            }

            math::BoundingBox bounds;
            bool has_bounds = false;
            std::function<void(Entity*)> merge_bounds =
                [&](Entity* current)
            {
                if (Render* renderable =
                    current->GetComponent<Render>())
                {
                    auto merge_matrix =
                        [&](const math::Matrix& matrix)
                    {
                        const math::BoundingBox world_bounds =
                            renderable->GetBoundingBoxMesh() *
                            matrix;
                        if (!has_bounds)
                        {
                            bounds = world_bounds;
                            has_bounds = true;
                        }
                        else
                        {
                            bounds.Merge(world_bounds);
                        }
                    };
                    if (renderable->HasInstancing())
                    {
                        for (
                            uint32_t i = 0;
                            i < renderable->GetInstanceCount();
                            i++
                        )
                        {
                            merge_matrix(
                                renderable->GetInstance(i, true)
                            );
                        }
                    }
                    else
                    {
                        merge_matrix(current->GetMatrix());
                    }
                }
                for (Entity* child : current->GetChildren())
                {
                    merge_bounds(child);
                }
            };
            merge_bounds(entity);
            if (!has_bounds)
            {
                return json_error("entity has no render bounds");
            }

            const math::Vector3 pivot = entity->GetPosition();
            float support_toward_surface =
                -std::numeric_limits<float>::max();
            std::function<void(Entity*)> compute_support =
                [&](Entity* current)
            {
                if (Render* renderable =
                    current->GetComponent<Render>())
                {
                    std::array<math::Vector3, 8> corners;
                    renderable->GetBoundingBoxMesh().GetCorners(
                        &corners
                    );
                    auto accumulate_matrix =
                        [&](const math::Matrix& matrix)
                    {
                        for (const math::Vector3& corner : corners)
                        {
                            const math::Vector3 world_corner =
                                matrix * corner;
                            support_toward_surface = std::max(
                                support_toward_surface,
                                math::Vector3::Dot(
                                    world_corner - pivot,
                                    -hit.normal
                                )
                            );
                        }
                    };
                    if (renderable->HasInstancing())
                    {
                        for (
                            uint32_t i = 0;
                            i < renderable->GetInstanceCount();
                            i++
                        )
                        {
                            accumulate_matrix(
                                renderable->GetInstance(i, true)
                            );
                        }
                    }
                    else
                    {
                        accumulate_matrix(current->GetMatrix());
                    }
                }
                for (Entity* child : current->GetChildren())
                {
                    compute_support(child);
                }
            };
            compute_support(entity);
            if (
                support_toward_surface ==
                -std::numeric_limits<float>::max()
            )
            {
                return json_error(
                    "failed to compute entity support extent"
                );
            }
            const math::Vector3 final_position =
                hit.position +
                hit.normal *
                (support_toward_surface + offset);
            entity->SetPosition(final_position);

            has_bounds = false;
            merge_bounds(entity);
            std::string json = "{\"ok\":true";
            json += ",\"mode\":" + json_string(mode);
            json += ",\"position\":" +
                json_vector3(entity->GetPosition());
            json += ",\"rotation\":" +
                json_quaternion(entity->GetRotation());
            json += ",\"hit\":{";
            json += "\"position\":" +
                json_vector3(hit.position);
            json += ",\"normal\":" +
                json_vector3(hit.normal);
            json += ",\"distance\":" +
                std::to_string(hit.distance);
            if (hit.entity)
            {
                json += ",\"entity_id\":" +
                    json_string(
                        std::to_string(hit.entity->GetObjectId())
                    );
                json += ",\"entity_name\":" +
                    json_string(hit.entity->GetObjectName());
            }
            json += "}";
            json += ",\"bounding_box\":" +
                json_bounding_box(bounds);
            json += "}";
            return json;
        }

        std::string command_entity_spatial_snapshot(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Entity* root = get_entity_from_request(request, error);
            if (root == nullptr)
            {
                return json_error(error);
            }

            bool include_descendants = true;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "include_descendants")
            )
            {
                if (!parse_bool(*value, include_descendants))
                {
                    return json_error("invalid include_descendants");
                }
            }

            uint32_t limit = 1000;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "limit")
            )
            {
                uint64_t parsed = 0;
                if (
                    !parse_uint64(*value, parsed) ||
                    parsed == 0 ||
                    parsed > 5000
                )
                {
                    return json_error(
                        "limit must be between 1 and 5000"
                    );
                }
                limit = static_cast<uint32_t>(parsed);
            }

            uint32_t offset = 0;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "offset")
            )
            {
                uint64_t parsed = 0;
                if (
                    !parse_uint64(*value, parsed) ||
                    parsed > std::numeric_limits<uint32_t>::max()
                )
                {
                    return json_error("invalid offset");
                }
                offset = static_cast<uint32_t>(parsed);
            }

            std::vector<Entity*> entities;
            entities.emplace_back(root);
            if (include_descendants)
            {
                root->GetDescendants(&entities);
            }

            std::string json = "{\"ok\":true";
            json += ",\"root_id\":" +
                json_string(std::to_string(root->GetObjectId()));
            json += ",\"root_name\":" +
                json_string(root->GetObjectName());
            json += ",\"entities\":[";
            bool first = true;
            uint32_t emitted = 0;
            uint32_t skipped = 0;
            for (Entity* entity : entities)
            {
                if (entity == nullptr)
                {
                    continue;
                }
                if (skipped < offset)
                {
                    skipped++;
                    continue;
                }
                if (emitted >= limit)
                {
                    continue;
                }

                math::BoundingBox own_bounds;
                bool has_own_bounds = false;
                if (Render* renderable =
                    entity->GetComponent<Render>())
                {
                    own_bounds = renderable->GetBoundingBox();
                    has_own_bounds =
                        own_bounds.GetMin().IsFinite() &&
                        own_bounds.GetMax().IsFinite() &&
                        own_bounds.GetSize().LengthSquared() > 0.0f;
                }

                math::BoundingBox subtree_bounds;
                bool has_subtree_bounds = false;
                std::vector<Entity*> subtree;
                subtree.emplace_back(entity);
                entity->GetDescendants(&subtree);
                for (Entity* current : subtree)
                {
                    if (current == nullptr)
                    {
                        continue;
                    }
                    if (Render* renderable =
                        current->GetComponent<Render>())
                    {
                        const math::BoundingBox& bounds =
                            renderable->GetBoundingBox();
                        if (
                            !bounds.GetMin().IsFinite() ||
                            !bounds.GetMax().IsFinite() ||
                            bounds.GetSize().LengthSquared() <= 0.0f
                        )
                        {
                            continue;
                        }
                        if (!has_subtree_bounds)
                        {
                            subtree_bounds = bounds;
                            has_subtree_bounds = true;
                        }
                        else
                        {
                            subtree_bounds.Merge(bounds);
                        }
                    }
                }

                PhysicsRaycastHit support_hit;
                bool has_support_hit = false;
                float support_gap = 0.0f;
                PhysicsRaycastHit ceiling_hit;
                bool has_ceiling_hit = false;
                float ceiling_gap = 0.0f;
                PhysicsRaycastHit wall_hit;
                bool has_wall_hit = false;
                float wall_gap =
                    std::numeric_limits<float>::max();
                if (has_own_bounds)
                {
                    const math::Vector3 size = own_bounds.GetSize();
                    const float probe_offset = std::max(
                        0.05f,
                        std::min(0.25f, size.y * 0.1f)
                    );
                    math::Vector3 probe_origin =
                        own_bounds.GetCenter();
                    probe_origin.y =
                        own_bounds.GetMin().y + probe_offset;
                    has_support_hit = PhysicsWorld::RaycastStatic(
                        probe_origin,
                        math::Vector3::Down,
                        1000.0f,
                        support_hit,
                        entity
                    );
                    if (has_support_hit)
                    {
                        support_gap =
                            own_bounds.GetMin().y -
                            support_hit.position.y;
                    }

                    math::Vector3 ceiling_origin =
                        own_bounds.GetCenter();
                    ceiling_origin.y =
                        own_bounds.GetMax().y - probe_offset;
                    has_ceiling_hit = PhysicsWorld::RaycastStatic(
                        ceiling_origin,
                        math::Vector3::Up,
                        1000.0f,
                        ceiling_hit,
                        entity
                    );
                    if (has_ceiling_hit)
                    {
                        ceiling_gap =
                            ceiling_hit.position.y -
                            own_bounds.GetMax().y;
                    }

                    const std::array<math::Vector3, 4>
                        wall_directions =
                    {
                        math::Vector3::Left,
                        math::Vector3::Right,
                        math::Vector3::Forward,
                        math::Vector3::Backward
                    };
                    for (
                        const math::Vector3& wall_direction :
                        wall_directions
                    )
                    {
                        PhysicsRaycastHit candidate;
                        if (
                            !PhysicsWorld::RaycastStatic(
                                own_bounds.GetCenter(),
                                wall_direction,
                                1000.0f,
                                candidate,
                                entity
                            )
                        )
                        {
                            continue;
                        }
                        const float own_extent =
                            std::abs(wall_direction.x) > 0.5f
                            ? size.x * 0.5f
                            : size.z * 0.5f;
                        const float candidate_gap =
                            candidate.distance - own_extent;
                        if (
                            !has_wall_hit ||
                            candidate_gap < wall_gap
                        )
                        {
                            has_wall_hit = true;
                            wall_gap = candidate_gap;
                            wall_hit = candidate;
                        }
                    }
                }

                if (!first)
                {
                    json += ",";
                }
                first = false;
                emitted++;

                json += "{";
                json += "\"id\":" +
                    json_string(std::to_string(entity->GetObjectId()));
                json += ",\"name\":" +
                    json_string(entity->GetObjectName());
                Entity* parent = entity->GetParent();
                json += ",\"parent_id\":";
                json += parent
                    ? json_string(
                        std::to_string(parent->GetObjectId())
                    )
                    : "null";
                json += ",\"active\":" +
                    json_bool(entity->IsActive());
                json += ",\"components\":" +
                    entity_components_json(entity);
                if (!entity->GetTags().empty())
                {
                    json += ",\"tags\":" +
                        entity_tags_json(entity);
                }
                json += ",\"position\":" +
                    json_vector3(entity->GetPosition());
                json += ",\"rotation_euler\":" +
                    json_vector3(
                        entity->GetRotation().ToEulerAngles()
                    );
                json += ",\"forward\":" +
                    json_vector3(entity->GetForward());
                json += ",\"has_render_bounds\":" +
                    json_bool(has_own_bounds);
                if (has_own_bounds)
                {
                    json += ",\"bounding_box\":" +
                        json_bounding_box(own_bounds);
                }
                if (has_subtree_bounds)
                {
                    json += ",\"subtree_bounding_box\":" +
                        json_bounding_box(subtree_bounds);
                }
                json += ",\"support_hit\":" +
                    json_bool(has_support_hit);
                if (has_support_hit)
                {
                    json += ",\"support_gap\":" +
                        std::to_string(support_gap);
                    json += ",\"support\":{";
                    json += "\"position\":" +
                        json_vector3(support_hit.position);
                    json += ",\"normal\":" +
                        json_vector3(support_hit.normal);
                    json += ",\"distance\":" +
                        std::to_string(support_hit.distance);
                    if (support_hit.entity)
                    {
                        json += ",\"entity_id\":" +
                            json_string(
                                std::to_string(
                                    support_hit.entity->GetObjectId()
                                )
                            );
                        json += ",\"entity_name\":" +
                            json_string(
                                support_hit.entity->GetObjectName()
                            );
                    }
                    json += "}";
                }
                json += ",\"ceiling_hit\":" +
                    json_bool(has_ceiling_hit);
                if (has_ceiling_hit)
                {
                    json += ",\"ceiling_gap\":" +
                        std::to_string(ceiling_gap);
                    if (ceiling_hit.entity)
                    {
                        json += ",\"ceiling_entity_id\":" +
                            json_string(
                                std::to_string(
                                    ceiling_hit.entity->GetObjectId()
                                )
                            );
                    }
                }
                json += ",\"wall_hit\":" +
                    json_bool(has_wall_hit);
                if (has_wall_hit)
                {
                    json += ",\"wall_gap\":" +
                        std::to_string(wall_gap);
                    json += ",\"wall_normal\":" +
                        json_vector3(wall_hit.normal);
                    if (wall_hit.entity)
                    {
                        json += ",\"wall_entity_id\":" +
                            json_string(
                                std::to_string(
                                    wall_hit.entity->GetObjectId()
                                )
                            );
                    }
                }
                json += "}";
            }
            json += "],\"count\":" +
                std::to_string(emitted);
            json += ",\"offset\":" +
                std::to_string(offset);
            json += ",\"total\":" +
                std::to_string(entities.size());
            json += ",\"truncated\":" +
                json_bool(
                    entities.size() >
                    static_cast<size_t>(offset) + emitted
                );
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
            const std::optional<std::string> tag  = get_argument(request, "tag");
            if ((!name || name->empty()) && (!tag || tag->empty()))
            {
                return json_error("missing name or tag");
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

            const std::string query = to_lower_copy(name.value_or(""));
            std::string json = "{\"ok\":true,\"matches\":[";
            bool first = true;
            uint32_t count = 0;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr)
                {
                    continue;
                }

                if (!query.empty())
                {
                    const std::string entity_name = to_lower_copy(entity->GetObjectName());
                    const bool is_match = match == "exact" ? entity_name == query : entity_name.find(query) != std::string::npos;
                    if (!is_match)
                    {
                        continue;
                    }
                }

                if (tag && !tag->empty() && !entity->HasTag(*tag))
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

            if (const std::optional<std::string> tags = get_argument(request, "tags"))
            {
                entity->SetTagsString(*tags);
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
                json += ",\"temperature\":" + json_number(light->GetTemperature());
                json += ",\"intensity\":" + json_number(light->GetIntensityPhotometric());
                json += ",\"range\":" + json_number(light->GetRange());
                json += ",\"angle_degrees\":" + json_number(light->GetAngle() / math::deg_to_rad);
                json += ",\"area_width\":" + json_number(light->GetAreaWidth());
                json += ",\"area_height\":" + json_number(light->GetAreaHeight());
                json += ",\"shadows\":" + json_bool(light->GetFlag(LightFlags::Shadows));
                json += ",\"volumetric\":" + json_bool(light->GetFlag(LightFlags::Volumetric));
                json += ",\"draw_distance\":" + json_number(light->GetDrawDistance());
                json += ",\"shadow_distance\":" + json_number(light->GetShadowDistance());
                json += ",\"volumetric_distance\":" + json_number(light->GetVolumetricDistance());
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
                add({ "intensity", "", "float", true, "lux for directional, lumens otherwise", range_json(0.0f, std::nullopt), "", { "updates photometric and radiometric light intensity" }, "point/spot 8500, area 12000, directional 120000 for visible blockouts", "8500" });
                add({ "range", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects light culling and shadow coverage" }, "point 30, spot 35, area 40", "30" });
                add({ "angle_degrees", "", "float", true, "degrees", range_json(0.1f, 179.0f), "", { "affects spot light cone and shadow projection" }, "", "45" });
                add({ "area_width", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects area light emitter size" }, "", "6" });
                add({ "area_height", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects area light emitter size" }, "", "3" });
                add({ "shadows", "", "bool", true, "", "", "", { "allocates and renders shadow maps when enabled" }, "", "true" });
                add({ "volumetric", "", "bool", true, "", "", "", { "enables volumetric lighting contribution" }, "", "false" });
                add({ "draw_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects light icon and debug drawing visibility" }, "", "60" });
                add({ "shadow_distance", "", "float", true, "meters", range_json(0.0f, std::nullopt), "", { "affects shadow rendering distance" }, "", "45" });
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

        std::string command_material_apply_preset(
            const McpRequest& request
        )
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
            Material* material =
                get_material_from_request(request, error);
            if (material == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> kind_arg =
                get_argument(request, "kind");
            const std::optional<std::string> preset_arg =
                get_argument(request, "preset");
            if (!kind_arg || !preset_arg)
            {
                return json_error("missing kind or preset");
            }

            const std::string kind = to_lower_copy(*kind_arg);
            const std::string preset = to_lower_copy(*preset_arg);
            if (kind == "paint")
            {
                const std::optional<MaterialPaintPreset> parsed =
                    material_paint_preset_from_name(preset);
                if (!parsed)
                {
                    return json_error("invalid paint preset");
                }

                Color color(
                    material->GetProperty(MaterialProperty::ColorR),
                    material->GetProperty(MaterialProperty::ColorG),
                    material->GetProperty(MaterialProperty::ColorB),
                    material->GetProperty(MaterialProperty::ColorA)
                );
                if (
                    const std::optional<std::string> color_arg =
                        get_argument(request, "color")
                )
                {
                    if (!parse_color(*color_arg, color))
                    {
                        return json_error("invalid color");
                    }
                }
                material->ApplyPaintPreset(*parsed, color, true);
            }
            else if (kind == "surface")
            {
                const std::optional<MaterialSurfacePreset> parsed =
                    material_surface_preset_from_name(preset);
                if (!parsed)
                {
                    return json_error("invalid surface preset");
                }
                material->ApplySurfacePreset(*parsed, true);
            }
            else
            {
                return json_error("kind must be paint or surface");
            }

            return "{\"ok\":true,\"material\":" +
                material_to_json(material) +
                "}";
        }

        std::string command_material_semantic_create(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("material creation requires edit mode");
            }

            const std::optional<std::string> path_arg =
                get_argument(request, "path");
            const std::optional<std::string> semantic_arg =
                get_argument(request, "semantic");
            if (
                !path_arg ||
                path_arg->empty() ||
                !semantic_arg
            )
            {
                return json_error("missing path or semantic");
            }

            const std::string path =
                FileSystem::GetRelativePath(*path_arg);
            const std::string semantic =
                to_lower_copy(*semantic_arg);
            std::shared_ptr<Material> material =
                ResourceCache::GetByPath<Material>(path);
            if (!material && FileSystem::IsFile(path))
            {
                material = ResourceCache::Load<Material>(path);
            }
            if (!material)
            {
                const std::filesystem::path file_path(path);
                if (file_path.has_parent_path())
                {
                    std::filesystem::create_directories(
                        file_path.parent_path()
                    );
                }
                material = std::make_shared<Material>();
                material->SetResourceFilePath(path);
                material = ResourceCache::Cache(material);
            }
            if (!material)
            {
                return json_error("failed to create material");
            }

            Color color(0.8f, 0.8f, 0.8f, 1.0f);
            if (semantic == "painted_wall")
            {
                color = Color(0.72f, 0.68f, 0.58f, 1.0f);
            }
            else if (semantic == "wood")
            {
                color = Color(0.32f, 0.14f, 0.055f, 1.0f);
            }
            else if (semantic == "fabric")
            {
                color = Color(0.55f, 0.42f, 0.3f, 1.0f);
            }
            else if (semantic == "concrete")
            {
                color = Color(0.42f, 0.44f, 0.43f, 1.0f);
            }
            else if (semantic == "asphalt")
            {
                color = Color(0.035f, 0.04f, 0.045f, 1.0f);
            }
            else if (semantic == "masonry")
            {
                color = Color(0.4f, 0.28f, 0.2f, 1.0f);
            }
            else if (semantic == "rubber")
            {
                color = Color(0.018f, 0.02f, 0.022f, 1.0f);
            }
            else if (semantic == "road_paint")
            {
                color = Color(0.78f, 0.76f, 0.62f, 1.0f);
            }
            else if (semantic == "painted_metal")
            {
                color = Color(0.32f, 0.38f, 0.44f, 1.0f);
            }
            else if (semantic == "screen")
            {
                color = Color(0.005f, 0.008f, 0.012f, 1.0f);
            }
            else if (semantic == "screen_on")
            {
                color = Color(0.08f, 0.22f, 0.5f, 1.0f);
            }
            bool color_overridden = false;
            if (
                const std::optional<std::string> color_arg =
                    get_argument(request, "color")
            )
            {
                if (!parse_color(*color_arg, color))
                {
                    return json_error("invalid color");
                }
                color_overridden = true;
            }

            if (semantic == "painted_wall" || semantic == "paint")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
            }
            else if (semantic == "wood")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.72f
                );
                material->SetProperty(
                    MaterialProperty::Sheen,
                    0.12f
                );
            }
            else if (
                semantic == "concrete" ||
                semantic == "asphalt" ||
                semantic == "masonry" ||
                semantic == "road_paint"
            )
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
                const float roughness =
                    semantic == "asphalt"
                    ? 0.92f
                    : semantic == "concrete"
                        ? 0.86f
                        : semantic == "masonry"
                            ? 0.82f
                            : 0.58f;
                material->SetProperty(
                    MaterialProperty::Roughness,
                    roughness
                );
            }
            else if (semantic == "rubber")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::RubberTire,
                    false
                );
                material->SetColor(color);
            }
            else if (semantic == "painted_metal")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Satin,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Metalness,
                    0.62f
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.38f
                );
            }
            else if (semantic == "black_plastic")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::BlackPlastic,
                    false
                );
                if (color_overridden)
                {
                    material->SetColor(color);
                }
            }
            else if (semantic == "fabric")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::Matte,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.88f
                );
                material->SetProperty(
                    MaterialProperty::Sheen,
                    0.35f
                );
            }
            else if (semantic == "metal")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::PolishedMetal,
                    false
                );
                material->SetColor(color);
            }
            else if (semantic == "chrome")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::Chrome,
                    false
                );
                if (color_overridden)
                {
                    material->SetColor(color);
                }
            }
            else if (semantic == "glass")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::GlassClear,
                    false
                );
                if (color_overridden)
                {
                    material->SetColor(color);
                }
            }
            else if (semantic == "screen")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::GlossSolid,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::Roughness,
                    0.18f
                );
            }
            else if (semantic == "screen_on")
            {
                material->ApplyPaintPreset(
                    MaterialPaintPreset::GlossSolid,
                    color,
                    false
                );
                material->SetProperty(
                    MaterialProperty::EmissiveFromAlbedo,
                    0.08f
                );
            }
            else if (semantic == "emissive")
            {
                material->ApplySurfacePreset(
                    MaterialSurfacePreset::EmissiveWhiteLight,
                    false
                );
                material->SetColor(color);
            }
            else
            {
                return json_error("unsupported semantic material");
            }

            std::optional<std::filesystem::file_time_type>
                previous_write_time;
            if (FileSystem::IsFile(path))
            {
                previous_write_time =
                    std::filesystem::last_write_time(path);
            }
            material->SaveToFile(path);
            if (!FileSystem::IsFile(path))
            {
                return json_error("failed to save material");
            }
            if (
                previous_write_time &&
                std::filesystem::last_write_time(path) <=
                *previous_write_time
            )
            {
                return json_error("material file was not updated");
            }

            return "{\"ok\":true,\"semantic\":" +
                json_string(semantic) +
                ",\"material\":" +
                material_to_json(material.get()) +
                "}";
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

        float fit_camera_distance_to_bounds(
            const math::BoundingBox& bounds,
            const math::Quaternion& rotation,
            const float fov_horizontal,
            const float fov_vertical,
            const float near_plane,
            const float padding
        )
        {
            const math::Vector3 center = bounds.GetCenter();
            const math::Vector3 forward =
                rotation * math::Vector3::Forward;
            const math::Vector3 right =
                rotation * math::Vector3::Right;
            const math::Vector3 up =
                rotation * math::Vector3::Up;
            const float tan_horizontal =
                std::tan(fov_horizontal * 0.5f);
            const float tan_vertical =
                std::tan(fov_vertical * 0.5f);
            float distance = near_plane * 2.0f;
            std::array<math::Vector3, 8> corners;
            bounds.GetCorners(&corners);
            for (const math::Vector3& corner : corners)
            {
                const math::Vector3 offset = corner - center;
                const float depth =
                    math::Vector3::Dot(offset, forward);
                const float horizontal =
                    std::abs(math::Vector3::Dot(offset, right));
                const float vertical =
                    std::abs(math::Vector3::Dot(offset, up));
                distance = std::max(
                    distance,
                    horizontal * padding /
                    tan_horizontal - depth
                );
                distance = std::max(
                    distance,
                    vertical * padding /
                    tan_vertical - depth
                );
                distance = std::max(
                    distance,
                    near_plane * 2.0f - depth
                );
            }
            return distance;
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

            Entity* target_entity = nullptr;
            if (get_argument(request, "id"))
            {
                std::string error;
                target_entity = get_entity_from_request(request, error);
                if (target_entity == nullptr)
                {
                    return json_error(error);
                }
                camera->ClearSelection();
                camera->AddToSelection(target_entity);
            }
            else
            {
                target_entity = camera->GetSelectedEntity();
            }
            if (target_entity == nullptr)
            {
                return json_error("no entity selected to frame");
            }

            math::BoundingBox bounds;
            bool has_bounds = false;
            std::vector<Entity*> entities = { target_entity };
            target_entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity == nullptr)
                {
                    continue;
                }
                if (Render* renderable = entity->GetComponent<Render>())
                {
                    const math::BoundingBox& render_bounds =
                        renderable->GetBoundingBox();
                    if (
                        !render_bounds.GetMin().IsFinite() ||
                        !render_bounds.GetMax().IsFinite() ||
                        render_bounds.GetSize().LengthSquared() <= 0.0f
                    )
                    {
                        continue;
                    }
                    if (!has_bounds)
                    {
                        bounds = render_bounds;
                        has_bounds = true;
                    }
                    else
                    {
                        bounds.Merge(render_bounds);
                    }
                }
            }

            const math::Vector3 target = has_bounds
                ? bounds.GetCenter()
                : target_entity->GetPosition();
            float padding = 1.2f;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "padding")
            )
            {
                if (
                    !parse_float(*value, padding) ||
                    padding < 1.0f ||
                    padding > 4.0f
                )
                {
                    return json_error(
                        "padding must be between 1 and 4"
                    );
                }
            }

            const std::string view = to_lower_copy(
                get_argument(request, "view").value_or("perspective")
            );
            math::Vector3 camera_direction;
            if (view == "perspective")
            {
                camera_direction =
                    math::Vector3(1.0f, 0.65f, -1.0f).Normalized();
            }
            else if (view == "front")
            {
                camera_direction = math::Vector3(0.0f, 0.0f, 1.0f);
            }
            else if (view == "back")
            {
                camera_direction = math::Vector3(0.0f, 0.0f, -1.0f);
            }
            else if (view == "left")
            {
                camera_direction = math::Vector3(-1.0f, 0.0f, 0.0f);
            }
            else if (view == "right")
            {
                camera_direction = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else if (view == "top")
            {
                camera_direction =
                    math::Vector3(0.0f, 1.0f, 0.001f).Normalized();
            }
            else
            {
                return json_error("unknown viewport frame view");
            }

            const float fov_horizontal =
                camera->GetFovHorizontalRad();
            const float fov_vertical =
                camera->GetFovVerticalRad();
            if (
                !std::isfinite(fov_horizontal) ||
                !std::isfinite(fov_vertical) ||
                fov_horizontal <= 0.01f ||
                fov_vertical <= 0.01f
            )
            {
                return json_error("camera field of view is invalid");
            }
            const math::Quaternion rotation =
                math::Quaternion::FromLookRotation(
                    camera_direction * -1.0f
                );
            const math::BoundingBox frame_bounds = has_bounds
                ? bounds
                : math::BoundingBox(
                    target - math::Vector3::One,
                    target + math::Vector3::One
                );
            const float distance =
                fit_camera_distance_to_bounds(
                    frame_bounds,
                    rotation,
                    fov_horizontal,
                    fov_vertical,
                    camera->GetNearPlane(),
                    padding
                );
            Entity* camera_entity = camera->GetEntity();
            const math::Vector3 position =
                target + camera_direction * distance;
            camera_entity->SetPosition(position);
            camera_entity->SetRotation(rotation);

            std::string json = command_camera_snapshot();
            if (!json.empty() && json.back() == '}')
            {
                json.pop_back();
                json += ",\"view\":" + json_string(view);
                json += ",\"target\":" + json_vector3(target);
                json += ",\"distance\":" + std::to_string(distance);
                json += ",\"padding\":" + std::to_string(padding);
                if (has_bounds)
                {
                    json += ",\"bounding_box\":" +
                        json_bounding_box(bounds);
                }
                json += "}";
            }
            return json;
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
            else
            {
                // look_at is an accepted alias for target
                std::optional<std::string> target = get_argument(request, "target");
                if (!target)
                {
                    target = get_argument(request, "look_at");
                }

                if (target)
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
            }

            return command_camera_snapshot();
        }

        std::string command_renderer_debug_get()
        {
            std::string json = "{\"ok\":true,\"options\":" + renderer_debug_options_json() + ",\"values\":{";
            bool first = true;
            const std::vector<std::string> options =
            {
                "aabb", "picking_ray", "grid", "transform_handle", "selection_outline", "entity_icons", "performance_metrics", "physics", "wireframe", "meshlet_visualize", "cluster_visualize"
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

        const char* car_view_to_name(CarView view)
        {
            switch (view)
            {
            case CarView::Chase: return "chase";
            case CarView::Hood:  return "hood";
            case CarView::Wheel: return "wheel";
            }
            return "chase";
        }

        bool car_view_from_name(const std::string& name, CarView& view)
        {
            const std::string lower = to_lower_copy(name);
            if (lower == "chase")
            {
                view = CarView::Chase;
                return true;
            }
            if (lower == "hood")
            {
                view = CarView::Hood;
                return true;
            }
            if (lower == "wheel")
            {
                view = CarView::Wheel;
                return true;
            }
            return false;
        }

        Car* find_car_from_request(const McpRequest& request, std::string& error)
        {
            const std::optional<std::string> id_arg = get_argument(request, "id");
            if (id_arg)
            {
                Entity* entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return nullptr;
                }

                for (Car* car : Car::GetAll())
                {
                    if (car == nullptr)
                    {
                        continue;
                    }
                    Entity* root = car->GetRootEntity();
                    Entity* body = car->GetBodyEntity();
                    if (root == entity || body == entity)
                    {
                        return car;
                    }
                    // prefab worlds parent the vehicle under an entity like player_car
                    if (root && root->GetParent() == entity)
                    {
                        return car;
                    }
                }

                error = "entity is not a drivable car";
                return nullptr;
            }

            Car* occupied = nullptr;
            Car* first_drivable = nullptr;
            int drivable_count = 0;
            for (Car* car : Car::GetAll())
            {
                if (car == nullptr || !car->IsDrivable() || !car->GetRootEntity())
                {
                    continue;
                }
                if (car->IsOccupied())
                {
                    if (occupied != nullptr)
                    {
                        error = "multiple occupied cars, pass id";
                        return nullptr;
                    }
                    occupied = car;
                }
                drivable_count++;
                if (first_drivable == nullptr)
                {
                    first_drivable = car;
                }
            }

            if (occupied != nullptr)
            {
                return occupied;
            }
            if (drivable_count == 1)
            {
                return first_drivable;
            }
            if (drivable_count == 0)
            {
                error = "no cars in world";
                return nullptr;
            }

            error = "multiple cars, pass id";
            return nullptr;
        }

        std::string car_status_json(Car* car)
        {
            Entity* root = car->GetRootEntity();
            Physics* physics = root ? root->GetComponent<Physics>() : nullptr;

            std::string json = "{\"ok\":true";
            if (root)
            {
                json += ",\"entity\":" + entity_to_json_compact(root);
            }
            json += ",\"occupied\":" + json_bool(car->IsOccupied());
            json += ",\"mcp_controlled\":" + json_bool(car->IsMcpControlled());
            json += ",\"view\":" + json_string(car_view_to_name(car->GetCurrentView()));
            json += ",\"show_telemetry\":" + json_bool(car->GetShowTelemetry());
            json += ",\"playing\":" + json_bool(Engine::IsFlagSet(EngineMode::Playing));
            if (physics && physics->GetBodyType() == BodyType::Vehicle)
            {
                const math::Vector3 velocity = physics->GetLinearVelocity();
                json += ",\"throttle\":" + std::to_string(physics->GetVehicleThrottle());
                json += ",\"brake\":" + std::to_string(physics->GetVehicleBrake());
                json += ",\"steering\":" + std::to_string(physics->GetVehicleSteering());
                json += ",\"handbrake\":" + std::to_string(physics->GetVehicleHandbrake());
                json += ",\"gear\":" + json_string(physics->GetCurrentGearString());
                json += ",\"engine_rpm\":" + std::to_string(physics->GetEngineRPM());
                json += ",\"speed_kmh\":" + std::to_string(velocity.Length() * 3.6f);
                json += ",\"position\":" + json_vector3(root->GetPosition());
                json += ",\"linear_velocity\":" + json_vector3(velocity);
                json += ",\"abs_active\":" + json_bool(physics->IsAbsActiveAny());
                json += ",\"tc_active\":" + json_bool(physics->IsTcActive());
            }
            json += "}";
            return json;
        }

        std::string command_vehicle_list()
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string json = "{\"ok\":true,\"cars\":[";
            bool first = true;
            for (Car* car : Car::GetAll())
            {
                if (car == nullptr || !car->IsDrivable() || car->GetRootEntity() == nullptr)
                {
                    continue;
                }
                if (!first)
                {
                    json += ",";
                }
                first = false;

                Entity* root = car->GetRootEntity();
                Physics* physics = root->GetComponent<Physics>();
                Entity* parent = root->GetParent();
                json += "{";
                json += "\"id\":" + json_string(std::to_string(root->GetObjectId()));
                json += ",\"name\":" + json_string(root->GetObjectName());
                if (parent)
                {
                    json += ",\"parent_id\":" + json_string(std::to_string(parent->GetObjectId()));
                    json += ",\"parent_name\":" + json_string(parent->GetObjectName());
                }
                json += ",\"occupied\":" + json_bool(car->IsOccupied());
                json += ",\"mcp_controlled\":" + json_bool(car->IsMcpControlled());
                json += ",\"view\":" + json_string(car_view_to_name(car->GetCurrentView()));
                json += ",\"position\":" + json_vector3(root->GetPosition());
                if (physics)
                {
                    json += ",\"speed_kmh\":" + std::to_string(physics->GetLinearVelocity().Length() * 3.6f);
                }
                json += "}";
            }
            json += "]}";
            return json;
        }

        std::string command_vehicle_get(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }
            return car_status_json(car);
        }

        std::string command_vehicle_enter(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle enter requires play mode");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            bool mcp_controlled = true;
            if (const std::optional<std::string> value = get_argument(request, "mcp_controlled"))
            {
                if (!parse_bool(*value, mcp_controlled))
                {
                    return json_error("invalid mcp_controlled");
                }
            }

            if (!car->IsOccupied())
            {
                car->Enter();
            }
            if (!car->IsOccupied())
            {
                return json_error("failed to enter car");
            }

            car->SetMcpControlled(mcp_controlled);
            return car_status_json(car);
        }

        std::string command_vehicle_exit(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            if (car->IsOccupied())
            {
                car->Exit();
            }
            car->SetMcpControlled(false);
            return car_status_json(car);
        }

        std::string command_vehicle_set_input(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle input requires play mode");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }
            if (!car->IsOccupied())
            {
                return json_error("car is not occupied, call vehicle_enter first");
            }

            car->SetMcpControlled(true);

            if (const std::optional<std::string> value = get_argument(request, "throttle"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid throttle");
                }
                car->SetThrottle(std::clamp(parsed, 0.0f, 1.0f));
            }
            if (const std::optional<std::string> value = get_argument(request, "brake"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid brake");
                }
                car->SetBrake(std::clamp(parsed, 0.0f, 1.0f));
            }
            if (const std::optional<std::string> value = get_argument(request, "steering"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid steering");
                }
                car->SetSteering(std::clamp(parsed, -1.0f, 1.0f));
            }
            if (const std::optional<std::string> value = get_argument(request, "handbrake"))
            {
                float parsed = 0.0f;
                if (!parse_float(*value, parsed))
                {
                    return json_error("invalid handbrake");
                }
                car->SetHandbrake(std::clamp(parsed, 0.0f, 1.0f));
            }

            return car_status_json(car);
        }

        std::string command_vehicle_shift(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                return json_error("vehicle shift requires play mode");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            Entity* root = car->GetRootEntity();
            Physics* physics = root ? root->GetComponent<Physics>() : nullptr;
            if (!physics || physics->GetBodyType() != BodyType::Vehicle)
            {
                return json_error("vehicle physics not found");
            }

            const std::optional<std::string> action_arg = get_argument(request, "action");
            if (!action_arg)
            {
                return json_error("missing action");
            }

            const std::string action = to_lower_copy(*action_arg);
            if (action == "up")
            {
                physics->ShiftUp();
            }
            else if (action == "down")
            {
                physics->ShiftDown();
            }
            else if (action == "neutral")
            {
                physics->ShiftToNeutral();
            }
            else
            {
                return json_error("action must be up, down, or neutral");
            }

            car->SetMcpControlled(true);
            return car_status_json(car);
        }

        std::string command_vehicle_reset(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            car->ResetToSpawn();
            car->SetThrottle(0.0f);
            car->SetBrake(0.0f);
            car->SetSteering(0.0f);
            car->SetHandbrake(1.0f);
            return car_status_json(car);
        }

        std::string command_vehicle_set_view(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (car == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> view_arg = get_argument(request, "view");
            if (!view_arg)
            {
                return json_error("missing view");
            }

            const std::string view_name = to_lower_copy(*view_arg);
            if (view_name == "cycle" || view_name == "next")
            {
                car->CycleView();
            }
            else
            {
                CarView view = CarView::Chase;
                if (!car_view_from_name(view_name, view))
                {
                    return json_error("view must be chase, hood, wheel, or cycle");
                }
                car->SetView(view);
            }

            return car_status_json(car);
        }

        std::string command_vehicle_telemetry(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            std::string error;
            Car* car = find_car_from_request(request, error);
            if (!car)
            {
                return json_error(error);
            }
            Entity* entity = car->GetRootEntity();
            Physics* physics = entity ? entity->GetComponent<Physics>() : nullptr;
            if (!physics)
            {
                return json_error("target car has no vehicle simulation");
            }
            ::car::Simulation* simulation = physics->GetVehicleSimulation();

            int max_rows = 200;
            if (const std::optional<std::string> rows_arg = get_argument(request, "max_rows"))
            {
                int32_t parsed = 0;
                if (!parse_int32(*rows_arg, parsed) || parsed < 1 || parsed > 5000)
                {
                    return json_error("max_rows must be between 1 and 5000");
                }
                max_rows = parsed;
            }

            bool include_csv = true;
            if (const std::optional<std::string> include_arg = get_argument(request, "include_csv"))
            {
                if (!parse_bool(*include_arg, include_csv))
                {
                    return json_error("invalid include_csv");
                }
            }

            std::string csv_text;
            std::string path;
            int total_lines = 0;
            const bool ok = simulation->snapshot_telemetry_tail(max_rows, csv_text, path, total_lines);

            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(path);
            json += ",\"log_to_file\":" + json_bool(simulation->get_log_to_file());
            json += ",\"total_lines\":" + std::to_string(total_lines);
            json += ",\"returned_data_rows\":" + std::to_string(std::max(0, std::min(max_rows, std::max(0, total_lines - 1))));
            json += ",\"file_ready\":" + json_bool(ok && total_lines > 0);
            if (include_csv)
            {
                json += ",\"csv\":" + json_string(csv_text);
            }
            if (!ok && total_lines == 0)
            {
                json += ",\"note\":" + json_string("telemetry file not found yet, enter play mode with a drivable car and drive first");
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

        bool assign_render_material(Render* renderable, const std::string& name_or_path, std::string& error)
        {
            if (name_or_path == "default")
            {
                renderable->SetDefaultMaterial();
                return true;
            }

            // prefer the cached resource so both resource names and paths bind
            if (std::shared_ptr<IResource> cached = get_resource_shared_by_name_or_path(name_or_path, ResourceType::Material))
            {
                renderable->SetMaterial(std::static_pointer_cast<Material>(cached));
                return true;
            }

            if (FileSystem::IsFile(name_or_path))
            {
                renderable->SetMaterial(name_or_path);
                return true;
            }

            error = "material not found by cached name, cached path, or file path: " + name_or_path;
            return false;
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
                return assign_render_material(renderable, value, error);
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

        std::string command_mesh_generate(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("mesh generation requires edit mode");
            }

            const std::optional<std::string> shape_arg =
                get_argument(request, "shape");
            const std::optional<std::string> path_arg =
                get_argument(request, "path");
            if (!shape_arg || !path_arg || path_arg->empty())
            {
                return json_error("missing shape or path");
            }

            const std::string shape = to_lower_copy(*shape_arg);
            std::string path = *path_arg;
            if (std::filesystem::path(path).extension() != EXTENSION_MESH)
            {
                path += EXTENSION_MESH;
            }
            path = FileSystem::GetRelativePath(path);

            bool reuse_existing = false;
            if (
                const std::optional<std::string> reuse_arg =
                    get_argument(request, "reuse_existing")
            )
            {
                if (!parse_bool(*reuse_arg, reuse_existing))
                {
                    return json_error("invalid reuse_existing");
                }
            }

            if (
                std::shared_ptr<Mesh> existing =
                    ResourceCache::GetByPath<Mesh>(path)
            )
            {
                if (!reuse_existing)
                {
                    return json_error(
                        "mesh path is already cached, use a new path or set reuse_existing"
                    );
                }
                std::string json = "{\"ok\":true,\"reused\":true";
                json += ",\"vertex_count\":" +
                    std::to_string(existing->GetVertexCount());
                json += ",\"index_count\":" +
                    std::to_string(existing->GetIndexCount());
                json += ",\"resource\":" +
                    resource_to_json(existing.get());
                json += "}";
                return json;
            }
            if (FileSystem::IsFile(path))
            {
                if (!reuse_existing)
                {
                    return json_error(
                        "mesh path already exists, use a new path or set reuse_existing"
                    );
                }

                std::shared_ptr<Mesh> existing =
                    ResourceCache::Load<Mesh>(path);
                if (!existing)
                {
                    return json_error(
                        "failed to load existing mesh"
                    );
                }

                std::string json =
                    "{\"ok\":true,\"reused\":true";
                json += ",\"vertex_count\":" +
                    std::to_string(existing->GetVertexCount());
                json += ",\"index_count\":" +
                    std::to_string(existing->GetIndexCount());
                json += ",\"resource\":" +
                    resource_to_json(existing.get());
                json += "}";
                return json;
            }

            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            std::vector<uint32_t> indices;

            math::Vector3 size = math::Vector3::One;
            if (const std::optional<std::string> size_arg =
                get_argument(request, "size"))
            {
                if (!parse_vector3(*size_arg, size))
                {
                    return json_error("invalid size");
                }
            }
            if (
                size.x <= 0.0f ||
                size.y <= 0.0f ||
                size.z <= 0.0f ||
                size.x > 1000.0f ||
                size.y > 1000.0f ||
                size.z > 1000.0f
            )
            {
                return json_error("size components must be between 0 and 1000");
            }

            uint32_t segments = 4;
            if (
                shape == "revolved_profile" ||
                shape == "torus" ||
                shape == "rounded_cylinder"
            )
            {
                segments = 24;
            }
            else if (shape == "capsule" || shape == "arch")
            {
                segments = 12;
            }
            else if (
                shape == "pipe" ||
                shape == "curved_profile" ||
                shape == "loft"
            )
            {
                segments = 8;
            }
            if (const std::optional<std::string> segments_arg =
                get_argument(request, "segments"))
            {
                if (!parse_uint32(*segments_arg, segments))
                {
                    return json_error("invalid segments");
                }
            }

            if (shape == "rounded_box" || shape == "beveled_box")
            {
                const float max_radius = std::min(
                    { size.x, size.y, size.z }
                ) * 0.5f;
                float radius = std::min(
                    0.05f,
                    max_radius * 0.25f
                );
                const std::optional<std::string> radius_arg =
                    get_argument(
                        request,
                        shape == "rounded_box" ? "radius" : "bevel"
                    );
                if (
                    radius_arg &&
                    !parse_float(*radius_arg, radius)
                )
                {
                    return json_error("invalid radius");
                }

                if (radius <= 0.0f || radius >= max_radius)
                {
                    return json_error(
                        "radius must be positive and smaller than half the smallest size component"
                    );
                }

                if (shape == "rounded_box")
                {
                    if (segments < 1 || segments > 16)
                    {
                        return json_error(
                            "rounded box segments must be between 1 and 16"
                        );
                    }
                    geometry_generation::generate_rounded_box(
                        &vertices,
                        &indices,
                        size,
                        radius,
                        segments
                    );
                }
                else
                {
                    geometry_generation::generate_beveled_box(
                        &vertices,
                        &indices,
                        size,
                        radius
                    );
                }
            }
            else if (shape == "wedge")
            {
                geometry_generation::generate_wedge(
                    &vertices,
                    &indices,
                    size
                );
            }
            else if (shape == "wall_opening")
            {
                math::Vector2 opening_size(
                    size.x * 0.35f,
                    size.y * 0.7f
                );
                math::Vector2 opening_center(
                    0.0f,
                    -size.y * 0.5f +
                        opening_size.y * 0.5f
                );
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "opening_size")
                )
                {
                    if (!parse_vector2(*value, opening_size))
                    {
                        return json_error("invalid opening_size");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "opening_center")
                )
                {
                    if (!parse_vector2(*value, opening_center))
                    {
                        return json_error(
                            "invalid opening_center"
                        );
                    }
                }
                const float opening_min_x =
                    opening_center.x - opening_size.x * 0.5f;
                const float opening_max_x =
                    opening_center.x + opening_size.x * 0.5f;
                const float opening_min_y =
                    opening_center.y - opening_size.y * 0.5f;
                const float opening_max_y =
                    opening_center.y + opening_size.y * 0.5f;
                if (
                    opening_size.x <= 0.0f ||
                    opening_size.y <= 0.0f ||
                    opening_min_x <= -size.x * 0.5f ||
                    opening_max_x >= size.x * 0.5f ||
                    opening_min_y < -size.y * 0.5f ||
                    opening_max_y >= size.y * 0.5f
                )
                {
                    return json_error(
                        "opening must fit inside the wall"
                    );
                }

                const auto append_box = [&](
                    const math::Vector3& part_size,
                    const math::Vector3& center
                )
                {
                    std::vector<RHI_Vertex_PosTexNorTan>
                        part_vertices;
                    std::vector<uint32_t> part_indices;
                    geometry_generation::generate_cube(
                        &part_vertices,
                        &part_indices
                    );
                    for (
                        RHI_Vertex_PosTexNorTan& vertex :
                        part_vertices
                    )
                    {
                        const math::Vector3 position =
                            vertex.get_position();
                        vertex.set_position(
                            math::Vector3(
                                position.x * part_size.x,
                                position.y * part_size.y,
                                position.z * part_size.z
                            ) + center
                        );
                    }
                    return mcp_geometry_kernel::append_mesh(
                        part_vertices,
                        part_indices,
                        vertices,
                        indices
                    );
                };

                const float left_width =
                    opening_min_x + size.x * 0.5f;
                const float right_width =
                    size.x * 0.5f - opening_max_x;
                const float bottom_height =
                    opening_min_y + size.y * 0.5f;
                const float top_height =
                    size.y * 0.5f - opening_max_y;
                const std::array<
                    std::pair<
                        math::Vector3,
                        math::Vector3
                    >,
                    4
                > parts =
                {{
                    {
                        math::Vector3(
                            left_width,
                            size.y,
                            size.z
                        ),
                        math::Vector3(
                            -size.x * 0.5f +
                                left_width * 0.5f,
                            0.0f,
                            0.0f
                        )
                    },
                    {
                        math::Vector3(
                            right_width,
                            size.y,
                            size.z
                        ),
                        math::Vector3(
                            size.x * 0.5f -
                                right_width * 0.5f,
                            0.0f,
                            0.0f
                        )
                    },
                    {
                        math::Vector3(
                            opening_size.x,
                            bottom_height,
                            size.z
                        ),
                        math::Vector3(
                            opening_center.x,
                            -size.y * 0.5f +
                                bottom_height * 0.5f,
                            0.0f
                        )
                    },
                    {
                        math::Vector3(
                            opening_size.x,
                            top_height,
                            size.z
                        ),
                        math::Vector3(
                            opening_center.x,
                            size.y * 0.5f -
                                top_height * 0.5f,
                            0.0f
                        )
                    }
                }};
                for (const auto& [part_size, center] : parts)
                {
                    if (
                        part_size.x <= 0.0001f ||
                        part_size.y <= 0.0001f
                    )
                    {
                        continue;
                    }
                    const auto result = append_box(
                        part_size,
                        center
                    );
                    if (!result.succeeded())
                    {
                        return json_error(
                            "wall opening failed, " +
                            result.message
                        );
                    }
                }
            }
            else if (shape == "wall_openings")
            {
                uint32_t opening_count = 0;
                const std::optional<std::string> count_arg =
                    get_argument(request, "opening_count");
                if (
                    !count_arg ||
                    !parse_uint32(*count_arg, opening_count) ||
                    opening_count < 1 ||
                    opening_count > 16
                )
                {
                    return json_error("invalid opening_count");
                }
                std::vector<float> opening_sizes;
                std::vector<float> opening_centers;
                const std::optional<std::string> sizes_arg =
                    get_argument(request, "opening_sizes");
                const std::optional<std::string> centers_arg =
                    get_argument(request, "opening_centers");
                if (
                    !sizes_arg ||
                    !centers_arg ||
                    !parse_float_list(
                        *sizes_arg,
                        opening_sizes,
                        opening_count * 2
                    ) ||
                    !parse_float_list(
                        *centers_arg,
                        opening_centers,
                        opening_count * 2
                    )
                )
                {
                    return json_error(
                        "opening sizes and centers must match opening_count"
                    );
                }

                struct wall_opening
                {
                    float min_x = 0.0f;
                    float max_x = 0.0f;
                    float min_y = 0.0f;
                    float max_y = 0.0f;
                };
                std::vector<wall_opening> openings;
                std::vector<float> x_boundaries = {
                    -size.x * 0.5f,
                    size.x * 0.5f
                };
                std::vector<float> y_boundaries = {
                    -size.y * 0.5f,
                    size.y * 0.5f
                };
                openings.reserve(opening_count);
                for (uint32_t i = 0; i < opening_count; i++)
                {
                    const float width = opening_sizes[i * 2];
                    const float height = opening_sizes[i * 2 + 1];
                    const float center_x = opening_centers[i * 2];
                    const float center_y =
                        opening_centers[i * 2 + 1];
                    wall_opening opening;
                    opening.min_x = center_x - width * 0.5f;
                    opening.max_x = center_x + width * 0.5f;
                    opening.min_y = center_y - height * 0.5f;
                    opening.max_y = center_y + height * 0.5f;
                    if (
                        width <= 0.0f ||
                        height <= 0.0f ||
                        opening.min_x <= -size.x * 0.5f ||
                        opening.max_x >= size.x * 0.5f ||
                        opening.min_y < -size.y * 0.5f ||
                        opening.max_y >= size.y * 0.5f
                    )
                    {
                        return json_error(
                            "every opening must fit inside the wall"
                        );
                    }
                    openings.push_back(opening);
                    x_boundaries.push_back(opening.min_x);
                    x_boundaries.push_back(opening.max_x);
                    y_boundaries.push_back(opening.min_y);
                    y_boundaries.push_back(opening.max_y);
                }
                const auto sort_unique = [](std::vector<float>& values)
                {
                    std::sort(values.begin(), values.end());
                    values.erase(
                        std::unique(
                            values.begin(),
                            values.end(),
                            [](float a, float b)
                            {
                                return std::abs(a - b) <= 0.0001f;
                            }
                        ),
                        values.end()
                    );
                };
                sort_unique(x_boundaries);
                sort_unique(y_boundaries);

                const size_t x_cells = x_boundaries.size() - 1;
                const size_t y_cells = y_boundaries.size() - 1;
                std::vector<bool> occupied(
                    x_cells * y_cells,
                    true
                );
                const auto cell_index = [y_cells](
                    size_t x,
                    size_t y
                )
                {
                    return x * y_cells + y;
                };
                for (size_t x = 0; x < x_cells; x++)
                {
                    for (size_t y = 0; y < y_cells; y++)
                    {
                        const float center_x =
                            (
                                x_boundaries[x] +
                                x_boundaries[x + 1]
                            ) * 0.5f;
                        const float center_y =
                            (
                                y_boundaries[y] +
                                y_boundaries[y + 1]
                            ) * 0.5f;
                        for (const wall_opening& opening : openings)
                        {
                            if (
                                center_x > opening.min_x &&
                                center_x < opening.max_x &&
                                center_y > opening.min_y &&
                                center_y < opening.max_y
                            )
                            {
                                occupied[cell_index(x, y)] = false;
                                break;
                            }
                        }
                    }
                }

                const auto append_quad = [&](
                    const math::Vector3& a,
                    const math::Vector3& b,
                    const math::Vector3& c,
                    const math::Vector3& d
                )
                {
                    const math::Vector3 normal =
                        math::Vector3::Cross(
                            b - a,
                            c - a
                        ).Normalized();
                    const math::Vector3 tangent =
                        (c - a).Normalized();
                    const uint32_t offset =
                        static_cast<uint32_t>(vertices.size());
                    vertices.emplace_back(
                        a,
                        math::Vector2(0, 1),
                        normal,
                        tangent
                    );
                    vertices.emplace_back(
                        b,
                        math::Vector2(0, 0),
                        normal,
                        tangent
                    );
                    vertices.emplace_back(
                        c,
                        math::Vector2(1, 1),
                        normal,
                        tangent
                    );
                    vertices.emplace_back(
                        d,
                        math::Vector2(1, 0),
                        normal,
                        tangent
                    );
                    indices.push_back(offset);
                    indices.push_back(offset + 1);
                    indices.push_back(offset + 2);
                    indices.push_back(offset + 2);
                    indices.push_back(offset + 1);
                    indices.push_back(offset + 3);
                };
                const float front = -size.z * 0.5f;
                const float back = size.z * 0.5f;
                for (size_t x = 0; x < x_cells; x++)
                {
                    for (size_t y = 0; y < y_cells; y++)
                    {
                        if (!occupied[cell_index(x, y)])
                        {
                            continue;
                        }
                        const float min_x = x_boundaries[x];
                        const float max_x = x_boundaries[x + 1];
                        const float min_y = y_boundaries[y];
                        const float max_y = y_boundaries[y + 1];
                        append_quad(
                            math::Vector3(min_x, min_y, front),
                            math::Vector3(min_x, max_y, front),
                            math::Vector3(max_x, min_y, front),
                            math::Vector3(max_x, max_y, front)
                        );
                        append_quad(
                            math::Vector3(max_x, min_y, back),
                            math::Vector3(max_x, max_y, back),
                            math::Vector3(min_x, min_y, back),
                            math::Vector3(min_x, max_y, back)
                        );
                        if (
                            x == 0 ||
                            !occupied[cell_index(x - 1, y)]
                        )
                        {
                            append_quad(
                                math::Vector3(min_x, min_y, back),
                                math::Vector3(min_x, max_y, back),
                                math::Vector3(min_x, min_y, front),
                                math::Vector3(min_x, max_y, front)
                            );
                        }
                        if (
                            x + 1 == x_cells ||
                            !occupied[cell_index(x + 1, y)]
                        )
                        {
                            append_quad(
                                math::Vector3(max_x, min_y, front),
                                math::Vector3(max_x, max_y, front),
                                math::Vector3(max_x, min_y, back),
                                math::Vector3(max_x, max_y, back)
                            );
                        }
                        if (
                            y == 0 ||
                            !occupied[cell_index(x, y - 1)]
                        )
                        {
                            append_quad(
                                math::Vector3(min_x, min_y, front),
                                math::Vector3(max_x, min_y, front),
                                math::Vector3(min_x, min_y, back),
                                math::Vector3(max_x, min_y, back)
                            );
                        }
                        if (
                            y + 1 == y_cells ||
                            !occupied[cell_index(x, y + 1)]
                        )
                        {
                            append_quad(
                                math::Vector3(min_x, max_y, back),
                                math::Vector3(max_x, max_y, back),
                                math::Vector3(min_x, max_y, front),
                                math::Vector3(max_x, max_y, front)
                            );
                        }
                    }
                }
            }
            else if (shape == "grid")
            {
                uint32_t grid_points = 16;
                float extent = size.x;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "grid_points")
                )
                {
                    if (!parse_uint32(*value, grid_points))
                    {
                        return json_error("invalid grid_points");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "extent")
                )
                {
                    if (!parse_float(*value, extent))
                    {
                        return json_error("invalid extent");
                    }
                }
                if (
                    grid_points < 2 ||
                    grid_points > 256 ||
                    extent <= 0.0f ||
                    extent > 10000.0f
                )
                {
                    return json_error(
                        "invalid grid dimensions"
                    );
                }
                geometry_generation::generate_grid(
                    &vertices,
                    &indices,
                    grid_points,
                    extent
                );
            }
            else if (shape == "grass_blade")
            {
                if (segments < 2 || segments > 32)
                {
                    return json_error(
                        "grass blade segments must be between 2 and 32"
                    );
                }
                geometry_generation::generate_foliage_grass_blade(
                    &vertices,
                    &indices,
                    segments
                );
            }
            else if (shape == "flower")
            {
                uint32_t petal_count = 12;
                uint32_t petal_segments = 6;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "petal_count")
                )
                {
                    if (!parse_uint32(*value, petal_count))
                    {
                        return json_error("invalid petal_count");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "petal_segments")
                )
                {
                    if (!parse_uint32(*value, petal_segments))
                    {
                        return json_error(
                            "invalid petal_segments"
                        );
                    }
                }
                if (
                    segments < 2 ||
                    segments > 32 ||
                    petal_count < 3 ||
                    petal_count > 64 ||
                    petal_segments < 2 ||
                    petal_segments > 32
                )
                {
                    return json_error(
                        "invalid flower segment counts"
                    );
                }
                geometry_generation::generate_foliage_flower(
                    &vertices,
                    &indices,
                    segments,
                    petal_count,
                    petal_segments
                );
            }
            else if (shape == "torus")
            {
                float major_radius = size.x * 0.5f;
                float minor_radius = std::min(size.y, size.z) * 0.25f;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "major_radius")
                )
                {
                    if (!parse_float(*value, major_radius))
                    {
                        return json_error("invalid major_radius");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "minor_radius")
                )
                {
                    if (!parse_float(*value, minor_radius))
                    {
                        return json_error("invalid minor_radius");
                    }
                }
                uint32_t minor_segments = 12;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "minor_segments")
                )
                {
                    if (!parse_uint32(*value, minor_segments))
                    {
                        return json_error("invalid minor_segments");
                    }
                }
                if (
                    major_radius <= 0.0f ||
                    minor_radius <= 0.0f ||
                    minor_radius >= major_radius ||
                    segments < 3 ||
                    segments > 96 ||
                    minor_segments < 3 ||
                    minor_segments > 48
                )
                {
                    return json_error("invalid torus dimensions or segments");
                }
                geometry_generation::generate_torus(
                    &vertices,
                    &indices,
                    major_radius,
                    minor_radius,
                    segments,
                    minor_segments
                );
            }
            else if (shape == "capsule")
            {
                float radius = std::min(size.x, size.z) * 0.5f;
                float height = size.y;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "radius")
                )
                {
                    if (!parse_float(*value, radius))
                    {
                        return json_error("invalid radius");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "height")
                )
                {
                    if (!parse_float(*value, height))
                    {
                        return json_error("invalid height");
                    }
                }
                if (
                    radius <= 0.0f ||
                    height < radius * 2.0f ||
                    segments < 4 ||
                    segments > 48
                )
                {
                    return json_error("invalid capsule dimensions or segments");
                }
                geometry_generation::generate_capsule(
                    &vertices,
                    &indices,
                    radius,
                    height,
                    segments
                );
            }
            else if (shape == "rounded_cylinder")
            {
                float radius = std::min(size.x, size.z) * 0.5f;
                float height = size.y;
                float bevel = std::min(radius, height * 0.5f) * 0.15f;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "radius")
                )
                {
                    if (!parse_float(*value, radius))
                    {
                        return json_error("invalid radius");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "height")
                )
                {
                    if (!parse_float(*value, height))
                    {
                        return json_error("invalid height");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "bevel")
                )
                {
                    if (!parse_float(*value, bevel))
                    {
                        return json_error("invalid bevel");
                    }
                }
                uint32_t bevel_segments = 4;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "bevel_segments")
                )
                {
                    if (!parse_uint32(*value, bevel_segments))
                    {
                        return json_error("invalid bevel_segments");
                    }
                }
                if (
                    radius <= 0.0f ||
                    height <= 0.0f ||
                    bevel <= 0.0f ||
                    bevel >= radius ||
                    bevel * 2.0f >= height ||
                    segments < 3 ||
                    segments > 96 ||
                    bevel_segments < 1 ||
                    bevel_segments > 16
                )
                {
                    return json_error(
                        "invalid rounded cylinder dimensions or segments"
                    );
                }
                geometry_generation::generate_rounded_cylinder(
                    &vertices,
                    &indices,
                    radius,
                    height,
                    bevel,
                    segments,
                    bevel_segments
                );
            }
            else if (
                shape == "pipe" ||
                shape == "curved_profile" ||
                shape == "loft"
            )
            {
                const std::optional<std::string> path_points_arg =
                    get_argument(request, "path_points");
                std::vector<math::Vector3> path_points;
                if (
                    !path_points_arg ||
                    !parse_path3(*path_points_arg, path_points)
                )
                {
                    return json_error(
                        "path_points must contain 2 to 64 distinct 3d points"
                    );
                }
                if (segments < 3 || segments > 32)
                {
                    return json_error(
                        "sweep segments must be between 3 and 32"
                    );
                }

                std::vector<float> sweep_scales;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "sweep_scales")
                )
                {
                    if (
                        !parse_float_list(
                            *value,
                            sweep_scales,
                            static_cast<uint32_t>(
                                path_points.size()
                            )
                        )
                    )
                    {
                        return json_error(
                            "sweep_scales must match path_points"
                        );
                    }
                    for (const float scale : sweep_scales)
                    {
                        if (scale <= 0.0f || scale > 100.0f)
                        {
                            return json_error(
                                "sweep scales must be between 0 and 100"
                            );
                        }
                    }
                }

                std::vector<float> sweep_twists;
                if (
                    const std::optional<std::string> value =
                        get_argument(
                            request,
                            "sweep_twists_degrees"
                        )
                )
                {
                    if (
                        !parse_float_list(
                            *value,
                            sweep_twists,
                            static_cast<uint32_t>(
                                path_points.size()
                            )
                        )
                    )
                    {
                        return json_error(
                            "sweep_twists_degrees must match path_points"
                        );
                    }
                    for (float& twist : sweep_twists)
                    {
                        if (std::abs(twist) > 3600.0f)
                        {
                            return json_error(
                                "sweep twist exceeds 3600 degrees"
                            );
                        }
                        twist *= math::deg_to_rad;
                    }
                }

                if (shape == "pipe")
                {
                    float radius = 0.02f;
                    if (
                        const std::optional<std::string> value =
                            get_argument(request, "radius")
                    )
                    {
                        if (!parse_float(*value, radius))
                        {
                            return json_error("invalid radius");
                        }
                    }
                    if (radius <= 0.0f || radius > 100.0f)
                    {
                        return json_error("invalid pipe radius");
                    }
                    geometry_generation::generate_pipe(
                        &vertices,
                        &indices,
                        path_points,
                        radius,
                        segments,
                        sweep_scales,
                        sweep_twists
                    );
                }
                else if (shape == "curved_profile")
                {
                    const std::optional<std::string> profile_arg =
                        get_argument(request, "profile");
                    std::vector<math::Vector2> profile;
                    if (
                        !profile_arg ||
                        !parse_profile(*profile_arg, profile) ||
                        !profile_is_counter_clockwise(profile)
                    )
                    {
                        return json_error(
                            "curved profile requires a valid closed profile"
                        );
                    }
                    geometry_generation::generate_swept_profile(
                        &vertices,
                        &indices,
                        path_points,
                        profile,
                        sweep_scales,
                        sweep_twists
                    );
                }
                else
                {
                    uint32_t point_count = 0;
                    const std::optional<std::string> count_arg =
                        get_argument(
                            request,
                            "loft_profile_points"
                        );
                    if (
                        !count_arg ||
                        !parse_uint32(*count_arg, point_count)
                    )
                    {
                        return json_error(
                            "loft requires loft_profile_points"
                        );
                    }
                    const std::optional<std::string> profiles_arg =
                        get_argument(request, "loft_profiles");
                    std::vector<
                        std::vector<math::Vector2>
                    > profiles;
                    if (
                        !profiles_arg ||
                        !parse_profile_set(
                            *profiles_arg,
                            static_cast<uint32_t>(
                                path_points.size()
                            ),
                            point_count,
                            profiles
                        )
                    )
                    {
                        return json_error(
                            "loft_profiles do not match the path"
                        );
                    }
                    for (
                        std::vector<math::Vector2>& profile :
                        profiles
                    )
                    {
                        if (
                            !profile_has_distinct_neighbors(
                                profile,
                                true
                            ) ||
                            !profile_is_counter_clockwise(profile)
                        )
                        {
                            return json_error(
                                "loft profiles must be distinct and counter clockwise"
                            );
                        }
                    }
                    if (!sweep_scales.empty())
                    {
                        for (size_t i = 0; i < profiles.size(); i++)
                        {
                            for (
                                math::Vector2& point :
                                profiles[i]
                            )
                            {
                                point *= sweep_scales[i];
                            }
                        }
                    }
                    geometry_generation::generate_loft(
                        &vertices,
                        &indices,
                        path_points,
                        profiles,
                        sweep_twists
                    );
                }
            }
            else if (shape == "arch")
            {
                float thickness = size.x * 0.15f;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "thickness")
                )
                {
                    if (!parse_float(*value, thickness))
                    {
                        return json_error("invalid thickness");
                    }
                }
                if (
                    size.y <= size.x * 0.5f ||
                    thickness <= 0.0f ||
                    thickness >= size.x * 0.5f ||
                    segments < 3 ||
                    segments > 64
                )
                {
                    return json_error("invalid arch dimensions or segments");
                }
                geometry_generation::generate_arch(
                    &vertices,
                    &indices,
                    size.x,
                    size.y,
                    size.z,
                    thickness,
                    segments
                );
            }
            else if (shape == "inset_panel")
            {
                float border = std::min(size.x, size.y) * 0.1f;
                float inset = size.z * 0.12f;
                float bevel = std::min(
                    { size.x, size.y, size.z }
                ) * 0.08f;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "border")
                )
                {
                    if (!parse_float(*value, border))
                    {
                        return json_error("invalid border");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "inset")
                )
                {
                    if (!parse_float(*value, inset))
                    {
                        return json_error("invalid inset");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "bevel")
                )
                {
                    if (!parse_float(*value, bevel))
                    {
                        return json_error("invalid bevel");
                    }
                }
                if (
                    border <= 0.0f ||
                    border * 2.0f >= std::min(size.x, size.y) ||
                    inset <= 0.0f ||
                    bevel <= 0.0f ||
                    bevel >= std::min({
                        size.x,
                        size.y,
                        size.z
                    }) * 0.5f
                )
                {
                    return json_error("invalid inset panel dimensions");
                }
                geometry_generation::generate_inset_panel(
                    &vertices,
                    &indices,
                    size,
                    border,
                    inset,
                    bevel
                );
            }
            else if (shape == "tapered_extrusion")
            {
                const std::optional<std::string> profile_arg =
                    get_argument(request, "profile");
                std::vector<math::Vector2> profile;
                if (
                    !profile_arg ||
                    !parse_profile(*profile_arg, profile) ||
                    !profile_is_convex_counter_clockwise(profile)
                )
                {
                    return json_error(
                        "tapered extrusion requires a convex counter clockwise profile"
                    );
                }
                float depth = size.z;
                float scale_start = 1.0f;
                float scale_end = 0.5f;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "depth")
                )
                {
                    if (!parse_float(*value, depth))
                    {
                        return json_error("invalid depth");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "scale_start")
                )
                {
                    if (!parse_float(*value, scale_start))
                    {
                        return json_error("invalid scale_start");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "scale_end")
                )
                {
                    if (!parse_float(*value, scale_end))
                    {
                        return json_error("invalid scale_end");
                    }
                }
                if (
                    depth <= 0.0f ||
                    scale_start <= 0.0f ||
                    scale_end <= 0.0f ||
                    scale_start > 100.0f ||
                    scale_end > 100.0f
                )
                {
                    return json_error(
                        "invalid tapered extrusion dimensions"
                    );
                }
                geometry_generation::generate_tapered_extrusion(
                    &vertices,
                    &indices,
                    profile,
                    depth,
                    scale_start,
                    scale_end
                );
            }
            else if (
                shape == "extruded_profile" ||
                shape == "revolved_profile"
            )
            {
                const std::optional<std::string> profile_arg =
                    get_argument(request, "profile");
                std::vector<math::Vector2> profile;
                if (
                    !profile_arg ||
                    !parse_profile(*profile_arg, profile)
                )
                {
                    return json_error(
                        "profile must contain between 3 and 32 finite 2d points"
                    );
                }

                if (shape == "extruded_profile")
                {
                    if (!profile_is_counter_clockwise(profile))
                    {
                        return json_error(
                            "extruded profile must be simple and counter clockwise"
                        );
                    }

                    float depth = 0.1f;
                    if (
                        const std::optional<std::string> depth_arg =
                            get_argument(request, "depth")
                    )
                    {
                        if (!parse_float(*depth_arg, depth))
                        {
                            return json_error("invalid depth");
                        }
                    }
                    if (depth <= 0.0f || depth > 1000.0f)
                    {
                        return json_error(
                            "depth must be between 0 and 1000"
                        );
                    }
                    geometry_generation::generate_extruded_profile(
                        &vertices,
                        &indices,
                        profile,
                        depth
                    );
                }
                else
                {
                    if (
                        !profile_has_valid_revolve_tangents(
                            profile
                        )
                    )
                    {
                        return json_error(
                            "revolved profile contains duplicate or backtracking points"
                        );
                    }
                    if (segments < 3 || segments > 64)
                    {
                        return json_error(
                            "revolved profile segments must be between 3 and 64"
                        );
                    }
                    bool has_positive_radius = false;
                    for (const math::Vector2& point : profile)
                    {
                        if (point.x < 0.0f || point.x > 1000.0f)
                        {
                            return json_error(
                                "revolved profile radii must be between 0 and 1000"
                            );
                        }
                        has_positive_radius |= point.x > 0.0001f;
                    }
                    if (!has_positive_radius)
                    {
                        return json_error(
                            "revolved profile requires a positive radius"
                        );
                    }
                    geometry_generation::generate_revolved_profile(
                        &vertices,
                        &indices,
                        profile,
                        segments
                    );
                }
            }
            else
            {
                return json_error("unsupported parametric shape");
            }

            std::vector<std::string> applied_modifiers;
            math::Vector3 modifier_pivot = math::Vector3::Zero;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "modifier_pivot")
            )
            {
                if (!parse_vector3(*value, modifier_pivot))
                {
                    return json_error("invalid modifier_pivot");
                }
            }

            if (
                get_argument(request, "taper_start") ||
                get_argument(request, "taper_end")
            )
            {
                float taper_start = 1.0f;
                float taper_end = 1.0f;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "taper_start")
                )
                {
                    if (!parse_float(*value, taper_start))
                    {
                        return json_error("invalid taper_start");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "taper_end")
                )
                {
                    if (!parse_float(*value, taper_end))
                    {
                        return json_error("invalid taper_end");
                    }
                }
                const std::string axis_name =
                    get_argument(request, "taper_axis").value_or("y");
                const auto selected_axis =
                    geometry_axis_from_name(to_lower_copy(axis_name));
                if (!selected_axis)
                {
                    return json_error("invalid taper_axis");
                }
                const auto result = mcp_geometry_kernel::taper(
                    vertices,
                    indices,
                    *selected_axis,
                    taper_start,
                    taper_end,
                    modifier_pivot
                );
                if (!result.succeeded())
                {
                    return json_error(
                        "taper modifier failed, " + result.message
                    );
                }
                applied_modifiers.emplace_back("taper");
            }

            if (
                const std::optional<std::string> value =
                    get_argument(request, "bend_degrees")
            )
            {
                float bend_degrees = 0.0f;
                if (!parse_float(*value, bend_degrees))
                {
                    return json_error("invalid bend_degrees");
                }
                const auto length_axis = geometry_axis_from_name(
                    to_lower_copy(
                        get_argument(
                            request,
                            "bend_axis"
                        ).value_or("x")
                    )
                );
                const auto radial_axis = geometry_axis_from_name(
                    to_lower_copy(
                        get_argument(
                            request,
                            "bend_radial_axis"
                        ).value_or("z")
                    )
                );
                if (!length_axis || !radial_axis)
                {
                    return json_error("invalid bend axis");
                }
                const auto result = mcp_geometry_kernel::bend(
                    vertices,
                    indices,
                    *length_axis,
                    *radial_axis,
                    bend_degrees * math::deg_to_rad,
                    modifier_pivot
                );
                if (!result.succeeded())
                {
                    return json_error(
                        "bend modifier failed, " + result.message
                    );
                }
                applied_modifiers.emplace_back("bend");
            }

            if (
                const std::optional<std::string> value =
                    get_argument(request, "mirror_axis")
            )
            {
                const auto selected_axis =
                    geometry_axis_from_name(to_lower_copy(*value));
                float mirror_plane = 0.0f;
                if (
                    const std::optional<std::string> plane =
                        get_argument(request, "mirror_plane")
                )
                {
                    if (!parse_float(*plane, mirror_plane))
                    {
                        return json_error("invalid mirror_plane");
                    }
                }
                if (!selected_axis)
                {
                    return json_error("invalid mirror_axis");
                }
                const auto result = mcp_geometry_kernel::mirror(
                    vertices,
                    indices,
                    *selected_axis,
                    mirror_plane
                );
                if (!result.succeeded())
                {
                    return json_error(
                        "mirror modifier failed, " + result.message
                    );
                }
                applied_modifiers.emplace_back("mirror");
            }

            if (
                const std::optional<std::string> value =
                    get_argument(request, "shell_thickness")
            )
            {
                float thickness = 0.0f;
                if (
                    !parse_float(*value, thickness) ||
                    thickness <= 0.0f
                )
                {
                    return json_error("invalid shell_thickness");
                }
                const auto result = mcp_geometry_kernel::solidify(
                    vertices,
                    indices,
                    thickness
                );
                if (!result.succeeded())
                {
                    return json_error(
                        "shell modifier failed, " + result.message
                    );
                }
                applied_modifiers.emplace_back("shell");
            }

            uint32_t linear_count = 1;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "linear_count")
            )
            {
                if (
                    !parse_uint32(*value, linear_count) ||
                    linear_count < 1 ||
                    linear_count > 128
                )
                {
                    return json_error("invalid linear_count");
                }
            }
            if (linear_count > 1)
            {
                math::Vector3 step = math::Vector3::Zero;
                const std::optional<std::string> value =
                    get_argument(request, "linear_step");
                if (!value || !parse_vector3(*value, step))
                {
                    return json_error(
                        "linear array requires linear_step"
                    );
                }
                std::vector<RHI_Vertex_PosTexNorTan> output_vertices;
                std::vector<uint32_t> output_indices;
                const auto result =
                    mcp_geometry_kernel::linear_array(
                        vertices,
                        indices,
                        linear_count,
                        step,
                        output_vertices,
                        output_indices
                    );
                if (!result.succeeded())
                {
                    return json_error(
                        "linear array failed, " + result.message
                    );
                }
                vertices = std::move(output_vertices);
                indices = std::move(output_indices);
                applied_modifiers.emplace_back("linear_array");
            }

            uint32_t radial_count = 1;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "radial_count")
            )
            {
                if (
                    !parse_uint32(*value, radial_count) ||
                    radial_count < 1 ||
                    radial_count > 128
                )
                {
                    return json_error("invalid radial_count");
                }
            }
            if (radial_count > 1)
            {
                float step_degrees =
                    360.0f / static_cast<float>(radial_count);
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "radial_step_degrees")
                )
                {
                    if (!parse_float(*value, step_degrees))
                    {
                        return json_error(
                            "invalid radial_step_degrees"
                        );
                    }
                }
                const auto selected_axis =
                    geometry_axis_from_name(
                        to_lower_copy(
                            get_argument(
                                request,
                                "radial_axis"
                            ).value_or("y")
                        )
                    );
                if (!selected_axis)
                {
                    return json_error("invalid radial_axis");
                }
                std::vector<RHI_Vertex_PosTexNorTan> output_vertices;
                std::vector<uint32_t> output_indices;
                const auto result =
                    mcp_geometry_kernel::radial_array(
                        vertices,
                        indices,
                        radial_count,
                        *selected_axis,
                        step_degrees * math::deg_to_rad,
                        modifier_pivot,
                        output_vertices,
                        output_indices
                    );
                if (!result.succeeded())
                {
                    return json_error(
                        "radial array failed, " + result.message
                    );
                }
                vertices = std::move(output_vertices);
                indices = std::move(output_indices);
                applied_modifiers.emplace_back("radial_array");
            }

            if (
                const std::optional<std::string> projection =
                    get_argument(request, "uv_projection")
            )
            {
                math::Vector2 uv_scale = math::Vector2::One;
                math::Vector2 uv_offset = math::Vector2::Zero;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "uv_scale")
                )
                {
                    if (!parse_vector2(*value, uv_scale))
                    {
                        return json_error("invalid uv_scale");
                    }
                }
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "uv_offset")
                )
                {
                    if (!parse_vector2(*value, uv_offset))
                    {
                        return json_error("invalid uv_offset");
                    }
                }
                const auto selected_axis =
                    geometry_axis_from_name(
                        to_lower_copy(
                            get_argument(
                                request,
                                "uv_axis"
                            ).value_or("y")
                        )
                    );
                if (!selected_axis)
                {
                    return json_error("invalid uv_axis");
                }
                mcp_geometry_kernel::operation_result result;
                const std::string projection_name =
                    to_lower_copy(*projection);
                if (projection_name == "planar")
                {
                    result =
                        mcp_geometry_kernel::project_uv_planar(
                            vertices,
                            indices,
                            *selected_axis,
                            uv_scale,
                            uv_offset
                        );
                }
                else if (projection_name == "box")
                {
                    bool split_seams = false;
                    if (
                        const std::optional<std::string> value =
                            get_argument(request, "uv_split_seams")
                    )
                    {
                        if (!parse_bool(*value, split_seams))
                        {
                            return json_error(
                                "invalid uv_split_seams"
                            );
                        }
                    }
                    if (split_seams)
                    {
                        result =
                            mcp_geometry_kernel::project_uv_box_seamed(
                                vertices,
                                indices,
                                uv_scale,
                                uv_offset
                            );
                    }
                    else
                    {
                        result =
                            mcp_geometry_kernel::project_uv_box(
                                vertices,
                                indices,
                                uv_scale,
                                uv_offset
                            );
                    }
                }
                else if (projection_name == "cylindrical")
                {
                    result =
                        mcp_geometry_kernel::project_uv_cylindrical(
                            vertices,
                            indices,
                            *selected_axis,
                            modifier_pivot,
                            uv_scale,
                            uv_offset
                        );
                }
                else
                {
                    return json_error("invalid uv_projection");
                }
                if (!result.succeeded())
                {
                    return json_error(
                        "uv projection failed, " + result.message
                    );
                }
                applied_modifiers.emplace_back(
                    "uv_" + projection_name
                );
            }

            const auto validation = mcp_geometry_kernel::validate(
                vertices,
                indices
            );
            if (!validation.succeeded())
            {
                return json_error(
                    "generated geometry is invalid, " +
                    validation.message
                );
            }

            if (
                vertices.empty() ||
                indices.empty() ||
                vertices.size() > 100000 ||
                indices.size() > 300000
            )
            {
                return json_error(
                    "generated geometry is empty or exceeds the mesh budget"
                );
            }

            const std::filesystem::path file_path(path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(
                    file_path.parent_path()
                );
            }

            std::shared_ptr<Mesh> mesh = std::make_shared<Mesh>();
            mesh->SetResourceFilePath(path);
            mesh->SetFlag(
                static_cast<uint32_t>(
                    MeshFlags::PostProcessOptimize
                ),
                false
            );
            mesh->AddGeometry(vertices, indices, false);
            mesh->SaveToFile(path);
            if (!FileSystem::IsFile(path))
            {
                return json_error(
                    "failed to save generated mesh"
                );
            }

            std::shared_ptr<Mesh> cached =
                ResourceCache::Cache(mesh);
            if (!cached)
            {
                return json_error("failed to cache generated mesh");
            }
            cached->CreateGpuBuffers();

            std::string json = "{\"ok\":true,\"reused\":false";
            json += ",\"shape\":" + json_string(shape);
            json += ",\"vertex_count\":" +
                std::to_string(vertices.size());
            json += ",\"index_count\":" +
                std::to_string(indices.size());
            json += ",\"modifiers\":[";
            for (
                size_t index = 0;
                index < applied_modifiers.size();
                index++
            )
            {
                if (index != 0)
                {
                    json += ",";
                }
                json += json_string(applied_modifiers[index]);
            }
            json += "]";
            json += ",\"resource\":" +
                resource_to_json(cached.get());
            json += "}";
            return json;
        }

        std::string command_mesh_generate_batch(
            const McpRequest& request
        )
        {
            const std::optional<std::string> count_arg =
                get_argument(request, "count");
            uint64_t count = 0;
            if (
                !count_arg ||
                !parse_uint64(*count_arg, count) ||
                count == 0 ||
                count > 32
            )
            {
                return json_error("count must be between 1 and 32");
            }

            const std::vector<std::string> keys =
            {
                "shape",
                "path",
                "size",
                "radius",
                "bevel",
                "segments",
                "profile",
                "depth",
                "height",
                "major_radius",
                "minor_radius",
                "minor_segments",
                "bevel_segments",
                "path_points",
                "thickness",
                "border",
                "inset",
                "scale_start",
                "scale_end",
                "reuse_existing"
            };

            std::string generated_json = "[";
            uint32_t generated_count = 0;
            for (uint64_t i = 0; i < count; i++)
            {
                McpRequest item_request;
                item_request.command = "mesh_generate";
                for (const std::string& key : keys)
                {
                    const std::string batch_key =
                        "item_" + std::to_string(i) + "_" + key;
                    const auto it =
                        request.arguments.find(batch_key);
                    if (it != request.arguments.end())
                    {
                        item_request.arguments[key] = it->second;
                    }
                }

                const std::string item_result =
                    command_mesh_generate(item_request);
                if (
                    item_result.find("\"ok\":true") ==
                    std::string::npos
                )
                {
                    generated_json += "]";
                    std::string json =
                        "{\"ok\":false,\"error\":\"failed to generate mesh batch item\"";
                    json += ",\"generated\":" + generated_json;
                    json += ",\"generated_count\":" +
                        std::to_string(generated_count);
                    json += ",\"failed_index\":" +
                        std::to_string(i);
                    json += ",\"failure\":" + item_result;
                    json += "}";
                    return json;
                }

                if (generated_count > 0)
                {
                    generated_json += ",";
                }
                generated_json += item_result;
                generated_count++;
            }

            std::string json =
                "{\"ok\":true,\"generated\":" +
                generated_json +
                "]";
            json += ",\"generated_count\":" +
                std::to_string(generated_count);
            json += "}";
            return json;
        }

        std::string command_render_set_mesh(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("mesh assignment requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            const std::optional<std::string> mesh_arg =
                get_argument(request, "mesh");
            if (!mesh_arg || mesh_arg->empty())
            {
                return json_error("missing mesh");
            }

            std::shared_ptr<IResource> resource =
                get_resource_shared_by_name_or_path(
                    *mesh_arg,
                    ResourceType::Mesh
                );
            if (!resource && FileSystem::IsFile(*mesh_arg))
            {
                resource = ResourceCache::Load<Mesh>(*mesh_arg);
            }
            if (!resource)
            {
                return json_error(
                    "mesh not found by cached name, cached path, or file path"
                );
            }

            const std::shared_ptr<Mesh> mesh =
                std::static_pointer_cast<Mesh>(resource);
            uint32_t sub_mesh_index = 0;
            if (
                const std::optional<std::string> sub_mesh_arg =
                    get_argument(request, "sub_mesh_index")
            )
            {
                if (
                    !parse_uint32(
                        *sub_mesh_arg,
                        sub_mesh_index
                    ) ||
                    sub_mesh_index >= mesh->GetSubMeshCount()
                )
                {
                    return json_error("invalid sub_mesh_index");
                }
            }

            Render* renderable = entity->GetComponent<Render>();
            if (renderable == nullptr)
            {
                renderable = entity->AddComponent<Render>();
            }
            if (renderable == nullptr)
            {
                return json_error("failed to add render component");
            }

            renderable->SetMesh(
                mesh.get(),
                sub_mesh_index
            );
            if (renderable->GetMaterial() == nullptr)
            {
                renderable->SetDefaultMaterial();
            }

            if (
                const std::optional<std::string> material =
                    get_argument(request, "material")
            )
            {
                std::string material_error;
                if (
                    !assign_render_material(
                        renderable,
                        *material,
                        material_error
                    )
                )
                {
                    return json_error(material_error);
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" +
                entity_to_json_compact(entity);
            json += ",\"mesh\":" +
                resource_to_json(mesh.get());
            json += ",\"sub_mesh_index\":" +
                std::to_string(sub_mesh_index);
            json += "}";
            return json;
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

            bool with_physics = true;
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
            std::optional<bool> physics_static = true;
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
                std::string material_error;
                if (!assign_render_material(renderable, *material, material_error))
                {
                    return json_error(material_error);
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

        std::string command_entity_set_transform_batch(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("transform requires edit mode");
            }

            const std::optional<std::string> count_arg = get_argument(request, "count");
            uint64_t count = 0;
            if (!count_arg || !parse_uint64(*count_arg, count) || count == 0 || count > 64)
            {
                return json_error("count must be between 1 and 64");
            }

            const std::vector<std::string> keys =
            {
                "id",
                "position",
                "rotation",
                "rotation_euler",
                "scale"
            };

            std::string updated_json = "[";
            uint32_t updated_count = 0;
            for (uint64_t i = 0; i < count; i++)
            {
                McpRequest item_request;
                item_request.command = "entity_set_transform";
                for (const std::string& key : keys)
                {
                    const std::string batch_key = "item_" + std::to_string(i) + "_" + key;
                    const auto it = request.arguments.find(batch_key);
                    if (it != request.arguments.end())
                    {
                        item_request.arguments[key] = it->second;
                    }
                }

                const std::string item_result = command_entity_set_transform(item_request);
                if (item_result.find("\"ok\":true") == std::string::npos)
                {
                    updated_json += "]";
                    std::string json = "{\"ok\":false,\"error\":\"failed to set transform batch item\"";
                    json += ",\"updated\":" + updated_json;
                    json += ",\"updated_count\":" + std::to_string(updated_count);
                    json += ",\"failed_index\":" + std::to_string(i);
                    json += ",\"failure\":" + item_result;
                    json += "}";
                    return json;
                }

                if (updated_count > 0)
                {
                    updated_json += ",";
                }
                updated_json += item_result;
                updated_count++;
            }

            std::string json = "{\"ok\":true,\"updated\":" + updated_json + "]";
            json += ",\"updated_count\":" + std::to_string(updated_count);
            json += "}";
            return json;
        }

        std::string command_spline_query(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }

            Entity* entity = nullptr;
            if (get_argument(request, "id"))
            {
                std::string error;
                entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return json_error(error);
                }
            }
            else
            {
                // no id given, prefer the spline that has followers, fall back to any spline
                Entity* any_spline = nullptr;
                for (Entity* candidate : World::GetEntities())
                {
                    if (SplineFollower* follower = candidate->GetComponent<SplineFollower>())
                    {
                        if (Entity* followed = World::GetEntityById(follower->GetSplineEntityId()))
                        {
                            entity = followed;
                            break;
                        }
                    }
                    if (!any_spline && candidate->GetComponent<Spline>())
                    {
                        any_spline = candidate;
                    }
                }
                if (entity == nullptr)
                {
                    entity = any_spline;
                }
                if (entity == nullptr)
                {
                    return json_error("no spline entity in the world, pass id to pick one");
                }
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (spline == nullptr)
            {
                return json_error("entity does not have a spline component");
            }

            const float length = spline->GetLength();

            std::string json = "{\"ok\":true";
            json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"length\":" + std::to_string(length);
            json += ",\"closed_loop\":" + json_bool(spline->GetClosedLoop());

            const std::vector<math::Vector3> control_points = spline->GetControlPoints();
            json += ",\"control_points\":[";
            for (size_t i = 0; i < control_points.size(); i++)
            {
                if (i > 0)
                {
                    json += ",";
                }
                json += json_vector3(control_points[i]);
            }
            json += "]";

            // every follower that is set to follow this spline, travel time makes camera cut math trivial
            float follower_speed = 0.0f;
            json += ",\"followers\":[";
            bool first = true;
            for (Entity* candidate : World::GetEntities())
            {
                SplineFollower* follower = candidate->GetComponent<SplineFollower>();
                if (!follower || follower->GetSplineEntityId() != entity->GetObjectId())
                {
                    continue;
                }
                if (follower_speed == 0.0f)
                {
                    follower_speed = follower->GetSpeed();
                }
                if (!first)
                {
                    json += ",";
                }
                first = false;
                json += "{\"id\":" + json_string(std::to_string(candidate->GetObjectId()));
                json += ",\"name\":" + json_string(candidate->GetObjectName());
                json += ",\"speed\":" + std::to_string(follower->GetSpeed());
                json += ",\"follow_mode\":" + json_string(spline_follow_mode_to_name(follower->GetFollowMode()));
                json += ",\"align_to_spline\":" + json_bool(follower->GetAlignToSpline());
                json += ",\"flip_forward\":" + json_bool(follower->GetFlipForward());
                json += ",\"progress\":" + std::to_string(follower->GetProgress());
                json += ",\"position\":" + json_vector3(candidate->GetPosition());
                json += ",\"travel_time_seconds\":" + (follower->GetSpeed() > 0.0f ? std::to_string(length / follower->GetSpeed()) : std::string("null"));
                json += "}";
            }
            json += "]";

            // targets to project onto the spline, explicit list or every camera in the world by default
            std::vector<Entity*> targets;
            std::vector<std::string> unresolved;
            if (const std::optional<std::string> closest_to = get_argument(request, "closest_to"))
            {
                std::stringstream tokens(*closest_to);
                std::string token;
                while (std::getline(tokens, token, ','))
                {
                    if (token.empty())
                    {
                        continue;
                    }

                    Entity* target = nullptr;
                    uint64_t target_id = 0;
                    if (parse_uint64(token, target_id))
                    {
                        target = World::GetEntityById(target_id);
                    }
                    else
                    {
                        std::string resolve_error;
                        target = find_entity_by_name_unique(token, true, resolve_error);
                        if (target == nullptr)
                        {
                            target = find_entity_by_name_unique(token, false, resolve_error);
                        }
                    }

                    if (target != nullptr)
                    {
                        targets.push_back(target);
                    }
                    else
                    {
                        unresolved.push_back(token);
                    }
                }
            }
            else
            {
                for (Entity* candidate : World::GetEntities())
                {
                    if (candidate->GetComponent<Camera>())
                    {
                        targets.push_back(candidate);
                    }
                }
            }

            // closest point on the spline for each target, arc distance and pass time give exact camera cut moments
            if (!targets.empty() || !unresolved.empty())
            {
                const uint32_t sample_count = 512;

                // sample once, keep cumulative arc length per sample
                std::vector<math::Vector3> samples(sample_count + 1);
                std::vector<float> arc_lengths(sample_count + 1, 0.0f);
                for (uint32_t i = 0; i <= sample_count; i++)
                {
                    samples[i] = spline->GetPoint(static_cast<float>(i) / static_cast<float>(sample_count));
                    if (i > 0)
                    {
                        arc_lengths[i] = arc_lengths[i - 1] + samples[i].Distance(samples[i - 1]);
                    }
                }

                json += ",\"closest\":[";
                first = true;
                for (const std::string& token : unresolved)
                {
                    if (!first)
                    {
                        json += ",";
                    }
                    first = false;
                    json += "{\"query\":" + json_string(token) + ",\"error\":\"entity not found\"}";
                }
                for (Entity* target : targets)
                {
                    if (!first)
                    {
                        json += ",";
                    }
                    first = false;

                    const math::Vector3 target_position = target->GetPosition();
                    uint32_t best_index = 0;
                    float best_distance_sq = std::numeric_limits<float>::max();
                    for (uint32_t i = 0; i <= sample_count; i++)
                    {
                        const float distance_sq = (samples[i] - target_position).LengthSquared();
                        if (distance_sq < best_distance_sq)
                        {
                            best_distance_sq = distance_sq;
                            best_index       = i;
                        }
                    }

                    json += "{\"id\":" + json_string(std::to_string(target->GetObjectId()));
                    json += ",\"name\":" + json_string(target->GetObjectName());
                    json += ",\"position\":" + json_vector3(target_position);
                    json += ",\"t\":" + std::to_string(static_cast<float>(best_index) / static_cast<float>(sample_count));
                    json += ",\"arc_distance\":" + std::to_string(arc_lengths[best_index]);
                    json += ",\"distance_to_spline\":" + std::to_string((samples[best_index] - target_position).Length());
                    json += ",\"spline_position\":" + json_vector3(samples[best_index]);
                    json += ",\"pass_time_seconds\":" + (follower_speed > 0.0f ? std::to_string(arc_lengths[best_index] / follower_speed) : std::string("null"));
                    json += "}";
                }
                json += "]";
            }

            json += "}";
            return json;
        }

        std::string command_spline_distribute(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("distributing entities requires edit mode");
            }

            Entity* entity = nullptr;
            if (get_argument(request, "id"))
            {
                std::string error;
                entity = get_entity_from_request(request, error);
                if (entity == nullptr)
                {
                    return json_error(error);
                }
            }
            else
            {
                for (Entity* candidate : World::GetEntities())
                {
                    if (candidate->GetComponent<Spline>())
                    {
                        entity = candidate;
                        break;
                    }
                }
                if (entity == nullptr)
                {
                    return json_error("no spline entity in the world, pass id to pick one");
                }
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (spline == nullptr)
            {
                return json_error("entity does not have a spline component");
            }

            const float length = spline->GetLength();
            if (length <= 0.0f)
            {
                return json_error("spline has no length");
            }

            // targets, explicit comma separated list or every camera child of the spline entity
            std::vector<Entity*> targets;
            if (const std::optional<std::string> entities = get_argument(request, "entities"))
            {
                std::stringstream tokens(*entities);
                std::string token;
                while (std::getline(tokens, token, ','))
                {
                    if (token.empty())
                    {
                        continue;
                    }

                    Entity* target = nullptr;
                    uint64_t target_id = 0;
                    if (parse_uint64(token, target_id))
                    {
                        target = World::GetEntityById(target_id);
                    }
                    else
                    {
                        std::string resolve_error;
                        target = find_entity_by_name_unique(token, true, resolve_error);
                        if (target == nullptr)
                        {
                            target = find_entity_by_name_unique(token, false, resolve_error);
                        }
                    }

                    if (target == nullptr)
                    {
                        return json_error("entity not found: " + token);
                    }
                    targets.push_back(target);
                }
            }
            else
            {
                for (Entity* child : entity->GetChildren())
                {
                    if (child->GetComponent<Camera>())
                    {
                        targets.push_back(child);
                    }
                }
            }

            if (targets.size() < 2)
            {
                return json_error("need at least two entities to distribute");
            }

            // sample once for closest point lookups
            const uint32_t sample_count = 512;
            std::vector<math::Vector3> samples(sample_count + 1);
            std::vector<float> arc_lengths(sample_count + 1, 0.0f);
            for (uint32_t i = 0; i <= sample_count; i++)
            {
                samples[i] = spline->GetPoint(static_cast<float>(i) / static_cast<float>(sample_count));
                if (i > 0)
                {
                    arc_lengths[i] = arc_lengths[i - 1] + samples[i].Distance(samples[i - 1]);
                }
            }

            auto closest_arc_distance = [&](const math::Vector3& position)
            {
                uint32_t best_index = 0;
                float best_distance_sq = std::numeric_limits<float>::max();
                for (uint32_t i = 0; i <= sample_count; i++)
                {
                    const float distance_sq = (samples[i] - position).LengthSquared();
                    if (distance_sq < best_distance_sq)
                    {
                        best_distance_sq = distance_sq;
                        best_index       = i;
                    }
                }
                return arc_lengths[best_index];
            };

            // keep the order the entities already have along the road
            std::sort(targets.begin(), targets.end(), [&](Entity* a, Entity* b)
            {
                return closest_arc_distance(a->GetPosition()) < closest_arc_distance(b->GetPosition());
            });

            // optional overrides, lateral is signed meters from the centerline, positive is the right of travel
            float lateral_override = 0.0f;
            bool has_lateral = false;
            if (const std::optional<std::string> lateral_arg = get_argument(request, "lateral_offset"))
            {
                if (!parse_float(*lateral_arg, lateral_override))
                {
                    return json_error("invalid lateral_offset");
                }
                has_lateral = true;
            }

            // edge offset is signed meters beyond the road edge, it tracks the varying road width so entities always clear the asphalt
            float edge_offset = 0.0f;
            bool has_edge = false;
            if (const std::optional<std::string> edge_arg = get_argument(request, "edge_offset"))
            {
                if (!parse_float(*edge_arg, edge_offset))
                {
                    return json_error("invalid edge_offset");
                }
                has_edge = true;
            }
            float height_override = 0.0f;
            bool has_height = false;
            if (const std::optional<std::string> height_arg = get_argument(request, "height"))
            {
                if (!parse_float(*height_arg, height_override))
                {
                    return json_error("invalid height");
                }
                has_height = true;
            }

            std::string json = "{\"ok\":true";
            json += ",\"id\":" + json_string(std::to_string(entity->GetObjectId()));
            json += ",\"name\":" + json_string(entity->GetObjectName());
            json += ",\"length\":" + std::to_string(length);
            json += ",\"placed\":[";

            // closed loops space n slots, open splines include both ends
            const float step = spline->GetClosedLoop() ? length / static_cast<float>(targets.size()) : length / static_cast<float>(targets.size() - 1);
            for (size_t i = 0; i < targets.size(); i++)
            {
                Entity* target = targets[i];

                // preserve each entity offset relative to the spline frame so framing survives the move
                const math::Vector3 old_position = target->GetPosition();
                const float old_t                = spline->GetTAtDistance(closest_arc_distance(old_position), 32);
                const math::Vector3 old_point    = spline->GetPoint(old_t);
                math::Vector3 old_tangent        = spline->GetTangent(old_t);
                old_tangent.Normalize();

                const float new_distance      = static_cast<float>(i) * step;
                const float new_t             = spline->GetTAtDistance(new_distance, 32);
                const math::Vector3 new_point = spline->GetPoint(new_t);
                math::Vector3 new_tangent     = spline->GetTangent(new_t);
                new_tangent.Normalize();

                const math::Quaternion old_frame = math::Quaternion::FromLookRotation(old_tangent, math::Vector3::Up);
                const math::Quaternion new_frame = math::Quaternion::FromLookRotation(new_tangent, math::Vector3::Up);
                const math::Quaternion delta     = new_frame * old_frame.Inverse();

                if (has_lateral || has_edge || has_height)
                {
                    math::Vector3 old_right = old_tangent.Cross(math::Vector3::Up);
                    old_right.Normalize();
                    math::Vector3 new_right = new_tangent.Cross(math::Vector3::Up);
                    new_right.Normalize();

                    float lateral = has_lateral ? lateral_override : old_right.Dot(old_position - old_point);
                    if (has_edge)
                    {
                        // road width interpolates from start to end, sidewalk extends the surface further
                        float half_width = (spline->GetRoadWidth() + (spline->GetRoadWidthEnd() - spline->GetRoadWidth()) * new_t) * 0.5f;
                        if (spline->GetSidewalkEnabled())
                        {
                            half_width += spline->GetSidewalkWidth();
                        }
                        lateral = (edge_offset < 0.0f ? -1.0f : 1.0f) * half_width + edge_offset;
                    }
                    const float height = has_height ? height_override : old_position.y - old_point.y;
                    target->SetPosition(new_point + new_right * lateral + math::Vector3::Up * height);
                }
                else
                {
                    target->SetPosition(new_point + delta * (old_position - old_point));
                }
                target->SetRotation(delta * target->GetRotation());

                if (i > 0)
                {
                    json += ",";
                }
                json += "{\"id\":" + json_string(std::to_string(target->GetObjectId()));
                json += ",\"name\":" + json_string(target->GetObjectName());
                json += ",\"arc_distance\":" + std::to_string(new_distance);
                json += ",\"position\":" + json_vector3(target->GetPosition());
                json += "}";
            }
            json += "]}";
            return json;
        }

        bool parse_vector3_list(const std::string& value, std::vector<math::Vector3>& points)
        {
            std::vector<float> values;
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

            if (values.empty() || (values.size() % 3) != 0)
            {
                return false;
            }

            points.clear();
            points.reserve(values.size() / 3);
            for (size_t i = 0; i + 2 < values.size(); i += 3)
            {
                const math::Vector3 point(values[i], values[i + 1], values[i + 2]);
                if (!point.IsFinite())
                {
                    return false;
                }
                points.emplace_back(point);
            }
            return !points.empty();
        }

        std::vector<std::string> split_csv_tokens(const std::string& value)
        {
            std::vector<std::string> tokens;
            std::stringstream stream(value);
            std::string part;
            while (std::getline(stream, part, ','))
            {
                // trim spaces
                size_t start = 0;
                while (start < part.size() && std::isspace(static_cast<unsigned char>(part[start])))
                {
                    start++;
                }
                size_t end = part.size();
                while (end > start && std::isspace(static_cast<unsigned char>(part[end - 1])))
                {
                    end--;
                }
                if (end > start)
                {
                    tokens.emplace_back(part.substr(start, end - start));
                }
            }
            return tokens;
        }

        Entity* resolve_entity_token(const std::string& token, std::string& error)
        {
            uint64_t id = 0;
            if (parse_uint64(token, id))
            {
                Entity* entity = World::GetEntityById(id);
                if (entity == nullptr)
                {
                    error = "entity id not found";
                    return nullptr;
                }
                return entity;
            }

            Entity* entity = find_entity_by_name_unique(token, true, error);
            if (entity != nullptr)
            {
                return entity;
            }
            return find_entity_by_name_unique(token, false, error);
        }

        math::Vector3 world_to_local_point(Entity* entity, const math::Vector3& world_position)
        {
            if (entity == nullptr)
            {
                return world_position;
            }
            return entity->GetMatrix().Inverted() * world_position;
        }

        void clear_spline_control_points(Entity* entity)
        {
            if (entity == nullptr)
            {
                return;
            }

            for (uint32_t pass = 0; pass < 64; pass++)
            {
                entity->AcquireChildren();
                std::vector<Entity*> to_remove;
                for (Entity* child : entity->GetChildren())
                {
                    if (child == nullptr || !World::EntityExists(child))
                    {
                        continue;
                    }
                    if (child->GetObjectName().find("spline_point_") == 0)
                    {
                        to_remove.push_back(child);
                    }
                }
                if (to_remove.empty())
                {
                    break;
                }
                for (Entity* child : to_remove)
                {
                    World::RemoveEntityImmediate(child);
                }
            }
        }

        bool subtree_render_bounds(Entity* root, math::BoundingBox& out_bounds)
        {
            bool has_bounds = false;
            out_bounds = math::BoundingBox::Zero;
            if (root == nullptr)
            {
                return false;
            }

            std::vector<Entity*> entities;
            entities.push_back(root);
            root->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                if (entity == nullptr)
                {
                    continue;
                }
                if (Render* renderable = entity->GetComponent<Render>())
                {
                    const math::BoundingBox& box = renderable->GetBoundingBox();
                    if (box.GetMin().IsFinite() && box.GetMax().IsFinite() && box.GetSize().LengthSquared() > 0.0f)
                    {
                        if (!has_bounds)
                        {
                            out_bounds = box;
                            has_bounds = true;
                        }
                        else
                        {
                            out_bounds.Merge(box);
                        }
                    }
                }
            }
            return has_bounds;
        }

        bool is_landmark_noise(Entity* entity)
        {
            if (entity == nullptr)
            {
                return true;
            }

            const std::string name = to_lower_copy(entity->GetObjectName());
            if (
                name == "ground" || name == "default_ground" || name == "sun" || name == "directional_light" ||
                name == "camera" || name == "editor_camera" || name == "sky" || name.empty()
            )
            {
                return true;
            }
            if (entity->GetComponent<Camera>() != nullptr && entity->GetParent() == nullptr && entity->GetChildrenCount() == 0)
            {
                return true;
            }
            if (entity->GetComponent<Light>() != nullptr && entity->GetParent() == nullptr && entity->GetChildrenCount() == 0)
            {
                // bare root lights are not city landmarks
                if (name.find("light") != std::string::npos || name.find("sun") != std::string::npos)
                {
                    return true;
                }
            }
            return false;
        }

        std::string spline_road_receipt(Entity* entity, Spline* spline)
        {
            std::string json = "{\"ok\":true";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += ",\"point_count\":" + std::to_string(spline->GetControlPointCount());
            json += ",\"length\":" + std::to_string(spline->GetLength());
            json += ",\"road_width\":" + std::to_string(spline->GetRoadWidth());
            json += ",\"profile\":" + json_string(spline_profile_to_name(spline->GetProfile()));
            json += ",\"conform_to_terrain\":" + json_bool(spline->GetConformToTerrain());
            json += ",\"closed_loop\":" + json_bool(spline->GetClosedLoop());
            json += ",\"mesh_enabled\":" + json_bool(spline->GetMeshEnabled());
            json += "}";
            return json;
        }

        bool apply_spline_road_properties(Spline* spline, const McpRequest& request, std::string& error, bool apply_defaults)
        {
            if (const std::optional<std::string> profile = get_argument(request, "profile"))
            {
                const std::optional<SplineProfile> parsed = spline_profile_from_name(*profile);
                if (!parsed)
                {
                    error = "invalid profile";
                    return false;
                }
                spline->SetProfile(*parsed);
            }
            else if (apply_defaults)
            {
                spline->SetProfile(SplineProfile::Road);
            }

            if (const std::optional<std::string> road_width = get_argument(request, "road_width"))
            {
                float parsed = 0.0f;
                if (!parse_float(*road_width, parsed) || parsed <= 0.0f)
                {
                    error = "invalid road_width";
                    return false;
                }
                spline->SetRoadWidth(parsed);
            }

            if (const std::optional<std::string> conform_to_terrain = get_argument(request, "conform_to_terrain"))
            {
                bool conform = true;
                if (!parse_bool(*conform_to_terrain, conform))
                {
                    error = "invalid conform_to_terrain";
                    return false;
                }
                spline->SetConformToTerrain(conform);
            }
            else if (apply_defaults)
            {
                spline->SetConformToTerrain(true);
            }

            if (const std::optional<std::string> closed = get_argument(request, "closed_loop"))
            {
                bool closed_loop = false;
                if (!parse_bool(*closed, closed_loop))
                {
                    error = "invalid closed_loop";
                    return false;
                }
                spline->SetClosedLoop(closed_loop);
            }
            else if (apply_defaults)
            {
                spline->SetClosedLoop(false);
            }

            if (const std::optional<std::string> mesh = get_argument(request, "mesh_enabled"))
            {
                bool mesh_enabled = true;
                if (!parse_bool(*mesh, mesh_enabled))
                {
                    error = "invalid mesh_enabled";
                    return false;
                }
                spline->SetMeshEnabled(mesh_enabled);
            }
            else if (apply_defaults)
            {
                spline->SetMeshEnabled(true);
            }

            if (const std::optional<std::string> sidewalk = get_argument(request, "sidewalk_enabled"))
            {
                bool sidewalk_enabled = false;
                if (!parse_bool(*sidewalk, sidewalk_enabled))
                {
                    error = "invalid sidewalk_enabled";
                    return false;
                }
                spline->SetSidewalkEnabled(sidewalk_enabled);
            }

            if (const std::optional<std::string> sidewalk_width = get_argument(request, "sidewalk_width"))
            {
                float parsed = 0.0f;
                if (!parse_float(*sidewalk_width, parsed) || parsed < 0.0f)
                {
                    error = "invalid sidewalk_width";
                    return false;
                }
                spline->SetSidewalkWidth(parsed);
                if (parsed > 0.0f)
                {
                    spline->SetSidewalkEnabled(true);
                }
            }
            return true;
        }

        bool set_spline_control_points_world(Entity* entity, Spline* spline, const std::vector<math::Vector3>& world_points, bool append, std::string& error)
        {
            if (entity == nullptr || spline == nullptr)
            {
                error = "missing spline entity";
                return false;
            }
            if (world_points.size() < 2 && !append)
            {
                error = "need at least 2 control points";
                return false;
            }
            if (world_points.empty())
            {
                error = "missing control points";
                return false;
            }

            if (!append)
            {
                clear_spline_control_points(entity);
            }

            for (const math::Vector3& world_point : world_points)
            {
                spline->AddControlPoint(world_to_local_point(entity, world_point));
            }

            if (spline->GetControlPointCount() < 2)
            {
                error = "need at least 2 control points";
                return false;
            }

            if (spline->GetMeshEnabled())
            {
                spline->GenerateRoadMesh();
            }
            return true;
        }

        struct RoadObstacle
        {
            math::BoundingBox box;
            std::string name;
            uint64_t entity_id = 0;
        };

        math::BoundingBox expand_box_xz(const math::BoundingBox& box, float clearance)
        {
            math::Vector3 min = box.GetMin();
            math::Vector3 max = box.GetMax();
            min.x -= clearance;
            min.z -= clearance;
            max.x += clearance;
            max.z += clearance;
            return math::BoundingBox(min, max);
        }

        bool point_in_box_xz(const math::Vector3& point, const math::BoundingBox& box)
        {
            return point.x >= box.GetMin().x && point.x <= box.GetMax().x &&
                   point.z >= box.GetMin().z && point.z <= box.GetMax().z;
        }

        math::Vector3 approach_point_outside_box(const math::Vector3& from, const math::BoundingBox& box, float standoff, float y)
        {
            const math::Vector3 min = box.GetMin();
            const math::Vector3 max = box.GetMax();
            const math::Vector3 center((min.x + max.x) * 0.5f, y, (min.z + max.z) * 0.5f);
            math::Vector3 dir = from - center;
            dir.y = 0.0f;
            if (dir.LengthSquared() < 0.0001f)
            {
                dir = math::Vector3(1.0f, 0.0f, 0.0f);
            }
            else
            {
                dir.Normalize();
            }

            // pick the face the approach comes from, then stand off outside that face
            const float half_x = (max.x - min.x) * 0.5f;
            const float half_z = (max.z - min.z) * 0.5f;
            math::Vector3 approach = center;
            if (std::fabs(dir.x) * half_z >= std::fabs(dir.z) * half_x)
            {
                approach.x = center.x + dir.x * (half_x + standoff);
                approach.z = std::clamp(from.z, min.z, max.z);
            }
            else
            {
                approach.z = center.z + dir.z * (half_z + standoff);
                approach.x = std::clamp(from.x, min.x, max.x);
            }
            approach.y = y;

            // if still inside due to clamp, push fully outside on the dominant axis
            if (point_in_box_xz(approach, box))
            {
                approach = center + dir * (std::max(half_x, half_z) + standoff);
                approach.y = y;
            }
            return approach;
        }

        bool landmark_approach_point(Entity* landmark, const math::Vector3& from, float standoff, math::Vector3& out_point)
        {
            if (landmark == nullptr)
            {
                return false;
            }

            math::BoundingBox bounds;
            if (!subtree_render_bounds(landmark, bounds))
            {
                out_point = landmark->GetPosition();
                return true;
            }

            const math::Vector3 size = bounds.GetSize();
            // tiny landmarks can use their origin; districts must be approached at the edge
            if (size.x < 8.0f && size.z < 8.0f)
            {
                out_point = landmark->GetPosition();
                return true;
            }

            out_point = approach_point_outside_box(from, bounds, standoff, landmark->GetPosition().y);
            return true;
        }

        bool segment_intersects_box_xz(const math::Vector3& a, const math::Vector3& b, const math::BoundingBox& box)
        {
            if (point_in_box_xz(a, box) || point_in_box_xz(b, box))
            {
                return true;
            }

            const float min_x = box.GetMin().x;
            const float max_x = box.GetMax().x;
            const float min_z = box.GetMin().z;
            const float max_z = box.GetMax().z;
            const math::Vector3 corners[4] = {
                { min_x, 0.0f, min_z },
                { max_x, 0.0f, min_z },
                { max_x, 0.0f, max_z },
                { min_x, 0.0f, max_z },
            };

            auto orient = [](const math::Vector3& p, const math::Vector3& q, const math::Vector3& r)
            {
                const float value = (q.z - p.z) * (r.x - q.x) - (q.x - p.x) * (r.z - q.z);
                if (std::fabs(value) < 0.0001f)
                {
                    return 0;
                }
                return value > 0.0f ? 1 : 2;
            };
            auto on_segment = [](const math::Vector3& p, const math::Vector3& q, const math::Vector3& r)
            {
                return q.x <= std::max(p.x, r.x) && q.x >= std::min(p.x, r.x) &&
                       q.z <= std::max(p.z, r.z) && q.z >= std::min(p.z, r.z);
            };
            auto segments_intersect = [&](const math::Vector3& p1, const math::Vector3& q1, const math::Vector3& p2, const math::Vector3& q2)
            {
                const int o1 = orient(p1, q1, p2);
                const int o2 = orient(p1, q1, q2);
                const int o3 = orient(p2, q2, p1);
                const int o4 = orient(p2, q2, q1);
                if (o1 != o2 && o3 != o4)
                {
                    return true;
                }
                if (o1 == 0 && on_segment(p1, p2, q1))
                {
                    return true;
                }
                if (o2 == 0 && on_segment(p1, q2, q1))
                {
                    return true;
                }
                if (o3 == 0 && on_segment(p2, p1, q2))
                {
                    return true;
                }
                if (o4 == 0 && on_segment(p2, q1, q2))
                {
                    return true;
                }
                return false;
            };

            for (uint32_t i = 0; i < 4; i++)
            {
                if (segments_intersect(a, b, corners[i], corners[(i + 1) % 4]))
                {
                    return true;
                }
            }
            return false;
        }

        bool segment_blocked(const math::Vector3& a, const math::Vector3& b, const std::vector<RoadObstacle>& obstacles)
        {
            for (const RoadObstacle& obstacle : obstacles)
            {
                if (segment_intersects_box_xz(a, b, obstacle.box))
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<RoadObstacle> collect_road_obstacles(const std::vector<uint64_t>& ignore_ids, float clearance)
        {
            std::vector<RoadObstacle> obstacles;
            auto is_ignored = [&](uint64_t id)
            {
                for (uint64_t ignore_id : ignore_ids)
                {
                    if (id == ignore_id)
                    {
                        return true;
                    }
                }
                return false;
            };

            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr || entity->GetParent() != nullptr)
                {
                    continue;
                }
                if (is_landmark_noise(entity) || is_ignored(entity->GetObjectId()))
                {
                    continue;
                }

                math::BoundingBox bounds;
                if (Spline* spline = entity->GetComponent<Spline>())
                {
                    // other roads are obstacles too, use control point span so we do not cross them
                    const std::vector<math::Vector3> points = spline->GetControlPoints();
                    if (points.size() < 2)
                    {
                        continue;
                    }
                    bounds = math::BoundingBox(points.data(), static_cast<uint32_t>(points.size()));
                    math::Vector3 min = bounds.GetMin();
                    math::Vector3 max = bounds.GetMax();
                    const float half_width = std::max(2.0f, spline->GetRoadWidth() * 0.5f + clearance * 0.25f);
                    min.x -= half_width;
                    min.z -= half_width;
                    max.x += half_width;
                    max.z += half_width;
                    min.y -= 2.0f;
                    max.y += 2.0f;
                    bounds = math::BoundingBox(min, max);
                }
                else if (!subtree_render_bounds(entity, bounds))
                {
                    continue;
                }

                const math::Vector3 size = bounds.GetSize();
                if (size.x < 4.0f && size.z < 4.0f)
                {
                    continue;
                }
                if (size.x > 5000.0f || size.z > 5000.0f)
                {
                    continue;
                }

                RoadObstacle obstacle;
                obstacle.box = expand_box_xz(bounds, clearance);
                obstacle.name = entity->GetObjectName();
                obstacle.entity_id = entity->GetObjectId();
                obstacles.push_back(obstacle);
            }
            return obstacles;
        }

        bool find_detour_point(const math::Vector3& a, const math::Vector3& b, const std::vector<RoadObstacle>& obstacles, float clearance, math::Vector3& out_point)
        {
            math::Vector3 delta = b - a;
            delta.y = 0.0f;
            const float length = delta.Length();
            if (length < 0.1f)
            {
                return false;
            }

            const math::Vector3 dir = delta / length;
            const math::Vector3 perp(-dir.z, 0.0f, dir.x);
            const math::Vector3 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f, (a.z + b.z) * 0.5f);

            float max_extent = clearance * 8.0f;
            for (const RoadObstacle& obstacle : obstacles)
            {
                if (!segment_intersects_box_xz(a, b, obstacle.box))
                {
                    continue;
                }
                const math::Vector3 size = obstacle.box.GetMax() - obstacle.box.GetMin();
                max_extent = std::max(max_extent, std::max(size.x, size.z) * 0.75f + clearance);
            }

            const float offsets[10] = {
                clearance * 1.5f, -clearance * 1.5f,
                clearance * 3.0f, -clearance * 3.0f,
                clearance * 5.0f, -clearance * 5.0f,
                max_extent * 0.5f, -max_extent * 0.5f,
                max_extent, -max_extent
            };

            for (float offset : offsets)
            {
                math::Vector3 candidate = mid + perp * offset;
                candidate.y = mid.y;
                if (!segment_blocked(a, candidate, obstacles) && !segment_blocked(candidate, b, obstacles))
                {
                    out_point = candidate;
                    return true;
                }
            }

            // fall back to expanded box corners of the first blocking obstacle
            for (const RoadObstacle& obstacle : obstacles)
            {
                if (!segment_intersects_box_xz(a, b, obstacle.box))
                {
                    continue;
                }

                const math::Vector3 min = obstacle.box.GetMin();
                const math::Vector3 max = obstacle.box.GetMax();
                const float pad = std::max(clearance, 4.0f);
                const math::Vector3 corners[8] = {
                    { min.x - pad, mid.y, min.z - pad },
                    { max.x + pad, mid.y, min.z - pad },
                    { max.x + pad, mid.y, max.z + pad },
                    { min.x - pad, mid.y, max.z + pad },
                    { min.x - pad, mid.y, (min.z + max.z) * 0.5f },
                    { max.x + pad, mid.y, (min.z + max.z) * 0.5f },
                    { (min.x + max.x) * 0.5f, mid.y, min.z - pad },
                    { (min.x + max.x) * 0.5f, mid.y, max.z + pad },
                };

                float best_score = std::numeric_limits<float>::max();
                bool found = false;
                for (const math::Vector3& corner : corners)
                {
                    if (segment_blocked(a, corner, obstacles) || segment_blocked(corner, b, obstacles))
                    {
                        continue;
                    }
                    const float score = (corner - a).Length() + (b - corner).Length();
                    if (score < best_score)
                    {
                        best_score = score;
                        out_point = corner;
                        found = true;
                    }
                }
                if (found)
                {
                    return true;
                }
            }
            return false;
        }

        std::vector<math::Vector3> avoid_obstacles_on_path(const std::vector<math::Vector3>& input, const std::vector<RoadObstacle>& obstacles, float clearance)
        {
            if (input.size() < 2 || obstacles.empty())
            {
                return input;
            }

            std::vector<math::Vector3> path = input;
            for (uint32_t pass = 0; pass < 6; pass++)
            {
                bool changed = false;
                std::vector<math::Vector3> next;
                next.reserve(path.size() * 2);
                next.push_back(path.front());
                for (size_t i = 0; i + 1 < path.size(); i++)
                {
                    const math::Vector3& a = next.back();
                    const math::Vector3& b = path[i + 1];
                    if (segment_blocked(a, b, obstacles))
                    {
                        math::Vector3 detour;
                        if (find_detour_point(a, b, obstacles, clearance, detour))
                        {
                            next.push_back(detour);
                            changed = true;
                        }
                    }
                    next.push_back(b);
                }
                path = std::move(next);
                if (!changed)
                {
                    break;
                }
            }
            return path;
        }

        std::string flat_points_string(const std::vector<math::Vector3>& points)
        {
            std::string flat;
            for (size_t i = 0; i < points.size(); i++)
            {
                if (i > 0)
                {
                    flat += ",";
                }
                flat += std::to_string(points[i].x) + "," + std::to_string(points[i].y) + "," + std::to_string(points[i].z);
            }
            return flat;
        }

        std::string command_world_landmarks(const McpRequest& request)
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

            bool include_tagged = true;
            if (const std::optional<std::string> tagged = get_argument(request, "include_tagged"))
            {
                if (!parse_bool(*tagged, include_tagged))
                {
                    return json_error("invalid include_tagged");
                }
            }

            std::string json = "{\"ok\":true,\"landmarks\":[";
            bool first = true;
            uint32_t count = 0;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr || count >= limit)
                {
                    continue;
                }

                const bool is_root = entity->GetParent() == nullptr;
                const bool is_tagged = include_tagged && entity->HasTag("landmark");
                if (!is_root && !is_tagged)
                {
                    continue;
                }
                if (is_landmark_noise(entity) && !is_tagged)
                {
                    continue;
                }

                entity->AcquireChildren();
                math::BoundingBox bounds;
                const bool has_bounds = subtree_render_bounds(entity, bounds);
                if (!first)
                {
                    json += ",";
                }
                first = false;
                count++;
                json += "{";
                json += "\"id\":" + json_string(std::to_string(entity->GetObjectId()));
                json += ",\"name\":" + json_string(entity->GetObjectName());
                json += ",\"position\":" + json_vector3(entity->GetPosition());
                json += ",\"child_count\":" + std::to_string(entity->GetChildrenCount());
                json += ",\"is_root\":" + json_bool(is_root);
                json += ",\"tagged_landmark\":" + json_bool(is_tagged);
                if (!entity->GetTags().empty())
                {
                    json += ",\"tags\":" + entity_tags_json(entity);
                }
                if (has_bounds)
                {
                    json += ",\"bounding_box\":" + json_bounding_box(bounds);
                }
                json += "}";
            }
            json += "],\"count\":" + std::to_string(count) + "}";
            return json;
        }

        std::string command_spline_create_road(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("road creation requires edit mode");
            }

            const std::optional<std::string> points_arg = get_argument(request, "points");
            if (!points_arg || points_arg->empty())
            {
                return json_error("missing points");
            }

            std::vector<math::Vector3> world_points;
            if (!parse_vector3_list(*points_arg, world_points) || world_points.size() < 2)
            {
                return json_error("points must be a flat xyz list with at least 2 points");
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

            Entity* entity = World::CreateEntity();
            if (entity == nullptr)
            {
                return json_error("failed to create entity");
            }

            if (const std::optional<std::string> name = get_argument(request, "name"))
            {
                entity->SetObjectName(*name);
            }
            else
            {
                entity->SetObjectName("spline_road");
            }

            if (parent != nullptr)
            {
                entity->SetParent(parent);
            }

            // place the road root at the first point so local control points stay readable
            entity->SetPosition(world_points[0]);

            Spline* spline = entity->AddComponent<Spline>();
            if (spline == nullptr)
            {
                return json_error("failed to add spline component");
            }

            std::string error;
            if (!apply_spline_road_properties(spline, request, error, true))
            {
                return json_error(error);
            }
            if (!set_spline_control_points_world(entity, spline, world_points, false, error))
            {
                return json_error(error);
            }

            return spline_road_receipt(entity, spline);
        }

        std::string command_spline_set_control_points(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("spline edits require edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (spline == nullptr)
            {
                return json_error("entity does not have a spline component");
            }

            const std::optional<std::string> points_arg = get_argument(request, "points");
            if (!points_arg || points_arg->empty())
            {
                return json_error("missing points");
            }

            std::vector<math::Vector3> world_points;
            if (!parse_vector3_list(*points_arg, world_points))
            {
                return json_error("points must be a flat xyz list");
            }

            bool append = false;
            if (const std::optional<std::string> append_arg = get_argument(request, "append"))
            {
                if (!parse_bool(*append_arg, append))
                {
                    return json_error("invalid append");
                }
            }

            if (!apply_spline_road_properties(spline, request, error, false))
            {
                return json_error(error);
            }
            if (!set_spline_control_points_world(entity, spline, world_points, append, error))
            {
                return json_error(error);
            }

            return spline_road_receipt(entity, spline);
        }

        struct SplineChildPlacement
        {
            Entity* entity = nullptr;
            float lateral = 0.0f;
            float height = 0.0f;
            float old_arc = 0.0f;
        };

        std::vector<SplineChildPlacement> capture_spline_child_placements(Entity* entity, Spline* spline)
        {
            std::vector<SplineChildPlacement> placements;
            if (entity == nullptr || spline == nullptr || spline->GetLength() <= 0.1f)
            {
                return placements;
            }

            const uint32_t sample_count = 512;
            std::vector<math::Vector3> samples(sample_count + 1);
            std::vector<float> arc_lengths(sample_count + 1, 0.0f);
            for (uint32_t i = 0; i <= sample_count; i++)
            {
                samples[i] = spline->GetPoint(static_cast<float>(i) / static_cast<float>(sample_count));
                if (i > 0)
                {
                    arc_lengths[i] = arc_lengths[i - 1] + samples[i].Distance(samples[i - 1]);
                }
            }
            auto closest_index = [&](const math::Vector3& position)
            {
                uint32_t best = 0;
                float best_dist = std::numeric_limits<float>::max();
                for (uint32_t i = 0; i <= sample_count; i++)
                {
                    const float dist = samples[i].Distance(position);
                    if (dist < best_dist)
                    {
                        best_dist = dist;
                        best = i;
                    }
                }
                return best;
            };

            auto already_captured = [&](Entity* candidate)
            {
                for (const SplineChildPlacement& placement : placements)
                {
                    if (placement.entity == candidate)
                    {
                        return true;
                    }
                }
                return false;
            };

            auto add_placement = [&](Entity* child)
            {
                if (child == nullptr || !World::EntityExists(child) || already_captured(child))
                {
                    return;
                }
                const math::Vector3 position = child->GetPosition();
                const uint32_t index = closest_index(position);
                const float t = static_cast<float>(index) / static_cast<float>(sample_count);
                const math::Vector3 point = samples[index];
                math::Vector3 tangent = spline->GetTangent(t);
                tangent.y = 0.0f;
                if (tangent.LengthSquared() < 0.0001f)
                {
                    tangent = math::Vector3(1.0f, 0.0f, 0.0f);
                }
                else
                {
                    tangent.Normalize();
                }
                math::Vector3 right = tangent.Cross(math::Vector3::Up);
                if (right.LengthSquared() < 0.0001f)
                {
                    right = math::Vector3(1.0f, 0.0f, 0.0f);
                }
                else
                {
                    right.Normalize();
                }

                SplineChildPlacement placement;
                placement.entity = child;
                placement.lateral = right.Dot(position - point);
                placement.height = position.y - point.y;
                placement.old_arc = arc_lengths[index];
                placements.push_back(placement);
            };

            // 1) every direct child except control points (poles carry nested lights)
            entity->AcquireChildren();
            for (Entity* child : entity->GetChildren())
            {
                if (child == nullptr)
                {
                    continue;
                }
                if (child->GetObjectName().find("spline_point_") == 0)
                {
                    continue;
                }
                add_placement(child);
            }

            // 2) reclaim road furniture near the current path, and stranded road_* furniture left behind by earlier edits
            const float grab_radius = std::max(18.0f, spline->GetRoadWidth() * 2.0f + 8.0f);
            for (Entity* candidate : World::GetEntities())
            {
                if (candidate == nullptr || candidate == entity || !World::EntityExists(candidate))
                {
                    continue;
                }
                if (candidate->GetParent() == entity)
                {
                    continue;
                }

                Entity* root = candidate;
                while (root->GetParent() != nullptr)
                {
                    root = root->GetParent();
                }
                if (root != candidate && root != entity)
                {
                    const std::string root_name = to_lower_copy(root->GetObjectName());
                    if (root->HasTag("landmark") || root->HasTag("district") ||
                        root_name.find("gas") != std::string::npos || root_name.find("dock") != std::string::npos ||
                        root_name.find("airway") != std::string::npos || root_name.find("market") != std::string::npos ||
                        root_name.find("downtown") != std::string::npos || root_name.find("park") != std::string::npos ||
                        root_name.find("industrial") != std::string::npos || root_name.find("residential") != std::string::npos)
                    {
                        continue;
                    }
                }

                const std::string name = to_lower_copy(candidate->GetObjectName());
                const bool is_road_named =
                    name.find("road_light") != std::string::npos ||
                    name.find("road_prop") != std::string::npos ||
                    name.find("street_light") != std::string::npos ||
                    name.find("streetlight") != std::string::npos;
                const bool looks_like_furniture =
                    is_road_named ||
                    candidate->GetComponent<Camera>() != nullptr ||
                    (candidate->GetComponent<Light>() != nullptr && (name.find("light") != std::string::npos || name.find("pole") != std::string::npos)) ||
                    (name.find("pole") != std::string::npos && name.find("light") != std::string::npos) ||
                    name.find("camera") != std::string::npos;
                if (!looks_like_furniture)
                {
                    continue;
                }

                const uint32_t index = closest_index(candidate->GetPosition());
                const float distance = samples[index].Distance(candidate->GetPosition());
                // named road furniture is reclaimed even if stranded far from the new path
                if (!is_road_named && distance > grab_radius)
                {
                    continue;
                }

                Entity* move_root = candidate;
                if (candidate->GetParent() != nullptr && candidate->GetParent() != entity)
                {
                    Entity* parent = candidate->GetParent();
                    const std::string parent_name = to_lower_copy(parent->GetObjectName());
                    if (parent->GetComponent<Render>() != nullptr || parent_name.find("pole") != std::string::npos ||
                        parent_name.find("road_light") != std::string::npos)
                    {
                        move_root = parent;
                    }
                }
                if (move_root->GetParent() != entity)
                {
                    move_root->SetParent(entity);
                }
                add_placement(move_root);
            }

            std::sort(placements.begin(), placements.end(), [](const SplineChildPlacement& a, const SplineChildPlacement& b)
            {
                return a.old_arc < b.old_arc;
            });
            return placements;
        }

        uint32_t apply_spline_child_placements(Spline* spline, const std::vector<SplineChildPlacement>& placements)
        {
            if (spline == nullptr || placements.empty())
            {
                return 0;
            }

            const float length = spline->GetLength();
            if (length <= 0.1f)
            {
                return 0;
            }

            const float step = spline->GetClosedLoop()
                ? length / static_cast<float>(placements.size())
                : (placements.size() == 1 ? length * 0.5f : length / static_cast<float>(placements.size() - 1));

            uint32_t moved = 0;
            for (size_t i = 0; i < placements.size(); i++)
            {
                Entity* target = placements[i].entity;
                if (target == nullptr || !World::EntityExists(target))
                {
                    continue;
                }
                const float new_distance = placements.size() == 1 ? step : static_cast<float>(i) * step;
                const float new_t = spline->GetTAtDistance(new_distance, 32);
                const math::Vector3 new_point = spline->GetPoint(new_t);
                math::Vector3 new_tangent = spline->GetTangent(new_t);
                new_tangent.y = 0.0f;
                if (new_tangent.LengthSquared() < 0.0001f)
                {
                    new_tangent = math::Vector3(1.0f, 0.0f, 0.0f);
                }
                else
                {
                    new_tangent.Normalize();
                }
                math::Vector3 new_right = new_tangent.Cross(math::Vector3::Up);
                if (new_right.LengthSquared() < 0.0001f)
                {
                    new_right = math::Vector3(1.0f, 0.0f, 0.0f);
                }
                else
                {
                    new_right.Normalize();
                }
                target->SetPosition(new_point + new_right * placements[i].lateral + math::Vector3::Up * placements[i].height);
                target->SetRotation(math::Quaternion::FromLookRotation(new_tangent, math::Vector3::Up));
                moved++;
            }
            return moved;
        }

        std::string command_spline_reroute(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("spline reroute requires edit mode");
            }

            std::string error;
            Entity* entity = nullptr;
            if (get_argument(request, "id") || get_argument(request, "entity_id"))
            {
                entity = get_entity_from_request(request, error);
            }
            if (entity == nullptr)
            {
                if (const std::optional<std::string> name = get_argument(request, "name"))
                {
                    entity = resolve_entity_token(*name, error);
                }
                else if (const std::optional<std::string> id_as_name = get_argument(request, "id"))
                {
                    entity = resolve_entity_token(*id_as_name, error);
                }
            }
            if (entity == nullptr)
            {
                // last resort: first root named spline_road
                entity = resolve_entity_token("spline_road", error);
            }
            if (entity == nullptr)
            {
                return json_error(error.empty() ? "missing spline id or name" : error);
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (spline == nullptr)
            {
                return json_error("entity does not have a spline component");
            }

            std::vector<math::Vector3> world_points = spline->GetControlPoints();
            if (world_points.size() < 2)
            {
                return json_error("spline needs at least 2 control points");
            }

            if (const std::optional<std::string> from_arg = get_argument(request, "from"))
            {
                Entity* from_entity = resolve_entity_token(*from_arg, error);
                if (from_entity == nullptr)
                {
                    return json_error("from landmark not found: " + *from_arg);
                }
                math::Vector3 approach;
                if (!landmark_approach_point(from_entity, world_points.back(), 16.0f, approach))
                {
                    approach = from_entity->GetPosition();
                }
                world_points.front() = approach;
            }
            if (const std::optional<std::string> to_arg = get_argument(request, "to"))
            {
                Entity* to_entity = resolve_entity_token(*to_arg, error);
                if (to_entity == nullptr)
                {
                    return json_error("to landmark not found: " + *to_arg);
                }
                math::Vector3 approach;
                if (!landmark_approach_point(to_entity, world_points.front(), 16.0f, approach))
                {
                    approach = to_entity->GetPosition();
                }
                world_points.back() = approach;
            }

            if (const std::optional<std::string> via = get_argument(request, "via"))
            {
                std::vector<math::Vector3> via_points;
                if (!parse_vector3_list(*via, via_points))
                {
                    return json_error("via must be a flat xyz list");
                }
                if (!via_points.empty())
                {
                    std::vector<math::Vector3> merged;
                    merged.push_back(world_points.front());
                    merged.insert(merged.end(), via_points.begin(), via_points.end());
                    merged.push_back(world_points.back());
                    world_points = std::move(merged);
                }
            }

            float clearance = 14.0f;
            if (const std::optional<std::string> clearance_arg = get_argument(request, "clearance"))
            {
                if (!parse_float(*clearance_arg, clearance) || clearance < 0.0f || clearance > 200.0f)
                {
                    return json_error("clearance must be between 0 and 200");
                }
            }

            bool keep_children = true;
            if (const std::optional<std::string> keep_arg = get_argument(request, "keep_children"))
            {
                if (!parse_bool(*keep_arg, keep_children))
                {
                    return json_error("invalid keep_children");
                }
            }

            const std::vector<SplineChildPlacement> child_placements = keep_children
                ? capture_spline_child_placements(entity, spline)
                : std::vector<SplineChildPlacement>{};

            const size_t points_before = world_points.size();
            const std::vector<uint64_t> ignore_ids = { entity->GetObjectId() };
            const std::vector<RoadObstacle> obstacles = collect_road_obstacles(ignore_ids, clearance);
            world_points = avoid_obstacles_on_path(world_points, obstacles, clearance);
            world_points = avoid_obstacles_on_path(world_points, obstacles, clearance);

            if (!set_spline_control_points_world(entity, spline, world_points, false, error))
            {
                return json_error(error);
            }

            const uint32_t redistributed = apply_spline_child_placements(spline, child_placements);

            std::string result = spline_road_receipt(entity, spline);
            if (!result.empty() && result.back() == '}')
            {
                result.pop_back();
                result += ",\"clearance\":" + std::to_string(clearance);
                result += ",\"obstacle_count\":" + std::to_string(obstacles.size());
                result += ",\"detour_points_added\":" + std::to_string(world_points.size() > points_before ? world_points.size() - points_before : 0);
                result += ",\"children_redistributed\":" + std::to_string(redistributed);
                result += "}";
            }
            return result;
        }

        std::string command_spline_connect(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("road creation requires edit mode");
            }

            const std::optional<std::string> landmarks_arg = get_argument(request, "landmarks");
            if (!landmarks_arg || landmarks_arg->empty())
            {
                return json_error("missing landmarks");
            }

            const std::vector<std::string> tokens = split_csv_tokens(*landmarks_arg);
            if (tokens.size() < 2)
            {
                return json_error("landmarks needs at least 2 names or ids");
            }

            struct LandmarkStop
            {
                Entity* entity = nullptr;
                std::string name;
                uint64_t id = 0;
                math::Vector3 position;
            };

            std::vector<LandmarkStop> stops;
            stops.reserve(tokens.size());
            for (const std::string& token : tokens)
            {
                std::string resolve_error;
                Entity* landmark = resolve_entity_token(token, resolve_error);
                if (landmark == nullptr)
                {
                    return json_error("landmark not found: " + token + (resolve_error.empty() ? "" : (", " + resolve_error)));
                }
                LandmarkStop stop;
                stop.entity = landmark;
                stop.name = landmark->GetObjectName();
                stop.id = landmark->GetObjectId();
                stop.position = landmark->GetPosition();
                stops.push_back(std::move(stop));
            }

            bool avoid_obstacles = true;
            if (const std::optional<std::string> avoid_arg = get_argument(request, "avoid_obstacles"))
            {
                if (!parse_bool(*avoid_arg, avoid_obstacles))
                {
                    return json_error("invalid avoid_obstacles");
                }
            }

            float clearance = 12.0f;
            if (const std::optional<std::string> clearance_arg = get_argument(request, "clearance"))
            {
                if (!parse_float(*clearance_arg, clearance) || clearance < 0.0f || clearance > 200.0f)
                {
                    return json_error("clearance must be between 0 and 200");
                }
            }

            float standoff = clearance + 4.0f;
            if (const std::optional<std::string> standoff_arg = get_argument(request, "standoff"))
            {
                if (!parse_float(*standoff_arg, standoff) || standoff < 0.0f || standoff > 200.0f)
                {
                    return json_error("standoff must be between 0 and 200");
                }
            }

            // approach district edges, never aim at landmark centers (that drives through runways/yards)
            std::vector<math::Vector3> world_points;
            std::vector<std::string> resolved_names;
            world_points.reserve(stops.size());
            resolved_names.reserve(stops.size());
            for (size_t i = 0; i < stops.size(); i++)
            {
                math::Vector3 toward = stops[i].position;
                if (i + 1 < stops.size())
                {
                    toward = stops[i + 1].position;
                }
                else if (i > 0)
                {
                    toward = stops[i - 1].position;
                }

                math::Vector3 approach;
                if (!landmark_approach_point(stops[i].entity, toward, standoff, approach))
                {
                    approach = stops[i].position;
                }
                world_points.push_back(approach);
                resolved_names.push_back(stops[i].name);
            }

            if (const std::optional<std::string> via = get_argument(request, "via"))
            {
                std::vector<math::Vector3> via_points;
                if (!parse_vector3_list(*via, via_points))
                {
                    return json_error("via must be a flat xyz list");
                }
                if (world_points.size() >= 2 && !via_points.empty())
                {
                    std::vector<math::Vector3> merged;
                    merged.push_back(world_points.front());
                    merged.insert(merged.end(), via_points.begin(), via_points.end());
                    for (size_t i = 1; i < world_points.size(); i++)
                    {
                        merged.push_back(world_points[i]);
                    }
                    world_points = std::move(merged);
                }
            }

            const size_t points_before_avoid = world_points.size();
            uint32_t obstacle_count = 0;
            if (avoid_obstacles)
            {
                // include destination districts as obstacles so paths skirt them instead of crossing
                const std::vector<uint64_t> ignore_ids;
                const std::vector<RoadObstacle> obstacles = collect_road_obstacles(ignore_ids, clearance);
                obstacle_count = static_cast<uint32_t>(obstacles.size());
                world_points = avoid_obstacles_on_path(world_points, obstacles, clearance);
            }

            McpRequest create_request = request;
            create_request.arguments["points"] = flat_points_string(world_points);
            if (!get_argument(create_request, "name"))
            {
                std::string auto_name = "road";
                for (const std::string& name : resolved_names)
                {
                    auto_name += "_";
                    auto_name += name;
                }
                create_request.arguments["name"] = auto_name;
            }

            std::string result = command_spline_create_road(create_request);
            if (result.find("\"ok\":true") == std::string::npos)
            {
                return result;
            }

            if (!result.empty() && result.back() == '}')
            {
                result.pop_back();
                result += ",\"landmarks\":[";
                for (size_t i = 0; i < resolved_names.size(); i++)
                {
                    if (i > 0)
                    {
                        result += ",";
                    }
                    result += json_string(resolved_names[i]);
                }
                result += "],\"avoid_obstacles\":" + json_bool(avoid_obstacles);
                result += ",\"clearance\":" + std::to_string(clearance);
                result += ",\"standoff\":" + std::to_string(standoff);
                result += ",\"obstacle_count\":" + std::to_string(obstacle_count);
                result += ",\"detour_points_added\":" + std::to_string(world_points.size() > points_before_avoid ? world_points.size() - points_before_avoid : 0);
                result += "}";
            }
            return result;
        }

        std::string command_spline_junction(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("junction edits require edit mode");
            }

            const std::optional<std::string> roads_arg = get_argument(request, "roads");
            if (!roads_arg || roads_arg->empty())
            {
                return json_error("missing roads");
            }

            const std::vector<std::string> tokens = split_csv_tokens(*roads_arg);
            if (tokens.size() < 2)
            {
                return json_error("roads needs at least 2 names or ids");
            }

            struct RoadEnd
            {
                Entity* entity = nullptr;
                Spline* spline = nullptr;
                std::vector<math::Vector3> points;
                std::string name;
            };

            std::vector<RoadEnd> roads;
            roads.reserve(tokens.size());
            for (const std::string& token : tokens)
            {
                std::string resolve_error;
                Entity* entity = resolve_entity_token(token, resolve_error);
                if (entity == nullptr)
                {
                    return json_error("road not found: " + token + (resolve_error.empty() ? "" : (", " + resolve_error)));
                }
                Spline* spline = entity->GetComponent<Spline>();
                if (spline == nullptr)
                {
                    return json_error("entity is not a spline road: " + token);
                }
                std::vector<math::Vector3> points = spline->GetControlPoints();
                if (points.size() < 2)
                {
                    return json_error("road needs at least 2 control points: " + token);
                }
                RoadEnd road;
                road.entity = entity;
                road.spline = spline;
                road.points = std::move(points);
                road.name = entity->GetObjectName();
                roads.push_back(std::move(road));
            }

            math::Vector3 junction;
            bool has_explicit_point = false;
            std::vector<bool> snap_start(roads.size(), false);

            if (const std::optional<std::string> point_arg = get_argument(request, "point"))
            {
                std::vector<math::Vector3> points;
                if (!parse_vector3_list(*point_arg, points) || points.size() != 1)
                {
                    return json_error("point must be a single xyz triple");
                }
                junction = points[0];
                has_explicit_point = true;
                for (size_t i = 0; i < roads.size(); i++)
                {
                    const float dist_start = (roads[i].points.front() - junction).Length();
                    const float dist_end = (roads[i].points.back() - junction).Length();
                    snap_start[i] = dist_start <= dist_end;
                }
            }
            else
            {
                // pick the closest endpoint pair across the first two roads, then fold in the rest
                float best_dist = std::numeric_limits<float>::max();
                bool best_a_start = true;
                bool best_b_start = true;
                const math::Vector3 ends_a[2] = { roads[0].points.front(), roads[0].points.back() };
                const math::Vector3 ends_b[2] = { roads[1].points.front(), roads[1].points.back() };
                for (size_t ia = 0; ia < 2; ia++)
                {
                    for (size_t ib = 0; ib < 2; ib++)
                    {
                        const float dist = (ends_a[ia] - ends_b[ib]).Length();
                        if (dist < best_dist)
                        {
                            best_dist = dist;
                            best_a_start = ia == 0;
                            best_b_start = ib == 0;
                        }
                    }
                }
                snap_start[0] = best_a_start;
                snap_start[1] = best_b_start;

                for (size_t i = 2; i < roads.size(); i++)
                {
                    math::Vector3 seed = math::Vector3::Zero;
                    for (size_t j = 0; j < i; j++)
                    {
                        seed += snap_start[j] ? roads[j].points.front() : roads[j].points.back();
                    }
                    seed = seed / static_cast<float>(i);
                    const float dist_start = (roads[i].points.front() - seed).Length();
                    const float dist_end = (roads[i].points.back() - seed).Length();
                    snap_start[i] = dist_start <= dist_end;
                }

                junction = math::Vector3::Zero;
                for (size_t i = 0; i < roads.size(); i++)
                {
                    junction += snap_start[i] ? roads[i].points.front() : roads[i].points.back();
                }
                junction = junction / static_cast<float>(roads.size());
            }

            std::string json = "{\"ok\":true,\"junction\":{\"x\":" + std::to_string(junction.x) +
                ",\"y\":" + std::to_string(junction.y) +
                ",\"z\":" + std::to_string(junction.z) +
                "},\"explicit_point\":" + json_bool(has_explicit_point) +
                ",\"roads\":[";

            for (size_t i = 0; i < roads.size(); i++)
            {
                RoadEnd& road = roads[i];
                if (snap_start[i])
                {
                    road.points.front() = junction;
                }
                else
                {
                    road.points.back() = junction;
                }

                std::string error;
                if (!set_spline_control_points_world(road.entity, road.spline, road.points, false, error))
                {
                    return json_error("failed to update " + road.name + ": " + error);
                }

                if (i > 0)
                {
                    json += ",";
                }
                json += "{";
                json += "\"id\":" + json_string(std::to_string(road.entity->GetObjectId()));
                json += ",\"name\":" + json_string(road.name);
                json += ",\"snapped\":" + json_string(snap_start[i] ? "start" : "end");
                json += ",\"point_count\":" + std::to_string(road.spline->GetControlPointCount());
                json += ",\"length\":" + std::to_string(road.spline->GetLength());
                json += "}";
            }

            json += "],\"count\":" + std::to_string(roads.size()) + "}";
            return json;
        }

        void clear_road_decoration(Entity* entity)
        {
            if (entity == nullptr)
            {
                return;
            }

            for (uint32_t pass = 0; pass < 64; pass++)
            {
                entity->AcquireChildren();
                std::vector<Entity*> to_remove;
                for (Entity* child : entity->GetChildren())
                {
                    if (child == nullptr || !World::EntityExists(child))
                    {
                        continue;
                    }
                    const std::string& name = child->GetObjectName();
                    if (name.find("road_light_") == 0 || name.find("road_prop_") == 0)
                    {
                        to_remove.push_back(child);
                    }
                }
                if (to_remove.empty())
                {
                    break;
                }
                for (Entity* child : to_remove)
                {
                    World::RemoveEntity(child);
                }
            }
        }

        std::string command_spline_decorate(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("road decoration requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (spline == nullptr)
            {
                return json_error("entity does not have a spline component");
            }

            const float length = spline->GetLength();
            if (length <= 1.0f)
            {
                return json_error("spline has no usable length");
            }

            float spacing = 28.0f;
            if (const std::optional<std::string> spacing_arg = get_argument(request, "spacing"))
            {
                if (!parse_float(*spacing_arg, spacing) || spacing < 5.0f || spacing > 200.0f)
                {
                    return json_error("spacing must be between 5 and 200");
                }
            }

            bool lights = true;
            if (const std::optional<std::string> lights_arg = get_argument(request, "lights"))
            {
                if (!parse_bool(*lights_arg, lights))
                {
                    return json_error("invalid lights");
                }
            }

            bool props = true;
            if (const std::optional<std::string> props_arg = get_argument(request, "props"))
            {
                if (!parse_bool(*props_arg, props))
                {
                    return json_error("invalid props");
                }
            }

            bool sidewalks = true;
            if (const std::optional<std::string> sidewalks_arg = get_argument(request, "sidewalks"))
            {
                if (!parse_bool(*sidewalks_arg, sidewalks))
                {
                    return json_error("invalid sidewalks");
                }
            }

            bool replace = true;
            if (const std::optional<std::string> replace_arg = get_argument(request, "replace"))
            {
                if (!parse_bool(*replace_arg, replace))
                {
                    return json_error("invalid replace");
                }
            }

            if (const std::optional<std::string> road_width = get_argument(request, "road_width"))
            {
                float parsed = 0.0f;
                if (!parse_float(*road_width, parsed) || parsed <= 0.0f)
                {
                    return json_error("invalid road_width");
                }
                spline->SetRoadWidth(parsed);
            }

            if (sidewalks)
            {
                spline->SetSidewalkEnabled(true);
                float sidewalk_width = 2.0f;
                if (const std::optional<std::string> width_arg = get_argument(request, "sidewalk_width"))
                {
                    if (!parse_float(*width_arg, sidewalk_width) || sidewalk_width < 0.0f)
                    {
                        return json_error("invalid sidewalk_width");
                    }
                }
                spline->SetSidewalkWidth(sidewalk_width);
            }

            if (spline->GetMeshEnabled())
            {
                spline->GenerateRoadMesh();
            }

            if (replace)
            {
                clear_road_decoration(entity);
            }

            const float half_width = spline->GetRoadWidth() * 0.5f;
            const float light_lateral = half_width + (spline->GetSidewalkEnabled() ? spline->GetSidewalkWidth() : 0.0f) + 1.2f;
            const float prop_lateral = half_width + 0.6f;
            const uint32_t count = std::max(2u, static_cast<uint32_t>(std::ceil(length / spacing)) + 1u);

            uint32_t light_count = 0;
            uint32_t prop_count = 0;
            for (uint32_t i = 0; i < count; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(count - 1);
                const math::Vector3 point = spline->GetPoint(t);
                math::Vector3 tangent = spline->GetTangent(t);
                tangent.y = 0.0f;
                if (tangent.LengthSquared() < 0.0001f)
                {
                    tangent = math::Vector3(1.0f, 0.0f, 0.0f);
                }
                else
                {
                    tangent.Normalize();
                }
                math::Vector3 right = tangent.Cross(math::Vector3::Up);
                if (right.LengthSquared() < 0.0001f)
                {
                    right = math::Vector3(1.0f, 0.0f, 0.0f);
                }
                else
                {
                    right.Normalize();
                }

                const float side = (i % 2 == 0) ? 1.0f : -1.0f;
                if (lights)
                {
                    Entity* pole = World::CreateEntity();
                    pole->SetObjectName("road_light_pole_" + std::to_string(light_count));
                    pole->SetParent(entity);
                    pole->SetPosition(point + right * (light_lateral * side) + math::Vector3(0.0f, 3.0f, 0.0f));
                    pole->SetScale(math::Vector3(0.25f, 6.0f, 0.25f));
                    if (Render* renderable = pole->AddComponent<Render>())
                    {
                        renderable->SetMesh(MeshType::Cylinder);
                        renderable->SetDefaultMaterial();
                    }

                    Entity* lamp = World::CreateEntity();
                    lamp->SetObjectName("road_light_" + std::to_string(light_count));
                    lamp->SetParent(pole);
                    lamp->SetPositionLocal(math::Vector3(0.0f, 0.55f, 0.0f));
                    if (Light* light = lamp->AddComponent<Light>())
                    {
                        light->SetLightType(LightType::Point);
                        light->SetColor(Color(1.0f, 0.92f, 0.78f, 1.0f));
                        light->SetTemperature(3200.0f);
                        light->SetIntensity(8500.0f);
                        light->SetRange(30.0f);
                        light->SetFlag(LightFlags::Shadows, true);
                        light->SetDrawDistance(60.0f);
                        light->SetShadowDistance(45.0f);
                    }
                    light_count++;
                }

                if (props && i > 0 && i + 1 < count && (i % 2 == 1))
                {
                    Entity* barrier = World::CreateEntity();
                    barrier->SetObjectName("road_prop_barrier_" + std::to_string(prop_count));
                    barrier->SetParent(entity);
                    barrier->SetPosition(point + right * (prop_lateral * -side) + math::Vector3(0.0f, 0.4f, 0.0f));
                    barrier->SetScale(math::Vector3(0.35f, 0.8f, 1.6f));
                    barrier->SetRotation(math::Quaternion::FromLookRotation(tangent, math::Vector3::Up));
                    if (Render* renderable = barrier->AddComponent<Render>())
                    {
                        renderable->SetMesh(MeshType::Cube);
                        renderable->SetDefaultMaterial();
                    }
                    prop_count++;
                }
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += ",\"length\":" + std::to_string(length);
            json += ",\"spacing\":" + std::to_string(spacing);
            json += ",\"lights\":" + std::to_string(light_count);
            json += ",\"props\":" + std::to_string(prop_count);
            json += ",\"sidewalks\":" + json_bool(spline->GetSidewalkEnabled());
            json += ",\"road_width\":" + std::to_string(spline->GetRoadWidth());
            json += "}";
            return json;
        }

        enum class DistrictPreset
        {
            Market,
            Downtown,
            Park,
            Industrial,
            Residential,
            Parking,
            Plaza,
            GasStation
        };

        std::optional<DistrictPreset> district_preset_from_name(const std::string& name)
        {
            const std::string value = to_lower_copy(name);
            if (value == "market")
            {
                return DistrictPreset::Market;
            }
            if (value == "downtown" || value == "skyscrapers" || value == "towers")
            {
                return DistrictPreset::Downtown;
            }
            if (value == "park")
            {
                return DistrictPreset::Park;
            }
            if (value == "industrial" || value == "dockyard")
            {
                return DistrictPreset::Industrial;
            }
            if (value == "residential" || value == "housing")
            {
                return DistrictPreset::Residential;
            }
            if (value == "parking" || value == "parking_lot")
            {
                return DistrictPreset::Parking;
            }
            if (value == "plaza" || value == "civic")
            {
                return DistrictPreset::Plaza;
            }
            if (value == "gas_station" || value == "gasstation")
            {
                return DistrictPreset::GasStation;
            }
            return std::nullopt;
        }

        std::string district_preset_to_name(DistrictPreset preset)
        {
            switch (preset)
            {
            case DistrictPreset::Market: return "market";
            case DistrictPreset::Downtown: return "downtown";
            case DistrictPreset::Park: return "park";
            case DistrictPreset::Industrial: return "industrial";
            case DistrictPreset::Residential: return "residential";
            case DistrictPreset::Parking: return "parking";
            case DistrictPreset::Plaza: return "plaza";
            case DistrictPreset::GasStation: return "gas_station";
            }
            return "market";
        }

        float district_density_scale(const std::string& density)
        {
            const std::string value = to_lower_copy(density);
            if (value == "low")
            {
                return 0.65f;
            }
            if (value == "high")
            {
                return 1.35f;
            }
            return 1.0f;
        }

        struct DistrictRng
        {
            uint32_t state = 1;

            explicit DistrictRng(uint32_t seed)
            {
                state = seed == 0 ? 1u : seed;
            }

            uint32_t next_u32()
            {
                state = state * 1664525u + 1013904223u;
                return state;
            }

            float next_01()
            {
                return static_cast<float>(next_u32() & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
            }

            float range(float min_value, float max_value)
            {
                return min_value + (max_value - min_value) * next_01();
            }

            int range_int(int min_value, int max_value)
            {
                if (max_value <= min_value)
                {
                    return min_value;
                }
                return min_value + static_cast<int>(next_u32() % static_cast<uint32_t>(max_value - min_value + 1));
            }
        };

        struct DistrictBuildContext
        {
            Entity* parent = nullptr;
            DistrictRng* rng = nullptr;
            float width = 40.0f;
            float depth = 40.0f;
            float density = 1.0f;
            bool lights = true;
            uint32_t part_count = 0;
            uint32_t light_count = 0;
        };

        void clear_entity_children(Entity* entity)
        {
            if (entity == nullptr)
            {
                return;
            }
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
                    deleted_any = true;
                }
                if (!deleted_any)
                {
                    break;
                }
            }
            entity->AcquireChildren();
        }

        Entity* blockout_part(DistrictBuildContext& ctx, const std::string& name, MeshType mesh, const math::Vector3& local_pos, const math::Vector3& local_scale, const math::Vector3& local_euler = math::Vector3::Zero)
        {
            Entity* entity = World::CreateEntity();
            if (entity == nullptr || ctx.parent == nullptr)
            {
                return nullptr;
            }
            entity->SetObjectName(name);
            entity->SetParent(ctx.parent);
            entity->SetPositionLocal(local_pos);
            entity->SetScaleLocal(local_scale);
            if (local_euler.x != 0.0f || local_euler.y != 0.0f || local_euler.z != 0.0f)
            {
                entity->SetRotationLocal(math::Quaternion::FromEulerAngles(local_euler));
            }
            if (Render* renderable = entity->AddComponent<Render>())
            {
                renderable->SetMesh(mesh);
                renderable->SetDefaultMaterial();
            }
            ctx.part_count++;
            return entity;
        }

        Entity* blockout_light(DistrictBuildContext& ctx, const std::string& name, const math::Vector3& local_pos, float intensity = 8500.0f, float range = 30.0f)
        {
            if (!ctx.lights || ctx.parent == nullptr)
            {
                return nullptr;
            }
            Entity* entity = World::CreateEntity();
            if (entity == nullptr)
            {
                return nullptr;
            }
            entity->SetObjectName(name);
            entity->SetParent(ctx.parent);
            entity->SetPositionLocal(local_pos);
            if (Light* light = entity->AddComponent<Light>())
            {
                light->SetLightType(LightType::Point);
                light->SetColor(Color(1.0f, 0.92f, 0.78f, 1.0f));
                light->SetTemperature(3200.0f);
                light->SetIntensity(intensity);
                light->SetRange(range);
                light->SetFlag(LightFlags::Shadows, true);
                light->SetDrawDistance(std::max(60.0f, range * 2.0f));
                light->SetShadowDistance(std::max(45.0f, range * 1.5f));
            }
            ctx.light_count++;
            return entity;
        }

        void build_district_pad(DistrictBuildContext& ctx, float thickness = 0.2f)
        {
            blockout_part(ctx, "pad", MeshType::Cube, math::Vector3(0.0f, thickness * 0.5f, 0.0f), math::Vector3(ctx.width, thickness, ctx.depth));
        }

        void build_district_market(DistrictBuildContext& ctx)
        {
            build_district_pad(ctx, 0.15f);
            const int stall_cols = std::max(2, static_cast<int>(3.0f * ctx.density));
            const int stall_rows = std::max(2, static_cast<int>(2.0f * ctx.density));
            const float stall_w = ctx.width * 0.18f;
            const float stall_d = ctx.depth * 0.16f;
            const float gap_x = ctx.width / static_cast<float>(stall_cols + 1);
            const float gap_z = ctx.depth / static_cast<float>(stall_rows + 1);
            uint32_t index = 0;
            for (int row = 0; row < stall_rows; row++)
            {
                for (int col = 0; col < stall_cols; col++)
                {
                    const float x = -ctx.width * 0.5f + gap_x * static_cast<float>(col + 1);
                    const float z = -ctx.depth * 0.5f + gap_z * static_cast<float>(row + 1);
                    const float h = ctx.rng->range(2.2f, 3.4f);
                    blockout_part(ctx, "stall_" + std::to_string(index), MeshType::Cube, math::Vector3(x, h * 0.5f, z), math::Vector3(stall_w, h, stall_d));
                    blockout_part(ctx, "canopy_" + std::to_string(index), MeshType::Cube, math::Vector3(x, h + 0.15f, z), math::Vector3(stall_w * 1.15f, 0.2f, stall_d * 1.15f));
                    if ((index % 2) == 0)
                    {
                        blockout_light(ctx, "stall_light_" + std::to_string(index), math::Vector3(x, h + 1.5f, z), 7000.0f, 18.0f);
                    }
                    index++;
                }
            }
            blockout_part(ctx, "shop_a", MeshType::Cube, math::Vector3(-ctx.width * 0.38f, 2.5f, -ctx.depth * 0.38f), math::Vector3(ctx.width * 0.22f, 5.0f, ctx.depth * 0.18f));
            blockout_part(ctx, "shop_b", MeshType::Cube, math::Vector3(ctx.width * 0.36f, 2.2f, ctx.depth * 0.34f), math::Vector3(ctx.width * 0.2f, 4.4f, ctx.depth * 0.16f));
            blockout_light(ctx, "market_center_light", math::Vector3(0.0f, 8.0f, 0.0f), 12000.0f, 40.0f);
        }

        void build_district_downtown(DistrictBuildContext& ctx)
        {
            build_district_pad(ctx, 0.25f);
            const int towers = std::max(3, static_cast<int>(5.0f * ctx.density));
            for (int i = 0; i < towers; i++)
            {
                const float x = ctx.rng->range(-ctx.width * 0.35f, ctx.width * 0.35f);
                const float z = ctx.rng->range(-ctx.depth * 0.35f, ctx.depth * 0.35f);
                const float w = ctx.rng->range(ctx.width * 0.08f, ctx.width * 0.14f);
                const float d = ctx.rng->range(ctx.depth * 0.08f, ctx.depth * 0.14f);
                const float h = ctx.rng->range(18.0f, 55.0f) * ctx.density;
                blockout_part(ctx, "tower_" + std::to_string(i), MeshType::Cube, math::Vector3(x, h * 0.5f, z), math::Vector3(w, h, d));
                blockout_light(ctx, "tower_light_" + std::to_string(i), math::Vector3(x, h + 2.0f, z), 9000.0f, 28.0f);
            }
            blockout_part(ctx, "podium", MeshType::Cube, math::Vector3(0.0f, 2.0f, 0.0f), math::Vector3(ctx.width * 0.35f, 4.0f, ctx.depth * 0.28f));
            blockout_light(ctx, "downtown_plaza_light", math::Vector3(0.0f, 10.0f, 0.0f), 14000.0f, 45.0f);
        }

        void build_district_park(DistrictBuildContext& ctx)
        {
            build_district_pad(ctx, 0.12f);
            blockout_part(ctx, "path_x", MeshType::Cube, math::Vector3(0.0f, 0.18f, 0.0f), math::Vector3(ctx.width * 0.9f, 0.08f, ctx.depth * 0.08f));
            blockout_part(ctx, "path_z", MeshType::Cube, math::Vector3(0.0f, 0.18f, 0.0f), math::Vector3(ctx.width * 0.08f, 0.08f, ctx.depth * 0.9f));
            const int trees = std::max(6, static_cast<int>(10.0f * ctx.density));
            for (int i = 0; i < trees; i++)
            {
                const float x = ctx.rng->range(-ctx.width * 0.4f, ctx.width * 0.4f);
                const float z = ctx.rng->range(-ctx.depth * 0.4f, ctx.depth * 0.4f);
                if (std::fabs(x) < ctx.width * 0.06f || std::fabs(z) < ctx.depth * 0.06f)
                {
                    continue;
                }
                const float trunk_h = ctx.rng->range(1.8f, 3.2f);
                blockout_part(ctx, "tree_trunk_" + std::to_string(i), MeshType::Cylinder, math::Vector3(x, trunk_h * 0.5f, z), math::Vector3(0.35f, trunk_h, 0.35f));
                blockout_part(ctx, "tree_crown_" + std::to_string(i), MeshType::Sphere, math::Vector3(x, trunk_h + 1.2f, z), math::Vector3(2.2f, 2.2f, 2.2f));
            }
            blockout_part(ctx, "bench_a", MeshType::Cube, math::Vector3(-ctx.width * 0.2f, 0.45f, ctx.depth * 0.12f), math::Vector3(2.2f, 0.5f, 0.55f));
            blockout_part(ctx, "bench_b", MeshType::Cube, math::Vector3(ctx.width * 0.18f, 0.45f, -ctx.depth * 0.15f), math::Vector3(2.2f, 0.5f, 0.55f), math::Vector3(0.0f, 90.0f, 0.0f));
            blockout_light(ctx, "park_light_a", math::Vector3(-ctx.width * 0.25f, 5.0f, -ctx.depth * 0.25f), 6500.0f, 25.0f);
            blockout_light(ctx, "park_light_b", math::Vector3(ctx.width * 0.25f, 5.0f, ctx.depth * 0.25f), 6500.0f, 25.0f);
        }

        void build_district_industrial(DistrictBuildContext& ctx)
        {
            build_district_pad(ctx, 0.2f);
            blockout_part(ctx, "warehouse", MeshType::Cube, math::Vector3(-ctx.width * 0.22f, 5.0f, 0.0f), math::Vector3(ctx.width * 0.42f, 10.0f, ctx.depth * 0.55f));
            blockout_part(ctx, "office", MeshType::Cube, math::Vector3(ctx.width * 0.32f, 2.5f, -ctx.depth * 0.28f), math::Vector3(ctx.width * 0.18f, 5.0f, ctx.depth * 0.2f));
            const int containers = std::max(4, static_cast<int>(8.0f * ctx.density));
            for (int i = 0; i < containers; i++)
            {
                const float x = ctx.width * 0.18f + static_cast<float>(i % 3) * 3.2f;
                const float z = -ctx.depth * 0.05f + static_cast<float>(i / 3) * 3.0f;
                const float y = 1.3f + static_cast<float>((i / 3) % 2) * 2.6f;
                blockout_part(ctx, "container_" + std::to_string(i), MeshType::Cube, math::Vector3(x, y, z), math::Vector3(6.0f, 2.6f, 2.5f), math::Vector3(0.0f, (i % 2) * 90.0f, 0.0f));
            }
            blockout_part(ctx, "crane_mast", MeshType::Cube, math::Vector3(ctx.width * 0.05f, 9.0f, ctx.depth * 0.28f), math::Vector3(1.0f, 18.0f, 1.0f));
            blockout_part(ctx, "crane_arm", MeshType::Cube, math::Vector3(ctx.width * 0.05f + 6.0f, 17.5f, ctx.depth * 0.28f), math::Vector3(14.0f, 0.8f, 0.8f));
            const float fence_h = 2.2f;
            blockout_part(ctx, "fence_n", MeshType::Cube, math::Vector3(0.0f, fence_h * 0.5f, -ctx.depth * 0.5f), math::Vector3(ctx.width, fence_h, 0.2f));
            blockout_part(ctx, "fence_s", MeshType::Cube, math::Vector3(0.0f, fence_h * 0.5f, ctx.depth * 0.5f), math::Vector3(ctx.width, fence_h, 0.2f));
            blockout_part(ctx, "fence_w", MeshType::Cube, math::Vector3(-ctx.width * 0.5f, fence_h * 0.5f, 0.0f), math::Vector3(0.2f, fence_h, ctx.depth));
            blockout_part(ctx, "fence_e", MeshType::Cube, math::Vector3(ctx.width * 0.5f, fence_h * 0.5f, 0.0f), math::Vector3(0.2f, fence_h, ctx.depth));
            blockout_light(ctx, "yard_light_a", math::Vector3(-ctx.width * 0.3f, 12.0f, -ctx.depth * 0.3f), 11000.0f, 40.0f);
            blockout_light(ctx, "yard_light_b", math::Vector3(ctx.width * 0.3f, 12.0f, ctx.depth * 0.2f), 11000.0f, 40.0f);
        }

        void build_district_residential(DistrictBuildContext& ctx)
        {
            build_district_pad(ctx, 0.15f);
            const int rows = std::max(2, static_cast<int>(2.0f * ctx.density));
            const int cols = std::max(3, static_cast<int>(3.0f * ctx.density));
            uint32_t index = 0;
            for (int row = 0; row < rows; row++)
            {
                for (int col = 0; col < cols; col++)
                {
                    const float x = -ctx.width * 0.35f + static_cast<float>(col) * (ctx.width * 0.7f / std::max(1, cols - 1));
                    const float z = -ctx.depth * 0.3f + static_cast<float>(row) * (ctx.depth * 0.6f / std::max(1, rows - 1));
                    const float h = ctx.rng->range(6.0f, 14.0f) * (0.85f + 0.15f * ctx.density);
                    blockout_part(ctx, "house_" + std::to_string(index), MeshType::Cube, math::Vector3(x, h * 0.5f, z), math::Vector3(ctx.width * 0.12f, h, ctx.depth * 0.14f));
                    if ((index % 3) == 0)
                    {
                        blockout_light(ctx, "house_light_" + std::to_string(index), math::Vector3(x, h + 1.5f, z), 6000.0f, 20.0f);
                    }
                    index++;
                }
            }
            blockout_part(ctx, "courtyard", MeshType::Cube, math::Vector3(0.0f, 0.2f, 0.0f), math::Vector3(ctx.width * 0.25f, 0.1f, ctx.depth * 0.25f));
        }

        void build_district_parking(DistrictBuildContext& ctx)
        {
            build_district_pad(ctx, 0.12f);
            const int rows = std::max(3, static_cast<int>(4.0f * ctx.density));
            for (int i = 0; i < rows; i++)
            {
                const float z = -ctx.depth * 0.4f + static_cast<float>(i) * (ctx.depth * 0.8f / std::max(1, rows - 1));
                blockout_part(ctx, "stall_strip_" + std::to_string(i), MeshType::Cube, math::Vector3(0.0f, 0.16f, z), math::Vector3(ctx.width * 0.85f, 0.05f, 0.25f));
            }
            const int poles = std::max(2, static_cast<int>(3.0f * ctx.density));
            for (int i = 0; i < poles; i++)
            {
                const float x = -ctx.width * 0.35f + static_cast<float>(i) * (ctx.width * 0.7f / std::max(1, poles - 1));
                blockout_part(ctx, "light_pole_" + std::to_string(i), MeshType::Cylinder, math::Vector3(x, 4.0f, ctx.depth * 0.42f), math::Vector3(0.2f, 8.0f, 0.2f));
                blockout_light(ctx, "lot_light_" + std::to_string(i), math::Vector3(x, 8.2f, ctx.depth * 0.42f), 9000.0f, 32.0f);
            }
        }

        void build_district_plaza(DistrictBuildContext& ctx)
        {
            build_district_pad(ctx, 0.18f);
            blockout_part(ctx, "wall_n", MeshType::Cube, math::Vector3(0.0f, 1.0f, -ctx.depth * 0.48f), math::Vector3(ctx.width * 0.9f, 2.0f, 0.35f));
            blockout_part(ctx, "wall_s", MeshType::Cube, math::Vector3(0.0f, 1.0f, ctx.depth * 0.48f), math::Vector3(ctx.width * 0.9f, 2.0f, 0.35f));
            blockout_part(ctx, "wall_w", MeshType::Cube, math::Vector3(-ctx.width * 0.48f, 1.0f, 0.0f), math::Vector3(0.35f, 2.0f, ctx.depth * 0.9f));
            blockout_part(ctx, "wall_e", MeshType::Cube, math::Vector3(ctx.width * 0.48f, 1.0f, 0.0f), math::Vector3(0.35f, 2.0f, ctx.depth * 0.9f));
            blockout_part(ctx, "monument_base", MeshType::Cube, math::Vector3(0.0f, 0.6f, 0.0f), math::Vector3(4.0f, 1.2f, 4.0f));
            blockout_part(ctx, "monument", MeshType::Cylinder, math::Vector3(0.0f, 5.0f, 0.0f), math::Vector3(1.2f, 8.0f, 1.2f));
            blockout_light(ctx, "plaza_light_a", math::Vector3(-ctx.width * 0.3f, 6.0f, -ctx.depth * 0.3f), 8000.0f, 28.0f);
            blockout_light(ctx, "plaza_light_b", math::Vector3(ctx.width * 0.3f, 6.0f, ctx.depth * 0.3f), 8000.0f, 28.0f);
            blockout_light(ctx, "plaza_center_light", math::Vector3(0.0f, 10.0f, 0.0f), 12000.0f, 35.0f);
        }

        void build_district_gas_station(DistrictBuildContext& ctx)
        {
            const float w = std::max(24.0f, ctx.width);
            const float d = std::max(18.0f, ctx.depth);
            ctx.width = w;
            ctx.depth = d;
            build_district_pad(ctx, 0.1f);
            blockout_part(ctx, "store", MeshType::Cube, math::Vector3(-w * 0.28f, 2.0f, -d * 0.2f), math::Vector3(8.0f, 4.0f, 6.0f));
            blockout_part(ctx, "store_roof", MeshType::Cube, math::Vector3(-w * 0.28f, 4.2f, -d * 0.2f), math::Vector3(8.6f, 0.35f, 6.6f));
            blockout_part(ctx, "canopy", MeshType::Cube, math::Vector3(w * 0.12f, 5.0f, d * 0.05f), math::Vector3(14.0f, 0.4f, 10.0f));
            blockout_part(ctx, "pillar_a", MeshType::Cylinder, math::Vector3(w * 0.12f - 5.0f, 2.5f, d * 0.05f - 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
            blockout_part(ctx, "pillar_b", MeshType::Cylinder, math::Vector3(w * 0.12f + 5.0f, 2.5f, d * 0.05f - 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
            blockout_part(ctx, "pillar_c", MeshType::Cylinder, math::Vector3(w * 0.12f - 5.0f, 2.5f, d * 0.05f + 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
            blockout_part(ctx, "pillar_d", MeshType::Cylinder, math::Vector3(w * 0.12f + 5.0f, 2.5f, d * 0.05f + 3.5f), math::Vector3(0.4f, 5.0f, 0.4f));
            for (int i = 0; i < 4; i++)
            {
                const float x = w * 0.12f - 3.0f + static_cast<float>(i) * 2.0f;
                blockout_part(ctx, "pump_" + std::to_string(i), MeshType::Cube, math::Vector3(x, 1.0f, d * 0.05f), math::Vector3(0.7f, 2.0f, 0.7f));
            }
            blockout_part(ctx, "sign_pole", MeshType::Cylinder, math::Vector3(w * 0.4f, 4.0f, -d * 0.35f), math::Vector3(0.25f, 8.0f, 0.25f));
            blockout_part(ctx, "sign_board", MeshType::Cube, math::Vector3(w * 0.4f, 7.5f, -d * 0.35f), math::Vector3(3.5f, 2.0f, 0.3f));
            blockout_light(ctx, "gs_light_canopy", math::Vector3(w * 0.12f, 4.7f, d * 0.05f), 12000.0f, 35.0f);
            blockout_light(ctx, "gs_light_store", math::Vector3(-w * 0.28f, 3.5f, -d * 0.05f), 8500.0f, 25.0f);
            blockout_light(ctx, "gs_light_sign", math::Vector3(w * 0.4f, 8.0f, -d * 0.35f), 7000.0f, 20.0f);
        }

        void build_district_preset(DistrictBuildContext& ctx, DistrictPreset preset)
        {
            switch (preset)
            {
            case DistrictPreset::Market: build_district_market(ctx); break;
            case DistrictPreset::Downtown: build_district_downtown(ctx); break;
            case DistrictPreset::Park: build_district_park(ctx); break;
            case DistrictPreset::Industrial: build_district_industrial(ctx); break;
            case DistrictPreset::Residential: build_district_residential(ctx); break;
            case DistrictPreset::Parking: build_district_parking(ctx); break;
            case DistrictPreset::Plaza: build_district_plaza(ctx); break;
            case DistrictPreset::GasStation: build_district_gas_station(ctx); break;
            }
        }

        math::Vector2 default_footprint_for_preset(DistrictPreset preset)
        {
            switch (preset)
            {
            case DistrictPreset::Market: return math::Vector2(48.0f, 40.0f);
            case DistrictPreset::Downtown: return math::Vector2(70.0f, 70.0f);
            case DistrictPreset::Park: return math::Vector2(60.0f, 50.0f);
            case DistrictPreset::Industrial: return math::Vector2(80.0f, 55.0f);
            case DistrictPreset::Residential: return math::Vector2(55.0f, 45.0f);
            case DistrictPreset::Parking: return math::Vector2(45.0f, 35.0f);
            case DistrictPreset::Plaza: return math::Vector2(40.0f, 40.0f);
            case DistrictPreset::GasStation: return math::Vector2(28.0f, 22.0f);
            }
            return math::Vector2(40.0f, 40.0f);
        }

        std::string district_receipt_json(Entity* entity, DistrictPreset preset, uint32_t part_count, uint32_t light_count)
        {
            std::string json = "{";
            json += "\"entity\":" + entity_to_json_compact(entity);
            json += ",\"preset\":" + json_string(district_preset_to_name(preset));
            json += ",\"created_count\":" + std::to_string(part_count);
            json += ",\"lights_count\":" + std::to_string(light_count);
            math::BoundingBox bounds;
            if (subtree_render_bounds(entity, bounds))
            {
                json += ",\"bounding_box\":" + json_bounding_box(bounds);
            }
            json += "}";
            return json;
        }

        bool boxes_overlap_xz(const math::BoundingBox& a, const math::BoundingBox& b, float gap)
        {
            return !(a.GetMax().x + gap < b.GetMin().x || b.GetMax().x + gap < a.GetMin().x ||
                     a.GetMax().z + gap < b.GetMin().z || b.GetMax().z + gap < a.GetMin().z);
        }

        math::BoundingBox footprint_box_at(const math::Vector3& center, float width, float depth)
        {
            const math::Vector3 half(width * 0.5f, 20.0f, depth * 0.5f);
            return math::BoundingBox(center - half, center + half);
        }

        std::vector<math::BoundingBox> collect_existing_landmark_boxes()
        {
            std::vector<math::BoundingBox> boxes;
            for (Entity* entity : World::GetEntities())
            {
                if (entity == nullptr || entity->GetParent() != nullptr)
                {
                    continue;
                }
                if (is_landmark_noise(entity))
                {
                    continue;
                }
                math::BoundingBox bounds;
                if (!subtree_render_bounds(entity, bounds))
                {
                    continue;
                }
                const math::Vector3 size = bounds.GetSize();
                if (size.x < 4.0f && size.z < 4.0f)
                {
                    continue;
                }
                if (size.x > 5000.0f || size.z > 5000.0f)
                {
                    continue;
                }
                boxes.push_back(bounds);
            }
            return boxes;
        }

        bool footprint_blocked(const math::Vector3& center, float width, float depth, const std::vector<math::BoundingBox>& reserved, float gap)
        {
            const math::BoundingBox candidate = footprint_box_at(center, width, depth);
            for (const math::BoundingBox& box : reserved)
            {
                if (boxes_overlap_xz(candidate, box, gap))
                {
                    return true;
                }
            }
            return false;
        }

        Entity* create_district_entity(const std::string& name, const math::Vector3& position, float rotation_y, DistrictPreset preset, float width, float depth, float density, bool lights, uint32_t seed, bool replace, uint32_t& part_count, uint32_t& light_count, std::string& error)
        {
            Entity* parent = nullptr;
            {
                std::string resolve_error;
                parent = find_entity_by_name_unique(name, true, resolve_error);
            }
            if (parent != nullptr)
            {
                if (!replace)
                {
                    error = "entity already exists: " + name;
                    return nullptr;
                }
                clear_entity_children(parent);
            }
            else
            {
                parent = World::CreateEntity();
                if (parent == nullptr)
                {
                    error = "failed to create district entity";
                    return nullptr;
                }
                parent->SetObjectName(name);
            }

            parent->SetPosition(position);
            if (std::fabs(rotation_y) > 0.001f)
            {
                parent->SetRotation(math::Quaternion::FromEulerAngles(math::Vector3(0.0f, rotation_y, 0.0f)));
            }
            parent->AddTag("landmark");
            parent->AddTag("district");
            parent->AddTag(district_preset_to_name(preset));

            DistrictRng rng(seed);
            DistrictBuildContext ctx;
            ctx.parent = parent;
            ctx.rng = &rng;
            ctx.width = width;
            ctx.depth = depth;
            ctx.density = density;
            ctx.lights = lights;
            build_district_preset(ctx, preset);
            part_count = ctx.part_count;
            light_count = ctx.light_count;
            return parent;
        }

        std::string command_district_blockout(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("district blockout requires edit mode");
            }

            const std::optional<std::string> preset_arg = get_argument(request, "preset");
            if (!preset_arg)
            {
                return json_error("missing preset");
            }
            const std::optional<DistrictPreset> preset = district_preset_from_name(*preset_arg);
            if (!preset)
            {
                return json_error("invalid preset, expected market, downtown, park, industrial, residential, parking, plaza, or gas_station");
            }

            math::Vector3 position = math::Vector3::Zero;
            if (const std::optional<std::string> position_arg = get_argument(request, "position"))
            {
                if (!parse_vector3(*position_arg, position))
                {
                    return json_error("invalid position");
                }
            }

            math::Vector2 footprint = default_footprint_for_preset(*preset);
            if (const std::optional<std::string> footprint_arg = get_argument(request, "footprint"))
            {
                if (!parse_vector2(*footprint_arg, footprint) || footprint.x <= 1.0f || footprint.y <= 1.0f)
                {
                    return json_error("footprint must be width,depth meters");
                }
            }

            float rotation_y = 0.0f;
            if (const std::optional<std::string> rotation_arg = get_argument(request, "rotation_y"))
            {
                if (!parse_float(*rotation_arg, rotation_y))
                {
                    return json_error("invalid rotation_y");
                }
            }

            uint32_t seed = 1;
            if (const std::optional<std::string> seed_arg = get_argument(request, "seed"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*seed_arg, parsed))
                {
                    return json_error("invalid seed");
                }
                seed = static_cast<uint32_t>(parsed);
            }

            bool lights = true;
            if (const std::optional<std::string> lights_arg = get_argument(request, "lights"))
            {
                if (!parse_bool(*lights_arg, lights))
                {
                    return json_error("invalid lights");
                }
            }

            bool replace = true;
            if (const std::optional<std::string> replace_arg = get_argument(request, "replace"))
            {
                if (!parse_bool(*replace_arg, replace))
                {
                    return json_error("invalid replace");
                }
            }

            float density = 1.0f;
            if (const std::optional<std::string> density_arg = get_argument(request, "density"))
            {
                density = district_density_scale(*density_arg);
            }

            const std::string name = get_argument(request, "name").value_or(district_preset_to_name(*preset));
            uint32_t part_count = 0;
            uint32_t light_count = 0;
            std::string error;
            Entity* entity = create_district_entity(name, position, rotation_y, *preset, footprint.x, footprint.y, density, lights, seed, replace, part_count, light_count, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            std::string json = "{\"ok\":true";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += ",\"preset\":" + json_string(district_preset_to_name(*preset));
            json += ",\"created_count\":" + std::to_string(part_count);
            json += ",\"lights_count\":" + std::to_string(light_count);
            json += ",\"footprint\":[" + std::to_string(footprint.x) + "," + std::to_string(footprint.y) + "]";
            math::BoundingBox bounds;
            if (subtree_render_bounds(entity, bounds))
            {
                json += ",\"bounding_box\":" + json_bounding_box(bounds);
            }
            json += "}";
            return json;
        }

        std::string command_city_blockout(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("city blockout requires edit mode");
            }

            math::Vector3 center = math::Vector3::Zero;
            if (const std::optional<std::string> center_arg = get_argument(request, "center"))
            {
                if (!parse_vector3(*center_arg, center))
                {
                    return json_error("invalid center");
                }
            }

            float extent = 220.0f;
            if (const std::optional<std::string> extent_arg = get_argument(request, "extent"))
            {
                if (!parse_float(*extent_arg, extent) || extent < 40.0f || extent > 2000.0f)
                {
                    return json_error("extent must be between 40 and 2000");
                }
            }

            uint32_t seed = 7;
            if (const std::optional<std::string> seed_arg = get_argument(request, "seed"))
            {
                uint64_t parsed = 0;
                if (!parse_uint64(*seed_arg, parsed))
                {
                    return json_error("invalid seed");
                }
                seed = static_cast<uint32_t>(parsed);
            }

            bool avoid_existing = true;
            if (const std::optional<std::string> avoid_arg = get_argument(request, "avoid_existing"))
            {
                if (!parse_bool(*avoid_arg, avoid_existing))
                {
                    return json_error("invalid avoid_existing");
                }
            }

            bool lights = true;
            if (const std::optional<std::string> lights_arg = get_argument(request, "lights"))
            {
                if (!parse_bool(*lights_arg, lights))
                {
                    return json_error("invalid lights");
                }
            }

            bool replace = true;
            if (const std::optional<std::string> replace_arg = get_argument(request, "replace"))
            {
                if (!parse_bool(*replace_arg, replace))
                {
                    return json_error("invalid replace");
                }
            }

            bool connect_roads = false;
            if (const std::optional<std::string> roads_arg = get_argument(request, "connect_roads"))
            {
                if (!parse_bool(*roads_arg, connect_roads))
                {
                    return json_error("invalid connect_roads");
                }
            }

            float density = 1.0f;
            if (const std::optional<std::string> density_arg = get_argument(request, "density"))
            {
                density = district_density_scale(*density_arg);
            }

            float corridor = 28.0f;
            if (const std::optional<std::string> corridor_arg = get_argument(request, "corridor"))
            {
                if (!parse_float(*corridor_arg, corridor) || corridor < 8.0f || corridor > 120.0f)
                {
                    return json_error("corridor must be between 8 and 120");
                }
            }

            struct PlannedDistrict
            {
                std::string name;
                DistrictPreset preset = DistrictPreset::Market;
                math::Vector3 position = math::Vector3::Zero;
                math::Vector2 footprint = math::Vector2(40.0f, 40.0f);
                bool has_position = false;
            };

            std::vector<PlannedDistrict> planned;
            if (const std::optional<std::string> districts_arg = get_argument(request, "districts"))
            {
                const std::vector<std::string> tokens = split_csv_tokens(*districts_arg);
                for (const std::string& token : tokens)
                {
                    std::string preset_name = token;
                    std::string district_name;
                    const size_t colon = token.find(':');
                    if (colon != std::string::npos)
                    {
                        preset_name = token.substr(0, colon);
                        district_name = token.substr(colon + 1);
                    }
                    const std::optional<DistrictPreset> preset = district_preset_from_name(preset_name);
                    if (!preset)
                    {
                        return json_error("invalid district preset: " + preset_name);
                    }
                    PlannedDistrict entry;
                    entry.preset = *preset;
                    entry.name = district_name.empty() ? district_preset_to_name(*preset) : district_name;
                    entry.footprint = default_footprint_for_preset(*preset);
                    planned.push_back(std::move(entry));
                }
            }
            else
            {
                const DistrictPreset defaults[] = {
                    DistrictPreset::Downtown,
                    DistrictPreset::Market,
                    DistrictPreset::Park,
                    DistrictPreset::Industrial,
                    DistrictPreset::Residential,
                    DistrictPreset::Parking
                };
                for (DistrictPreset preset : defaults)
                {
                    PlannedDistrict entry;
                    entry.preset = preset;
                    entry.name = district_preset_to_name(preset);
                    entry.footprint = default_footprint_for_preset(preset);
                    planned.push_back(std::move(entry));
                }
            }

            if (planned.empty())
            {
                return json_error("no districts to place");
            }

            if (const std::optional<std::string> names_arg = get_argument(request, "names"))
            {
                const std::vector<std::string> names = split_csv_tokens(*names_arg);
                for (size_t i = 0; i < planned.size() && i < names.size(); i++)
                {
                    if (!names[i].empty())
                    {
                        planned[i].name = names[i];
                    }
                }
            }

            if (const std::optional<std::string> footprints_arg = get_argument(request, "footprints"))
            {
                std::vector<float> values;
                std::stringstream stream(*footprints_arg);
                std::string part;
                while (std::getline(stream, part, ','))
                {
                    float parsed = 0.0f;
                    if (!parse_float(part, parsed))
                    {
                        return json_error("footprints must be flat width,depth pairs");
                    }
                    values.push_back(parsed);
                }
                if (values.size() < 2 || (values.size() % 2) != 0)
                {
                    return json_error("footprints must be flat width,depth pairs");
                }
                for (size_t i = 0; i < planned.size() && (i * 2 + 1) < values.size(); i++)
                {
                    planned[i].footprint = math::Vector2(values[i * 2], values[i * 2 + 1]);
                }
            }

            if (const std::optional<std::string> positions_arg = get_argument(request, "positions"))
            {
                std::vector<math::Vector3> positions;
                if (!parse_vector3_list(*positions_arg, positions))
                {
                    return json_error("positions must be a flat xyz list");
                }
                for (size_t i = 0; i < planned.size() && i < positions.size(); i++)
                {
                    planned[i].position = positions[i];
                    planned[i].has_position = true;
                }
            }

            std::vector<math::BoundingBox> reserved;
            if (avoid_existing)
            {
                reserved = collect_existing_landmark_boxes();
            }

            DistrictRng rng(seed);
            const float ring = extent * 0.55f;
            for (size_t i = 0; i < planned.size(); i++)
            {
                PlannedDistrict& entry = planned[i];
                if (entry.has_position)
                {
                    continue;
                }

                bool placed = false;
                for (int attempt = 0; attempt < 48; attempt++)
                {
                    const float angle = (static_cast<float>(i) / static_cast<float>(planned.size())) * 6.2831853f + rng.range(-0.35f, 0.35f);
                    const float radius = ring * rng.range(0.55f, 1.05f);
                    math::Vector3 candidate = center + math::Vector3(std::cos(angle) * radius, 0.0f, std::sin(angle) * radius);
                    candidate.y = center.y;
                    if (!footprint_blocked(candidate, entry.footprint.x, entry.footprint.y, reserved, corridor))
                    {
                        entry.position = candidate;
                        entry.has_position = true;
                        reserved.push_back(footprint_box_at(candidate, entry.footprint.x, entry.footprint.y));
                        placed = true;
                        break;
                    }
                }
                if (!placed)
                {
                    // fall back to a spaced slot even if tight
                    const float angle = (static_cast<float>(i) / static_cast<float>(planned.size())) * 6.2831853f;
                    entry.position = center + math::Vector3(std::cos(angle) * ring, 0.0f, std::sin(angle) * ring);
                    entry.has_position = true;
                    reserved.push_back(footprint_box_at(entry.position, entry.footprint.x, entry.footprint.y));
                }
            }

            std::string json = "{\"ok\":true,\"districts\":[";
            std::vector<std::string> created_names;
            created_names.reserve(planned.size());
            for (size_t i = 0; i < planned.size(); i++)
            {
                const PlannedDistrict& entry = planned[i];
                uint32_t part_count = 0;
                uint32_t light_count = 0;
                std::string error;
                Entity* entity = create_district_entity(
                    entry.name,
                    entry.position,
                    0.0f,
                    entry.preset,
                    entry.footprint.x,
                    entry.footprint.y,
                    density,
                    lights,
                    seed + static_cast<uint32_t>(i) * 97u,
                    replace,
                    part_count,
                    light_count,
                    error
                );
                if (entity == nullptr)
                {
                    return json_error(error);
                }
                if (i > 0)
                {
                    json += ",";
                }
                json += district_receipt_json(entity, entry.preset, part_count, light_count);
                created_names.push_back(entry.name);
            }
            json += "]";

            // road hint points: approach edges facing city center for a later arterial pass
            json += ",\"road_hints\":[";
            for (size_t i = 0; i < planned.size(); i++)
            {
                const PlannedDistrict& entry = planned[i];
                math::Vector3 toward = center;
                if ((center - entry.position).LengthSquared() < 1.0f && planned.size() > 1)
                {
                    toward = planned[(i + 1) % planned.size()].position;
                }
                math::Vector3 approach = entry.position;
                {
                    std::string resolve_error;
                    Entity* entity = find_entity_by_name_unique(entry.name, true, resolve_error);
                    if (entity != nullptr)
                    {
                        landmark_approach_point(entity, toward, corridor * 0.35f, approach);
                    }
                }
                if (i > 0)
                {
                    json += ",";
                }
                json += "{\"district\":" + json_string(entry.name);
                json += ",\"point\":" + json_vector3(approach) + "}";
            }
            json += "]";

            uint32_t roads_created = 0;
            if (connect_roads && created_names.size() >= 2)
            {
                json += ",\"roads\":[";
                bool first_road = true;
                for (size_t i = 0; i + 1 < created_names.size(); i++)
                {
                    McpRequest connect_request;
                    connect_request.command = "spline_connect";
                    connect_request.arguments["landmarks"] = created_names[i] + "," + created_names[i + 1];
                    connect_request.arguments["name"] = "road_" + created_names[i] + "_" + created_names[i + 1];
                    connect_request.arguments["avoid_obstacles"] = "true";
                    connect_request.arguments["conform_to_terrain"] = "true";
                    connect_request.arguments["sidewalk_enabled"] = "true";
                    const std::string road_result = command_spline_connect(connect_request);
                    if (road_result.find("\"ok\":true") == std::string::npos)
                    {
                        continue;
                    }
                    if (!first_road)
                    {
                        json += ",";
                    }
                    first_road = false;
                    json += "{\"from\":" + json_string(created_names[i]) + ",\"to\":" + json_string(created_names[i + 1]) + "}";
                    roads_created++;
                }
                json += "]";
            }

            json += ",\"count\":" + std::to_string(created_names.size());
            json += ",\"roads_created\":" + std::to_string(roads_created);
            json += ",\"center\":" + json_vector3(center);
            json += ",\"extent\":" + std::to_string(extent);
            json += ",\"corridor\":" + std::to_string(corridor);
            json += "}";
            return json;
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

    namespace
    {
        // commands registered by higher layers like the editor
        std::unordered_map<std::string, McpCommandHandler>& get_external_commands()
        {
            static std::unordered_map<std::string, McpCommandHandler> commands;
            return commands;
        }
    }

    void RegisterMcpCommand(const std::string& name, McpCommandHandler handler)
    {
        get_external_commands()[name] = std::move(handler);
    }

    void UnregisterMcpCommand(const std::string& name)
    {
        get_external_commands().erase(name);
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
        if (request.command == "profiler_snapshot")
        {
            return command_profiler_snapshot(request);
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
        if (request.command == "world_resources_clean")
        {
            return command_world_resources_clean();
        }
        if (request.command == "world_set_environment")
        {
            return command_world_set_environment(request);
        }
        if (request.command == "world_raycast")
        {
            return command_world_raycast(request);
        }
        if (request.command == "entity_snap")
        {
            return command_entity_snap(request);
        }
        if (request.command == "entity_spatial_snapshot")
        {
            return command_entity_spatial_snapshot(request);
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
        if (request.command == "mesh_generate")
        {
            return command_mesh_generate(request);
        }
        if (request.command == "mesh_generate_batch")
        {
            return command_mesh_generate_batch(request);
        }
        if (request.command == "render_set_mesh")
        {
            return command_render_set_mesh(request);
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
        if (request.command == "entity_set_transform_batch")
        {
            return command_entity_set_transform_batch(request);
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
        if (request.command == "material_apply_preset")
        {
            return command_material_apply_preset(request);
        }
        if (request.command == "material_semantic_create")
        {
            return command_material_semantic_create(request);
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
        if (request.command == "vehicle_list")
        {
            return command_vehicle_list();
        }
        if (request.command == "vehicle_get")
        {
            return command_vehicle_get(request);
        }
        if (request.command == "vehicle_enter")
        {
            return command_vehicle_enter(request);
        }
        if (request.command == "vehicle_exit")
        {
            return command_vehicle_exit(request);
        }
        if (request.command == "vehicle_set_input")
        {
            return command_vehicle_set_input(request);
        }
        if (request.command == "vehicle_shift")
        {
            return command_vehicle_shift(request);
        }
        if (request.command == "vehicle_reset")
        {
            return command_vehicle_reset(request);
        }
        if (request.command == "vehicle_set_view")
        {
            return command_vehicle_set_view(request);
        }
        if (request.command == "vehicle_telemetry")
        {
            return command_vehicle_telemetry(request);
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
        if (request.command == "spline_query")
        {
            return command_spline_query(request);
        }
        if (request.command == "spline_distribute")
        {
            return command_spline_distribute(request);
        }
        if (request.command == "world_landmarks")
        {
            return command_world_landmarks(request);
        }
        if (request.command == "spline_create_road")
        {
            return command_spline_create_road(request);
        }
        if (request.command == "spline_set_control_points")
        {
            return command_spline_set_control_points(request);
        }
        if (request.command == "spline_reroute")
        {
            return command_spline_reroute(request);
        }
        if (request.command == "spline_connect")
        {
            return command_spline_connect(request);
        }
        if (request.command == "spline_junction")
        {
            return command_spline_junction(request);
        }
        if (request.command == "spline_decorate")
        {
            return command_spline_decorate(request);
        }
        if (request.command == "district_blockout")
        {
            return command_district_blockout(request);
        }
        if (request.command == "city_blockout")
        {
            return command_city_blockout(request);
        }
        if (request.command == "execute_lua")
        {
            return command_execute_lua(request);
        }

        const auto& external_commands = get_external_commands();
        const auto it = external_commands.find(request.command);
        if (it != external_commands.end())
        {
            return it->second(request);
        }

        return json_error("unknown command");
    }
}
