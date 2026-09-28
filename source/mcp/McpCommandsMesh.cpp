/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==========================
#include "pch.h"
#include "McpCommandsMesh.h"
#include "McpCommandsCommon.h"
#include "McpGeometryKernel.h"
#include "../core/ProgressTracker.h"
#include "../file_system/FileSystem.h"
#include "../geometry/GeometryGeneration.h"
#include "../geometry/Mesh.h"
#include "../resource/ResourceCache.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <sstream>
//=====================================

namespace spartan::mcp_mesh
{
    namespace
    {
        using namespace mcp_common;

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

        // the largest side of the profile's bounding box, every profile tolerance is derived from
        // it because a coffee mug handle is millimeters across and a building footprint is meters,
        // fixed epsilons rejected the former as degenerate
        float profile_extent(
            const std::vector<math::Vector2>& profile
        )
        {
            if (profile.empty())
            {
                return 0.0f;
            }
            math::Vector2 minimum = profile.front();
            math::Vector2 maximum = profile.front();
            for (const math::Vector2& point : profile)
            {
                minimum.x = std::min(minimum.x, point.x);
                minimum.y = std::min(minimum.y, point.y);
                maximum.x = std::max(maximum.x, point.x);
                maximum.y = std::max(maximum.y, point.y);
            }
            return std::max(
                maximum.x - minimum.x,
                maximum.y - minimum.y
            );
        }

        float profile_length_epsilon(
            const std::vector<math::Vector2>& profile
        )
        {
            return std::max(profile_extent(profile), 0.000001f) * 0.0001f;
        }

        float profile_area_epsilon(
            const std::vector<math::Vector2>& profile
        )
        {
            const float length = profile_length_epsilon(profile);
            return length * length;
        }

        // agents describe a closed outline the way most formats do, with the first point repeated
        // at the end, the generators treat the outline as implicitly closed so that repeat and any
        // other consecutive duplicate would become a zero length edge and fail validation
        void normalize_closed_profile(
            std::vector<math::Vector2>& profile
        )
        {
            const float epsilon = profile_length_epsilon(profile);
            const float epsilon_squared = epsilon * epsilon;
            std::vector<math::Vector2> cleaned;
            cleaned.reserve(profile.size());
            for (const math::Vector2& point : profile)
            {
                if (
                    !cleaned.empty() &&
                    (point - cleaned.back()).LengthSquared() <=
                        epsilon_squared
                )
                {
                    continue;
                }
                cleaned.push_back(point);
            }
            while (
                cleaned.size() > 1 &&
                (cleaned.back() - cleaned.front()).LengthSquared() <=
                    epsilon_squared
            )
            {
                cleaned.pop_back();
            }
            profile = std::move(cleaned);
        }

        bool profile_has_distinct_neighbors(
            const std::vector<math::Vector2>& profile,
            bool closed
        )
        {
            if (profile.size() < 2)
            {
                return false;
            }
            const float epsilon = profile_length_epsilon(profile);
            const float epsilon_squared = epsilon * epsilon;
            const size_t edge_count =
                closed ? profile.size() : profile.size() - 1;
            for (size_t i = 0; i < edge_count; i++)
            {
                const size_t next = (i + 1) % profile.size();
                if (
                    (
                        profile[next] -
                        profile[i]
                    ).LengthSquared() <= epsilon_squared
                )
                {
                    return false;
                }
            }

            return true;
        }

        float profile_signed_area_twice(
            const std::vector<math::Vector2>& profile
        )
        {
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
            return signed_area_twice;
        }

        bool profile_is_convex_counter_clockwise(
            const std::vector<math::Vector2>& profile
        )
        {
            if (
                profile.size() < 3 ||
                !profile_has_distinct_neighbors(profile, true)
            )
            {
                return false;
            }

            const float epsilon = profile_area_epsilon(profile);
            bool has_positive_turn = false;
            for (size_t i = 0; i < profile.size(); i++)
            {
                const math::Vector2& a = profile[i];
                const math::Vector2& b =
                    profile[(i + 1) % profile.size()];
                const math::Vector2& c =
                    profile[(i + 2) % profile.size()];
                const math::Vector2 edge_a = b - a;
                const math::Vector2 edge_b = c - b;
                const float turn =
                    edge_a.x * edge_b.y -
                    edge_a.y * edge_b.x;
                if (turn < -epsilon)
                {
                    return false;
                }
                has_positive_turn |= turn > epsilon;
            }

            return profile_signed_area_twice(profile) > epsilon &&
                has_positive_turn;
        }

