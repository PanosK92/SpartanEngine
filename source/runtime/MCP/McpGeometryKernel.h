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

// builds meshes on the cpu from parametric descriptions, the agent asks for shapes and this returns vertices and indices

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

    float component(const math::Vector3& value, axis selected_axis);

    void set_component(math::Vector3& value, axis selected_axis, float component_value);

    bool add_would_exceed(size_t current, size_t added, size_t maximum);

    math::Vector3 safe_normalized(const math::Vector3& value, const math::Vector3& fallback);

    math::Vector3 fallback_tangent(const math::Vector3& normal);

    statistics calculate_statistics(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices
    );

    operation_result validate(
        const std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        const budgets& limits = budgets()
    );

    operation_result recalculate_normals_tangents(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        const budgets& limits = budgets()
    );

    math::Vector3 transform_direction(const math::Vector3& value, const math::Matrix& matrix);

    operation_result transform(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        const math::Matrix& matrix,
        const budgets& limits = budgets()
    );

    operation_result append_mesh(
        const std::vector<RHI_Vertex_PosTexNorTan>& source_vertices,
        const std::vector<uint32_t>& source_indices,
        std::vector<RHI_Vertex_PosTexNorTan>& target_vertices,
        std::vector<uint32_t>& target_indices,
        const budgets& limits = budgets()
    );

    operation_result linear_array(
        const std::vector<RHI_Vertex_PosTexNorTan>& source_vertices,
        const std::vector<uint32_t>& source_indices,
        uint32_t count,
        const math::Vector3& step,
        std::vector<RHI_Vertex_PosTexNorTan>& output_vertices,
        std::vector<uint32_t>& output_indices,
        const budgets& limits = budgets()
    );

    operation_result radial_array(
        const std::vector<RHI_Vertex_PosTexNorTan>& source_vertices,
        const std::vector<uint32_t>& source_indices,
        uint32_t count,
        axis rotation_axis,
        float angle_radians,
        const math::Vector3& center,
        std::vector<RHI_Vertex_PosTexNorTan>& output_vertices,
        std::vector<uint32_t>& output_indices,
        const budgets& limits = budgets()
    );

    operation_result mirror(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        axis mirror_axis,
        float plane,
        const budgets& limits = budgets()
    );

    operation_result taper(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis taper_axis,
        float start_scale,
        float end_scale,
        const math::Vector3& pivot,
        const budgets& limits = budgets()
    );

    operation_result bend(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis length_axis,
        axis radial_axis,
        float angle_radians,
        const math::Vector3& pivot,
        const budgets& limits = budgets()
    );

    void projection_axes(axis normal_axis, axis& u_axis, axis& v_axis);

    void projection_signs(axis normal_axis, const math::Vector3& normal, float& u_sign, float& v_sign);

    float normalized_component(const math::Vector3& position, axis selected_axis, const statistics& stats);

    bool uv_is_representable(const math::Vector2& uv);

    operation_result project_uv_planar(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis normal_axis,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    );

    operation_result project_uv_box(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    );

    operation_result project_uv_box_seamed(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    );

    operation_result solidify(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        std::vector<uint32_t>& indices,
        float thickness,
        const budgets& limits = budgets()
    );

    operation_result project_uv_cylindrical(
        std::vector<RHI_Vertex_PosTexNorTan>& vertices,
        const std::vector<uint32_t>& indices,
        axis cylinder_axis,
        const math::Vector3& center,
        const math::Vector2& scale = math::Vector2::One,
        const math::Vector2& offset = math::Vector2::Zero,
        const budgets& limits = budgets()
    );

    capability boolean_capability(boolean_operation operation);
}
