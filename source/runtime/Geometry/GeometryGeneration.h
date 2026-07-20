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

//= INCLUDES =================
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>
#include "../RHI/RHI_Vertex.h"
//============================

namespace spartan::geometry_generation
{
    static void generate_cube(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices)
    {
        using namespace math;

        // front
        vertices->emplace_back(Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(0, 0, -1), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(0, 0, -1), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(0, 0, -1), Vector3(0, 1, 0));

        // bottom
        vertices->emplace_back(Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, -1, 0), Vector3(1, 0, 0));
        vertices->emplace_back(Vector3(-0.5f, -0.5f, -0.5f), Vector2(0, 0), Vector3(0, -1, 0), Vector3(1, 0, 0));
        vertices->emplace_back(Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, -1, 0), Vector3(1, 0, 0));
        vertices->emplace_back(Vector3(0.5f, -0.5f, -0.5f), Vector2(1, 0), Vector3(0, -1, 0), Vector3(1, 0, 0));

        // back
        vertices->emplace_back(Vector3(-0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(0, 0, 1), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(-0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 0, 1), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(0, 0, 1), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 0, 1), Vector3(0, 1, 0));

        // top
        vertices->emplace_back(Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0));
        vertices->emplace_back(Vector3(-0.5f, 0.5f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0));
        vertices->emplace_back(Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0));
        vertices->emplace_back(Vector3(0.5f, 0.5f, -0.5f), Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0));

        // left
        vertices->emplace_back(Vector3(-0.5f, -0.5f, 0.5f), Vector2(0, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(-0.5f, 0.5f, 0.5f), Vector2(0, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(-0.5f, -0.5f, -0.5f), Vector2(1, 1), Vector3(-1, 0, 0), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(-0.5f, 0.5f, -0.5f), Vector2(1, 0), Vector3(-1, 0, 0), Vector3(0, 1, 0));

        // right
        vertices->emplace_back(Vector3(0.5f, -0.5f, 0.5f), Vector2(1, 1), Vector3(1, 0, 0), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(0.5f, 0.5f, 0.5f), Vector2(1, 0), Vector3(1, 0, 0), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(0.5f, -0.5f, -0.5f), Vector2(0, 1), Vector3(1, 0, 0), Vector3(0, 1, 0));
        vertices->emplace_back(Vector3(0.5f, 0.5f, -0.5f), Vector2(0, 0), Vector3(1, 0, 0), Vector3(0, 1, 0));

        // front
        indices->emplace_back(0); indices->emplace_back(1); indices->emplace_back(2);
        indices->emplace_back(2); indices->emplace_back(1); indices->emplace_back(3);

        // bottom
        indices->emplace_back(4); indices->emplace_back(5); indices->emplace_back(6);
        indices->emplace_back(6); indices->emplace_back(5); indices->emplace_back(7);

        // back
        indices->emplace_back(10); indices->emplace_back(9); indices->emplace_back(8);
        indices->emplace_back(11); indices->emplace_back(9); indices->emplace_back(10);

        // top
        indices->emplace_back(14); indices->emplace_back(13); indices->emplace_back(12);
        indices->emplace_back(15); indices->emplace_back(13); indices->emplace_back(14);

        // left
        indices->emplace_back(16); indices->emplace_back(17); indices->emplace_back(18);
        indices->emplace_back(18); indices->emplace_back(17); indices->emplace_back(19);

        // right
        indices->emplace_back(22); indices->emplace_back(21); indices->emplace_back(20);
        indices->emplace_back(23); indices->emplace_back(21); indices->emplace_back(22);
    }

    static void generate_quad(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices)
    {
        using namespace math;

        vertices->emplace_back(Vector3(-0.5f, 0.0f, 0.5f),  Vector2(0, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)); // 0 top-left
        vertices->emplace_back(Vector3(0.5f,  0.0f, 0.5f),  Vector2(1, 0), Vector3(0, 1, 0), Vector3(1, 0, 0)); // 1 top-right
        vertices->emplace_back(Vector3(-0.5f, 0.0f, -0.5f), Vector2(0, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)); // 2 bottom-left
        vertices->emplace_back(Vector3(0.5f,  0.0f, -0.5f), Vector2(1, 1), Vector3(0, 1, 0), Vector3(1, 0, 0)); // 3 bottom-right

        indices->emplace_back(3);
        indices->emplace_back(2);
        indices->emplace_back(0);
        indices->emplace_back(3);
        indices->emplace_back(0);
        indices->emplace_back(1);
    }

    static void generate_grid(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, uint32_t grid_points_per_dimension, float extent)
    {
        using namespace math;
    
        const float spacing = extent / static_cast<float>(grid_points_per_dimension - 1); // scale spacing based on extent
        const Vector3 normal(0, 1, 0);  // upward-facing normal (Y-axis)
        const Vector3 tangent(1, 0, 0); // tangent along X-axis
    
        // generate vertices
        for (uint32_t i = 0; i < grid_points_per_dimension; ++i)
        {
            for (uint32_t j = 0; j < grid_points_per_dimension; ++j)
            {
                const float x = static_cast<float>(i) * spacing - (extent / 2.0f); // center the grid around origin
                const float z = static_cast<float>(j) * spacing - (extent / 2.0f); // center the grid around origin
                const Vector2 texCoord(static_cast<float>(i) / (grid_points_per_dimension - 1), static_cast<float>(j) / (grid_points_per_dimension - 1)); // normalized UVs [0,1]
                vertices->emplace_back(Vector3(x, 0.0f, z), texCoord, normal, tangent);
            }
        }
    
        // generate indices (clockwise winding order for DirectX with back-face culling)
        for (uint32_t i = 0; i < grid_points_per_dimension - 1; ++i)
        {
            for (uint32_t j = 0; j < grid_points_per_dimension - 1; ++j)
            {
                int topLeft     = i * grid_points_per_dimension + j;
                int topRight    = i * grid_points_per_dimension + j + 1;
                int bottomLeft  = (i + 1) * grid_points_per_dimension + j;
                int bottomRight = (i + 1) * grid_points_per_dimension + j + 1;
    
                // triangle 1 (top-left, bottom-right, bottom-left) - clockwise when viewed from above
                indices->emplace_back(topLeft);
                indices->emplace_back(bottomRight);
                indices->emplace_back(bottomLeft);
    
                // triangle 2 (top-left, top-right, bottom-right) - clockwise when viewed from above
                indices->emplace_back(topLeft);
                indices->emplace_back(topRight);
                indices->emplace_back(bottomRight);
            }
        }
    }

    // camera-centered ocean clipmap, a single mesh made of concentric square levels
    // level 0 is a dense grid, each outer level doubles the cell size and skips the inner block
    // so the rings tile seamlessly, the vertex shader recenters the whole mesh on the camera
    static void generate_ocean_clipmap(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, uint32_t resolution, uint32_t levels, float base_cell_size)
    {
        using namespace math;

        const Vector3 normal(0, 1, 0);
        const Vector3 tangent(1, 0, 0);
        const uint32_t verts_per_side = resolution + 1;
        const uint32_t quarter        = resolution / 4;

        for (uint32_t level = 0; level < levels; ++level)
        {
            const float cell      = base_cell_size * static_cast<float>(1u << level);
            const float half      = resolution * cell * 0.5f;
            const uint32_t v_base = static_cast<uint32_t>(vertices->size());

            for (uint32_t i = 0; i < verts_per_side; ++i)
            {
                for (uint32_t j = 0; j < verts_per_side; ++j)
                {
                    const float x = static_cast<float>(i) * cell - half;
                    const float z = static_cast<float>(j) * cell - half;
                    const Vector2 uv(static_cast<float>(i) / resolution, static_cast<float>(j) / resolution);
                    vertices->emplace_back(Vector3(x, 0.0f, z), uv, normal, tangent);
                }
            }

            for (uint32_t i = 0; i < resolution; ++i)
            {
                for (uint32_t j = 0; j < resolution; ++j)
                {
                    // outer levels leave a hole for the finer level nested inside them
                    const bool inner_hole = level > 0 && i >= quarter && i < quarter * 3 && j >= quarter && j < quarter * 3;
                    if (inner_hole)
                    {
                        continue;
                    }

                    const uint32_t top_left     = v_base + i * verts_per_side + j;
                    const uint32_t top_right    = v_base + i * verts_per_side + j + 1;
                    const uint32_t bottom_left  = v_base + (i + 1) * verts_per_side + j;
                    const uint32_t bottom_right = v_base + (i + 1) * verts_per_side + j + 1;

                    indices->emplace_back(top_left);
                    indices->emplace_back(bottom_right);
                    indices->emplace_back(bottom_left);
                    indices->emplace_back(top_left);
                    indices->emplace_back(top_right);
                    indices->emplace_back(bottom_right);
                }
            }
        }
    }

    static void generate_sphere(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float radius = 1.0f, int slices = 20, int stacks = 20)
    {
        using namespace math;

        Vector3 normal = Vector3(0, 1, 0);
        Vector3 tangent = Vector3(1, 0, 0);
        vertices->emplace_back(Vector3(0, radius, 0), Vector2::Zero, normal, tangent);

        const float phiStep   = pi / stacks;
        const float thetaStep = 2.0f * pi / slices;

        for (int i = 1; i <= stacks - 1; i++)
        {
            const float phi = i * phiStep;
            for (int j = 0; j <= slices; j++)
            {
                const float theta = j * thetaStep;
                Vector3 p = Vector3(
                    (radius * sin(phi) * cos(theta)),
                    (radius * cos(phi)),
                    (radius * sin(phi) * sin(theta))
                );

                Vector3 t = Vector3(-radius * sin(phi) * sin(theta), 0, radius * sin(phi) * cos(theta)).Normalized();
                Vector3 n = p.Normalized();
                Vector2 uv = Vector2(theta / (pi * 2), phi / pi);
                vertices->emplace_back(p, uv, n, t);
            }
        }

        normal = Vector3(0, -1, 0);
        tangent = Vector3(1, 0, 0);
        vertices->emplace_back(Vector3(0, -radius, 0), Vector2(0, 1), normal, tangent);

        for (int i = 1; i <= slices; i++)
        {
            indices->emplace_back(0);
            indices->emplace_back(i + 1);
            indices->emplace_back(i);
        }
        int baseIndex = 1;
        const int ringVertexCount = slices + 1;
        for (int i = 0; i < stacks - 2; i++)
        {
            for (int j = 0; j < slices; j++)
            {
                indices->emplace_back(baseIndex + i * ringVertexCount + j);
                indices->emplace_back(baseIndex + i * ringVertexCount + j + 1);
                indices->emplace_back(baseIndex + (i + 1) * ringVertexCount + j);

                indices->emplace_back(baseIndex + (i + 1) * ringVertexCount + j);
                indices->emplace_back(baseIndex + i * ringVertexCount + j + 1);
                indices->emplace_back(baseIndex + (i + 1) * ringVertexCount + j + 1);
            }
        }
        int southPoleIndex = (int)vertices->size() - 1;
        baseIndex = southPoleIndex - ringVertexCount;
        for (int i = 0; i < slices; i++)
        {
            indices->emplace_back(southPoleIndex);
            indices->emplace_back(baseIndex + i);
            indices->emplace_back(baseIndex + i + 1);
        }
    }

    static void generate_cylinder(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float radiusTop = 1.0f, float radiusBottom = 1.0f, float height = 1.0f, int slices = 64, int stacks = 1)
    {
        using namespace math;

        const float stackHeight = height / stacks;
        const float radiusStep = (radiusTop - radiusBottom) / stacks;
        const float ringCount = (float)(stacks + 1);

        for (int i = 0; i < ringCount; i++)
        {
            const float y = -0.5f * height + i * stackHeight;
            const float r = radiusBottom + i * radiusStep;
            const float dTheta = 2.0f * pi / slices;
            for (int j = 0; j <= slices; j++)
            {
                const float c = cos(j * dTheta);
                const float s = sin(j * dTheta);

                Vector3 v = Vector3(r*c, y, r*s);
                Vector2 uv = Vector2((float)j / slices, 1.0f - (float)i / stacks);
                Vector3 t = Vector3(-s, 0.0f, c);

                const float dr = radiusBottom - radiusTop;
                Vector3 bitangent = Vector3(dr*c, -height, dr*s);

                Vector3 n = Vector3::Cross(t, bitangent).Normalized();
                vertices->emplace_back(v, uv, n, t);

            }
        }

        const int ringVertexCount = slices + 1;
        for (int i = 0; i < stacks; i++)
        {
            for (int j = 0; j < slices; j++)
            {
                indices->push_back(i * ringVertexCount + j);
                indices->push_back((i + 1) * ringVertexCount + j);
                indices->push_back((i + 1) * ringVertexCount + j + 1);

                indices->push_back(i * ringVertexCount + j);
                indices->push_back((i + 1) * ringVertexCount + j + 1);
                indices->push_back(i * ringVertexCount + j + 1);
            }
        }

        // build top cap
        int baseIndex = (int)vertices->size();
        float y = 0.5f * height;
        const float dTheta = 2.0f * pi / slices;

        Vector3 normal;
        Vector3 tangent;

        for (int i = 0; i <= slices; i++)
        {
            const float x = radiusTop * cos(i*dTheta);
            const float z = radiusTop * sin(i*dTheta);
            const float u = x / height + 0.5f;
            const float v = z / height + 0.5f;

            normal = Vector3(0, 1, 0);
            tangent = Vector3(1, 0, 0);
            vertices->emplace_back(Vector3(x, y, z), Vector2(u, v), normal, tangent);
        }

        normal = Vector3(0, 1, 0);
        tangent = Vector3(1, 0, 0);
        vertices->emplace_back(Vector3(0, y, 0), Vector2(0.5f, 0.5f), normal, tangent);

        int centerIndex = (int)vertices->size() - 1;
        for (int i = 0; i < slices; i++)
        {
            indices->push_back(centerIndex);
            indices->push_back(baseIndex + i + 1);
            indices->push_back(baseIndex + i);
        }

        // build bottom cap
        baseIndex = (int)vertices->size();
        y = -0.5f * height;

        for (int i = 0; i <= slices; i++)
        {
            const float x = radiusBottom * cos(i * dTheta);
            const float z = radiusBottom * sin(i * dTheta);
            const float u = x / height + 0.5f;
            const float v = z / height + 0.5f;

            normal  = Vector3(0, -1, 0);
            tangent = Vector3(1, 0, 0);
            vertices->emplace_back(Vector3(x, y, z), Vector2(u, v), normal, tangent);
        }

        normal  = Vector3(0, -1, 0);
        tangent = Vector3(1, 0, 0);
        vertices->emplace_back(Vector3(0, y, 0), Vector2(0.5f, 0.5f), normal, tangent);

        centerIndex = (int)vertices->size() - 1;
        for (int i = 0; i < slices; i++)
        {
            indices->push_back(centerIndex);
            indices->push_back(baseIndex + i);
            indices->push_back(baseIndex + i + 1);
        }
    }

    static void generate_cone(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float radius = 1.0f, float height = 2.0f)
    {
        generate_cylinder(vertices, indices, 0.0f, radius, height, 32, 8);
    }

    static void generate_rounded_box(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size,
        float radius,
        uint32_t segments
    )
    {
        using namespace math;

        const Vector3 half_size = size * 0.5f;
        radius = std::clamp(
            radius,
            0.0001f,
            std::min({ half_size.x, half_size.y, half_size.z })
        );
        segments = std::max(segments, 1u);

        const Vector3 inner(
            half_size.x - radius,
            half_size.y - radius,
            half_size.z - radius
        );

        auto axis_coordinates = [segments](
            float half_extent,
            float inner_extent
        )
        {
            std::vector<float> coordinates;
            coordinates.reserve(segments * 2 + 2);

            for (uint32_t i = 0; i <= segments; i++)
            {
                const float t = static_cast<float>(i) /
                    static_cast<float>(segments);
                coordinates.push_back(
                    -half_extent + t * (half_extent - inner_extent)
                );
            }

            for (uint32_t i = 0; i <= segments; i++)
            {
                const float t = static_cast<float>(i) /
                    static_cast<float>(segments);
                coordinates.push_back(
                    inner_extent + t * (half_extent - inner_extent)
                );
            }

            return coordinates;
        };

        auto append_face = [&](
            const Vector3& face_normal,
            const Vector3& axis_u,
            const Vector3& axis_v,
            float half_u,
            float inner_u,
            float half_v,
            float inner_v
        )
        {
            const std::vector<float> coordinates_u =
                axis_coordinates(half_u, inner_u);
            const std::vector<float> coordinates_v =
                axis_coordinates(half_v, inner_v);
            const uint32_t count_u =
                static_cast<uint32_t>(coordinates_u.size());
            const uint32_t count_v =
                static_cast<uint32_t>(coordinates_v.size());
            const uint32_t vertex_offset =
                static_cast<uint32_t>(vertices->size());

            const Vector3 face_point(
                face_normal.x * half_size.x,
                face_normal.y * half_size.y,
                face_normal.z * half_size.z
            );

            for (uint32_t u = 0; u < count_u; u++)
            {
                for (uint32_t v = 0; v < count_v; v++)
                {
                    const Vector3 point =
                        face_point +
                        axis_u * coordinates_u[u] +
                        axis_v * coordinates_v[v];
                    const Vector3 closest(
                        std::clamp(point.x, -inner.x, inner.x),
                        std::clamp(point.y, -inner.y, inner.y),
                        std::clamp(point.z, -inner.z, inner.z)
                    );
                    const Vector3 normal = (point - closest).Normalized();
                    const Vector3 position = closest + normal * radius;
                    const Vector3 tangent = (
                        axis_u -
                        normal * Vector3::Dot(axis_u, normal)
                    ).Normalized();
                    const Vector2 uv(
                        static_cast<float>(u) /
                            static_cast<float>(count_u - 1),
                        1.0f - static_cast<float>(v) /
                            static_cast<float>(count_v - 1)
                    );

                    vertices->emplace_back(
                        position,
                        uv,
                        normal,
                        tangent
                    );
                }
            }

            for (uint32_t u = 0; u < count_u - 1; u++)
            {
                for (uint32_t v = 0; v < count_v - 1; v++)
                {
                    const uint32_t a =
                        vertex_offset + u * count_v + v;
                    const uint32_t b = a + 1;
                    const uint32_t c = a + count_v;
                    const uint32_t d = c + 1;

                    indices->push_back(a);
                    indices->push_back(b);
                    indices->push_back(c);
                    indices->push_back(c);
                    indices->push_back(b);
                    indices->push_back(d);
                }
            }
        };

        append_face(
            Vector3(1, 0, 0),
            Vector3(0, 0, 1),
            Vector3(0, 1, 0),
            half_size.z,
            inner.z,
            half_size.y,
            inner.y
        );
        append_face(
            Vector3(-1, 0, 0),
            Vector3(0, 0, -1),
            Vector3(0, 1, 0),
            half_size.z,
            inner.z,
            half_size.y,
            inner.y
        );
        append_face(
            Vector3(0, 1, 0),
            Vector3(1, 0, 0),
            Vector3(0, 0, 1),
            half_size.x,
            inner.x,
            half_size.z,
            inner.z
        );
        append_face(
            Vector3(0, -1, 0),
            Vector3(1, 0, 0),
            Vector3(0, 0, -1),
            half_size.x,
            inner.x,
            half_size.z,
            inner.z
        );
        append_face(
            Vector3(0, 0, 1),
            Vector3(-1, 0, 0),
            Vector3(0, 1, 0),
            half_size.x,
            inner.x,
            half_size.y,
            inner.y
        );
        append_face(
            Vector3(0, 0, -1),
            Vector3(1, 0, 0),
            Vector3(0, 1, 0),
            half_size.x,
            inner.x,
            half_size.y,
            inner.y
        );
    }

    static void generate_beveled_box(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size,
        float bevel
    )
    {
        generate_rounded_box(
            vertices,
            indices,
            size,
            bevel,
            1
        );
    }

    static void generate_wedge(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size
    )
    {
        using namespace math;

        const Vector3 half_size = size * 0.5f;
        const Vector3 points[] =
        {
            Vector3(-half_size.x, -half_size.y, -half_size.z),
            Vector3(half_size.x, -half_size.y, -half_size.z),
            Vector3(-half_size.x, -half_size.y, half_size.z),
            Vector3(half_size.x, -half_size.y, half_size.z),
            Vector3(-half_size.x, half_size.y, half_size.z),
            Vector3(half_size.x, half_size.y, half_size.z)
        };

        auto append_face = [&](
            std::initializer_list<uint32_t> face
        )
        {
            const uint32_t offset =
                static_cast<uint32_t>(vertices->size());
            const std::vector<uint32_t> face_indices(face);
            const Vector3 edge_a =
                points[face_indices[1]] - points[face_indices[0]];
            const Vector3 edge_b =
                points[face_indices[2]] - points[face_indices[0]];
            const Vector3 normal =
                Vector3::Cross(edge_a, edge_b).Normalized();
            const Vector3 tangent = edge_a.Normalized();

            for (uint32_t i = 0; i < face_indices.size(); i++)
            {
                const Vector2 uv(
                    i == 1 || i == 2 ? 1.0f : 0.0f,
                    i >= 2 ? 0.0f : 1.0f
                );
                vertices->emplace_back(
                    points[face_indices[i]],
                    uv,
                    normal,
                    tangent
                );
            }

            for (uint32_t i = 1; i + 1 < face_indices.size(); i++)
            {
                indices->push_back(offset);
                indices->push_back(offset + i);
                indices->push_back(offset + i + 1);
            }
        };

        append_face({ 0, 1, 3, 2 });
        append_face({ 2, 3, 5, 4 });
        append_face({ 0, 4, 5, 1 });
        append_face({ 0, 2, 4 });
        append_face({ 1, 5, 3 });
    }

    static void generate_extruded_profile(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector2>& profile,
        float depth
    )
    {
        using namespace math;

        const uint32_t point_count =
            static_cast<uint32_t>(profile.size());
        const float half_depth = depth * 0.5f;

        for (uint32_t side = 0; side < 2; side++)
        {
            const float z = side == 0 ? -half_depth : half_depth;
            const Vector3 normal =
                side == 0 ? Vector3(0, 0, -1) : Vector3(0, 0, 1);
            const uint32_t offset =
                static_cast<uint32_t>(vertices->size());

            for (const Vector2& point : profile)
            {
                vertices->emplace_back(
                    Vector3(point.x, point.y, z),
                    point,
                    normal,
                    Vector3(1, 0, 0)
                );
            }

            for (uint32_t i = 1; i + 1 < point_count; i++)
            {
                if (side == 0)
                {
                    indices->push_back(offset);
                    indices->push_back(offset + i + 1);
                    indices->push_back(offset + i);
                }
                else
                {
                    indices->push_back(offset);
                    indices->push_back(offset + i);
                    indices->push_back(offset + i + 1);
                }
            }
        }

        for (uint32_t i = 0; i < point_count; i++)
        {
            const uint32_t next = (i + 1) % point_count;
            const Vector2 edge = profile[next] - profile[i];
            const Vector3 normal =
                Vector3(edge.y, -edge.x, 0).Normalized();
            const Vector3 tangent =
                Vector3(edge.x, edge.y, 0).Normalized();
            const uint32_t offset =
                static_cast<uint32_t>(vertices->size());

            vertices->emplace_back(
                Vector3(profile[i].x, profile[i].y, -half_depth),
                Vector2(0, 1),
                normal,
                tangent
            );
            vertices->emplace_back(
                Vector3(profile[i].x, profile[i].y, half_depth),
                Vector2(1, 1),
                normal,
                tangent
            );
            vertices->emplace_back(
                Vector3(profile[next].x, profile[next].y, -half_depth),
                Vector2(0, 0),
                normal,
                tangent
            );
            vertices->emplace_back(
                Vector3(profile[next].x, profile[next].y, half_depth),
                Vector2(1, 0),
                normal,
                tangent
            );

            indices->push_back(offset);
            indices->push_back(offset + 2);
            indices->push_back(offset + 1);
            indices->push_back(offset + 2);
            indices->push_back(offset + 3);
            indices->push_back(offset + 1);
        }
    }

    static void generate_revolved_profile(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector2>& profile,
        uint32_t segments
    )
    {
        using namespace math;

        const uint32_t point_count =
            static_cast<uint32_t>(profile.size());
        const uint32_t ring_count = segments + 1;

        for (uint32_t segment = 0; segment < ring_count; segment++)
        {
            const float u = static_cast<float>(segment) /
                static_cast<float>(segments);
            const float angle = u * pi_2;
            const float cosine = std::cos(angle);
            const float sine = std::sin(angle);
            const Vector3 tangent(-sine, 0, cosine);

            for (uint32_t i = 0; i < point_count; i++)
            {
                const Vector2 previous =
                    profile[i == 0 ? i : i - 1];
                const Vector2 next =
                    profile[i + 1 < point_count ? i + 1 : i];
                const Vector2 profile_tangent =
                    (next - previous).Normalized();
                const Vector3 normal = Vector3(
                    profile_tangent.y * cosine,
                    -profile_tangent.x,
                    profile_tangent.y * sine
                ).Normalized();
                const float v = static_cast<float>(i) /
                    static_cast<float>(point_count - 1);

                vertices->emplace_back(
                    Vector3(
                        profile[i].x * cosine,
                        profile[i].y,
                        profile[i].x * sine
                    ),
                    Vector2(u, 1.0f - v),
                    normal,
                    tangent
                );
            }
        }

        for (uint32_t segment = 0; segment < segments; segment++)
        {
            for (uint32_t i = 0; i < point_count - 1; i++)
            {
                const uint32_t a =
                    segment * point_count + i;
                const uint32_t b = a + point_count;
                const uint32_t c = a + 1;
                const uint32_t d = b + 1;

                indices->push_back(a);
                indices->push_back(c);
                indices->push_back(b);
                indices->push_back(c);
                indices->push_back(d);
                indices->push_back(b);
            }
        }
    }

    static void generate_torus(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float major_radius,
        float minor_radius,
        uint32_t major_segments,
        uint32_t minor_segments
    )
    {
        using namespace math;

        for (uint32_t major = 0; major <= major_segments; major++)
        {
            const float u = static_cast<float>(major) /
                static_cast<float>(major_segments);
            const float major_angle = u * pi_2;
            const float major_cos = std::cos(major_angle);
            const float major_sin = std::sin(major_angle);
            const Vector3 tangent(
                -major_sin,
                0,
                major_cos
            );

            for (uint32_t minor = 0; minor <= minor_segments; minor++)
            {
                const float v = static_cast<float>(minor) /
                    static_cast<float>(minor_segments);
                const float minor_angle = v * pi_2;
                const float minor_cos = std::cos(minor_angle);
                const float minor_sin = std::sin(minor_angle);
                const float radial =
                    major_radius + minor_radius * minor_cos;
                const Vector3 normal(
                    major_cos * minor_cos,
                    minor_sin,
                    major_sin * minor_cos
                );

                vertices->emplace_back(
                    Vector3(
                        radial * major_cos,
                        minor_radius * minor_sin,
                        radial * major_sin
                    ),
                    Vector2(u, v),
                    normal,
                    tangent
                );
            }
        }

        const uint32_t ring_size = minor_segments + 1;
        for (uint32_t major = 0; major < major_segments; major++)
        {
            for (uint32_t minor = 0; minor < minor_segments; minor++)
            {
                const uint32_t a = major * ring_size + minor;
                const uint32_t b = a + ring_size;
                const uint32_t c = a + 1;
                const uint32_t d = b + 1;

                indices->push_back(a);
                indices->push_back(c);
                indices->push_back(b);
                indices->push_back(c);
                indices->push_back(d);
                indices->push_back(b);
            }
        }
    }

    static void generate_capsule(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float radius,
        float height,
        uint32_t segments
    )
    {
        using namespace math;

        const uint32_t slices = segments * 2;
        const uint32_t stacks = segments;
        const float cylinder_half =
            std::max(0.0f, height * 0.5f - radius);

        for (uint32_t stack = 0; stack <= stacks; stack++)
        {
            const float v = static_cast<float>(stack) /
                static_cast<float>(stacks);
            const float phi = v * pi;
            const float ring_radius = radius * std::sin(phi);
            const float sphere_y = radius * std::cos(phi);
            const float center_y =
                sphere_y >= 0.0f ? cylinder_half : -cylinder_half;

            for (uint32_t slice = 0; slice <= slices; slice++)
            {
                const float u = static_cast<float>(slice) /
                    static_cast<float>(slices);
                const float theta = u * pi_2;
                const float cosine = std::cos(theta);
                const float sine = std::sin(theta);
                const Vector3 normal(
                    std::sin(phi) * cosine,
                    std::cos(phi),
                    std::sin(phi) * sine
                );

                vertices->emplace_back(
                    Vector3(
                        ring_radius * cosine,
                        center_y + sphere_y,
                        ring_radius * sine
                    ),
                    Vector2(u, v),
                    normal,
                    Vector3(-sine, 0, cosine)
                );
            }
        }

        const uint32_t ring_size = slices + 1;
        for (uint32_t stack = 0; stack < stacks; stack++)
        {
            for (uint32_t slice = 0; slice < slices; slice++)
            {
                const uint32_t a = stack * ring_size + slice;
                const uint32_t b = a + ring_size;
                const uint32_t c = a + 1;
                const uint32_t d = b + 1;

                indices->push_back(a);
                indices->push_back(c);
                indices->push_back(b);
                indices->push_back(c);
                indices->push_back(d);
                indices->push_back(b);
            }
        }
    }

    static void generate_rounded_cylinder(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float radius,
        float height,
        float bevel,
        uint32_t radial_segments,
        uint32_t bevel_segments
    )
    {
        using namespace math;

        const float half_height = height * 0.5f;
        const float side_radius = radius - bevel;
        std::vector<Vector2> profile;
        profile.emplace_back(0.0f, -half_height);
        profile.emplace_back(side_radius, -half_height);

        for (uint32_t i = 1; i <= bevel_segments; i++)
        {
            const float t = static_cast<float>(i) /
                static_cast<float>(bevel_segments);
            const float angle = -pi * 0.5f + t * pi * 0.5f;
            profile.emplace_back(
                side_radius + std::cos(angle) * bevel,
                -half_height + bevel + std::sin(angle) * bevel
            );
        }

        profile.emplace_back(radius, half_height - bevel);
        for (uint32_t i = 1; i <= bevel_segments; i++)
        {
            const float t = static_cast<float>(i) /
                static_cast<float>(bevel_segments);
            const float angle = t * pi * 0.5f;
            profile.emplace_back(
                side_radius + std::cos(angle) * bevel,
                half_height - bevel + std::sin(angle) * bevel
            );
        }
        profile.emplace_back(0.0f, half_height);

        generate_revolved_profile(
            vertices,
            indices,
            profile,
            radial_segments
        );
    }

    static void generate_swept_profile(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector3>& path,
        const std::vector<math::Vector2>& profile
    )
    {
        using namespace math;

        const uint32_t path_count =
            static_cast<uint32_t>(path.size());
        const uint32_t profile_count =
            static_cast<uint32_t>(profile.size());

        for (uint32_t i = 0; i < path_count; i++)
        {
            const Vector3 previous =
                path[i == 0 ? i : i - 1];
            const Vector3 next =
                path[i + 1 < path_count ? i + 1 : i];
            const Vector3 path_tangent =
                (next - previous).Normalized();
            const Vector3 reference =
                std::abs(Vector3::Dot(path_tangent, Vector3::Up)) >
                0.95f
                ? Vector3::Right
                : Vector3::Up;
            const Vector3 axis_x =
                Vector3::Cross(reference, path_tangent).Normalized();
            const Vector3 axis_y =
                Vector3::Cross(path_tangent, axis_x).Normalized();
            const float u = static_cast<float>(i) /
                static_cast<float>(path_count - 1);

            for (uint32_t j = 0; j < profile_count; j++)
            {
                const Vector2 previous_profile =
                    profile[j == 0 ? profile_count - 1 : j - 1];
                const Vector2 next_profile =
                    profile[(j + 1) % profile_count];
                const Vector2 profile_tangent =
                    (next_profile - previous_profile).Normalized();
                const Vector2 profile_normal(
                    profile_tangent.y,
                    -profile_tangent.x
                );
                const Vector3 normal = (
                    axis_x * profile_normal.x +
                    axis_y * profile_normal.y
                ).Normalized();

                vertices->emplace_back(
                    path[i] +
                    axis_x * profile[j].x +
                    axis_y * profile[j].y,
                    Vector2(
                        u,
                        static_cast<float>(j) /
                            static_cast<float>(profile_count)
                    ),
                    normal,
                    path_tangent
                );
            }
        }

        for (uint32_t i = 0; i < path_count - 1; i++)
        {
            for (uint32_t j = 0; j < profile_count; j++)
            {
                const uint32_t next_j = (j + 1) % profile_count;
                const uint32_t a = i * profile_count + j;
                const uint32_t b = (i + 1) * profile_count + j;
                const uint32_t c = i * profile_count + next_j;
                const uint32_t d =
                    (i + 1) * profile_count + next_j;

                indices->push_back(a);
                indices->push_back(c);
                indices->push_back(b);
                indices->push_back(c);
                indices->push_back(d);
                indices->push_back(b);
            }
        }
    }

    static void generate_pipe(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector3>& path,
        float radius,
        uint32_t sides
    )
    {
        using namespace math;

        std::vector<Vector2> profile;
        profile.reserve(sides);
        for (uint32_t i = 0; i < sides; i++)
        {
            const float angle =
                static_cast<float>(i) /
                static_cast<float>(sides) *
                pi_2;
            profile.emplace_back(
                std::cos(angle) * radius,
                std::sin(angle) * radius
            );
        }

        generate_swept_profile(
            vertices,
            indices,
            path,
            profile
        );
    }

    static void generate_arch(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        float width,
        float height,
        float depth,
        float thickness,
        uint32_t segments
    )
    {
        using namespace math;

        const float outer_radius = width * 0.5f;
        const float inner_radius = outer_radius - thickness;
        const float spring_y = height - outer_radius;
        std::vector<Vector2> outer;
        std::vector<Vector2> inner;

        outer.emplace_back(-outer_radius, 0.0f);
        inner.emplace_back(-inner_radius, 0.0f);
        for (uint32_t i = 0; i <= segments; i++)
        {
            const float angle =
                pi - static_cast<float>(i) /
                static_cast<float>(segments) *
                pi;
            outer.emplace_back(
                std::cos(angle) * outer_radius,
                spring_y + std::sin(angle) * outer_radius
            );
            inner.emplace_back(
                std::cos(angle) * inner_radius,
                spring_y + std::sin(angle) * inner_radius
            );
        }
        outer.emplace_back(outer_radius, 0.0f);
        inner.emplace_back(inner_radius, 0.0f);

        auto append_quad = [&](
            const Vector3& a,
            const Vector3& b,
            const Vector3& c,
            const Vector3& d
        )
        {
            const uint32_t offset =
                static_cast<uint32_t>(vertices->size());
            const Vector3 normal =
                Vector3::Cross(b - a, c - a).Normalized();
            const Vector3 tangent = (b - a).Normalized();
            vertices->emplace_back(
                a,
                Vector2(0, 1),
                normal,
                tangent
            );
            vertices->emplace_back(
                b,
                Vector2(1, 1),
                normal,
                tangent
            );
            vertices->emplace_back(
                c,
                Vector2(0, 0),
                normal,
                tangent
            );
            vertices->emplace_back(
                d,
                Vector2(1, 0),
                normal,
                tangent
            );
            indices->push_back(offset);
            indices->push_back(offset + 1);
            indices->push_back(offset + 2);
            indices->push_back(offset + 2);
            indices->push_back(offset + 1);
            indices->push_back(offset + 3);
        };

        const float front = -depth * 0.5f;
        const float back = depth * 0.5f;
        for (uint32_t i = 0; i + 1 < outer.size(); i++)
        {
            const Vector3 outer_front_a(
                outer[i].x,
                outer[i].y,
                front
            );
            const Vector3 outer_front_b(
                outer[i + 1].x,
                outer[i + 1].y,
                front
            );
            const Vector3 inner_front_a(
                inner[i].x,
                inner[i].y,
                front
            );
            const Vector3 inner_front_b(
                inner[i + 1].x,
                inner[i + 1].y,
                front
            );
            const Vector3 outer_back_a(
                outer[i].x,
                outer[i].y,
                back
            );
            const Vector3 outer_back_b(
                outer[i + 1].x,
                outer[i + 1].y,
                back
            );
            const Vector3 inner_back_a(
                inner[i].x,
                inner[i].y,
                back
            );
            const Vector3 inner_back_b(
                inner[i + 1].x,
                inner[i + 1].y,
                back
            );

            append_quad(
                outer_front_b,
                inner_front_b,
                outer_front_a,
                inner_front_a
            );
            append_quad(
                outer_back_a,
                inner_back_a,
                outer_back_b,
                inner_back_b
            );
            append_quad(
                outer_front_b,
                outer_front_a,
                outer_back_b,
                outer_back_a
            );
            append_quad(
                inner_front_a,
                inner_front_b,
                inner_back_a,
                inner_back_b
            );
        }

        append_quad(
            Vector3(outer.front().x, outer.front().y, front),
            Vector3(inner.front().x, inner.front().y, front),
            Vector3(outer.front().x, outer.front().y, back),
            Vector3(inner.front().x, inner.front().y, back)
        );
        append_quad(
            Vector3(inner.back().x, inner.back().y, front),
            Vector3(outer.back().x, outer.back().y, front),
            Vector3(inner.back().x, inner.back().y, back),
            Vector3(outer.back().x, outer.back().y, back)
        );
    }

    static void generate_inset_panel(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const math::Vector3& size,
        float border,
        float inset,
        float bevel
    )
    {
        using namespace math;

        generate_beveled_box(
            vertices,
            indices,
            size,
            bevel
        );

        auto append_rail = [&](
            const Vector3& rail_size,
            const Vector3& offset
        )
        {
            std::vector<RHI_Vertex_PosTexNorTan> rail_vertices;
            std::vector<uint32_t> rail_indices;
            generate_beveled_box(
                &rail_vertices,
                &rail_indices,
                rail_size,
                std::min(
                    bevel,
                    std::min({
                        rail_size.x,
                        rail_size.y,
                        rail_size.z
                    }) * 0.24f
                )
            );
            const uint32_t vertex_offset =
                static_cast<uint32_t>(vertices->size());
            for (RHI_Vertex_PosTexNorTan& vertex : rail_vertices)
            {
                vertex.pos[0] += offset.x;
                vertex.pos[1] += offset.y;
                vertex.pos[2] += offset.z;
                vertices->push_back(vertex);
            }
            for (uint32_t index : rail_indices)
            {
                indices->push_back(vertex_offset + index);
            }
        };

        const float rail_depth = inset;
        const float front =
            size.z * 0.5f + rail_depth * 0.5f;
        append_rail(
            Vector3(size.x, border, rail_depth),
            Vector3(
                0,
                size.y * 0.5f - border * 0.5f,
                front
            )
        );
        append_rail(
            Vector3(size.x, border, rail_depth),
            Vector3(
                0,
                -size.y * 0.5f + border * 0.5f,
                front
            )
        );
        append_rail(
            Vector3(border, size.y - border * 2, rail_depth),
            Vector3(
                -size.x * 0.5f + border * 0.5f,
                0,
                front
            )
        );
        append_rail(
            Vector3(border, size.y - border * 2, rail_depth),
            Vector3(
                size.x * 0.5f - border * 0.5f,
                0,
                front
            )
        );
    }

    static void generate_tapered_extrusion(
        std::vector<RHI_Vertex_PosTexNorTan>* vertices,
        std::vector<uint32_t>* indices,
        const std::vector<math::Vector2>& profile,
        float depth,
        float scale_start,
        float scale_end
    )
    {
        using namespace math;

        const uint32_t point_count =
            static_cast<uint32_t>(profile.size());
        const float half_depth = depth * 0.5f;

        for (uint32_t side = 0; side < 2; side++)
        {
            const float scale =
                side == 0 ? scale_start : scale_end;
            const float z = side == 0 ? -half_depth : half_depth;
            const Vector3 normal =
                side == 0 ? Vector3(0, 0, -1) : Vector3(0, 0, 1);
            const uint32_t offset =
                static_cast<uint32_t>(vertices->size());
            for (const Vector2& point : profile)
            {
                vertices->emplace_back(
                    Vector3(point.x * scale, point.y * scale, z),
                    point,
                    normal,
                    Vector3(1, 0, 0)
                );
            }
            for (uint32_t i = 1; i + 1 < point_count; i++)
            {
                indices->push_back(offset);
                if (side == 0)
                {
                    indices->push_back(offset + i + 1);
                    indices->push_back(offset + i);
                }
                else
                {
                    indices->push_back(offset + i);
                    indices->push_back(offset + i + 1);
                }
            }
        }

        for (uint32_t i = 0; i < point_count; i++)
        {
            const uint32_t next = (i + 1) % point_count;
            const Vector3 a(
                profile[i].x * scale_start,
                profile[i].y * scale_start,
                -half_depth
            );
            const Vector3 b(
                profile[next].x * scale_start,
                profile[next].y * scale_start,
                -half_depth
            );
            const Vector3 c(
                profile[i].x * scale_end,
                profile[i].y * scale_end,
                half_depth
            );
            const Vector3 d(
                profile[next].x * scale_end,
                profile[next].y * scale_end,
                half_depth
            );
            const Vector3 normal =
                Vector3::Cross(b - a, c - a).Normalized();
            const uint32_t offset =
                static_cast<uint32_t>(vertices->size());
            vertices->emplace_back(
                a,
                Vector2(0, 1),
                normal,
                (b - a).Normalized()
            );
            vertices->emplace_back(
                c,
                Vector2(1, 1),
                normal,
                (d - c).Normalized()
            );
            vertices->emplace_back(
                b,
                Vector2(0, 0),
                normal,
                (b - a).Normalized()
            );
            vertices->emplace_back(
                d,
                Vector2(1, 0),
                normal,
                (d - c).Normalized()
            );
            indices->push_back(offset);
            indices->push_back(offset + 2);
            indices->push_back(offset + 1);
            indices->push_back(offset + 2);
            indices->push_back(offset + 3);
            indices->push_back(offset + 1);
        }
    }

    static void generate_foliage_grass_blade(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, const uint32_t segment_count)
    {
        using namespace math;

        // constants
        const float grass_width    = 0.05f; // base width
        const float grass_height   = 0.2f;  // blade height
        const float thinning_start = 0.2f;  // thinning start (0=base, 1=top)
        const float thinning_power = 1.0f;  // thinning sharpness

        // clear output vectors
        vertices->clear();
        indices->clear();

        // helper to compute width factor
        auto compute_width_factor = [=](float t) -> float
        {
            return (t <= thinning_start) ? 1.0f : std::pow(1.0f - ((t - thinning_start) / (1.0f - thinning_start)), thinning_power);
        };

        // helper to push vertex
        auto push_vertex = [&](const Vector3& pos, const Vector2& tex)
        {
            RHI_Vertex_PosTexNorTan v{};
            v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
            v.set_uv(tex);
            vertices->push_back(v);
        };

        // total verts per face
        uint32_t verts_per_strip = (segment_count + 1) * 2 - 1;
        uint32_t total_vertices  = verts_per_strip;
        vertices->reserve(total_vertices);

        // generate vertices for front face
        for (uint32_t i = 0; i <= segment_count; ++i)
        {
            float t            = float(i) / segment_count;
            float y            = t * grass_height;
            float width_factor = compute_width_factor(t);
            if (i < segment_count)
            {
                // left vertex
                push_vertex(Vector3(-grass_width * 0.5f * width_factor, y, 0.0f), Vector2(0.0f, t));
                // right vertex
                push_vertex(Vector3(grass_width * 0.5f * width_factor, y, 0.0f), Vector2(1.0f, t));
            }
            else
            {
                // top vertex (single vertex at tip)
                push_vertex(Vector3(0.0f, y, 0.0f), Vector2(0.5f, t));
            }
        }

        // bend towards downwards (emulate gravity pulling the blade down)
        const float bend_amount = 0.25f;
        for (RHI_Vertex_PosTexNorTan& v : *vertices)
        {
            float uv_misc_z     = v.get_uv().y;
            float gravity_angle = bend_amount * uv_misc_z;
            float c             = std::cos(gravity_angle);
            float s             = std::sin(gravity_angle);
            float y             = v.pos[1];
            float z             = v.pos[2];
            v.pos[1]            = c * y - s * z;
            v.pos[2]            = s * y + c * z;
        }

        // build indices for front face
        uint32_t verts_per_face = verts_per_strip;
        uint32_t offset         = 0;
        bool invert_winding     = false;
        for (uint32_t i = 0; i < segment_count; ++i)
        {
            if (i < segment_count - 1)
            {
                if (!invert_winding)
                {
                    indices->push_back(offset + i * 2);
                    indices->push_back(offset + i * 2 + 1);
                    indices->push_back(offset + i * 2 + 2);
                    indices->push_back(offset + i * 2 + 2);
                    indices->push_back(offset + i * 2 + 1);
                    indices->push_back(offset + i * 2 + 3);
                }
                else
                {
                    indices->push_back(offset + i * 2 + 2);
                    indices->push_back(offset + i * 2 + 1);
                    indices->push_back(offset + i * 2);
                    indices->push_back(offset + i * 2 + 3);
                    indices->push_back(offset + i * 2 + 1);
                    indices->push_back(offset + i * 2 + 2);
                }
            }
            else
            {
                // last triangle at tip
                if (!invert_winding)
                {
                    indices->push_back(offset + i * 2);
                    indices->push_back(offset + i * 2 + 1);
                    indices->push_back(offset + i * 2 + 2);
                }
                else
                {
                    indices->push_back(offset + i * 2 + 2);
                    indices->push_back(offset + i * 2 + 1);
                    indices->push_back(offset + i * 2);
                }
            }
        }

        // compute normals and tangents in float buffers, then pack at the end
        std::vector<Vector3> tmp_normals(vertices->size(), Vector3::Zero);
        std::vector<Vector3> tmp_tangents(vertices->size(), Vector3::Zero);
        for (size_t i = 0; i < indices->size(); i += 3)
        {
            uint32_t i0 = (*indices)[i];
            uint32_t i1 = (*indices)[i + 1];
            uint32_t i2 = (*indices)[i + 2];
            Vector3 p0((*vertices)[i0].pos[0], (*vertices)[i0].pos[1], (*vertices)[i0].pos[2]);
            Vector3 p1((*vertices)[i1].pos[0], (*vertices)[i1].pos[1], (*vertices)[i1].pos[2]);
            Vector3 p2((*vertices)[i2].pos[0], (*vertices)[i2].pos[1], (*vertices)[i2].pos[2]);
            Vector3 edge1 = p1 - p0;
            Vector3 edge2 = p2 - p0;
            Vector3 face_normal = Vector3::Normalize(Vector3::Cross(edge1, edge2));

            tmp_normals[i0] += face_normal;
            tmp_normals[i1] += face_normal;
            tmp_normals[i2] += face_normal;

            // approximate tangent as direction along width (x axis), assuming grass blade vertical along y
            Vector3 tangent = Vector3::Normalize(Vector3(edge1.x, 0.0f, edge1.z));
            tmp_tangents[i0] += tangent;
            tmp_tangents[i1] += tangent;
            tmp_tangents[i2] += tangent;
        }

        for (size_t i = 0; i < vertices->size(); ++i)
        {
            (*vertices)[i].set_normal(Vector3::Normalize(tmp_normals[i]));
            (*vertices)[i].set_tangent(Vector3::Normalize(tmp_tangents[i]));
        }
    }

    static void generate_foliage_flower(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, const uint32_t stem_segment_count, const uint32_t petal_count, const uint32_t petal_segment_count)
    {
        using namespace math;

        // constants
        const float stem_radius                  = 0.032f; // was width/2
        const float stem_height                  = 0.64f;
        const float stem_thinning_start          = 0.7f;
        const float stem_thinning_power          = 1.0f;
        const uint32_t stem_side_count           = 6; // new: for cylinder
        const float petal_width                  = 0.13f;
        const float petal_length                 = 0.26f;
        const float petal_thinning_power         = 1.5f; // for oval sharpness
        const float min_petal_tilt               = 20.0f * deg_to_rad; // outer open
        const float max_petal_tilt               = 70.0f * deg_to_rad; // inner upright
        const float min_petal_bend               = 0.0f; // inner straight
        const float max_petal_bend               = 0.3f; // outer droop
        const float small_petal_scale            = 0.5f; // inner small
        const float large_petal_scale            = 1.0f; // outer large
        const float min_petal_curvature          = 0.1f; // new: outer less cupped
        const float max_petal_curvature          = 0.4f; // inner more cupped
        const uint32_t petal_width_segment_count = 2; // new: >=1, allows cupping
        const float spiral_height                = 0.13f;
        const float spiral_radius                = 0.0f; // new: 0 to attach at center
        const float golden_angle                 = 137.5f * deg_to_rad;

        // clear output
        vertices->clear();
        indices->clear();

        // helper
        auto push_vertex = [&](const Vector3& pos, const Vector2& tex)
        {
            RHI_Vertex_PosTexNorTan v{};
            v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
            v.set_uv(tex);
            vertices->push_back(v);
        };

        // stem: tapered cylinder
        auto stem_radius_factor = [=](float t) -> float
        {
            if (t <= stem_thinning_start)
            {
                return 1.0f;
            }
            float x = (t - stem_thinning_start) / (1.0f - stem_thinning_start);
            return std::pow(1.0f - std::clamp(x, 0.0f, 1.0f), stem_thinning_power);
        };
        uint32_t offset = 0;
        for (uint32_t i = 0; i <= stem_segment_count; ++i)
        {
            float t  = static_cast<float>(i) / stem_segment_count;
            float y  = t * stem_height;
            float rf = stem_radius_factor(t);
            float r  = stem_radius * rf;
            for (uint32_t j = 0; j < stem_side_count; ++j)
            {
                float a = static_cast<float>(j) / stem_side_count * math::pi_2;
                float x = std::cos(a) * r;
                float z = std::sin(a) * r;
                push_vertex(Vector3(x, y, z), Vector2(static_cast<float>(j) / stem_side_count, t));
            }
        }
        for (uint32_t i = 0; i < stem_segment_count; ++i)
        {
            for (uint32_t j = 0; j < stem_side_count; ++j)
            {
                uint32_t a = offset + i * stem_side_count + j;
                uint32_t b = offset + i * stem_side_count + (j + 1) % stem_side_count;
                uint32_t c = a + stem_side_count;
                uint32_t d = b + stem_side_count;
                indices->push_back(a); indices->push_back(c); indices->push_back(b);
                indices->push_back(b); indices->push_back(c); indices->push_back(d);
            }
        }
        uint32_t current_offset = (stem_segment_count + 1) * stem_side_count;

        // petals: spiral with oval taper, cup curvature
        auto petal_width_factor = [=](float t) -> float
        {
            float s = std::sin(t * math::pi);
            return std::pow(s, petal_thinning_power);
        };
        for (uint32_t p = 0; p < petal_count; ++p)
        {
            float frac           = petal_count > 1 ? static_cast<float>(p) / (petal_count - 1) : 0.0f;
            float this_tilt      = min_petal_tilt + frac * (max_petal_tilt - min_petal_tilt);
            float this_bend      = max_petal_bend - frac * (max_petal_bend - min_petal_bend);
            float this_scale     = large_petal_scale - frac * (large_petal_scale - small_petal_scale);
            float this_curvature = min_petal_curvature + frac * (max_petal_curvature - min_petal_curvature);
            float this_height    = stem_height + frac * spiral_height;
            float this_radius    = spiral_radius;
            float angle          = static_cast<float>(p) * golden_angle;
            float ca             = std::cos(angle);
            float sa             = std::sin(angle);
            float cos_t          = std::cos(this_tilt);
            float sin_t          = std::sin(this_tilt);
            uint32_t petal_start = static_cast<uint32_t>(vertices->size());
            for (uint32_t i = 0; i <= petal_segment_count; ++i)
            {
                float t = static_cast<float>(i) / petal_segment_count;
                float wf = petal_width_factor(t);
                Vector3 local_pos;
                Vector2 tex;
                if (i < petal_segment_count)
                {
                    for (uint32_t j = 0; j <= petal_width_segment_count; ++j)
                    {
                        float frac_j   = static_cast<float>(j) / petal_width_segment_count;
                        float local_x  = (frac_j - 0.5f) * petal_width * wf * this_scale;
                        local_pos      = Vector3(local_x, 0.0f, t * petal_length * this_scale);
                        tex            = Vector2(frac_j, t);
                        local_pos.y   -= this_bend * t * t;

                        // curvature: concave cup, stronger at base
                        float norm_x  = 2.0f * (frac_j - 0.5f); // -1 to 1
                        local_pos.y  += this_curvature * (norm_x * norm_x - 1.0f) * (1.0f - t);

                        // tilt
                        float new_y = cos_t * local_pos.y + sin_t * local_pos.z;
                        float new_z = -sin_t * local_pos.y + cos_t * local_pos.z;
                        local_pos.y = new_y;
                        local_pos.z = new_z;

                        // rotate
                        float new_x = ca * local_pos.x - sa * local_pos.z;
                        new_z       = sa * local_pos.x + ca * local_pos.z;
                        local_pos.x = new_x;
                        local_pos.z = new_z;

                        // offset (minimal)
                        local_pos.x += ca * this_radius;
                        local_pos.z += sa * this_radius;
                        local_pos.y += this_height;
                        push_vertex(local_pos, tex);
                    }
                }
                else
                {
                    // tip
                    local_pos    = Vector3(0.0f, 0.0f, t * petal_length * this_scale);
                    tex          = Vector2(0.5f, t);
                    local_pos.y -= this_bend * t * t;
                    local_pos.y += this_curvature * (0.0f - 1.0f) * (1.0f - t); // center adjust
                    float new_y  = cos_t * local_pos.y + sin_t * local_pos.z;
                    float new_z  = -sin_t * local_pos.y + cos_t * local_pos.z;
                    local_pos.y  = new_y;
                    local_pos.z  = new_z;
                    float new_x  = ca * local_pos.x - sa * local_pos.z;
                    new_z        = sa * local_pos.x + ca * local_pos.z;
                    local_pos.x  = new_x;
                    local_pos.z  = new_z;
                    local_pos.x += ca * this_radius;
                    local_pos.z += sa * this_radius;
                    local_pos.y += this_height;
                    push_vertex(local_pos, tex);
                }
            }
            // petal indices (grid + fan to tip)
            uint32_t row_size = petal_width_segment_count + 1;
            for (uint32_t i = 0; i < petal_segment_count - 1; ++i)
            {
                for (uint32_t j = 0; j < petal_width_segment_count; ++j)
                {
                    uint32_t a = current_offset + i * row_size + j;
                    uint32_t b = a + 1;
                    uint32_t c = a + row_size;
                    uint32_t d = c + 1;
                    indices->push_back(a); indices->push_back(c); indices->push_back(b);
                    indices->push_back(b); indices->push_back(c); indices->push_back(d);
                }
            }
            // last row to tip
            uint32_t last_row_start = current_offset + (petal_segment_count - 1) * row_size;
            uint32_t tip_index = current_offset + petal_segment_count * row_size;
            for (uint32_t j = 0; j < petal_width_segment_count; ++j)
            {
                uint32_t a = last_row_start + j;
                uint32_t b = a + 1;
                indices->push_back(a); indices->push_back(tip_index); indices->push_back(b);
            }
            current_offset += petal_segment_count * row_size + 1;
        }

        // compute normals and tangents in float buffers, then pack at the end
        std::vector<Vector3> tmp_normals(vertices->size(), Vector3::Zero);
        std::vector<Vector3> tmp_tangents(vertices->size(), Vector3::Zero);
        for (size_t i = 0; i < indices->size(); i += 3)
        {
            uint32_t i0 = (*indices)[i];
            uint32_t i1 = (*indices)[i + 1];
            uint32_t i2 = (*indices)[i + 2];
            Vector3 p0((*vertices)[i0].pos[0], (*vertices)[i0].pos[1], (*vertices)[i0].pos[2]);
            Vector3 p1((*vertices)[i1].pos[0], (*vertices)[i1].pos[1], (*vertices)[i1].pos[2]);
            Vector3 p2((*vertices)[i2].pos[0], (*vertices)[i2].pos[1], (*vertices)[i2].pos[2]);
            Vector3 edge1 = p1 - p0;
            Vector3 edge2 = p2 - p0;
            Vector3 face_normal = Vector3::Normalize(Vector3::Cross(edge1, edge2));
            tmp_normals[i0] += face_normal; tmp_normals[i1] += face_normal; tmp_normals[i2] += face_normal;
            Vector3 tangent = Vector3::Normalize(Vector3(edge1.x, 0.0f, edge1.z));
            tmp_tangents[i0] += tangent; tmp_tangents[i1] += tangent; tmp_tangents[i2] += tangent;
        }
        for (size_t i = 0; i < vertices->size(); ++i)
        {
            (*vertices)[i].set_normal(Vector3::Normalize(tmp_normals[i]));
            (*vertices)[i].set_tangent(Vector3::Normalize(tmp_tangents[i]));
        }
    }
}
