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
#include "McpCommandsCommon.h"
#include "McpCommandsWorldBuild.h"
#include "McpGeometryKernel.h"
#include "McpTextureKernel.h"
#include "../commands/console/ConsoleCommands.h"
#include "../commands/CommandStack.h"
#include "../core/ProgressTracker.h"
#include "../logging/Log.h"
#include "../physics/PhysicsWorld.h"
#include "../profiling/Profiler.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/components/Component.h"
#include "../world/components/AudioSource.h"
#include "../world/components/Light.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Physics.h"
#include "../world/components/Render.h"
#include "../world/components/Script.h"
#include "../world/components/Spline.h"
#include "../world/components/SplineFollower.h"
#include "../world/components/Terrain.h"
#include "../world/components/Text3D.h"
#include "../world/Prefab.h"
#include "../world/GameReady.h"
#include "../car/Car.h"
#include "../car/CarSimulation.h"
#include "../car/CarState.h"
#include "../resource/ResourceCache.h"
#include "../resource/import/ImageImporter.h"
#include "../animation/Animation.h"
#include "../geometry/GeometryGeneration.h"
#include "../geometry/Mesh.h"
#include "../rhi/RHI_Texture.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include "../math/Vector2.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <optional>
#include <sstream>
#include <typeinfo>
#include <unordered_map>
//==============================================

namespace spartan
{
    namespace
    {
        // the json, argument and entity helpers now live in McpCommandsCommon so the world build commands can
        // reach them from their own file, this keeps every call site here written the same way as before
        using namespace mcp_common;

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

        // a batch runs its items through the singular handler and reads the reply back, which is the only
        // thing a handler hands over
        bool item_succeeded(const std::string& item_result)
        {
            return item_result.find("\"ok\":true") != std::string::npos;
        }

        // a batch stops at the first item that fails and does not undo the ones before it, several of them
        // have already written files or created entities. so the reply says how far it got and where it
        // stopped, which is what lets a caller carry on from there instead of guessing or starting over.
        // items_field names the list the way the singular command does, created for entities and so on
        std::string json_batch_failure(
            const std::string& error,
            const std::string& items_field,
            const std::string& applied_items,
            const uint32_t applied_count,
            const uint64_t failed_index,
            const std::string& failure
        )
        {
            std::string json = "{\"ok\":false,\"error\":" + json_string(error);
            json += ",\"" + items_field + "\":" + applied_items + "]";
            json += ",\"" + items_field + "_count\":" + std::to_string(applied_count);
            json += ",\"failed_index\":" + std::to_string(failed_index);
            json += ",\"failure\":" + failure;
            json += "}";
            return json;
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

        bool parse_float_array(
            const std::string& value,
            std::vector<float>& values,
            size_t max_count
        )
        {
            if (value.empty())
            {
                return false;
            }

            std::stringstream stream(value);
            std::string part;
            while (std::getline(stream, part, ','))
            {
                if (values.size() >= max_count)
                {
                    return false;
                }

                float parsed = 0.0f;
                if (!parse_float(part, parsed))
                {
                    return false;
                }
                values.emplace_back(parsed);
            }

            return !values.empty();
        }

        bool parse_index_array(
            const std::string& value,
            std::vector<uint32_t>& values,
            size_t max_count
        )
        {
            if (value.empty())
            {
                return false;
            }

            std::stringstream stream(value);
            std::string part;
            while (std::getline(stream, part, ','))
            {
                if (values.size() >= max_count || part.empty())
                {
                    return false;
                }
                if (
                    !std::all_of(
                        part.begin(),
                        part.end(),
                        [](unsigned char character)
                        {
                            return std::isdigit(character) != 0;
                        }
                    )
                )
                {
                    return false;
                }

                char* end = nullptr;
                const unsigned long long parsed =
                    std::strtoull(part.c_str(), &end, 10);
                if (
                    end == part.c_str() ||
                    *end != '\0' ||
                    parsed > UINT32_MAX
                )
                {
                    return false;
                }
                values.emplace_back(static_cast<uint32_t>(parsed));
            }

            return !values.empty();
        }

        bool path_is_within(
            const std::filesystem::path& path,
            const std::filesystem::path& directory
        )
        {
            const std::filesystem::path normalized_path =
                std::filesystem::absolute(path).lexically_normal();
            const std::filesystem::path normalized_directory =
                std::filesystem::absolute(directory).lexically_normal();

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
                if (
                    to_lower_copy(path_it->string()) !=
                    to_lower_copy(directory_it->string())
                )
                {
                    return false;
                }
            }

            return true;
        }

        std::string active_mcp_resource_directory()
        {
            return World::GetGeneratedResourceDirectory();
        }

        std::optional<std::string> resolve_mcp_mesh_path(
            const McpRequest& request,
            std::string& error
        )
        {
            const std::optional<std::string> path_arg =
                get_argument(request, "path");
            const std::optional<std::string> name_arg =
                get_argument(request, "name");
            if (
                (!path_arg || path_arg->empty()) &&
                (!name_arg || name_arg->empty())
            )
            {
                error = "missing path or name";
                return std::nullopt;
            }

            const std::filesystem::path meshes_directory =
                std::filesystem::path(
                    active_mcp_resource_directory()
                ) /
                "meshes";
            std::filesystem::path requested =
                path_arg && !path_arg->empty()
                ? std::filesystem::path(*path_arg)
                : std::filesystem::path(*name_arg);

            std::filesystem::path resolved;
            if (path_is_within(requested, meshes_directory))
            {
                resolved = requested;
            }
            else
            {
                resolved =
                    meshes_directory /
                    requested.filename();
            }

            if (resolved.extension() != EXTENSION_MESH)
            {
                resolved += EXTENSION_MESH;
            }
            resolved = std::filesystem::absolute(
                resolved
            ).lexically_normal();
            if (!path_is_within(resolved, meshes_directory))
            {
                error =
                    "mesh path must be inside the shared project mcp/meshes directory";
                return std::nullopt;
            }

            return FileSystem::GetRelativePath(
                resolved.generic_string()
            );
        }

        std::optional<std::string> resolve_mcp_texture_path(
            const McpRequest& request,
            std::string& error
        )
        {
            const std::optional<std::string> path_arg =
                get_argument(request, "path");
            const std::optional<std::string> name_arg =
                get_argument(request, "name");
            if (
                (!path_arg || path_arg->empty()) &&
                (!name_arg || name_arg->empty())
            )
            {
                error = "missing path or name";
                return std::nullopt;
            }

            const std::filesystem::path textures_directory =
                std::filesystem::path(
                    active_mcp_resource_directory()
                ) /
                "textures";
            std::filesystem::path requested =
                path_arg && !path_arg->empty()
                ? std::filesystem::path(*path_arg)
                : std::filesystem::path(*name_arg);

            std::filesystem::path resolved;
            if (path_is_within(requested, textures_directory))
            {
                resolved = requested;
            }
            else
            {
                resolved =
                    textures_directory /
                    requested.filename();
            }

            if (to_lower_copy(resolved.extension().string()) != ".png")
            {
                resolved.replace_extension(".png");
            }
            resolved = std::filesystem::absolute(
                resolved
            ).lexically_normal();
            if (!path_is_within(resolved, textures_directory))
            {
                error =
                    "texture path must be inside the shared project mcp/textures directory";
                return std::nullopt;
            }

            return FileSystem::GetRelativePath(
                resolved.generic_string()
            );
        }

