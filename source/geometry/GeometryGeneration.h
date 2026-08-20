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

//= INCLUDES ==================
#include <vector>
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../rhi/RHI_Vertex.h"
//=============================

namespace spartan::geometry_generation
{
    void generate_cube(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices);
    void generate_quad(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices);
    void generate_grid(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, uint32_t grid_points_per_dimension, float extent);

    // camera centered ocean clipmap, each outer level doubles the cell size and skips the inner block so the rings tile
    void generate_ocean_clipmap(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, uint32_t resolution, uint32_t levels, float base_cell_size);
    // flat square ring past the clipmap so the ocean never shows a hard rim from altitude
    void generate_ocean_horizon_skirt(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float inner_half_extent, float outer_half_extent);
    void generate_sphere(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float radius = 1.0f, int slices = 20, int stacks = 20);
    void generate_cylinder(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float radius_top = 1.0f, float radius_bottom = 1.0f, float height = 1.0f, int slices = 64, int stacks = 1);
    void generate_cone(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float radius = 1.0f, float height = 2.0f);
    void generate_rounded_box(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size,
        float radius,
        uint32_t segments
    );
    void generate_beveled_box(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size,
        float bevel
    );
    void generate_wedge(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size
    );
    void generate_extruded_profile(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector2>& profile,
        float depth
    );
    void generate_revolved_profile(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector2>& profile,
        uint32_t segments
    );
    void generate_torus(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float major_radius,
        float minor_radius,
        uint32_t major_segments,
        uint32_t minor_segments
    );
    void generate_capsule(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float radius,
        float height,
        uint32_t segments
    );
    void generate_rounded_cylinder(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float radius,
        float height,
        float bevel,
        uint32_t radial_segments,
        uint32_t bevel_segments
    );
    void generate_loft(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector3>& path,
        const std::vector<std::vector<math::Vector2>>& profiles,
        const std::vector<float>& twists = {}
    );
    void generate_swept_profile(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector3>& path,
        const std::vector<math::Vector2>& profile,
        const std::vector<float>& scales = {},
        const std::vector<float>& twists = {}
    );
    void generate_pipe(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector3>& path,
        float radius,
        uint32_t sides,
        const std::vector<float>& scales = {},
        const std::vector<float>& twists = {}
    );
    void generate_arch(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float width,
        float height,
        float depth,
        float thickness,
        uint32_t segments
    );
    void generate_inset_panel(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size,
        float border,
        float inset,
        float bevel
    );
    void generate_tapered_extrusion(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector2>& profile,
        float depth,
        float scale_start,
        float scale_end
    );
    // a stone chip for micro detail scatter, flat shaded so it reads as rock at a few centimetres across
    void generate_pebble(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, const uint32_t subdivisions, const uint32_t seed);
    void generate_foliage_grass_blade(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, const uint32_t segment_count);
    void generate_foliage_flower(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, const uint32_t stem_segment_count, const uint32_t petal_count, const uint32_t petal_segment_count);
}