        bool profile_is_simple(
            const std::vector<math::Vector2>& profile
        )
        {
            const float length_epsilon =
                profile_length_epsilon(profile);
            const float area_epsilon =
                profile_area_epsilon(profile);
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
            auto on_segment = [length_epsilon](
                const math::Vector2& a,
                const math::Vector2& b,
                const math::Vector2& point
            )
            {
                return
                    point.x >= std::min(a.x, b.x) - length_epsilon &&
                    point.x <= std::max(a.x, b.x) + length_epsilon &&
                    point.y >= std::min(a.y, b.y) - length_epsilon &&
                    point.y <= std::max(a.y, b.y) + length_epsilon;
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
                        (o1 > area_epsilon && o2 < -area_epsilon) ||
                        (o1 < -area_epsilon && o2 > area_epsilon)
                    ) &&
                    (
                        (o3 > area_epsilon && o4 < -area_epsilon) ||
                        (o3 < -area_epsilon && o4 > area_epsilon)
                    )
                )
                {
                    return true;
                }
                if (
                    std::abs(o1) <= area_epsilon &&
                    on_segment(a, b, c)
                )
                {
                    return true;
                }
                if (
                    std::abs(o2) <= area_epsilon &&
                    on_segment(a, b, d)
                )
                {
                    return true;
                }
                if (
                    std::abs(o3) <= area_epsilon &&
                    on_segment(c, d, a)
                )
                {
                    return true;
                }
                return
                    std::abs(o4) <= area_epsilon &&
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
                profile.size() < 3 ||
                !profile_has_distinct_neighbors(profile, true) ||
                !profile_is_simple(profile)
            )
            {
                return false;
            }

