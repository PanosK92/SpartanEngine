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

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include "../Math/Matrix.h"
#include "../RHI/RHI_Vertex.h"

namespace spartan::mcp_geometry_kernel
{
    enum class status
    {
        success,
        invalid_argument,
        invalid_topology,
        non_finite_value,
        budget_exceeded,
        unsupported
    };

    enum class axis
    {
        x,
        y,
        z
    };

    enum class boolean_operation
    {
        union_mesh,
        intersection,
        difference
    };

    struct budgets
    {
        size_t max_vertices = 100000;
        size_t max_indices  = 300000;
    };

    struct statistics
    {
        size_t vertex_count       = 0;
        size_t index_count        = 0;
        size_t triangle_count     = 0;
        size_t degenerate_count   = 0;
        uint32_t max_index        = 0;
        math::Vector3 bounds_min  = math::Vector3::Zero;
        math::Vector3 bounds_max  = math::Vector3::Zero;
    };

    struct operation_result
    {
        status code = status::success;
        std::string message;
        statistics stats;
        std::vector<std::string> warnings;

        bool succeeded() const
        {
            return code == status::success;
        }
    };

    struct capability
    {
        bool available = false;
        status code = status::unsupported;
        std::string message;
    };

    inline float component(
        const math::Vector3& value,
        axis selected_axis
    )
    {
        if (selected_axis == axis::x)
        {
            return value.x;
        }

        if (selected_axis == axis::y)
        {
            return value.y;
        }

        return value.z;
    }

    inline void set_component(
        math::Vector3& value,
        axis selected_axis,
        float component_value
    )
    {
        if (selected_axis == axis::x)
        {
            value.x = component_value;
        }
        else if (selected_axis == axis::y)
        {
            value.y = component_value;
        }
        else
        {
            value.z = component_value;
        }
    }

    inline bool add_would_exceed(
        size_t current,
        size_t added,
        size_t maximum
    )
    {
        return current > maximum || added > maximum - current;
    }

    inline math::Vector3 safe_normalized(
        const math::Vector3& value,
        const math::Vector3& fallback
    )
    {
        if (!value.IsFinite() || value.LengthSquared() <= 1e-14f)
        {
            return fallback;
        }

        return value.Normalized();
    }

    inline math::Vector3 fallback_tangent(
        const math::Vector3& normal
    )
    {
        const math::Vector3 reference =
            std::abs(normal.y) < 0.9f
            ? math::Vector3::Up
            : math::Vector3::Right;

        return safe_normalized(
            math::Vector3::Cross(reference, normal),
            math::Vector3::Right
        );
    }

    inline statistics calculate_statistics(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices
    )
    {
        statistics stats;
        stats.vertex_count   = vertices.size();
        stats.index_count    = indices.size();
        stats.triangle_count = indices.size() / 3;

        if (!vertices.empty())
        {
            stats.bounds_min = vertices.front().get_position();
            stats.bounds_max = stats.bounds_min;

            for (const RHI_Vertex_PosTexNorTan& vertex : vertices)
            {
                const math::Vector3 position = vertex.get_position();
                stats.bounds_min.x = std::min(
                    stats.bounds_min.x,
                    position.x
                );
                stats.bounds_min.y = std::min(
                    stats.bounds_min.y,
                    position.y
                );
                stats.bounds_min.z = std::min(
                    stats.bounds_min.z,
                    position.z
                );
                stats.bounds_max.x = std::max(
                    stats.bounds_max.x,
                    position.x
                );
                stats.bounds_max.y = std::max(
                    stats.bounds_max.y,
                    position.y
                );
                stats.bounds_max.z = std::max(
                    stats.bounds_max.z,
                    position.z
                );
            }
        }

        for (uint32_t index : indices)
        {
            stats.max_index = std::max(stats.max_index, index);
        }

        for (size_t i = 0; i + 2 < indices.size(); i += 3)
        {
            const uint32_t i0 = indices[i];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];

            if (
                i0 >= vertices.size() ||
                i1 >= vertices.size() ||
                i2 >= vertices.size()
            )
            {
                continue;
            }

            const math::Vector3 edge_a =
                vertices[i1].get_position() -
                vertices[i0].get_position();
            const math::Vector3 edge_b =
                vertices[i2].get_position() -
                vertices[i0].get_position();

            if (
                i0 == i1 ||
                i1 == i2 ||
                i2 == i0 ||
                math::Vector3::Cross(edge_a, edge_b).LengthSquared() <=
                1e-14f
            )
            {
                stats.degenerate_count++;
            }
        }