        std::optional<std::string> resolve_mcp_output_path(
            const std::string& requested_path,
            const char* directory_name,
            const std::string& extension,
            std::string& error
        )
        {
            const std::filesystem::path directory =
                std::filesystem::path(
                    active_mcp_resource_directory()
                ) /
                directory_name;
            const std::filesystem::path requested(
                requested_path
            );
            std::filesystem::path resolved;
            if (path_is_within(requested, directory))
            {
                resolved = requested;
            }
            else
            {
                resolved = directory / requested.filename();
            }
            if (
                to_lower_copy(resolved.extension().string()) !=
                extension
            )
            {
                resolved.replace_extension(extension);
            }
            resolved = std::filesystem::absolute(
                resolved
            ).lexically_normal();
            if (!path_is_within(resolved, directory))
            {
                error =
                    "resource path must be inside project/mcp/blockout/" +
                    std::string(directory_name);
                return std::nullopt;
            }
            return FileSystem::GetRelativePath(
                resolved.generic_string()
            );
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
                values.size() > 256 ||
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
            const std::string normalized = to_lower_copy(name);
            if (
                normalized == "cube" ||
                normalized == "box" ||
                normalized == "standard_cube"
            )
            {
                return MeshType::Cube;
            }
            if (
                normalized == "quad" ||
                normalized == "plane" ||
                normalized == "standard_quad"
            )
            {
                return MeshType::Quad;
            }
            if (
                normalized == "sphere" ||
                normalized == "ball" ||
                normalized == "standard_sphere"
            )
            {
                return MeshType::Sphere;
            }
            if (
                normalized == "cylinder" ||
                normalized == "standard_cylinder"
            )
            {
                return MeshType::Cylinder;
            }
            if (
                normalized == "cone" ||
                normalized == "standard_cone"
            )
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
            if (name == "heightfield")
            {
                return BodyType::Heightfield;
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
            case BodyType::Heightfield:
                return "heightfield";
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

        void add_entity_tags(
            Entity* entity,
            const std::string& comma_separated
        )
        {
            size_t start = 0;
            while (start <= comma_separated.size())
            {
                const size_t end =
                    comma_separated.find(',', start);
                std::string tag = comma_separated.substr(
                    start,
                    end == std::string::npos
                        ? std::string::npos
                        : end - start
                );
                const size_t first = tag.find_first_not_of(
                    " \t\r\n"
                );
                const size_t last = tag.find_last_not_of(
                    " \t\r\n"
                );
                tag = first == std::string::npos
                    ? ""
                    : tag.substr(first, last - first + 1);
                if (!tag.empty())
                {
                    entity->AddTag(tag);
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
        }

        void apply_entity_identity(
            Entity* entity,
            const McpRequest& request
        )
        {
            if (
                const std::optional<std::string> tags =
                    get_argument(request, "tags")
            )
            {
                add_entity_tags(entity, *tags);
            }

            const std::array<std::string, 2> keys =
            {
                "semantic_id",
                "plan_element"
            };
            for (const std::string& key : keys)
            {
                if (
                    const std::optional<std::string> value =
                        get_argument(request, key);
                    value && !value->empty()
                )
                {
                    entity->AddTag(key + "=" + *value);
                }
            }

            if (
                const std::optional<std::string> semantic_tags =
                    get_argument(request, "semantic_tags")
            )
            {
                add_entity_tags(entity, *semantic_tags);
            }
        }

        bool is_world_path_valid(const std::string& path)
        {
            std::filesystem::path file_path(path);
            return file_path.is_absolute() && file_path.extension() == ".world";
        }

        std::string normalize_screenshot_path(const std::optional<std::string>& path)
        {
            std::filesystem::path file_name =
                path && !path->empty()
                ? std::filesystem::path(*path).filename()
                : std::filesystem::path(
                    "mcp_screenshot_" +
                    std::to_string(
                        Renderer::GetFrameNumber()
                    ) +
                    ".png"
                );
            if (file_name.extension().empty())
            {
                file_name.replace_extension(".png");
            }
            return std::filesystem::absolute(
                std::filesystem::path(
                    World::GetGeneratedResourceDirectory()
                ) /
                "thumbnails" /
                file_name
            ).lexically_normal().generic_string();
        }

        bool is_screenshot_path_valid(const std::string& path)
        {
            const std::filesystem::path screenshot_root =
                std::filesystem::absolute(
                    std::filesystem::path(
                        World::GetGeneratedResourceDirectory()
                    ) /
                    "thumbnails"
                ).lexically_normal();
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

        std::string text_3d_alignment_to_name(
            const Text3DAlignment alignment
        )
        {
            switch (alignment)
            {
            case Text3DAlignment::Left:
                return "left";
            case Text3DAlignment::Center:
                return "center";
            case Text3DAlignment::Right:
                return "right";
            default:
                return "unknown";
            }
        }

        std::optional<Text3DAlignment>
        text_3d_alignment_from_name(const std::string& name)
        {
            const std::string normalized = to_lower_copy(name);
            if (normalized == "left" || normalized == "0")
            {
                return Text3DAlignment::Left;
            }
            if (normalized == "center" || normalized == "1")
            {
                return Text3DAlignment::Center;
            }
            if (normalized == "right" || normalized == "2")
            {
                return Text3DAlignment::Right;
            }

            return std::nullopt;
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
            case MaterialProperty::IsSkidMark:
                return "is_skid_mark";
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
            if (
                name == "alpha" ||
                name == "opacity"
            )
            {
                return MaterialProperty::ColorA;
            }
            if (name == "metallic")
            {
                return MaterialProperty::Metalness;
            }
            if (
                name == "ior" ||
                name == "refraction" ||
                name == "refractive_index" ||
                name == "index_of_refraction"
            )
            {
                return MaterialProperty::Ior;
            }
            if (
                name == "glass_thickness" ||
                name == "shell_thickness"
            )
            {
                return MaterialProperty::Thickness;
            }
            if (
                name == "dye_density" ||
                name == "tint_density"
            )
            {
                return MaterialProperty::Absorption;
            }
            if (name == "subsurface")
            {
                return MaterialProperty::SubsurfaceScattering;
            }
            if (name == "emissive")
            {
                return MaterialProperty::EmissiveFromAlbedo;
            }

            return std::nullopt;
        }

        // gltf style names whose convention is inverted relative to the engine property
        std::optional<MaterialProperty> material_property_inverted_from_name(
            const std::string& name
        )
        {
            if (
                name == "transmission" ||
                name == "transparency"
            )
            {
                return MaterialProperty::ColorA;
            }

            return std::nullopt;
        }

        std::string material_property_names_csv()
        {
            std::string names;
            for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialProperty::Max); i++)
            {
                const std::string name =
                    material_property_to_name(static_cast<MaterialProperty>(i));
                if (name == "unknown")
                {
                    continue;
                }

                if (!names.empty())
                {
                    names += ", ";
                }
                names += name;
            }

            return names;
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
            if (name == "ragdoll")
            {
                return "r.ragdoll";
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
            return "[\"aabb\",\"picking_ray\",\"grid\",\"transform_handle\",\"selection_outline\",\"entity_icons\",\"performance_metrics\",\"physics\",\"ragdoll\",\"wireframe\",\"meshlet_visualize\",\"cluster_visualize\"]";
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

        std::string camera_exposure_mode_enum_values_json()
        {
            return enum_values_json(
                {
                    { "manual", json_string("manual") },
                    { "automatic", json_string("automatic") }
                }
            );
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

        std::string text_3d_alignment_enum_values_json()
        {
            return enum_values_json({
                { "left", json_string("left") },
                { "center", json_string("center") },
                { "right", json_string("right") }
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
            if (type == typeid(Text3DAlignment))
            {
                return json_string(
                    text_3d_alignment_to_name(
                        std::any_cast<Text3DAlignment>(value)
                    )
                );
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
                type == typeid(Text3DAlignment) ||
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
            if (type == typeid(Text3DAlignment))
            {
                const std::optional<Text3DAlignment> result =
                    text_3d_alignment_from_name(value);
                if (!result)
                {
                    error = "invalid text_3d_alignment";
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
            else if (property == "alignment")
            {
                metadata.type = "enum";
                metadata.enum_values =
                    text_3d_alignment_enum_values_json();
                metadata.side_effects.emplace_back(
                    "regenerates 3d text geometry"
                );
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
                    "screenshot path must be inside project/mcp/blockout/thumbnails"
                );
            }
            std::string extension = std::filesystem::path(path).extension().generic_string();
            std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (extension != ".png")
            {
                return json_error("screenshot path must be a .png file");
            }
            std::filesystem::create_directories(
                std::filesystem::path(path).parent_path()
            );

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

        std::string command_world_resource_directory_get()
        {
            // generated assets live in a fixed directory that has nothing to do with the active world, so an
            // unsaved world only leaves the world specific fields blank, it does not make the answer unknown
            const std::string world_path = World::GetFilePath();
            const std::string resource_directory =
                world_path.empty()
                ? std::string()
                : World::GetResourceDirectory(world_path);
            const std::string mcp_blockout =
                World::GetGeneratedResourceDirectory();
            const std::string mcp_library =
                World::GetLibraryResourceDirectory();
            std::string json = "{\"ok\":true";
            json += ",\"world_saved\":" +
                std::string(world_path.empty() ? "false" : "true");
            json += ",\"world_path\":" +
                json_string(world_path);
            json += ",\"resource_directory\":" +
                json_string(resource_directory);
            json += ",\"mcp\":{";
            json += "\"blockout\":{";
            json += "\"root\":" +
                json_string(mcp_blockout);
            json += ",\"meshes\":" +
                json_string(mcp_blockout + "meshes/");
            json += ",\"materials\":" +
                json_string(mcp_blockout + "materials/");
            json += ",\"textures\":" +
                json_string(mcp_blockout + "textures/");
            json += ",\"prefabs\":" +
                json_string(mcp_blockout + "prefabs/");
            json += ",\"sources\":" +
                json_string(mcp_blockout + "sources/");
            json += ",\"thumbnails\":" +
                json_string(mcp_blockout + "thumbnails/");
            json += ",\"catalog\":" +
                json_string(mcp_blockout + "catalog.json");
            json += "}";
            json += ",\"library\":{";
            json += "\"root\":" +
                json_string(mcp_library);
            json += ",\"meshes\":" +
                json_string(mcp_library + "meshes/");
            json += ",\"materials\":" +
                json_string(mcp_library + "materials/");
            json += ",\"textures\":" +
                json_string(mcp_library + "textures/");
            json += ",\"prefabs\":" +
                json_string(mcp_library + "prefabs/");
            json += ",\"sources\":" +
                json_string(mcp_library + "sources/");
            json += ",\"thumbnails\":" +
                json_string(mcp_library + "thumbnails/");
            json += ",\"catalog\":" +
                json_string(mcp_library + "catalog.json");
            json += "}";
            // legacy root alias points at mcp blockout output
            json += ",\"root\":" +
                json_string(mcp_blockout);
            json += "}}";
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
                if (Render* render =
                    current->GetComponent<Render>())
                {
                    auto merge_matrix =
                        [&](const math::Matrix& matrix)
                    {
                        const math::BoundingBox world_bounds =
                            render->GetBoundingBoxMesh() *
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
                    if (render->HasInstancing())
                    {
                        for (
                            uint32_t i = 0;
                            i < render->GetInstanceCount();
                            i++
                        )
                        {
                            merge_matrix(
                                render->GetInstance(i, true)
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
                if (Render* render =
                    current->GetComponent<Render>())
                {
                    std::array<math::Vector3, 8> corners;
                    render->GetBoundingBoxMesh().GetCorners(
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
                    if (render->HasInstancing())
                    {
                        for (
                            uint32_t i = 0;
                            i < render->GetInstanceCount();
                            i++
                        )
                        {
                            accumulate_matrix(
                                render->GetInstance(i, true)
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
                if (Render* render =
                    entity->GetComponent<Render>())
                {
                    own_bounds = render->GetBoundingBox();
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
                    if (Render* render =
                        current->GetComponent<Render>())
                    {
                        const math::BoundingBox& bounds =
                            render->GetBoundingBox();
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
            if (
                const std::optional<std::string> transient =
                    get_argument(request, "transient")
            )
            {
                bool parsed = false;
                if (!parse_bool(*transient, parsed))
                {
                    return json_error("invalid transient");
                }
                entity->SetTransient(parsed);
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
                const std::string mode = to_lower_copy(
                    get_argument(request, "tags_mode").value_or(
                        "replace"
                    )
                );
                if (mode == "replace")
                {
                    entity->SetTagsString(*tags);
                }
                else if (mode == "merge")
                {
                    add_entity_tags(entity, *tags);
                }
                else
                {
                    return json_error(
                        "tags_mode must be replace or merge"
                    );
                }
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

            const bool component_exists =
                entity->GetComponentByType(*type) != nullptr;
            Component* component = entity->AddComponent(*type);
            if (component == nullptr)
            {
                return json_error("failed to add component");
            }
            if (
                *type == ComponentType::Render &&
                !component_exists
            )
            {
                Render* render =
                    static_cast<Render*>(component);
                render->SetMesh(MeshType::Cube);
                render->SetDefaultMaterial();
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
                Render* render = static_cast<Render*>(component);
                json += "\"mesh\":" + json_string(render->GetMeshName());
                json += ",\"material\":" + json_string(render->GetMaterialName());
                json += ",\"default_material\":" + json_bool(render->IsUsingDefaultMaterial());
                json += ",\"visible\":" + json_bool(render->IsVisible());
                json += ",\"casts_shadows\":" + json_bool(render->HasFlag(RenderFlags::CastsShadows));
                json += ",\"exclude_from_ray_tracing\":" + json_bool(render->HasFlag(RenderFlags::ExcludeFromRayTracing));
                json += ",\"max_render_distance\":" + std::to_string(render->GetMaxRenderDistance());
                json += ",\"max_shadow_distance\":" + std::to_string(render->GetMaxShadowDistance());
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
                json += ",\"exposure_mode\":" + json_string(
                    camera->GetExposureMode() == CameraExposureMode::automatic ?
                    "automatic" :
                    "manual"
                );
                json += ",\"auto_exposure_adaptation_speed\":" +
                    std::to_string(camera->GetAutoExposureAdaptationSpeed());
                json += ",\"auto_exposure_compensation\":" +
                    std::to_string(camera->GetAutoExposureCompensation());
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
            else if (type == ComponentType::Text3D)
            {
                Text3D* text_3d = static_cast<Text3D*>(component);
                json += "\"text\":" + json_string(text_3d->GetText());
                json += ",\"font_path\":" +
                    json_string(text_3d->GetFontPath());
                json += ",\"size\":" +
                    json_number(text_3d->GetSize());
                json += ",\"depth\":" +
                    json_number(text_3d->GetDepth());
                json += ",\"weight\":" +
                    json_number(text_3d->GetWeight());
                json += ",\"letter_spacing\":" +
                    json_number(text_3d->GetLetterSpacing());
                json += ",\"line_spacing\":" +
                    json_number(text_3d->GetLineSpacing());
                json += ",\"resolution\":" +
                    std::to_string(text_3d->GetResolution());
                json += ",\"alignment\":" +
                    json_string(
                        text_3d_alignment_to_name(
                            text_3d->GetAlignment()
                        )
                    );
                json += ",\"has_mesh\":" +
                    json_bool(text_3d->HasMesh());
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
                return
                    "[\"fov_degrees\",\"aperture\",\"shutter_speed\",\"iso\","
                    "\"exposure_mode\",\"auto_exposure_adaptation_speed\","
                    "\"auto_exposure_compensation\",\"projection\","
                    "\"controllable\",\"flashlight\"]";
            }
            if (type == ComponentType::AudioSource)
            {
                return "[\"clip\",\"mute\",\"play_on_start\",\"loop\",\"is_3d\",\"volume\",\"pitch\",\"reverb_enabled\",\"reverb_room_size\",\"reverb_decay\",\"reverb_wet\"]";
            }
            if (type == ComponentType::Script)
            {
                return "[\"file_path\"]";
            }
            if (type == ComponentType::Text3D)
            {
                return "[\"text\",\"font_path\",\"size\",\"depth\",\"weight\",\"letter_spacing\",\"line_spacing\",\"resolution\",\"alignment\"]";
            }

            return "[]";
        }

        std::string component_actions_json(
            const ComponentType type
        )
        {
            if (type == ComponentType::Terrain)
            {
                return "[\"generate\"]";
            }
            if (type == ComponentType::Spline)
            {
                return "[\"generate_road_mesh\",\"clear_road_mesh\",\"spawn_instances\",\"clear_instances\"]";
            }
            if (type == ComponentType::ParticleSystem)
            {
                return "[\"apply_preset\",\"trigger_burst\"]";
            }
            if (type == ComponentType::Physics)
            {
                return "[\"apply_force\",\"sync_wheel_offsets\",\"reset_tire_wear\",\"shift_up\",\"shift_down\",\"shift_to_neutral\"]";
            }
            if (type == ComponentType::AudioSource)
            {
                return "[\"play\",\"stop\"]";
            }
            if (type == ComponentType::Light)
            {
                return "[\"fit_to_mesh\"]";
            }
            if (type == ComponentType::Camera)
            {
                return "[\"focus_selected\"]";
            }
            if (type == ComponentType::Text3D)
            {
                return "[\"generate_mesh\",\"clear_mesh\"]";
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
                add({ "aperture", "", "float", true, "f stop", range_json(0.01f, std::nullopt), "", { "changes manual exposure and depth of field behavior" }, "", "5.6" });
                add({ "shutter_speed", "", "float", true, "seconds", range_json(0.0001f, std::nullopt), "", { "changes manual exposure and motion blur behavior" }, "", "0.008" });
                add({ "iso", "", "float", true, "iso", range_json(1.0f, std::nullopt), "", { "changes manual exposure and film grain" }, "", "200" });
                add({
                    "exposure_mode",
                    "",
                    "enum",
                    true,
                    "",
                    "",
                    camera_exposure_mode_enum_values_json(),
                    { "selects physical or scene metered exposure" },
                    "",
                    json_string("automatic")
                });
                add({
                    "auto_exposure_adaptation_speed",
                    "",
                    "float",
                    true,
                    "",
                    range_json(0.0f, 10.0f),
                    "",
                    { "zero adapts immediately" },
                    "",
                    "1"
                });
                add({
                    "auto_exposure_compensation",
                    "",
                    "float",
                    true,
                    "ev stops",
                    range_json(-10.0f, 10.0f),
                    "",
                    { "positive values brighten automatic exposure" },
                    "",
                    "0"
                });
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
            else if (type == ComponentType::Text3D)
            {
                const std::vector<std::string> regeneration =
                {
                    "marks generated mesh dirty",
                    "regenerates geometry after a short debounce"
                };
                add({
                    "text",
                    "",
                    "string",
                    true,
                    "utf 8",
                    "",
                    "",
                    regeneration,
                    "",
                    json_string("Text")
                });
                add({
                    "font_path",
                    "",
                    "string",
                    true,
                    "font resource path",
                    "",
                    "",
                    regeneration,
                    "",
                    json_string("OpenSans/OpenSans-Medium.ttf")
                });
                add({
                    "size",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(0.01f, 1000.0f),
                    "",
                    regeneration,
                    "",
                    "1"
                });
                add({
                    "depth",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(0.001f, 1000.0f),
                    "",
                    regeneration,
                    "",
                    "0.1"
                });
                add({
                    "weight",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(0.0f, 1.0f),
                    "",
                    regeneration,
                    "",
                    "0"
                });
                add({
                    "letter_spacing",
                    "",
                    "float",
                    true,
                    "meters",
                    range_json(-10.0f, 100.0f),
                    "",
                    regeneration,
                    "",
                    "0"
                });
                add({
                    "line_spacing",
                    "",
                    "float",
                    true,
                    "multiplier",
                    range_json(0.1f, 10.0f),
                    "",
                    regeneration,
                    "",
                    "1.2"
                });
                add({
                    "resolution",
                    "",
                    "uint32",
                    true,
                    "pixels per em",
                    range_json(32.0f, 512.0f),
                    "",
                    regeneration,
                    "",
                    "128"
                });
                add({
                    "alignment",
                    "",
                    "enum",
                    true,
                    "",
                    "",
                    text_3d_alignment_enum_values_json(),
                    regeneration,
                    "",
                    json_string("left")
                });
                add({
                    "has_mesh",
                    "",
                    "bool",
                    false,
                    "",
                    "",
                    "",
                    {},
                    "derived generated mesh state",
                    "false"
                });
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
            json += ",\"actions\":" + component_actions_json(*type);
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

            if (Render* render = entity->GetComponent<Render>())
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
                json += ",\"mesh\":" + json_string(render->GetMeshName());
                json += ",\"material\":" + json_string(render->GetMaterialName());
                json += ",\"default_material\":" + json_bool(render->IsUsingDefaultMaterial());
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

            const std::string property_name = to_lower_copy(*property_arg);
            bool inverted = false;
            std::optional<MaterialProperty> property =
                material_property_from_name(property_name);
            if (!property)
            {
                property = material_property_inverted_from_name(property_name);
                inverted = property.has_value();
            }
            if (!property)
            {
                return json_error(
                    "invalid material property, valid names: " +
                    material_property_names_csv()
                );
            }

            float value = 0.0f;
            if (!parse_float(*value_arg, value))
            {
                return json_error("invalid material property value");
            }

            if (inverted)
            {
                value = std::clamp(1.0f - value, 0.0f, 1.0f);
            }

            material->SetProperty(*property, value);
            std::string json = "{\"ok\":true,\"applied_property\":";
            json += json_string(material_property_to_name(*property));
            json += ",\"applied_value\":" + std::to_string(value);
            json += ",\"material\":" + material_to_json(material) + "}";
            return json;
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

            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    *path_arg,
                    "materials",
                    EXTENSION_MATERIAL,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }
            const std::string path = *resolved_path;
            std::filesystem::create_directories(
                std::filesystem::path(path).parent_path()
            );
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
            const std::string requested_path =
                save_path && !save_path->empty()
                ? *save_path
                : resource->GetResourceFilePath();
            if (requested_path.empty())
            {
                return json_error("resource has no save path");
            }

            const char* directory_name = "resources";
            std::string extension =
                std::filesystem::path(
                    requested_path
                ).extension().string();
            switch (resource->GetResourceType())
            {
            case ResourceType::Texture:
            case ResourceType::Cubemap:
                directory_name = "textures";
                if (extension.empty())
                {
                    extension = EXTENSION_TEXTURE;
                }
                break;
            case ResourceType::Audio:
                directory_name = "audio";
                if (extension.empty())
                {
                    extension = EXTENSION_AUDIO;
                }
                break;
            case ResourceType::Material:
                directory_name = "materials";
                extension = EXTENSION_MATERIAL;
                break;
            case ResourceType::Mesh:
                directory_name = "meshes";
                extension = EXTENSION_MESH;
                break;
            case ResourceType::Font:
                directory_name = "fonts";
                if (extension.empty())
                {
                    extension = EXTENSION_FONT;
                }
                break;
            case ResourceType::Shader:
                directory_name = "shaders";
                if (extension.empty())
                {
                    extension = EXTENSION_SHADER;
                }
                break;
            case ResourceType::Animation:
                directory_name = "animations";
                if (extension.empty())
                {
                    extension = ".animation";
                }
                break;
            default:
                if (extension.empty())
                {
                    extension = ".resource";
                }
                break;
            }
            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    requested_path,
                    directory_name,
                    extension,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }
            const std::string path = *resolved_path;
            const std::filesystem::path file_path(path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(file_path.parent_path());
            }
            resource->SetResourceFilePath(path);
            resource->SaveToFile(path);
            // SaveToFile cannot report a full disk or a denied write, so the file itself is the receipt,
            // reporting a save that did not happen sends the catalog off to register a missing file
            if (!FileSystem::IsFile(path))
            {
                return json_error("the resource did not reach " + path);
            }
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

            bool delete_file = false;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "delete_file")
            )
            {
                if (!parse_bool(*value, delete_file))
                {
                    return json_error("delete_file must be boolean");
                }
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

            const std::string file_path =
                (*it)->GetResourceFilePath();
            if (delete_file)
            {
                if (
                    file_path.empty() ||
                    !path_is_within(
                        file_path,
                        World::GetGeneratedResourceDirectory()
                    )
                )
                {
                    return json_error(
                        "only shared MCP resource files can be deleted"
                    );
                }

                std::error_code error;
                const bool removed_file =
                    std::filesystem::remove(file_path, error);
                error.clear();
                const bool file_still_exists =
                    std::filesystem::exists(file_path, error);
                if (!removed_file && file_still_exists)
                {
                    return json_error(
                        "resource file could not be deleted"
                    );
                }
            }

            const std::string removed = resource_to_json(it->get());
            resources.erase(it);
            return
                "{\"ok\":true,\"file_deleted\":" +
                std::string(delete_file ? "true" : "false") +
                ",\"removed\":" +
                removed +
                "}";
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
            std::string path_error;
            const std::optional<std::string> path =
                resolve_mcp_output_path(
                    *path_arg,
                    "materials",
                    EXTENSION_MATERIAL,
                    path_error
                );
            if (!path)
            {
                return json_error(path_error);
            }

            std::shared_ptr<Material> material = std::make_shared<Material>();
            const std::filesystem::path file_path(*path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(file_path.parent_path());
            }
            material->SetResourceFilePath(*path);
            material->SaveToFile(*path);
            // caching a material whose file never landed hands back something that looks usable and then
            // resolves to nothing the next time the prefab is loaded
            if (!FileSystem::IsFile(*path))
            {
                return json_error("the material did not reach " + *path);
            }
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
                if (Render* render = entity->GetComponent<Render>())
                {
                    const math::BoundingBox& render_bounds =
                        render->GetBoundingBox();
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
                "aabb", "picking_ray", "grid", "transform_handle", "selection_outline", "entity_icons", "performance_metrics", "physics", "ragdoll", "wireframe", "meshlet_visualize", "cluster_visualize"
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
            json += ",\"mcp_controlled\":" + json_bool(car->IsExternallyControlled());
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
                json += ",\"mcp_controlled\":" + json_bool(car->IsExternallyControlled());
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

            car->SetExternallyControlled(mcp_controlled);
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
            car->SetExternallyControlled(false);
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

            car->SetExternallyControlled(true);

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

            car->SetExternallyControlled(true);
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

        std::string command_entity_make_game_ready(const McpRequest& request)
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("making an entity game ready requires edit mode");
            }

            std::string error;
            Entity* entity = get_entity_from_request(request, error);
            if (entity == nullptr)
            {
                return json_error(error);
            }

            bool generate_lods = true;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "generate_lods")
            )
            {
                if (!parse_bool(*value, generate_lods))
                {
                    return json_error("generate_lods must be a boolean");
                }
            }

            // one mesh file for the whole hierarchy, named after what it came from unless asked otherwise
            const std::optional<std::string> requested_path =
                get_argument(request, "path");
            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    requested_path && !requested_path->empty()
                        ? *requested_path
                        : entity->GetObjectName() + "_merged",
                    "meshes",
                    EXTENSION_MESH,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }

            const game_ready::MergeReport report =
                game_ready::MergeRenderersByMaterial(
                    entity,
                    *resolved_path,
                    generate_lods
                );
            if (!report.ok)
            {
                return json_error(report.error);
            }

            std::string json = "{\"ok\":true";
            json += ",\"mesh_path\":" + json_string(report.mesh_path);
            json += ",\"renderers_before\":" +
                std::to_string(report.renderers_before);
            json += ",\"renderers_after\":" +
                std::to_string(report.renderers_after);
            json += ",\"entities_removed\":" +
                std::to_string(report.entities_removed);
            json += ",\"vertices_before\":" +
                std::to_string(report.vertices_before);
            json += ",\"vertices_after\":" +
                std::to_string(report.vertices_after);
            json += ",\"indices_before\":" +
                std::to_string(report.indices_before);
            json += ",\"indices_after\":" +
                std::to_string(report.indices_after);

            json += ",\"merged\":[";
            for (size_t index = 0; index < report.groups.size(); index++)
            {
                if (index != 0)
                {
                    json += ",";
                }
                const game_ready::MergeGroup& group = report.groups[index];
                json += "{\"material\":" + json_string(group.material_name);
                json += ",\"entity\":" + json_string(group.entity_name);
                json += ",\"sub_mesh_index\":" +
                    std::to_string(group.sub_mesh_index);
                json += ",\"parts\":" + std::to_string(group.source_count);
                json += ",\"vertex_count\":" +
                    std::to_string(group.vertex_count);
                json += ",\"index_count\":" +
                    std::to_string(group.index_count);
                json += "}";
            }
            json += "]";

            // naming what was left alone is the whole value of the reply, a hierarchy that did not
            // collapse as far as expected always has a reason and it is usually fixable
            json += ",\"skipped\":[";
            for (size_t index = 0; index < report.skipped.size(); index++)
            {
                if (index != 0)
                {
                    json += ",";
                }
                json += "{\"entity\":" +
                    json_string(report.skipped[index].entity_name);
                json += ",\"reason\":" +
                    json_string(report.skipped[index].reason);
                json += "}";
            }
            json += "]";
            json += ",\"entity\":" + entity_to_json_compact(entity);
            json += "}";
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
            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    *path,
                    "prefabs",
                    EXTENSION_PREFAB,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }
            std::filesystem::create_directories(
                std::filesystem::path(
                    *resolved_path
                ).parent_path()
            );

            const bool saved = Prefab::SaveToFile(
                entity,
                *resolved_path
            );
            if (!saved)
            {
                return json_error("failed to save prefab");
            }

            // a save that reports success without leaving a file is worse than a failure, because the run
            // carries on believing the asset exists. the whole point of the prefab is the file
            if (!FileSystem::IsFile(*resolved_path))
            {
                return json_error(
                    "prefab reported saved but no file was written to " +
                    *resolved_path
                );
            }

            return
                "{\"ok\":true,\"path\":" +
                json_string(*resolved_path) +
                ",\"entity\":" +
                entity_to_json_compact(entity) +
                "}";
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

        bool assign_render_material(Render* render, const std::string& name_or_path, std::string& error)
        {
            if (name_or_path == "default")
            {
                render->SetDefaultMaterial();
                return true;
            }

            // prefer the cached resource so both resource names and paths bind
            if (std::shared_ptr<IResource> cached = get_resource_shared_by_name_or_path(name_or_path, ResourceType::Material))
            {
                render->SetMaterial(std::static_pointer_cast<Material>(cached));
                return true;
            }

            if (FileSystem::IsFile(name_or_path))
            {
                render->SetMaterial(name_or_path);
                return true;
            }

            error = "material not found by cached name, cached path, or file path: " + name_or_path;
            return false;
        }

        bool set_render_property(Render* render, const std::string& property, const std::string& value, std::string& error)
        {
            if (property == "mesh")
            {
                const std::optional<MeshType> parsed = mesh_type_from_name(value);
                if (!parsed)
                {
                    error = "invalid mesh";
                    return false;
                }
                render->SetMesh(*parsed);
                return true;
            }
            if (property == "material")
            {
                if (render->GetMesh() == nullptr)
                {
                    error =
                        "assign a mesh before assigning a render material";
                    return false;
                }
                return assign_render_material(render, value, error);
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
                    render->SetDefaultMaterial();
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
                render->SetVisible(parsed);
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
                render->SetFlag(property == "casts_shadows" ? RenderFlags::CastsShadows : RenderFlags::ExcludeFromRayTracing, parsed);
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
                    render->SetMaxRenderDistance(parsed);
                }
                else
                {
                    render->SetMaxShadowDistance(parsed);
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
            if (property == "exposure_mode")
            {
                if (value == "manual")
                {
                    camera->SetExposureMode(CameraExposureMode::manual);
                    return true;
                }
                if (value == "automatic")
                {
                    camera->SetExposureMode(CameraExposureMode::automatic);
                    return true;
                }

                error = "invalid camera exposure mode";
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
            if (
                property == "fov_degrees" ||
                property == "aperture" ||
                property == "shutter_speed" ||
                property == "iso" ||
                property == "auto_exposure_adaptation_speed" ||
                property == "auto_exposure_compensation"
            )
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
                else if (property == "iso")
                {
                    camera->SetIso(parsed);
                }
                else if (property == "auto_exposure_adaptation_speed")
                {
                    camera->SetAutoExposureAdaptationSpeed(parsed);
                }
                else
                {
                    camera->SetAutoExposureCompensation(parsed);
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

        bool set_text_3d_property(
            Text3D* text_3d,
            const std::string& property,
            const std::string& value,
            std::string& error
        )
        {
            if (property == "text")
            {
                text_3d->SetText(value);
                return true;
            }

            if (property == "font_path")
            {
                if (!FileSystem::IsSupportedFontFile(value))
                {
                    error = "unsupported font file";
                    return false;
                }

                std::string resolved_path = value;
                if (!FileSystem::IsFile(resolved_path))
                {
                    resolved_path =
                        ResourceCache::GetResourceDirectory(
                            ResourceDirectory::Fonts
                        ) +
                        "/" +
                        value;
                }
                if (!FileSystem::IsFile(resolved_path))
                {
                    error = "font file not found";
                    return false;
                }

                text_3d->SetFontPath(resolved_path);
                return true;
            }

            if (property == "alignment")
            {
                const std::optional<Text3DAlignment> alignment =
                    text_3d_alignment_from_name(value);
                if (!alignment)
                {
                    error = "invalid text_3d alignment";
                    return false;
                }

                text_3d->SetAlignment(*alignment);
                return true;
            }

            if (property == "resolution")
            {
                uint32_t resolution = 0;
                if (!parse_uint32(value, resolution))
                {
                    error = "invalid text_3d resolution";
                    return false;
                }

                text_3d->SetResolution(resolution);
                return true;
            }

            if (
                property == "size" ||
                property == "depth" ||
                property == "weight" ||
                property == "letter_spacing" ||
                property == "line_spacing"
            )
            {
                float parsed = 0.0f;
                if (!parse_float(value, parsed))
                {
                    error = "invalid text_3d float";
                    return false;
                }

                if (property == "size")
                {
                    text_3d->SetSize(parsed);
                }
                else if (property == "depth")
                {
                    text_3d->SetDepth(parsed);
                }
                else if (property == "weight")
                {
                    text_3d->SetWeight(parsed);
                }
                else if (property == "letter_spacing")
                {
                    text_3d->SetLetterSpacing(parsed);
                }
                else
                {
                    text_3d->SetLineSpacing(parsed);
                }

                return true;
            }

            error = "unsupported text_3d property";
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
            else if (type == ComponentType::Text3D)
            {
                const std::string text_property =
                    property.rfind("m_", 0) == 0
                    ? property.substr(2)
                    : property;
                changed = set_text_3d_property(
                    static_cast<Text3D*>(component),
                    text_property,
                    value,
                    error
                );
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
                    // the properties before this one are already set, so the count says how many took
                    const std::string message = error.empty()
                        ? "failed to set component property"
                        : error;
                    return json_batch_failure(
                        message,
                        "applied",
                        "[",
                        static_cast<uint32_t>(i),
                        i,
                        json_error(message)
                    );
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
            else if (
                *type == ComponentType::Text3D &&
                action == "generate_mesh"
            )
            {
                Text3D* text_3d = static_cast<Text3D*>(component);
                if (!text_3d->GenerateMesh())
                {
                    return json_error(
                        "failed to generate 3d text mesh"
                    );
                }
                result_json =
                    "{\"has_mesh\":" +
                    json_bool(text_3d->HasMesh()) +
                    "}";
            }
            else if (
                *type == ComponentType::Text3D &&
                action == "clear_mesh"
            )
            {
                Text3D* text_3d = static_cast<Text3D*>(component);
                text_3d->ClearMesh();
                result_json = "{\"has_mesh\":false}";
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

            if (
                const std::optional<std::string> position =
                    get_argument(request, "position")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*position, parsed))
                {
                    return json_error("invalid position");
                }
                entity->SetPositionLocal(parsed);
            }
            if (
                const std::optional<std::string> rotation_euler =
                    get_argument(request, "rotation_euler")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*rotation_euler, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                entity->SetRotationLocal(
                    math::Quaternion::FromEulerAngles(parsed)
                );
            }
            if (
                const std::optional<std::string> scale =
                    get_argument(request, "scale")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*scale, parsed))
                {
                    return json_error("invalid scale");
                }
                entity->SetScaleLocal(parsed);
            }
            if (
                const std::optional<std::string> transient =
                    get_argument(request, "transient")
            )
            {
                bool parsed = false;
                if (!parse_bool(*transient, parsed))
                {
                    return json_error("invalid transient");
                }
                entity->SetTransient(parsed);
            }
            if (
                const std::optional<std::string> active =
                    get_argument(request, "active")
            )
            {
                bool parsed = false;
                if (!parse_bool(*active, parsed))
                {
                    return json_error("invalid active");
                }
                entity->SetActive(parsed);
            }

            apply_entity_identity(entity, request);
            return "{\"ok\":true,\"entity\":" + entity_to_json_compact(entity) + "}";
        }

        math::Vector3 fallback_tangent(
            const math::Vector3& normal
        )
        {
            const math::Vector3 reference =
                std::abs(normal.y) < 0.999f
                ? math::Vector3(0.0f, 1.0f, 0.0f)
                : math::Vector3(1.0f, 0.0f, 0.0f);
            return math::Vector3::Cross(
                reference,
                normal
            ).Normalized();
        }

        std::string command_mesh_raw_create(
            const McpRequest& request
        )
        {
            constexpr size_t max_vertex_count = 100000;
            constexpr size_t max_index_count  = 300000;
            constexpr size_t max_payload_size = 16 * 1024 * 1024;

            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error(
                    "raw mesh creation requires edit mode"
                );
            }

            std::string path_error;
            const std::optional<std::string> path =
                resolve_mcp_mesh_path(request, path_error);
            if (!path)
            {
                return json_error(path_error);
            }

            const std::optional<std::string> positions_arg =
                get_argument(request, "positions");
            const std::optional<std::string> indices_arg =
                get_argument(request, "indices");
            if (!positions_arg || !indices_arg)
            {
                return json_error(
                    "positions and indices are required"
                );
            }

            size_t payload_size =
                positions_arg->size() +
                indices_arg->size();
            const std::optional<std::string> normals_arg =
                get_argument(request, "normals");
            const std::optional<std::string> uv0_arg =
                get_argument(request, "uv0");
            const std::optional<std::string> colors_arg =
                get_argument(request, "colors");
            if (normals_arg)
            {
                payload_size += normals_arg->size();
            }
            if (uv0_arg)
            {
                payload_size += uv0_arg->size();
            }
            if (colors_arg)
            {
                return json_error(
                    "colors are not supported by RHI_Vertex_PosTexNorTan"
                );
            }
            if (payload_size > max_payload_size)
            {
                return json_error(
                    "raw mesh payload exceeds 16 MiB"
                );
            }

            std::vector<float> position_values;
            std::vector<uint32_t> indices;
            if (
                !parse_float_array(
                    *positions_arg,
                    position_values,
                    max_vertex_count * 3
                ) ||
                position_values.size() < 9 ||
                position_values.size() % 3 != 0
            )
            {
                return json_error(
                    "positions must be finite comma-separated triples with 3 to 100000 vertices"
                );
            }
            if (
                !parse_index_array(
                    *indices_arg,
                    indices,
                    max_index_count
                ) ||
                indices.size() < 3 ||
                indices.size() % 3 != 0
            )
            {
                return json_error(
                    "indices must be comma-separated unsigned triangle indices with at most 300000 values"
                );
            }

            const size_t vertex_count =
                position_values.size() / 3;
            std::vector<float> normal_values;
            if (
                normals_arg &&
                (
                    !parse_float_array(
                        *normals_arg,
                        normal_values,
                        max_vertex_count * 3
                    ) ||
                    normal_values.size() != vertex_count * 3
                )
            )
            {
                return json_error(
                    "normals must contain one finite triple per vertex"
                );
            }

            std::vector<float> uv_values;
            if (
                uv0_arg &&
                (
                    !parse_float_array(
                        *uv0_arg,
                        uv_values,
                        max_vertex_count * 2
                    ) ||
                    uv_values.size() != vertex_count * 2
                )
            )
            {
                return json_error(
                    "uv0 must contain one finite pair per vertex"
                );
            }

            std::vector<math::Vector3> positions(vertex_count);
            std::vector<math::Vector3> normals(
                vertex_count,
                math::Vector3::Zero
            );
            std::vector<math::Vector3> tangents(
                vertex_count,
                math::Vector3::Zero
            );
            std::vector<math::Vector2> uv0(
                vertex_count,
                math::Vector2::Zero
            );
            for (size_t i = 0; i < vertex_count; i++)
            {
                positions[i] = math::Vector3(
                    position_values[i * 3],
                    position_values[i * 3 + 1],
                    position_values[i * 3 + 2]
                );
                if (normals_arg)
                {
                    normals[i] = math::Vector3(
                        normal_values[i * 3],
                        normal_values[i * 3 + 1],
                        normal_values[i * 3 + 2]
                    );
                    if (
                        normals[i].LengthSquared() <=
                        0.000000000001f
                    )
                    {
                        return json_error(
                            "normals cannot contain zero-length vectors"
                        );
                    }
                    normals[i].Normalize();
                }
                if (uv0_arg)
                {
                    uv0[i] = math::Vector2(
                        uv_values[i * 2],
                        uv_values[i * 2 + 1]
                    );
                }
            }

            for (size_t i = 0; i < indices.size(); i += 3)
            {
                const uint32_t index_a = indices[i];
                const uint32_t index_b = indices[i + 1];
                const uint32_t index_c = indices[i + 2];
                if (
                    index_a >= vertex_count ||
                    index_b >= vertex_count ||
                    index_c >= vertex_count
                )
                {
                    return json_error(
                        "an index is outside the vertex range"
                    );
                }
                if (
                    index_a == index_b ||
                    index_b == index_c ||
                    index_c == index_a
                )
                {
                    return json_error(
                        "indices contain a degenerate triangle"
                    );
                }

                const math::Vector3 edge_a =
                    positions[index_b] - positions[index_a];
                const math::Vector3 edge_b =
                    positions[index_c] - positions[index_a];
                const math::Vector3 face_normal =
                    math::Vector3::Cross(edge_a, edge_b);
                if (
                    face_normal.LengthSquared() <=
                    0.000000000001f
                )
                {
                    return json_error(
                        "positions contain a zero-area triangle"
                    );
                }
                if (!normals_arg)
                {
                    normals[index_a] += face_normal;
                    normals[index_b] += face_normal;
                    normals[index_c] += face_normal;
                }

                if (uv0_arg)
                {
                    const math::Vector2 delta_a =
                        uv0[index_b] - uv0[index_a];
                    const math::Vector2 delta_b =
                        uv0[index_c] - uv0[index_a];
                    const float determinant =
                        delta_a.x * delta_b.y -
                        delta_a.y * delta_b.x;
                    if (std::abs(determinant) > 0.00000001f)
                    {
                        const math::Vector3 tangent =
                            (
                                edge_a * delta_b.y -
                                edge_b * delta_a.y
                            ) /
                            determinant;
                        tangents[index_a] += tangent;
                        tangents[index_b] += tangent;
                        tangents[index_c] += tangent;
                    }
                }
            }

            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            vertices.reserve(vertex_count);
            for (size_t i = 0; i < vertex_count; i++)
            {
                if (!normals_arg)
                {
                    if (
                        normals[i].LengthSquared() <=
                        0.000000000001f
                    )
                    {
                        return json_error(
                            "a vertex has no valid triangle normal"
                        );
                    }
                    normals[i].Normalize();
                }

                tangents[i] -=
                    normals[i] *
                    math::Vector3::Dot(
                        normals[i],
                        tangents[i]
                    );
                if (
                    tangents[i].LengthSquared() <=
                    0.000000000001f
                )
                {
                    tangents[i] = fallback_tangent(normals[i]);
                }
                else
                {
                    tangents[i].Normalize();
                }

                vertices.emplace_back(
                    positions[i],
                    uv0[i],
                    normals[i],
                    tangents[i]
                );
            }

            if (
                ResourceCache::GetByPath<Mesh>(*path) ||
                FileSystem::IsFile(*path)
            )
            {
                return json_error(
                    "mesh path already exists"
                );
            }

            const std::filesystem::path file_path(*path);
            if (file_path.has_parent_path())
            {
                std::filesystem::create_directories(
                    file_path.parent_path()
                );
            }

            std::shared_ptr<Mesh> mesh =
                std::make_shared<Mesh>();
            mesh->SetResourceFilePath(*path);
            mesh->SetFlag(
                static_cast<uint32_t>(
                    MeshFlags::PostProcessOptimize
                ),
                false
            );
            mesh->AddGeometry(vertices, indices, false);
            mesh->SaveToFile(*path);
            if (!FileSystem::IsFile(*path))
            {
                return json_error("failed to save raw mesh");
            }

            std::shared_ptr<Mesh> cached =
                ResourceCache::Cache(mesh);
            if (!cached)
            {
                return json_error("failed to cache raw mesh");
            }
            cached->CreateGpuBuffers();

            std::string json = "{\"ok\":true";
            json += ",\"path\":" + json_string(*path);
            json += ",\"vertex_count\":" +
                std::to_string(vertex_count);
            json += ",\"index_count\":" +
                std::to_string(indices.size());
            json += ",\"generated_normals\":" +
                json_bool(!normals_arg);
            json += ",\"generated_tangents\":true";
            json += ",\"channels\":[\"positions\",\"indices\",\"normals\",\"uv0\",\"tangents\"]";
            json += ",\"resource\":" +
                resource_to_json(cached.get());
            json += "}";
            return json;
        }

        std::string command_texture_generate(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error(
                    "texture generation requires edit mode"
                );
            }

            std::string path_error;
            const std::optional<std::string> path =
                resolve_mcp_texture_path(request, path_error);
            if (!path)
            {
                return json_error(path_error);
            }

            const std::optional<std::string> layers_arg =
                get_argument(request, "layers");
            if (!layers_arg || layers_arg->empty())
            {
                return json_error("layers are required");
            }

            mcp_texture_kernel::request settings;
            settings.font_directory =
                ResourceCache::GetResourceDirectory(
                    ResourceDirectory::Fonts
                );

            // a value that will not parse is refused rather than left at the default, silently handing back
            // a 512 texture when 2048 was asked for produces an asset nobody ordered and no error to explain it
            std::string argument_error;
            auto read_uint = [&request, &argument_error](
                const char* key,
                uint32_t& target
            )
            {
                if (
                    const std::optional<std::string> value =
                        get_argument(request, key)
                )
                {
                    uint32_t parsed = 0;
                    if (!parse_uint32(*value, parsed))
                    {
                        argument_error = std::string(key) + " must be a whole number";
                        return;
                    }
                    target = parsed;
                }
            };
            auto read_float = [&request, &argument_error](
                const char* key,
                float& target
            )
            {
                if (
                    const std::optional<std::string> value =
                        get_argument(request, key)
                )
                {
                    float parsed = 0.0f;
                    if (!parse_float(*value, parsed))
                    {
                        argument_error = std::string(key) + " must be a number";
                        return;
                    }
                    target = parsed;
                }
            };

            read_uint("width", settings.width);
            read_uint("height", settings.height);
            read_uint("seed", settings.seed);
            read_float("normal_strength", settings.normal_strength);
            read_float("base_roughness", settings.base_roughness);
            read_float("base_metalness", settings.base_metalness);
            if (!argument_error.empty())
            {
                return json_error(argument_error);
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "seamless")
            )
            {
                settings.seamless = to_lower_copy(*value) != "false";
            }

            std::string error;
            if (
                !mcp_texture_kernel::request_from_json(
                    *layers_arg,
                    settings,
                    error
                )
            )
            {
                return json_error(error);
            }

            bool contributes_surface = false;
            for (
                const mcp_texture_kernel::layer& entry :
                settings.layers
            )
            {
                contributes_surface =
                    contributes_surface ||
                    entry.roughness_value >= 0.0f ||
                    entry.metalness_value >= 0.0f ||
                    entry.occlusion > 0.0f;
            }

            mcp_texture_kernel::result generated;
            if (
                !mcp_texture_kernel::generate(
                    settings,
                    generated,
                    error
                )
            )
            {
                return json_error(error);
            }

            const std::filesystem::path color_path(*path);
            if (color_path.has_parent_path())
            {
                std::filesystem::create_directories(
                    color_path.parent_path()
                );
            }

            const std::string stem =
                (
                    color_path.parent_path() /
                    color_path.stem()
                ).generic_string();
            const std::string normal_path    = stem + "_normal.png";
            const std::string roughness_path = stem + "_roughness.png";
            const std::string packed_path    = stem + "_packed.png";

            const bool write_normal =
                generated.stats.relief_range > 0.0001f;
            const bool write_packed =
                contributes_surface || write_normal;

            ImageImporter::SaveSdrRgba8(
                *path,
                generated.width,
                generated.height,
                generated.albedo.data()
            );
            ImageImporter::SaveSdrRgba8(
                roughness_path,
                generated.width,
                generated.height,
                generated.roughness.data()
            );
            if (write_normal)
            {
                ImageImporter::SaveSdrRgba8(
                    normal_path,
                    generated.width,
                    generated.height,
                    generated.normal.data()
                );
            }
            if (write_packed)
            {
                ImageImporter::SaveSdrRgba8(
                    packed_path,
                    generated.width,
                    generated.height,
                    generated.packed.data()
                );
            }

            if (!FileSystem::Exists(*path))
            {
                return json_error(
                    "texture could not be written to " + *path
                );
            }
            if (!FileSystem::Exists(roughness_path))
            {
                return json_error(
                    "roughness texture could not be written to " +
                    roughness_path
                );
            }

            // wiring the maps here keeps the agent from creating a material and
            // forgetting to attach what it just generated
            std::string assigned_material;
            if (
                const std::optional<std::string> material_path =
                    get_argument(request, "material_path")
            )
            {
                if (!material_path->empty())
                {
                    std::shared_ptr<Material> material =
                        ResourceCache::GetByPath<Material>(
                            *material_path
                        );
                    if (!material && FileSystem::IsFile(*material_path))
                    {
                        material = ResourceCache::Load<Material>(
                            *material_path
                        );
                    }
                    if (!material)
                    {
                        return json_error(
                            "material_path could not be resolved: " +
                            *material_path
                        );
                    }

                    material->SetTexture(
                        MaterialTextureType::Color,
                        *path,
                        0
                    );
                    material->SetTexture(
                        MaterialTextureType::Roughness,
                        roughness_path,
                        0
                    );
                    if (write_normal)
                    {
                        material->SetTexture(
                            MaterialTextureType::Normal,
                            normal_path,
                            0
                        );
                    }
                    if (write_packed)
                    {
                        material->SetTexture(
                            MaterialTextureType::Packed,
                            packed_path,
                            0
                        );
                    }
                    assigned_material = *material_path;
                }
            }

            std::string json = "{\"ok\":true,\"path\":";
            json += json_string(*path);
            json += ",\"normal_path\":" +
                json_string(write_normal ? normal_path : "");
            json += ",\"roughness_path\":" +
                json_string(roughness_path);
            json += ",\"packed_path\":" +
                json_string(write_packed ? packed_path : "");
            json += ",\"material_path\":" +
                json_string(assigned_material);
            json += ",\"width\":" +
                std::to_string(generated.width);
            json += ",\"height\":" +
                std::to_string(generated.height);
            json += ",\"seamless\":" +
                json_bool(settings.seamless);
            json += ",\"layer_count\":" +
                std::to_string(settings.layers.size());
            json += ",\"stats\":{";
            json += "\"mean_color\":[" +
                std::to_string(generated.stats.mean_r) + "," +
                std::to_string(generated.stats.mean_g) + "," +
                std::to_string(generated.stats.mean_b) + "]";
            json += ",\"mean_luminance\":" +
                std::to_string(generated.stats.mean_luminance);
            json += ",\"contrast\":" +
                std::to_string(generated.stats.contrast);
            json += ",\"coverage\":" +
                std::to_string(generated.stats.coverage);
            json += ",\"seam_error\":" +
                std::to_string(generated.stats.seam_error);
            json += ",\"relief_range\":" +
                std::to_string(generated.stats.relief_range);
            json += "}}";
            return json;
        }

        std::string command_mesh_raw_get(
            const McpRequest& request
        )
        {
            constexpr uint32_t max_output_vertices = 25000;
            constexpr uint32_t max_output_indices  = 75000;

            std::string path_error;
            const std::optional<std::string> path =
                resolve_mcp_mesh_path(request, path_error);
            if (!path)
            {
                return json_error(path_error);
            }

            std::shared_ptr<Mesh> mesh =
                ResourceCache::GetByPath<Mesh>(*path);
            if (!mesh && FileSystem::IsFile(*path))
            {
                mesh = ResourceCache::Load<Mesh>(*path);
            }
            if (!mesh)
            {
                return json_error(
                    "mesh was not found in the active world's mcp resource library"
                );
            }

            uint32_t sub_mesh_index = 0;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "sub_mesh")
            )
            {
                if (!parse_uint32(*value, sub_mesh_index))
                {
                    return json_error("invalid sub_mesh");
                }
            }
            if (sub_mesh_index >= mesh->GetSubMeshCount())
            {
                return json_error(
                    "sub_mesh is outside the mesh range"
                );
            }

            uint32_t vertex_limit = max_output_vertices;
            uint32_t index_limit  = max_output_indices;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "max_vertices")
            )
            {
                if (
                    !parse_uint32(*value, vertex_limit) ||
                    vertex_limit == 0 ||
                    vertex_limit > max_output_vertices
                )
                {
                    return json_error(
                        "max_vertices must be between 1 and 25000"
                    );
                }
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "max_indices")
            )
            {
                if (
                    !parse_uint32(*value, index_limit) ||
                    index_limit == 0 ||
                    index_limit > max_output_indices
                )
                {
                    return json_error(
                        "max_indices must be between 1 and 75000"
                    );
                }
            }

            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            std::vector<uint32_t> indices;
            mesh->GetGeometry(
                sub_mesh_index,
                &indices,
                &vertices
            );
            if (
                vertices.size() > vertex_limit ||
                indices.size() > index_limit
            )
            {
                std::string error =
                    "mesh output exceeds requested limits, vertex_count=" +
                    std::to_string(vertices.size()) +
                    ", index_count=" +
                    std::to_string(indices.size()) +
                    ", max_vertices=" +
                    std::to_string(vertex_limit) +
                    ", max_indices=" +
                    std::to_string(index_limit);
                return json_error(error);
            }

            std::ostringstream json;
            json << std::setprecision(
                std::numeric_limits<float>::max_digits10
            );
            json << "{\"ok\":true,\"path\":"
                 << json_string(*path)
                 << ",\"sub_mesh\":" << sub_mesh_index
                 << ",\"vertex_count\":" << vertices.size()
                 << ",\"index_count\":" << indices.size()
                 << ",\"truncated\":false";

            json << ",\"positions\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector3 value =
                    vertices[i].get_position();
                json << value.x << "," << value.y << "," << value.z;
            }
            json << "],\"indices\":[";
            for (size_t i = 0; i < indices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                json << indices[i];
            }
            json << "],\"normals\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector3 value =
                    vertices[i].get_normal();
                json << value.x << "," << value.y << "," << value.z;
            }
            json << "],\"uv0\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector2 value =
                    vertices[i].get_uv();
                json << value.x << "," << value.y;
            }
            json << "],\"tangents\":[";
            for (size_t i = 0; i < vertices.size(); i++)
            {
                if (i != 0)
                {
                    json << ",";
                }
                const math::Vector3 value =
                    vertices[i].get_tangent();
                json << value.x << "," << value.y << "," << value.z;
            }
            json << "],\"unsupported_channels\":[\"colors\"]}";
            return json.str();
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
            std::string path_error;
            const std::optional<std::string> resolved_path =
                resolve_mcp_output_path(
                    *path_arg,
                    "meshes",
                    EXTENSION_MESH,
                    path_error
                );
            if (!resolved_path)
            {
                return json_error(path_error);
            }
            const std::string path = *resolved_path;

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
                        "profile must contain between 3 and 128 finite 2d points"
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
                float radial_radius = 0.0f;
                if (
                    const std::optional<std::string> value =
                        get_argument(request, "radial_radius")
                )
                {
                    if (
                        !parse_float(*value, radial_radius) ||
                        radial_radius < 0.0f
                    )
                    {
                        return json_error("invalid radial_radius");
                    }
                }
                if (radial_radius > 0.0f)
                {
                    for (
                        RHI_Vertex_PosTexNorTan& vertex :
                        vertices
                    )
                    {
                        if (
                            *selected_axis ==
                            mcp_geometry_kernel::axis::x
                        )
                        {
                            vertex.pos[1] += radial_radius;
                        }
                        else
                        {
                            vertex.pos[0] += radial_radius;
                        }
                    }
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
                if (!item_succeeded(item_result))
                {
                    return json_batch_failure(
                        "failed to generate mesh batch item",
                        "generated",
                        generated_json,
                        generated_count,
                        i,
                        item_result
                    );
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

            Render* render = entity->GetComponent<Render>();
            if (render == nullptr)
            {
                render = entity->AddComponent<Render>();
            }
            if (render == nullptr)
            {
                return json_error("failed to add render component");
            }

            render->SetMesh(
                mesh.get(),
                sub_mesh_index
            );
            if (render->GetMaterial() == nullptr)
            {
                render->SetDefaultMaterial();
            }

            if (
                const std::optional<std::string> material =
                    get_argument(request, "material")
            )
            {
                std::string material_error;
                if (
                    !assign_render_material(
                        render,
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
            Render* render = entity->AddComponent<Render>();
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: AddComponent<Render> took %.1f ms", step_ms);
            }
            if (render == nullptr)
            {
                return json_error("failed to add render component");
            }
            step_timer.Start();
            render->SetMesh(mesh_type);
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: Render::SetMesh took %.1f ms", step_ms);
            }
            step_timer.Start();
            render->SetDefaultMaterial();
            step_ms = step_timer.GetElapsedTimeMs();
            if (step_ms > 500.0f)
            {
                SP_LOG_WARNING("MCP entity_create_primitive: Render::SetDefaultMaterial took %.1f ms", step_ms);
            }
            if (const std::optional<std::string> material = get_argument(request, "material"))
            {
                std::string material_error;
                if (!assign_render_material(render, *material, material_error))
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

            apply_entity_identity(entity, request);
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
                "physics_restitution",
                "tags",
                "semantic_id",
                "plan_element",
                "semantic_tags"
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
                if (!item_succeeded(item_result))
                {
                    return json_batch_failure(
                        "failed to create primitive batch item",
                        "created",
                        created_json,
                        created_count,
                        i,
                        item_result
                    );
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

        std::vector<std::pair<std::string, std::string>>
        calibrated_light_create_properties(
            const LightType type
        )
        {
            if (type == LightType::Directional)
            {
                return {
                    { "temperature", "5500" },
                    { "color", "1,0.96,0.9,1" },
                    { "intensity", "120000" },
                    { "shadows", "true" },
                    { "volumetric", "false" }
                };
            }
            if (type == LightType::Area)
            {
                return {
                    { "temperature", "3200" },
                    { "color", "1,0.93,0.82,1" },
                    { "intensity", "12000" },
                    { "range", "40" },
                    { "area_width", "6" },
                    { "area_height", "3" },
                    { "shadows", "true" },
                    { "volumetric", "false" },
                    { "draw_distance", "80" },
                    { "shadow_distance", "60" }
                };
            }
            if (type == LightType::Spot)
            {
                return {
                    { "temperature", "3500" },
                    { "color", "1,0.94,0.84,1" },
                    { "intensity", "8500" },
                    { "range", "35" },
                    { "angle_degrees", "45" },
                    { "shadows", "true" },
                    { "volumetric", "false" },
                    { "draw_distance", "70" },
                    { "shadow_distance", "50" }
                };
            }
            return {
                { "temperature", "3200" },
                { "color", "1,0.92,0.78,1" },
                { "intensity", "8500" },
                { "range", "30" },
                { "shadows", "true" },
                { "volumetric", "false" },
                { "draw_distance", "60" },
                { "shadow_distance", "45" }
            };
        }

        float calibrated_light_intensity_floor(
            const LightType type
        )
        {
            if (type == LightType::Directional)
            {
                return 1000.0f;
            }
            if (type == LightType::Area)
            {
                return 1600.0f;
            }
            return 800.0f;
        }

        std::string command_entity_create_light(
            const McpRequest& request
        )
        {
            if (ProgressTracker::IsLoading())
            {
                return json_error("world is loading");
            }
            if (!is_edit_mode())
            {
                return json_error("light creation requires edit mode");
            }

            LightType light_type = LightType::Point;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "light_type")
            )
            {
                const std::optional<LightType> parsed =
                    light_type_from_name(to_lower_copy(*value));
                if (!parsed)
                {
                    return json_error("invalid light_type");
                }
                light_type = *parsed;
            }

            bool calibrated = true;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "calibrated")
            )
            {
                if (!parse_bool(*value, calibrated))
                {
                    return json_error("invalid calibrated");
                }
            }

            Entity* parent = nullptr;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "parent_id")
            )
            {
                uint64_t parent_id = 0;
                if (!parse_uint64(*value, parent_id))
                {
                    return json_error("invalid parent_id");
                }
                parent = World::GetEntityById(parent_id);
                if (parent == nullptr)
                {
                    return json_error("parent entity not found");
                }
            }

            std::optional<math::Vector3> position;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "position")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*value, parsed))
                {
                    return json_error("invalid position");
                }
                position = parsed;
            }

            std::optional<math::Vector3> rotation_euler;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "rotation_euler")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*value, parsed))
                {
                    return json_error("invalid rotation_euler");
                }
                rotation_euler = parsed;
            }

            std::optional<math::Vector3> scale;
            if (
                const std::optional<std::string> value =
                    get_argument(request, "scale")
            )
            {
                math::Vector3 parsed;
                if (!parse_vector3(*value, parsed))
                {
                    return json_error("invalid scale");
                }
                scale = parsed;
            }

            Entity* entity = World::CreateEntity();
            if (entity == nullptr)
            {
                return json_error("failed to create entity");
            }
            entity->SetObjectName(
                get_argument(request, "name").value_or("light")
            );
            if (parent != nullptr)
            {
                entity->SetParent(parent);
            }
            if (position)
            {
                entity->SetPositionLocal(*position);
            }
            if (rotation_euler)
            {
                entity->SetRotationLocal(
                    math::Quaternion::FromEulerAngles(
                        *rotation_euler
                    )
                );
            }
            if (scale)
            {
                entity->SetScaleLocal(*scale);
            }
            apply_entity_identity(entity, request);

            Light* light = entity->AddComponent<Light>();
            if (light == nullptr)
            {
                return json_error("failed to add light component");
            }

            std::string error;
            if (
                !set_light_property(
                    light,
                    "light_type",
                    light_type_to_name(light_type),
                    error
                )
            )
            {
                return json_error(error);
            }

            if (calibrated)
            {
                for (
                    const auto& [property, value] :
                    calibrated_light_create_properties(light_type)
                )
                {
                    if (
                        !set_light_property(
                            light,
                            property,
                            value,
                            error
                        )
                    )
                    {
                        return json_error(error);
                    }
                }
            }

            const std::array<std::string, 11> property_order =
            {
                "temperature",
                "color",
                "intensity",
                "range",
                "angle_degrees",
                "area_width",
                "area_height",
                "shadows",
                "volumetric",
                "draw_distance",
                "shadow_distance"
            };
            for (const std::string& property : property_order)
            {
                const std::optional<std::string> value =
                    get_argument(request, property);
                if (!value)
                {
                    continue;
                }
                if (
                    calibrated &&
                    property == "intensity"
                )
                {
                    float intensity = 0.0f;
                    if (!parse_float(*value, intensity))
                    {
                        return json_error("invalid intensity");
                    }
                    if (
                        intensity <
                        calibrated_light_intensity_floor(light_type)
                    )
                    {
                        continue;
                    }
                }
                if (
                    !set_light_property(
                        light,
                        property,
                        *value,
                        error
                    )
                )
                {
                    return json_error(error);
                }
            }
            if (
                const std::optional<std::string> value =
                    get_argument(request, "volumetric_distance")
            )
            {
                if (
                    !set_light_property(
                        light,
                        "volumetric_distance",
                        *value,
                        error
                    )
                )
                {
                    return json_error(error);
                }
            }

            return
                "{\"ok\":true,\"entity\":" +
                entity_to_json_compact(entity) +
                "}";
        }

        std::string command_entity_create_light_batch(
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
                count > 64
            )
            {
                return json_error(
                    "count must be between 1 and 64"
                );
            }

            const std::vector<std::string> keys =
            {
                "name",
                "parent_id",
                "position",
                "rotation_euler",
                "scale",
                "light_type",
                "color",
                "temperature",
                "intensity",
                "range",
                "angle_degrees",
                "area_width",
                "area_height",
                "shadows",
                "volumetric",
                "draw_distance",
                "shadow_distance",
                "volumetric_distance",
                "calibrated",
                "tags",
                "semantic_id",
                "plan_element",
                "semantic_tags"
            };

            std::string created_json = "[";
            uint32_t created_count = 0;
            for (uint64_t i = 0; i < count; i++)
            {
                McpRequest item_request;
                item_request.command = "entity_create_light";
                for (const std::string& key : keys)
                {
                    const std::string batch_key =
                        "item_" +
                        std::to_string(i) +
                        "_" +
                        key;
                    const auto it =
                        request.arguments.find(batch_key);
                    if (it != request.arguments.end())
                    {
                        item_request.arguments[key] = it->second;
                    }
                }

                const std::string item_result =
                    command_entity_create_light(item_request);
                if (!item_succeeded(item_result))
                {
                    return json_batch_failure(
                        "failed to create light batch item",
                        "created",
                        created_json,
                        created_count,
                        i,
                        item_result
                    );
                }

                if (created_count > 0)
                {
                    created_json += ",";
                }
                created_json += item_result;
                created_count++;
            }

            std::string json =
                "{\"ok\":true,\"created\":" +
                created_json +
                "]";
            json += ",\"created_count\":" +
                std::to_string(created_count);
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
                if (!item_succeeded(item_result))
                {
                    return json_batch_failure(
                        "failed to set transform batch item",
                        "updated",
                        updated_json,
                        updated_count,
                        i,
                        item_result
                    );
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
        // a table rather than a chain of comparisons, so a command is one line to add and the cost of
        // dispatch does not depend on where in the list it happens to sit
        static const std::unordered_map<std::string, std::string (*)(const McpRequest&)> commands =
        {
            { "ping",                          [](const McpRequest&) { return command_ping(); } },
            { "engine_status",                 [](const McpRequest&) { return command_engine_status(); } },
            { "profiler_snapshot",             command_profiler_snapshot },
            { "engine_set_mode",               command_engine_set_mode },
            { "undo_redo",                     command_undo_redo },
            { "cvar_list",                     [](const McpRequest&) { return command_cvar_list(); } },
            { "cvar_get",                      command_cvar_get },
            { "cvar_set",                      command_cvar_set },
            { "console_read",                  command_console_read },
            { "world_summary",                 [](const McpRequest&) { return command_world_summary(); } },
            { "world_load",                    command_world_load },
            { "world_save",                    command_world_save },
            { "world_resources_clean",         [](const McpRequest&) { return command_world_resources_clean(); } },
            { "world_resource_directory_get",  [](const McpRequest&) { return command_world_resource_directory_get(); } },
            { "world_set_environment",         command_world_set_environment },
            { "world_raycast",                 command_world_raycast },
            { "entity_snap",                   command_entity_snap },
            { "entity_spatial_snapshot",       command_entity_spatial_snapshot },
            { "entity_list",                   command_entity_list },
            { "entity_find",                   command_entity_find },
            { "entity_find_by_component",      command_entity_find_by_component },
            { "entity_get",                    command_entity_get },
            { "selection_get",                 [](const McpRequest&) { return command_selection_get(); } },
            { "context_snapshot",              [](const McpRequest&) { return command_context_snapshot(); } },
            { "camera_snapshot",               [](const McpRequest&) { return command_camera_snapshot(); } },
            { "camera_set_view",               command_camera_set_view },
            { "screenshot_take",               command_screenshot_take },
            { "entity_resolve",                command_entity_resolve },
            { "component_types",               [](const McpRequest&) { return command_component_types(); } },
            { "primitive_types",               [](const McpRequest&) { return command_primitive_types(); } },
            { "entity_create_empty",           command_entity_create_empty },
            { "mesh_generate",                 command_mesh_generate },
            { "mesh_raw_create",               command_mesh_raw_create },
            { "mesh_raw_get",                  command_mesh_raw_get },
            { "texture_generate",              command_texture_generate },
            { "mesh_generate_batch",           command_mesh_generate_batch },
            { "render_set_mesh",               command_render_set_mesh },
            { "entity_create_primitive",       command_entity_create_primitive },
            { "entity_create_primitive_batch", command_entity_create_primitive_batch },
            { "entity_create_light",           command_entity_create_light },
            { "entity_create_light_batch",     command_entity_create_light_batch },
            { "entity_update",                 command_entity_update },
            { "entity_delete",                 command_entity_delete },
            { "entity_delete_children",        command_entity_delete_children },
            { "entity_select",                 command_entity_select },
            { "selection_update",              command_selection_update },
            { "entity_clone",                  command_entity_clone },
            { "entity_move_index",             command_entity_move_index },
            { "viewport_frame",                command_viewport_frame },
            { "entity_set_transform",          command_entity_set_transform },
            { "entity_set_transform_batch",    command_entity_set_transform_batch },
            { "entity_add_component",          command_entity_add_component },
            { "entity_remove_component",       command_entity_remove_component },
            { "component_get",                 command_component_get },
            { "entity_render_materials",       command_entity_render_materials },
            { "resource_list",                 command_resource_list },
            { "resource_load",                 command_resource_load },
            { "resource_reload",               command_resource_reload },
            { "resource_save",                 command_resource_save },
            { "resource_remove",               command_resource_remove },
            { "material_get",                  command_material_get },
            { "material_create",               command_material_create },
            { "material_set_property",         command_material_set_property },
            { "material_set_texture",          command_material_set_texture },
            { "material_apply_preset",         command_material_apply_preset },
            { "material_semantic_create",      command_material_semantic_create },
            { "component_set",                 command_component_set },
            { "component_set_batch",           command_component_set_batch },
            { "component_action",              command_component_action },
            { "renderer_debug_get",            [](const McpRequest&) { return command_renderer_debug_get(); } },
            { "renderer_debug_set",            command_renderer_debug_set },
            { "physics_state",                 command_physics_state },
            { "vehicle_list",                  [](const McpRequest&) { return command_vehicle_list(); } },
            { "vehicle_get",                   command_vehicle_get },
            { "vehicle_enter",                 command_vehicle_enter },
            { "vehicle_exit",                  command_vehicle_exit },
            { "vehicle_set_input",             command_vehicle_set_input },
            { "vehicle_shift",                 command_vehicle_shift },
            { "vehicle_reset",                 command_vehicle_reset },
            { "vehicle_set_view",              command_vehicle_set_view },
            { "vehicle_telemetry",             command_vehicle_telemetry },
            { "prefab_types",                  [](const McpRequest&) { return command_prefab_types(); } },
            { "entity_make_game_ready",        command_entity_make_game_ready },
            { "prefab_save",                   command_prefab_save },
            { "prefab_load",                   command_prefab_load },
            // these live in McpCommandsWorldBuild.cpp
            { "spline_query",                  mcp_world_build::command_spline_query },
            { "spline_distribute",             mcp_world_build::command_spline_distribute },
            { "world_landmarks",               mcp_world_build::command_world_landmarks },
            { "spline_create_road",            mcp_world_build::command_spline_create_road },
            { "spline_set_control_points",     mcp_world_build::command_spline_set_control_points },
            { "spline_reroute",                mcp_world_build::command_spline_reroute },
            { "spline_connect",                mcp_world_build::command_spline_connect },
            { "spline_junction",               mcp_world_build::command_spline_junction },
            { "spline_decorate",               mcp_world_build::command_spline_decorate },
            { "district_blockout",             mcp_world_build::command_district_blockout },
            { "city_blockout",                 mcp_world_build::command_city_blockout },
            { "execute_lua",                   command_execute_lua },
        };

        const auto command = commands.find(request.command);
        if (command != commands.end())
        {
            return command->second(request);
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