            return profile_signed_area_twice(profile) >
                profile_area_epsilon(profile);
        }

        // empty when the outline is usable as a closed counter clockwise profile, otherwise the one
        // thing the caller has to change, an opaque rejection cost an agent five calls of guessing
        std::string closed_profile_problem(
            const std::vector<math::Vector2>& profile
        )
        {
            if (profile.size() < 3)
            {
                return "profile needs at least 3 distinct points";
            }
            if (!profile_has_distinct_neighbors(profile, true))
            {
                return "profile has consecutive duplicate points";
            }
            if (!profile_is_simple(profile))
            {
                return "profile edges cross each other, the outline must be simple";
            }
            if (
                profile_signed_area_twice(profile) <=
                profile_area_epsilon(profile)
            )
            {
                return "profile is clockwise, reverse the point order";
            }
            return "";
        }

        bool profile_has_valid_revolve_tangents(
            const std::vector<math::Vector2>& profile
        )
        {
            if (!profile_has_distinct_neighbors(profile, false))
            {
                return false;
            }

            const float epsilon = profile_length_epsilon(profile);
            const float epsilon_squared = epsilon * epsilon;
            for (size_t i = 1; i + 1 < profile.size(); i++)
            {
                if (
                    (
                        profile[i + 1] -
                        profile[i - 1]
                    ).LengthSquared() <= epsilon_squared
                )
                {
                    return false;
                }
            }

            return true;
        }

        // a wall panel with one rectangular opening cut through it, returns a json error or an empty string
        std::string build_wall_opening(
            const McpRequest& request,
            const math::Vector3& size,
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices
        )
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

            return "";
        }

        // a wall panel with up to 16 rectangular openings, returns a json error or an empty string
        std::string build_wall_openings(
            const McpRequest& request,
            const math::Vector3& size,
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices
        )
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

            return "";
        }

        // pipe, curved_profile and loft, swept along path points, returns a json error or an empty string
        std::string build_swept_shape(
            const McpRequest& request,
            const std::string& shape,
            uint32_t segments,
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices
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
                    !parse_profile(*profile_arg, profile)
                )
                {
                    return json_error(
                        "curved profile requires 3 to 128 finite x,y points"
                    );
                }
                normalize_closed_profile(profile);
                if (
                    const std::string problem =
                        closed_profile_problem(profile);
                    !problem.empty()
                )
                {
                    return json_error(
                        "curved profile rejected: " + problem
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
                for (size_t i = 0; i < profiles.size(); i++)
                {
                    std::vector<math::Vector2>& profile = profiles[i];
                    normalize_closed_profile(profile);
                    if (
                        const std::string problem =
                            closed_profile_problem(profile);
                        !problem.empty()
                    )
                    {
                        return json_error(
                            "loft profile " +
                            std::to_string(i) +
                            " rejected: " +
                            problem
                        );
                    }
                    if (profile.size() != profiles.front().size())
                    {
                        return json_error(
                            "loft profiles must all have the same number of distinct points"
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

            return "";
        }

        // extruded_profile and revolved_profile from a 2d profile, returns a json error or an empty string
        std::string build_profile_shape(
            const McpRequest& request,
            const std::string& shape,
            uint32_t segments,
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices
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
                normalize_closed_profile(profile);
                if (
                    const std::string problem =
                        closed_profile_problem(profile);
                    !problem.empty()
                )
                {
                    return json_error(
                        "extruded profile rejected: " + problem
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

            return "";
        }

        // fills vertices and indices with one parametric shape, returns a json error or an empty string
        std::string generate_parametric_shape(
            const McpRequest& request,
            const std::string& shape,
            math::Vector3 size,
            uint32_t segments,
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices
        )
        {
            if (shape == "box" || shape == "cube" || shape == "plane" || shape == "quad")
            {
                if (shape == "plane" || shape == "quad")
                    geometry_generation::generate_quad(&vertices, &indices);
                else
                    geometry_generation::generate_cube(&vertices, &indices);
                for (auto& vertex : vertices)
                {
                    vertex.pos[0] *= size.x;
                    vertex.pos[1] *= size.y;
                    vertex.pos[2] *= size.z;
                }
            }
            else if (shape == "arc" || shape == "sector" || shape == "ring" || shape == "tube" || shape == "disk")
            {
                const bool upright = shape == "arc" || shape == "sector";
                float radius = std::min(size.x, upright ? size.y : size.z) * 0.5f;
                float inner_radius = (shape == "sector" || shape == "disk") ? 0.0f : radius * 0.75f;
                float depth = shape == "disk" ? 0.0f : (upright ? size.z : size.y);
                float start_degrees = 0.0f;
                float sweep_degrees = (shape == "arc" || shape == "sector") ? 90.0f : 360.0f;
                const auto read = [&](const char* key, float& value)
                {
                    const auto argument = get_argument(request, key);
                    return !argument || (parse_float(*argument, value) && std::isfinite(value));
                };
                if (!read("radius", radius))
                    return json_error("invalid radius");
                inner_radius = (shape == "sector" || shape == "disk") ? 0.0f : radius * 0.75f;
                if (!read("inner_radius", inner_radius) || !read("depth", depth) ||
                    !read("start_degrees", start_degrees) || !read("sweep_degrees", sweep_degrees))
                    return json_error("invalid arc dimensions or angles");
                if (!get_argument(request, "segments"))
                    segments = 32;
                if (radius <= 0 || radius > 1000 || inner_radius < 0 || inner_radius >= radius ||
                    depth < 0 || depth > 1000 || sweep_degrees <= 0 || sweep_degrees > 360 ||
                    std::abs(start_degrees) > 3600 || segments < 3 || segments > 64)
                    return json_error("arc requires 0 <= inner_radius < radius <= 1000, depth 0..1000, sweep (0,360], start [-3600,3600], segments 3..64");
                geometry_generation::generate_arc(&vertices, &indices, radius, inner_radius, depth,
                    start_degrees * math::deg_to_rad, sweep_degrees * math::deg_to_rad, segments);
                // Arcs stand in XY; disks, rings and tubes lie in XZ with their axis along Y.
                if (shape == "ring" || shape == "tube" || shape == "disk")
                {
                    for (auto& vertex : vertices)
                    {
                        const auto rotate = [](const math::Vector3& value)
                        {
                            return math::Vector3(value.x, value.z, -value.y);
                        };
                        vertex.set_position(rotate(vertex.get_position()));
                        vertex.set_normal(rotate(vertex.get_normal()));
                        vertex.set_tangent(rotate(vertex.get_tangent()));
                    }
                }
            }
            else if (shape == "sphere" || shape == "ellipsoid" || shape == "hemisphere" ||
                     shape == "cylinder" || shape == "cone" || shape == "frustum")
            {
                float radius = std::min(size.x, size.z) * 0.5f;
                float height = size.y;
                const auto read = [&](const char* key, float& value)
                {
                    const auto argument = get_argument(request, key);
                    return !argument || (parse_float(*argument, value) && std::isfinite(value));
                };
                if (!read("radius", radius) || !read("height", height))
                    return json_error("invalid radius or height");
                float radius_top = shape == "cone" ? 0.0f : (shape == "frustum" ? radius * 0.5f : radius);
                if (!read("radius_top", radius_top))
                    return json_error("invalid radius_top");
                if (!get_argument(request, "segments"))
                    segments = 32;
                if (radius <= 0 || radius > 1000 || height <= 0 || height > 1000 ||
                    radius_top < 0 || radius_top > 1000 || segments < 3 || segments > 64)
                    return json_error("radius and height must be (0,1000], radius_top [0,1000], segments 3..64");
                if (shape == "sphere" || shape == "ellipsoid")
                {
                    geometry_generation::generate_sphere(&vertices, &indices, radius, segments, segments);
                    if (shape == "ellipsoid")
                    {
                        const math::Vector3 scale = size * (0.5f / radius);
                        for (auto& vertex : vertices)
                        {
                            for (uint32_t i = 0; i < 3; i++)
                                vertex.pos[i] *= i == 0 ? scale.x : (i == 1 ? scale.y : scale.z);
                            const math::Vector3 normal = vertex.get_normal();
                            const math::Vector3 tangent = vertex.get_tangent();
                            vertex.set_normal(math::Vector3(normal.x / scale.x, normal.y / scale.y, normal.z / scale.z).Normalized());
                            vertex.set_tangent(math::Vector3(tangent.x * scale.x, tangent.y * scale.y, tangent.z * scale.z).Normalized());
                        }
                    }
                }
                else if (shape == "hemisphere")
                {
                    std::vector<math::Vector2> profile;
                    profile.emplace_back(0.0f, 0.0f);
                    for (uint32_t i = 0; i <= segments; i++)
                    {
                        const float angle = static_cast<float>(i) / static_cast<float>(segments) * math::pi * 0.5f;
                        profile.emplace_back(i == segments ? 0.0f : radius * std::cos(angle), radius * std::sin(angle));
                    }
                    geometry_generation::generate_revolved_profile(&vertices, &indices, profile, segments);
                }
                else
                {
                    geometry_generation::generate_cylinder(&vertices, &indices, radius_top, radius, height, segments, 1);
                }
            }
            else if (shape == "rounded_box" || shape == "beveled_box")
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
                return build_wall_opening(request, size, vertices, indices);
            }
            else if (shape == "wall_openings")
            {
                return build_wall_openings(request, size, vertices, indices);
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
                return build_swept_shape(request, shape, segments, vertices, indices);
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
                    !parse_profile(*profile_arg, profile)
                )
                {
                    return json_error(
                        "tapered extrusion requires 3 to 128 finite x,y points"
                    );
                }
                normalize_closed_profile(profile);
                if (!profile_is_convex_counter_clockwise(profile))
                {
                    const std::string problem =
                        closed_profile_problem(profile);
                    return json_error(
                        "tapered extrusion rejected: " +
                        (
                            problem.empty()
                                ? std::string("profile is concave, every turn must bend the same way")
                                : problem
                        )
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
                return build_profile_shape(request, shape, segments, vertices, indices);
            }
            else
            {
                return json_error("unsupported parametric shape");
            }

            return "";
        }

        // taper, bend, mirror, shell, linear and radial arrays, then uv projection, returns a json error or an empty string
        std::string apply_mesh_modifiers(
            const McpRequest& request,
            std::vector<RHI_Vertex_PosTexNorTan>& vertices,
            std::vector<uint32_t>& indices,
            std::vector<std::string>& applied_modifiers
        )
        {
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
                bool mirror_copy = false;
                if (const auto copy = get_argument(request, "mirror_copy"))
                {
                    if (!parse_bool(*copy, mirror_copy))
                        return json_error("invalid mirror_copy");
                }
                const auto original_vertices = mirror_copy ? vertices : std::vector<RHI_Vertex_PosTexNorTan>();
                const auto original_indices = mirror_copy ? indices : std::vector<uint32_t>();
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
                if (mirror_copy)
                {
                    const auto appended = mcp_geometry_kernel::append_mesh(original_vertices, original_indices, vertices, indices);
                    if (!appended.succeeded())
                        return json_error("mirror copy failed, " + appended.message);
                    applied_modifiers.emplace_back("mirror_copy");
                }
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

            return "";
        }

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

        if (const std::string error = generate_parametric_shape(request, shape, size, segments, vertices, indices); !error.empty())
        {
            return error;
        }

        std::vector<std::string> applied_modifiers;
        if (const std::string error = apply_mesh_modifiers(request, vertices, indices, applied_modifiers); !error.empty())
        {
            return error;
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

        std::string generated_json = "[";
        uint32_t generated_count = 0;
        for (uint64_t i = 0; i < count; i++)
        {
            McpRequest item_request;
            item_request.command = "mesh_generate";
            // Forward the complete per-item argument set so batch generation has the same
            // shape, opening, modifier and UV capabilities as a single generation call.
            const std::string prefix = "item_" + std::to_string(i) + "_";
            for (const auto& [key, value] : request.arguments)
            {
                if (key.rfind(prefix, 0) == 0)
                    item_request.arguments[key.substr(prefix.size())] = value;
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
}