        return stats;
    }

    inline operation_result validate(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        const budgets& limits = budgets()
    )
    {
        operation_result result;
        result.stats = calculate_statistics(vertices, indices);

        if (vertices.empty() || indices.empty())
        {
            result.code = status::invalid_topology;
            result.message = "mesh must contain vertices and indices";
            return result;
        }

        if (vertices.size() > limits.max_vertices)
        {
            result.code = status::budget_exceeded;
            result.message = "vertex budget exceeded";
            return result;
        }

        if (
            vertices.size() >
            static_cast<size_t>(
                std::numeric_limits<uint32_t>::max()
            )
        )
        {
            result.code = status::budget_exceeded;
            result.message = "vertex count exceeds the index format";
            return result;
        }

        if (indices.size() > limits.max_indices)
        {
            result.code = status::budget_exceeded;
            result.message = "index budget exceeded";
            return result;
        }

        if (indices.size() % 3 != 0)
        {
            result.code = status::invalid_topology;
            result.message = "index count must be divisible by three";
            return result;
        }

        for (const RHI_Vertex_PosTexNorTan& vertex : vertices)
        {
            if (
                !vertex.get_position().IsFinite() ||
                !vertex.get_uv().IsFinite() ||
                !vertex.get_normal().IsFinite() ||
                !vertex.get_tangent().IsFinite()
            )
            {
                result.code = status::non_finite_value;
                result.message = "vertex contains a non finite value";
                return result;
            }
        }

        for (uint32_t index : indices)
        {
            if (index >= vertices.size())
            {
                result.code = status::invalid_topology;
                result.message = "index is outside the vertex buffer";
                return result;
            }
        }

        if (result.stats.degenerate_count != 0)
        {
            result.warnings.push_back(
                "mesh contains degenerate triangles"
            );
        }

        result.message = "mesh is valid";
        return result;
    }

    inline operation_result recalculate_normals_tangents(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        std::vector<math::Vector3> normals(
            vertices.size(),
            math::Vector3::Zero
        );
        std::vector<math::Vector3> tangents(
            vertices.size(),
            math::Vector3::Zero
        );
        size_t uv_degenerate_count = 0;

        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const uint32_t i0 = indices[i];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            const math::Vector3 p0 = vertices[i0].get_position();
            const math::Vector3 p1 = vertices[i1].get_position();
            const math::Vector3 p2 = vertices[i2].get_position();
            const math::Vector3 edge_a = p1 - p0;
            const math::Vector3 edge_b = p2 - p0;
            const math::Vector3 face_normal =
                math::Vector3::Cross(edge_a, edge_b);

            if (face_normal.LengthSquared() <= 1e-14f)
            {
                continue;
            }

            normals[i0] += face_normal;
            normals[i1] += face_normal;
            normals[i2] += face_normal;

            const math::Vector2 uv0 = vertices[i0].get_uv();
            const math::Vector2 uv1 = vertices[i1].get_uv();
            const math::Vector2 uv2 = vertices[i2].get_uv();
            const math::Vector2 delta_a = uv1 - uv0;
            const math::Vector2 delta_b = uv2 - uv0;
            const float determinant =
                delta_a.x * delta_b.y -
                delta_a.y * delta_b.x;

            if (std::abs(determinant) <= 1e-8f)
            {
                uv_degenerate_count++;
                continue;
            }

            const math::Vector3 tangent = (
                edge_a * delta_b.y -
                edge_b * delta_a.y
            ) / determinant;
            tangents[i0] += tangent;
            tangents[i1] += tangent;
            tangents[i2] += tangent;
        }

        for (size_t i = 0; i < vertices.size(); i++)
        {
            const math::Vector3 normal = safe_normalized(
                normals[i],
                vertices[i].get_normal().LengthSquared() > 1e-14f
                    ? vertices[i].get_normal()
                    : math::Vector3::Up
            );
            math::Vector3 tangent =
                tangents[i] -
                normal * math::Vector3::Dot(normal, tangents[i]);
            tangent = safe_normalized(
                tangent,
                fallback_tangent(normal)
            );
            vertices[i].set_normal(normal);
            vertices[i].set_tangent(tangent);
        }

        result.stats = calculate_statistics(vertices, indices);
        result.message = "normals and tangents recalculated";
        if (uv_degenerate_count != 0)
        {
            result.warnings.push_back(
                "some triangles used fallback tangents"
            );
        }
        return result;
    }

    inline math::Vector3 transform_direction(
        const math::Vector3& value,
        const math::Matrix& matrix
    )
    {
        return math::Vector3(
            value.x * matrix.m00 +
                value.y * matrix.m10 +
                value.z * matrix.m20,
            value.x * matrix.m01 +
                value.y * matrix.m11 +
                value.z * matrix.m21,
            value.x * matrix.m02 +
                value.y * matrix.m12 +
                value.z * matrix.m22
        );
    }

    inline operation_result transform(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        const math::Matrix& matrix,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        const float determinant =
            matrix.m00 * (
                matrix.m11 * matrix.m22 -
                matrix.m12 * matrix.m21
            ) -
            matrix.m01 * (
                matrix.m10 * matrix.m22 -
                matrix.m12 * matrix.m20
            ) +
            matrix.m02 * (
                matrix.m10 * matrix.m21 -
                matrix.m11 * matrix.m20
            );

        if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-8f)
        {
            result.code = status::invalid_argument;
            result.message = "transform matrix must be finite and invertible";
            return result;
        }

        const float* matrix_values = matrix.Data();
        for (size_t i = 0; i < 16; i++)
        {
            if (!std::isfinite(matrix_values[i]))
            {
                result.code = status::invalid_argument;
                result.message = "transform matrix must be finite";
                return result;
            }
        }

        const math::Matrix normal_matrix =
            matrix.Inverted().Transposed();

        std::vector<RHI_Vertex_PosTexNorTan> transformed = vertices;
        std::vector<uint32_t> transformed_indices = indices;
        for (RHI_Vertex_PosTexNorTan& vertex : transformed)
        {
            const math::Vector3 position =
                matrix * vertex.get_position();
            const math::Vector3 normal = safe_normalized(
                transform_direction(
                    vertex.get_normal(),
                    normal_matrix
                ),
                math::Vector3::Up
            );
            math::Vector3 tangent = transform_direction(
                vertex.get_tangent(),
                matrix
            );
            tangent -= normal * math::Vector3::Dot(normal, tangent);
            tangent = safe_normalized(
                tangent,
                fallback_tangent(normal)
            );

            if (!position.IsFinite())
            {
                result.code = status::non_finite_value;
                result.message = "transform produced a non finite position";
                return result;
            }

            vertex.set_position(position);
            vertex.set_normal(normal);
            vertex.set_tangent(tangent);
        }

        if (determinant < 0.0f)
        {
            for (
                size_t i = 0;
                i < transformed_indices.size();
                i += 3
            )
            {
                std::swap(
                    transformed_indices[i + 1],
                    transformed_indices[i + 2]
                );
            }
        }

        result = validate(
            transformed,
            transformed_indices,
            limits
        );
        if (!result.succeeded())
        {
            result.message =
                "transformed mesh is invalid, " + result.message;
            return result;
        }

        vertices = std::move(transformed);
        indices  = std::move(transformed_indices);
        result.stats = calculate_statistics(vertices, indices);
        result.message = "mesh transformed";
        return result;
    }

    inline operation_result append_mesh(
        const std::vector<RHI_Vertex_PosTexNorTan>& source_vertices,
        const std::vector<uint32_t>& source_indices,
        std::vector<RHI_Vertex_PosTexNorTan>& target_vertices,
        std::vector<uint32_t>& target_indices,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(
            source_vertices,
            source_indices,
            limits
        );
        if (!result.succeeded())
        {
            return result;
        }

        if (
            &source_vertices == &target_vertices ||
            &source_indices == &target_indices
        )
        {
            result.code = status::invalid_argument;
            result.message = "source and target buffers must not alias";
            return result;
        }

        if (!target_vertices.empty() || !target_indices.empty())
        {
            result = validate(
                target_vertices,
                target_indices,
                limits
            );
            if (!result.succeeded())
            {
                result.message =
                    "target mesh is invalid, " + result.message;
                return result;
            }
        }

        if (
            add_would_exceed(
                target_vertices.size(),
                source_vertices.size(),
                limits.max_vertices
            ) ||
            add_would_exceed(
                target_indices.size(),
                source_indices.size(),
                limits.max_indices
            ) ||
            target_vertices.size() >
                std::numeric_limits<uint32_t>::max() ||
            source_vertices.size() >
                std::numeric_limits<uint32_t>::max() -
                target_vertices.size()
        )
        {
            result.code = status::budget_exceeded;
            result.message = "merged mesh exceeds the mesh budget";
            return result;
        }

        const uint32_t vertex_offset =
            static_cast<uint32_t>(target_vertices.size());
        target_vertices.insert(
            target_vertices.end(),
            source_vertices.begin(),
            source_vertices.end()
        );
        target_indices.reserve(
            target_indices.size() + source_indices.size()
        );
        for (uint32_t index : source_indices)
        {
            target_indices.push_back(vertex_offset + index);
        }

        result = validate(
            target_vertices,
            target_indices,
            limits
        );
        if (result.succeeded())
        {
            result.message = "mesh appended";
        }
        return result;
    }

    inline operation_result linear_array(
        const std::vector<RHI_Vertex_PosTexNorTan>& source_vertices,
        const std::vector<uint32_t>& source_indices,
        uint32_t count,
        const math::Vector3& step,
        std::vector<RHI_Vertex_PosTexNorTan>& output_vertices,
        std::vector<uint32_t>& output_indices,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(
            source_vertices,
            source_indices,
            limits
        );
        if (!result.succeeded())
        {
            return result;
        }

        if (count == 0 || !step.IsFinite())
        {
            result.code = status::invalid_argument;
            result.message = "array count and step are invalid";
            return result;
        }

        if (
            source_vertices.size() >
                limits.max_vertices / count ||
            source_indices.size() >
                limits.max_indices / count ||
            source_vertices.size() >
                std::numeric_limits<uint32_t>::max() / count
        )
        {
            result.code = status::budget_exceeded;
            result.message = "linear array exceeds the mesh budget";
            return result;
        }

        std::vector<RHI_Vertex_PosTexNorTan> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(source_vertices.size() * count);
        indices.reserve(source_indices.size() * count);

        for (uint32_t instance = 0; instance < count; instance++)
        {
            const math::Vector3 offset =
                step * static_cast<float>(instance);
            const uint32_t vertex_offset =
                static_cast<uint32_t>(vertices.size());

            for (
                const RHI_Vertex_PosTexNorTan& source_vertex :
                source_vertices
            )
            {
                RHI_Vertex_PosTexNorTan vertex = source_vertex;
                vertex.set_position(
                    source_vertex.get_position() + offset
                );
                vertices.push_back(vertex);
            }

            for (uint32_t index : source_indices)
            {
                indices.push_back(vertex_offset + index);
            }
        }

        result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            result.message =
                "linear array is invalid, " + result.message;
            return result;
        }

        output_vertices = std::move(vertices);
        output_indices  = std::move(indices);
        result.message = "linear array generated";
        return result;
    }

    inline operation_result radial_array(
        const std::vector<RHI_Vertex_PosTexNorTan>& source_vertices,
        const std::vector<uint32_t>& source_indices,
        uint32_t count,
        axis rotation_axis,
        float angle_radians,
        const math::Vector3& center,
        std::vector<RHI_Vertex_PosTexNorTan>& output_vertices,
        std::vector<uint32_t>& output_indices,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(
            source_vertices,
            source_indices,
            limits
        );
        if (!result.succeeded())
        {
            return result;
        }

        if (
            count == 0 ||
            !std::isfinite(angle_radians) ||
            !center.IsFinite()
        )
        {
            result.code = status::invalid_argument;
            result.message = "radial array parameters are invalid";
            return result;
        }

        if (
            source_vertices.size() >
                limits.max_vertices / count ||
            source_indices.size() >
                limits.max_indices / count ||
            source_vertices.size() >
                std::numeric_limits<uint32_t>::max() / count
        )
        {
            result.code = status::budget_exceeded;
            result.message = "radial array exceeds the mesh budget";
            return result;
        }

        std::vector<RHI_Vertex_PosTexNorTan> vertices;
        std::vector<uint32_t> indices;
        vertices.reserve(source_vertices.size() * count);
        indices.reserve(source_indices.size() * count);

        for (uint32_t instance = 0; instance < count; instance++)
        {
            const float angle =
                angle_radians * static_cast<float>(instance);
            const float cosine = std::cos(angle);
            const float sine   = std::sin(angle);
            const uint32_t vertex_offset =
                static_cast<uint32_t>(vertices.size());

            auto rotate = [&](const math::Vector3& value)
            {
                if (rotation_axis == axis::x)
                {
                    return math::Vector3(
                        value.x,
                        value.y * cosine - value.z * sine,
                        value.y * sine + value.z * cosine
                    );
                }

                if (rotation_axis == axis::y)
                {
                    return math::Vector3(
                        value.x * cosine + value.z * sine,
                        value.y,
                        -value.x * sine + value.z * cosine
                    );
                }

                return math::Vector3(
                    value.x * cosine - value.y * sine,
                    value.x * sine + value.y * cosine,
                    value.z
                );
            };

            for (
                const RHI_Vertex_PosTexNorTan& source_vertex :
                source_vertices
            )
            {
                RHI_Vertex_PosTexNorTan vertex = source_vertex;
                vertex.set_position(
                    center +
                    rotate(source_vertex.get_position() - center)
                );
                vertex.set_normal(
                    safe_normalized(
                        rotate(source_vertex.get_normal()),
                        math::Vector3::Up
                    )
                );
                vertex.set_tangent(
                    safe_normalized(
                        rotate(source_vertex.get_tangent()),
                        math::Vector3::Right
                    )
                );
                vertices.push_back(vertex);
            }

            for (uint32_t index : source_indices)
            {
                indices.push_back(vertex_offset + index);
            }
        }

        result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            result.message =
                "radial array is invalid, " + result.message;
            return result;
        }

        output_vertices = std::move(vertices);
        output_indices  = std::move(indices);
        result.message = "radial array generated";
        return result;
    }

    inline operation_result mirror(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        axis mirror_axis,
        float plane,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        if (!std::isfinite(plane))
        {
            result.code = status::invalid_argument;
            result.message = "mirror plane must be finite";
            return result;
        }

        std::vector<RHI_Vertex_PosTexNorTan> mirrored = vertices;
        std::vector<uint32_t> mirrored_indices = indices;
        for (RHI_Vertex_PosTexNorTan& vertex : mirrored)
        {
            math::Vector3 position = vertex.get_position();
            math::Vector3 normal   = vertex.get_normal();
            math::Vector3 tangent  = vertex.get_tangent();
            set_component(
                position,
                mirror_axis,
                plane * 2.0f - component(position, mirror_axis)
            );
            set_component(
                normal,
                mirror_axis,
                -component(normal, mirror_axis)
            );
            set_component(
                tangent,
                mirror_axis,
                -component(tangent, mirror_axis)
            );
            vertex.set_position(position);
            vertex.set_normal(normal);
            vertex.set_tangent(tangent);
        }

        for (size_t i = 0; i < mirrored_indices.size(); i += 3)
        {
            std::swap(
                mirrored_indices[i + 1],
                mirrored_indices[i + 2]
            );
        }

        result = validate(mirrored, mirrored_indices, limits);
        if (!result.succeeded())
        {
            result.message =
                "mirrored mesh is invalid, " + result.message;
            return result;
        }

        vertices = std::move(mirrored);
        indices  = std::move(mirrored_indices);
        result.message = "mesh mirrored";
        return result;
    }

    inline operation_result taper(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis taper_axis,
        float start_scale,
        float end_scale,
        const math::Vector3& pivot,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        if (
            !std::isfinite(start_scale) ||
            !std::isfinite(end_scale) ||
            start_scale <= 0.0f ||
            end_scale <= 0.0f ||
            !pivot.IsFinite()
        )
        {
            result.code = status::invalid_argument;
            result.message = "taper scales and pivot are invalid";
            return result;
        }

        const float minimum = component(
            result.stats.bounds_min,
            taper_axis
        );
        const float maximum = component(
            result.stats.bounds_max,
            taper_axis
        );
        const float extent = maximum - minimum;
        if (extent <= 1e-7f)
        {
            result.code = status::invalid_argument;
            result.message = "taper axis has no extent";
            return result;
        }

        std::vector<RHI_Vertex_PosTexNorTan> tapered = vertices;
        for (RHI_Vertex_PosTexNorTan& vertex : tapered)
        {
            math::Vector3 position = vertex.get_position();
            const float t =
                (component(position, taper_axis) - minimum) /
                extent;
            const float scale =
                start_scale +
                (end_scale - start_scale) * t;

            if (taper_axis != axis::x)
            {
                position.x = pivot.x +
                    (position.x - pivot.x) * scale;
            }
            if (taper_axis != axis::y)
            {
                position.y = pivot.y +
                    (position.y - pivot.y) * scale;
            }
            if (taper_axis != axis::z)
            {
                position.z = pivot.z +
                    (position.z - pivot.z) * scale;
            }
            vertex.set_position(position);
        }

        result = recalculate_normals_tangents(
            tapered,
            indices,
            limits
        );
        if (result.succeeded())
        {
            vertices = std::move(tapered);
            result.message = "mesh tapered";
        }
        return result;
    }

    inline operation_result bend(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis length_axis,
        axis radial_axis,
        float angle_radians,
        const math::Vector3& pivot,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        if (
            length_axis == radial_axis ||
            !std::isfinite(angle_radians) ||
            !pivot.IsFinite()
        )
        {
            result.code = status::invalid_argument;
            result.message = "bend axes or angle are invalid";
            return result;
        }

        if (std::abs(angle_radians) <= 1e-7f)
        {
            result.message = "bend angle is zero";
            return result;
        }

        const float minimum = component(
            result.stats.bounds_min,
            length_axis
        );
        const float maximum = component(
            result.stats.bounds_max,
            length_axis
        );
        const float extent = maximum - minimum;
        if (extent <= 1e-7f)
        {
            result.code = status::invalid_argument;
            result.message = "bend axis has no extent";
            return result;
        }

        const float radius = extent / angle_radians;
        const float radial_pivot = component(pivot, radial_axis);

        std::vector<RHI_Vertex_PosTexNorTan> bent = vertices;
        for (RHI_Vertex_PosTexNorTan& vertex : bent)
        {
            math::Vector3 position = vertex.get_position();
            const float distance =
                component(position, length_axis) - minimum;
            const float angle =
                distance / extent * angle_radians;
            const float radial_distance =
                component(position, radial_axis) -
                radial_pivot;
            const float ring_radius = radius + radial_distance;
            set_component(
                position,
                length_axis,
                minimum + std::sin(angle) * ring_radius
            );
            set_component(
                position,
                radial_axis,
                radial_pivot +
                    std::cos(angle) * ring_radius -
                    radius
            );
            vertex.set_position(position);
        }

        result = recalculate_normals_tangents(
            bent,
            indices,
            limits
        );
        if (result.succeeded())
        {
            vertices = std::move(bent);
            result.message = "mesh bent";
        }
        return result;
    }

    inline void projection_axes(
        axis normal_axis,
        axis& u_axis,
        axis& v_axis
    )
    {
        if (normal_axis == axis::x)
        {
            u_axis = axis::z;
            v_axis = axis::y;
        }
        else if (normal_axis == axis::y)
        {
            u_axis = axis::x;
            v_axis = axis::z;
        }
        else
        {
            u_axis = axis::x;
            v_axis = axis::y;
        }
    }

    inline void projection_signs(
        axis normal_axis,
        const math::Vector3& normal,
        float& u_sign,
        float& v_sign
    )
    {
        u_sign = 1.0f;
        v_sign = 1.0f;
        if (normal_axis == axis::x)
        {
            u_sign = normal.x >= 0.0f ? -1.0f : 1.0f;
        }
        else if (normal_axis == axis::y)
        {
            v_sign = normal.y >= 0.0f ? -1.0f : 1.0f;
        }
        else
        {
            u_sign = normal.z >= 0.0f ? 1.0f : -1.0f;
        }
    }

    inline float normalized_component(
        const math::Vector3& position,
        axis selected_axis,
        const statistics& stats
    )
    {
        const float minimum = component(
            stats.bounds_min,
            selected_axis
        );
        const float extent =
            component(stats.bounds_max, selected_axis) -
            minimum;
        if (extent <= 1e-7f)
        {
            return 0.0f;
        }

        return (
            component(position, selected_axis) -
            minimum
        ) / extent;
    }

    inline bool uv_is_representable(const math::Vector2& uv)
    {
        constexpr float half_max = 65504.0f;
        return
            uv.IsFinite() &&
            std::abs(uv.x) <= half_max &&
            std::abs(uv.y) <= half_max;
    }

    inline operation_result project_uv_planar(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis normal_axis,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        if (!scale.IsFinite() || !offset.IsFinite())
        {
            result.code = status::invalid_argument;
            result.message = "uv scale and offset must be finite";
            return result;
        }

        axis u_axis = axis::x;
        axis v_axis = axis::y;
        projection_axes(normal_axis, u_axis, v_axis);
        std::vector<RHI_Vertex_PosTexNorTan> projected = vertices;
        for (RHI_Vertex_PosTexNorTan& vertex : projected)
        {
            const math::Vector3 position = vertex.get_position();
            const math::Vector2 uv(
                normalized_component(
                    position,
                    u_axis,
                    result.stats
                ) * scale.x + offset.x,
                normalized_component(
                    position,
                    v_axis,
                    result.stats
                ) * scale.y + offset.y
            );
            if (!uv_is_representable(uv))
            {
                result.code = status::invalid_argument;
                result.message = "projected uv exceeds half precision";
                return result;
            }
            vertex.set_uv(uv);
        }

        result = recalculate_normals_tangents(
            projected,
            indices,
            limits
        );
        if (result.succeeded())
        {
            vertices = std::move(projected);
            result.message = "planar uv projection applied";
        }
        return result;
    }

    inline operation_result project_uv_box(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        if (!scale.IsFinite() || !offset.IsFinite())
        {
            result.code = status::invalid_argument;
            result.message = "uv scale and offset must be finite";
            return result;
        }

        std::vector<RHI_Vertex_PosTexNorTan> projected = vertices;
        for (RHI_Vertex_PosTexNorTan& vertex : projected)
        {
            const math::Vector3 position = vertex.get_position();
            const math::Vector3 signed_normal =
                vertex.get_normal();
            const math::Vector3 normal = signed_normal.Abs();
            axis normal_axis = axis::z;

            if (normal.x >= normal.y && normal.x >= normal.z)
            {
                normal_axis = axis::x;
            }
            else if (normal.y >= normal.z)
            {
                normal_axis = axis::y;
            }

            axis u_axis = axis::x;
            axis v_axis = axis::y;
            projection_axes(normal_axis, u_axis, v_axis);
            float u_sign = 1.0f;
            float v_sign = 1.0f;
            projection_signs(
                normal_axis,
                signed_normal,
                u_sign,
                v_sign
            );
            float u = normalized_component(
                position,
                u_axis,
                result.stats
            );
            float v = normalized_component(
                position,
                v_axis,
                result.stats
            );
            if (u_sign < 0.0f)
            {
                u = 1.0f - u;
            }
            if (v_sign < 0.0f)
            {
                v = 1.0f - v;
            }
            const math::Vector2 uv(
                u * scale.x + offset.x,
                v * scale.y + offset.y
            );
            if (!uv_is_representable(uv))
            {
                result.code = status::invalid_argument;
                result.message = "projected uv exceeds half precision";
                return result;
            }
            vertex.set_uv(uv);
        }

        result = recalculate_normals_tangents(
            projected,
            indices,
            limits
        );
        if (result.succeeded())
        {
            vertices = std::move(projected);
            result.message = "box uv projection applied";
            result.warnings.push_back(
                "box projection does not split shared face vertices"
            );
        }
        return result;
    }

    inline operation_result project_uv_box_seamed(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }
        if (!scale.IsFinite() || !offset.IsFinite())
        {
            result.code = status::invalid_argument;
            result.message = "uv scale and offset must be finite";
            return result;
        }
        if (
            indices.size() > limits.max_vertices ||
            indices.size() > limits.max_indices
        )
        {
            result.code = status::budget_exceeded;
            result.message = "seam split exceeds the mesh budget";
            return result;
        }

        std::vector<RHI_Vertex_PosTexNorTan> projected;
        std::vector<uint32_t> projected_indices;
        projected.reserve(indices.size());
        projected_indices.reserve(indices.size());
        for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            const math::Vector3 a =
                vertices[indices[triangle]].get_position();
            const math::Vector3 b =
                vertices[indices[triangle + 1]].get_position();
            const math::Vector3 c =
                vertices[indices[triangle + 2]].get_position();
            const math::Vector3 face_normal = safe_normalized(
                math::Vector3::Cross(b - a, c - a),
                math::Vector3::Up
            );
            const math::Vector3 absolute_normal = face_normal.Abs();
            axis normal_axis = axis::z;
            if (
                absolute_normal.x >= absolute_normal.y &&
                absolute_normal.x >= absolute_normal.z
            )
            {
                normal_axis = axis::x;
            }
            else if (absolute_normal.y >= absolute_normal.z)
            {
                normal_axis = axis::y;
            }
            axis u_axis = axis::x;
            axis v_axis = axis::y;
            projection_axes(normal_axis, u_axis, v_axis);
            float u_sign = 1.0f;
            float v_sign = 1.0f;
            projection_signs(
                normal_axis,
                face_normal,
                u_sign,
                v_sign
            );

            for (size_t corner = 0; corner < 3; corner++)
            {
                RHI_Vertex_PosTexNorTan vertex =
                    vertices[indices[triangle + corner]];
                const math::Vector3 position = vertex.get_position();
                float u = normalized_component(
                    position,
                    u_axis,
                    result.stats
                );
                float v = normalized_component(
                    position,
                    v_axis,
                    result.stats
                );
                if (u_sign < 0.0f)
                {
                    u = 1.0f - u;
                }
                if (v_sign < 0.0f)
                {
                    v = 1.0f - v;
                }
                const math::Vector2 uv(
                    u * scale.x + offset.x,
                    v * scale.y + offset.y
                );
                if (!uv_is_representable(uv))
                {
                    result.code = status::invalid_argument;
                    result.message =
                        "projected uv exceeds half precision";
                    return result;
                }
                vertex.set_uv(uv);
                vertex.set_normal(face_normal);
                projected_indices.push_back(
                    static_cast<uint32_t>(projected.size())
                );
                projected.push_back(vertex);
            }
        }

        result = recalculate_normals_tangents(
            projected,
            projected_indices,
            limits
        );
        if (result.succeeded())
        {
            vertices = std::move(projected);
            indices = std::move(projected_indices);
            result.message =
                "seam aware box uv projection applied";
        }
        return result;
    }

    inline operation_result solidify(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        float thickness,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }
        if (!std::isfinite(thickness) || thickness <= 0.0f)
        {
            result.code = status::invalid_argument;
            result.message = "shell thickness must be positive";
            return result;
        }

        struct position_key
        {
            int64_t x = 0;
            int64_t y = 0;
            int64_t z = 0;

            bool operator==(const position_key& other) const
            {
                return
                    x == other.x &&
                    y == other.y &&
                    z == other.z;
            }
        };
        struct position_key_hash
        {
            size_t operator()(const position_key& key) const
            {
                size_t hash = std::hash<int64_t>{}(key.x);
                hash ^= std::hash<int64_t>{}(key.y) +
                    0x9e3779b9 + (hash << 6) + (hash >> 2);
                hash ^= std::hash<int64_t>{}(key.z) +
                    0x9e3779b9 + (hash << 6) + (hash >> 2);
                return hash;
            }
        };
        const auto make_position_key = [](
            const math::Vector3& position
        )
        {
            constexpr double precision = 100000.0;
            return position_key{
                static_cast<int64_t>(
                    std::llround(position.x * precision)
                ),
                static_cast<int64_t>(
                    std::llround(position.y * precision)
                ),
                static_cast<int64_t>(
                    std::llround(position.z * precision)
                )
            };
        };
        std::unordered_map<
            position_key,
            uint32_t,
            position_key_hash
        > welded_lookup;
        std::vector<uint32_t> welded_ids(vertices.size());
        std::vector<math::Vector3> welded_normals;
        welded_lookup.reserve(vertices.size());
        welded_normals.reserve(vertices.size());
        for (size_t i = 0; i < vertices.size(); i++)
        {
            const position_key key = make_position_key(
                vertices[i].get_position()
            );
            const auto existing = welded_lookup.find(key);
            if (existing == welded_lookup.end())
            {
                const uint32_t id = static_cast<uint32_t>(
                    welded_normals.size()
                );
                welded_lookup.emplace(key, id);
                welded_ids[i] = id;
                welded_normals.push_back(
                    vertices[i].get_normal()
                );
            }
            else
            {
                welded_ids[i] = existing->second;
                welded_normals[existing->second] +=
                    vertices[i].get_normal();
            }
        }
        for (math::Vector3& normal : welded_normals)
        {
            normal = safe_normalized(normal, math::Vector3::Up);
        }

        struct edge_record
        {
            uint32_t a = 0;
            uint32_t b = 0;
            uint32_t count = 0;
        };
        std::unordered_map<uint64_t, edge_record> edges;
        edges.reserve(indices.size());
        const auto add_edge = [&](uint32_t a, uint32_t b)
        {
            const uint32_t welded_a = welded_ids[a];
            const uint32_t welded_b = welded_ids[b];
            const uint32_t minimum =
                std::min(welded_a, welded_b);
            const uint32_t maximum =
                std::max(welded_a, welded_b);
            const uint64_t key =
                (static_cast<uint64_t>(minimum) << 32) |
                static_cast<uint64_t>(maximum);
            edge_record& edge = edges[key];
            if (edge.count == 0)
            {
                edge.a = a;
                edge.b = b;
            }
            edge.count++;
        };
        for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            add_edge(indices[triangle], indices[triangle + 1]);
            add_edge(indices[triangle + 1], indices[triangle + 2]);
            add_edge(indices[triangle + 2], indices[triangle]);
        }

        size_t boundary_count = 0;
        for (const auto& [key, edge] : edges)
        {
            static_cast<void>(key);
            if (edge.count > 2)
            {
                result.code = status::invalid_topology;
                result.message =
                    "shell input contains a non manifold edge";
                return result;
            }
            if (edge.count == 1)
            {
                boundary_count++;
            }
        }
        const size_t required_vertices =
            vertices.size() * 2 + boundary_count * 4;
        const size_t required_indices =
            indices.size() * 2 + boundary_count * 6;
        if (
            required_vertices > limits.max_vertices ||
            required_indices > limits.max_indices ||
            required_vertices >
                std::numeric_limits<uint32_t>::max()
        )
        {
            result.code = status::budget_exceeded;
            result.message = "solidified mesh exceeds the mesh budget";
            return result;
        }

        const float half_thickness = thickness * 0.5f;
        std::vector<RHI_Vertex_PosTexNorTan> shell;
        std::vector<uint32_t> shell_indices;
        shell.reserve(required_vertices);
        shell_indices.reserve(required_indices);
        for (size_t i = 0; i < vertices.size(); i++)
        {
            const RHI_Vertex_PosTexNorTan& source = vertices[i];
            RHI_Vertex_PosTexNorTan outer = source;
            const math::Vector3 normal =
                welded_normals[welded_ids[i]];
            outer.set_position(
                source.get_position() + normal * half_thickness
            );
            shell.push_back(outer);
        }
        const uint32_t inner_offset =
            static_cast<uint32_t>(shell.size());
        for (size_t i = 0; i < vertices.size(); i++)
        {
            const RHI_Vertex_PosTexNorTan& source = vertices[i];
            RHI_Vertex_PosTexNorTan inner = source;
            const math::Vector3 normal =
                welded_normals[welded_ids[i]];
            inner.set_position(
                source.get_position() - normal * half_thickness
            );
            inner.set_normal(
                source.get_normal() * -1.0f
            );
            inner.set_tangent(
                source.get_tangent() * -1.0f
            );
            shell.push_back(inner);
        }
        for (size_t triangle = 0; triangle < indices.size(); triangle += 3)
        {
            shell_indices.push_back(indices[triangle]);
            shell_indices.push_back(indices[triangle + 1]);
            shell_indices.push_back(indices[triangle + 2]);
            shell_indices.push_back(
                inner_offset + indices[triangle]
            );
            shell_indices.push_back(
                inner_offset + indices[triangle + 2]
            );
            shell_indices.push_back(
                inner_offset + indices[triangle + 1]
            );
        }

        for (const auto& [key, edge] : edges)
        {
            static_cast<void>(key);
            if (edge.count != 1)
            {
                continue;
            }
            const math::Vector3 outer_a =
                shell[edge.a].get_position();
            const math::Vector3 outer_b =
                shell[edge.b].get_position();
            const math::Vector3 inner_a =
                shell[inner_offset + edge.a].get_position();
            const math::Vector3 inner_b =
                shell[inner_offset + edge.b].get_position();
            const math::Vector3 side_normal = safe_normalized(
                math::Vector3::Cross(
                    outer_b - outer_a,
                    inner_a - outer_a
                ),
                math::Vector3::Up
            );
            const math::Vector3 tangent = safe_normalized(
                outer_b - outer_a,
                fallback_tangent(side_normal)
            );
            const uint32_t side_offset =
                static_cast<uint32_t>(shell.size());
            shell.emplace_back(
                outer_a,
                math::Vector2(0, 0),
                side_normal,
                tangent
            );
            shell.emplace_back(
                outer_b,
                math::Vector2(1, 0),
                side_normal,
                tangent
            );
            shell.emplace_back(
                inner_a,
                math::Vector2(0, 1),
                side_normal,
                tangent
            );
            shell.emplace_back(
                inner_b,
                math::Vector2(1, 1),
                side_normal,
                tangent
            );
            shell_indices.push_back(side_offset);
            shell_indices.push_back(side_offset + 1);
            shell_indices.push_back(side_offset + 2);
            shell_indices.push_back(side_offset + 1);
            shell_indices.push_back(side_offset + 3);
            shell_indices.push_back(side_offset + 2);
        }

        result = validate(shell, shell_indices, limits);
        if (result.succeeded())
        {
            vertices = std::move(shell);
            indices = std::move(shell_indices);
            result.message = "mesh solidified";
            if (boundary_count == 0)
            {
                result.warnings.push_back(
                    "closed input produced nested shell surfaces"
                );
            }
        }
        return result;
    }

    inline operation_result project_uv_cylindrical(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis cylinder_axis,
        const math::Vector3& center,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    )
    {
        operation_result result = validate(vertices, indices, limits);
        if (!result.succeeded())
        {
            return result;
        }

        if (
            !center.IsFinite() ||
            !scale.IsFinite() ||
            !offset.IsFinite()
        )
        {
            result.code = status::invalid_argument;
            result.message = "cylindrical uv parameters must be finite";
            return result;
        }

        std::vector<RHI_Vertex_PosTexNorTan> projected = vertices;
        for (RHI_Vertex_PosTexNorTan& vertex : projected)
        {
            const math::Vector3 position = vertex.get_position();
            const math::Vector3 relative = position - center;
            float first = relative.x;
            float second = relative.z;

            if (cylinder_axis == axis::x)
            {
                first  = relative.y;
                second = relative.z;
            }
            else if (cylinder_axis == axis::z)
            {
                first  = relative.x;
                second = relative.y;
            }

            const float angle = std::atan2(second, first);
            const float u =
                angle / (2.0f * math::pi) + 0.5f;
            const float v = normalized_component(
                position,
                cylinder_axis,
                result.stats
            );
            const math::Vector2 uv(
                u * scale.x + offset.x,
                v * scale.y + offset.y
            );
            if (!uv_is_representable(uv))
            {
                result.code = status::invalid_argument;
                result.message = "projected uv exceeds half precision";
                return result;
            }
            vertex.set_uv(uv);
        }

        result = recalculate_normals_tangents(
            projected,
            indices,
            limits
        );
        if (result.succeeded())
        {
            vertices = std::move(projected);
            result.message = "cylindrical uv projection applied";
            result.warnings.push_back(
                "cylindrical projection does not split seam vertices"
            );
        }
        return result;
    }

    inline capability boolean_capability(
        boolean_operation operation
    )
    {
        capability result;
        result.available = false;
        result.code = status::unsupported;

        if (operation == boolean_operation::union_mesh)
        {
            result.message =
                "robust boolean union is unavailable, use append_mesh for a non welded merge";
        }
        else if (operation == boolean_operation::intersection)
        {
            result.message =
                "robust boolean intersection is unavailable";
        }
        else
        {
            result.message =
                "robust boolean difference is unavailable";
        }

        return result;
    }
}
