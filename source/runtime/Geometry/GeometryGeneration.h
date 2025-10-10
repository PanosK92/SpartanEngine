/*
Copyright(c) 2015-2025 Panos Karabelas

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

    static void generate_cylinder(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, float radiusTop = 1.0f, float radiusBottom = 1.0f, float height = 1.0f, int slices = 15, int stacks = 15)
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
        generate_cylinder(vertices, indices, 0.0f, radius, height);
    }

    static void generate_foliage_grass_blade(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, const uint32_t segment_count)
    {
        using namespace math;

        // constants
        const float grass_width    = 0.2f; // base width
        const float grass_height   = 1.2f;  // blade height
        const float thinning_start = 0.4f;  // thinning start (0=base, 1=top)
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
            v.tex[0] = tex.x; v.tex[1] = tex.y;
            v.nor[0] = 0.0f; v.nor[1] = 0.0f; v.nor[2] = 0.0f;
            v.tan[0] = 0.0f; v.tan[1] = 0.0f; v.tan[2] = 0.0f;
            vertices->push_back(v);
        };

        // total verts per face
        uint32_t verts_per_strip = (segment_count + 1) * 2 - 1;
        uint32_t total_vertices = verts_per_strip;
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

        // compute normals and tangents
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

            // add face normal to vertices
            (*vertices)[i0].nor[0] += face_normal.x;
            (*vertices)[i0].nor[1] += face_normal.y;
            (*vertices)[i0].nor[2] += face_normal.z;
            (*vertices)[i1].nor[0] += face_normal.x;
            (*vertices)[i1].nor[1] += face_normal.y;
            (*vertices)[i1].nor[2] += face_normal.z;
            (*vertices)[i2].nor[0] += face_normal.x;
            (*vertices)[i2].nor[1] += face_normal.y;
            (*vertices)[i2].nor[2] += face_normal.z;

            // approximate tangent as direction along width (x axis), assuming grass blade vertical along y
            Vector3 tangent = Vector3::Normalize(Vector3(edge1.x, 0.0f, edge1.z));
            (*vertices)[i0].tan[0] += tangent.x;
            (*vertices)[i0].tan[1] += tangent.y;
            (*vertices)[i0].tan[2] += tangent.z;
            (*vertices)[i1].tan[0] += tangent.x;
            (*vertices)[i1].tan[1] += tangent.y;
            (*vertices)[i1].tan[2] += tangent.z;
            (*vertices)[i2].tan[0] += tangent.x;
            (*vertices)[i2].tan[1] += tangent.y;
            (*vertices)[i2].tan[2] += tangent.z;
        }

        // normalize normals and tangents per vertex
        for (auto& v : *vertices)
        {
            Vector3 n(v.nor[0], v.nor[1], v.nor[2]);
            n = Vector3::Normalize(n);
            v.nor[0] = n.x; v.nor[1] = n.y; v.nor[2] = n.z;
            Vector3 t(v.tan[0], v.tan[1], v.tan[2]);
            t = Vector3::Normalize(t);
            v.tan[0] = t.x; v.tan[1] = t.y; v.tan[2] = t.z;
        }
    }

    static void generate_foliage_flower(std::vector<RHI_Vertex_PosTexNorTan>* vertices, std::vector<uint32_t>* indices, const uint32_t stem_segment_count, const uint32_t petal_count, const uint32_t petal_segment_count)
    {
        using namespace math;

        // constants
        const float stem_width           = 0.1f;
        const float stem_height          = 1.0f;
        const float stem_thinning_start  = 0.7f; // taper near top
        const float stem_thinning_power  = 1.0f;
        const float petal_width          = 0.2f;
        const float petal_length         = 0.4f;
        const float petal_thinning_start = 0.5f; // for oval shape
        const float petal_thinning_power = 2.0f;
        const float min_petal_tilt       = 20.0f * math::deg_to_rad; // outer more open
        const float max_petal_tilt       = 70.0f * math::deg_to_rad; // inner more upright
        const float min_petal_bend       = 0.0f; // inner less droop
        const float max_petal_bend       = 0.3f; // outer more droop
        const float small_petal_scale    = 0.5f; // inner smaller
        const float large_petal_scale    = 1.0f; // outer larger
        const float spiral_height        = 0.2f; // inner higher for bud effect
        const float min_spiral_radius    = 0.0f; // inner close to center
        const float max_spiral_radius    = 0.15f; // outer farther out
        const float golden_angle         = 137.5f * math::deg_to_rad; // for spiral arrangement

        // clear output vectors
        vertices->clear();
        indices->clear();

        // helper to push vertex
        auto push_vertex = [&](const Vector3& pos, const Vector2& tex)
        {
            RHI_Vertex_PosTexNorTan v{};
            v.pos[0] = pos.x; v.pos[1] = pos.y; v.pos[2] = pos.z;
            v.tex[0] = tex.x; v.tex[1] = tex.y;
            v.nor[0] = 0.0f; v.nor[1] = 0.0f; v.nor[2] = 0.0f;
            v.tan[0] = 0.0f; v.tan[1] = 0.0f; v.tan[2] = 0.0f;
            vertices->push_back(v);
        };

        // stem: flat strip with constant width, no tip
        auto stem_width_factor = [=](float t) -> float
        {
            float denom = 1.0f - stem_thinning_start;
            if (denom <= 0.0f)
                return 1.0f;
    
            if (t <= stem_thinning_start)
                return 1.0f;
    
            float x = (t - stem_thinning_start) / denom;
            x = std::clamp(x, 0.0f, 1.0f);
            return std::pow(1.0f - x, stem_thinning_power);
        };
    
        for (uint32_t i = 0; i <= stem_segment_count; ++i)
        {
            float t = static_cast<float>(i) / stem_segment_count;
            float y = t * stem_height;
            float wf = stem_width_factor(t);
            push_vertex(Vector3(-stem_width * 0.5f * wf, y, 0.0f), Vector2(0.0f, t));
            push_vertex(Vector3( stem_width * 0.5f * wf, y, 0.0f), Vector2(1.0f, t));
        }
        uint32_t offset = 0;
        for (uint32_t i = 0; i < stem_segment_count; ++i)
        {
            indices->push_back(offset + i * 2);
            indices->push_back(offset + i * 2 + 1);
            indices->push_back(offset + i * 2 + 2);
            indices->push_back(offset + i * 2 + 2);
            indices->push_back(offset + i * 2 + 1);
            indices->push_back(offset + i * 2 + 3);
        }

        // petals: spiral arrangement with variations for rose-like effect
        uint32_t current_offset = (stem_segment_count + 1) * 2;
        auto petal_width_factor = [=](float t) -> float
        {
            return (t <= petal_thinning_start) ? 1.0f : std::pow(1.0f - ((t - petal_thinning_start) / (1.0f - petal_thinning_start)), petal_thinning_power);
        };
        for (uint32_t p = 0; p < petal_count; ++p)
        {
            float frac        = petal_count > 1 ? static_cast<float>(p) / (petal_count - 1) : 0.0f;
            float this_tilt   = min_petal_tilt + frac * (max_petal_tilt - min_petal_tilt);
            float this_bend   = max_petal_bend - frac * (max_petal_bend - min_petal_bend);
            float this_scale  = large_petal_scale - frac * (large_petal_scale - small_petal_scale);
            float this_height = stem_height + frac * spiral_height;
            float this_radius = max_spiral_radius - frac * (max_spiral_radius - min_spiral_radius);
            float angle       = static_cast<float>(p) * golden_angle;
            float ca          = std::cos(angle);
            float sa          = std::sin(angle);
            float cos_t       = std::cos(this_tilt);
            float sin_t       = std::sin(this_tilt);

            for (uint32_t i = 0; i <= petal_segment_count; ++i)
            {
                float t = static_cast<float>(i) / petal_segment_count;
                float wf = petal_width_factor(t);
                Vector3 local_pos;
                Vector2 tex;
                if (i < petal_segment_count)
                {
                    // left
                    local_pos = Vector3(-petal_width * 0.5f * wf * this_scale, 0.0f, t * petal_length * this_scale);
                    tex = Vector2(0.0f, t);

                    // apply bend (quadratic droop)
                    local_pos.y -= this_bend * t * t;

                    // apply tilt (upward rotation around local x)
                    float new_y = cos_t * local_pos.y + sin_t * local_pos.z;
                    float new_z = -sin_t * local_pos.y + cos_t * local_pos.z;
                    local_pos.y = new_y;
                    local_pos.z = new_z;

                    // rotate to radial position
                    float new_x = ca * local_pos.x - sa * local_pos.z;
                    new_z = sa * local_pos.x + ca * local_pos.z;
                    local_pos.x = new_x;
                    local_pos.z = new_z;

                    // offset base outward
                    local_pos.x += ca * this_radius;
                    local_pos.z += sa * this_radius;

                    // offset to petal height
                    local_pos.y += this_height;
                    push_vertex(local_pos, tex);

                    // right
                    local_pos = Vector3(petal_width * 0.5f * wf * this_scale, 0.0f, t * petal_length * this_scale);
                    tex = Vector2(1.0f, t);

                    // apply bend
                    local_pos.y -= this_bend * t * t;

                    // apply tilt
                    new_y = cos_t * local_pos.y + sin_t * local_pos.z;
                    new_z = -sin_t * local_pos.y + cos_t * local_pos.z;
                    local_pos.y = new_y;
                    local_pos.z = new_z;

                    // rotate
                    new_x = ca * local_pos.x - sa * local_pos.z;
                    new_z = sa * local_pos.x + ca * local_pos.z;
                    local_pos.x = new_x;
                    local_pos.z = new_z;

                    // offset base outward
                    local_pos.x += ca * this_radius;
                    local_pos.z += sa * this_radius;

                    // offset to petal height
                    local_pos.y += this_height;
                    push_vertex(local_pos, tex);
                }
                else
                {
                    // tip
                    local_pos = Vector3(0.0f, 0.0f, t * petal_length * this_scale);
                    tex = Vector2(0.5f, t);

                    // apply bend
                    local_pos.y -= this_bend * t * t;

                    // apply tilt
                    float new_y = cos_t * local_pos.y + sin_t * local_pos.z;
                    float new_z = -sin_t * local_pos.y + cos_t * local_pos.z;
                    local_pos.y = new_y;
                    local_pos.z = new_z;

                    // rotate
                    float new_x = ca * local_pos.x - sa * local_pos.z;
                    new_z = sa * local_pos.x + ca * local_pos.z;
                    local_pos.x = new_x;
                    local_pos.z = new_z;

                    // offset base outward
                    local_pos.x += ca * this_radius;
                    local_pos.z += sa * this_radius;

                    // offset to petal height
                    local_pos.y += this_height;
                    push_vertex(local_pos, tex);
                }
            }
            for (uint32_t i = 0; i < petal_segment_count; ++i)
            {
                if (i < petal_segment_count - 1)
                {
                    indices->push_back(current_offset + i * 2);
                    indices->push_back(current_offset + i * 2 + 1);
                    indices->push_back(current_offset + i * 2 + 2);
                    indices->push_back(current_offset + i * 2 + 2);
                    indices->push_back(current_offset + i * 2 + 1);
                    indices->push_back(current_offset + i * 2 + 3);
                }
                else
                {
                    indices->push_back(current_offset + i * 2);
                    indices->push_back(current_offset + i * 2 + 1);
                    indices->push_back(current_offset + i * 2 + 2);
                }
            }
            current_offset += (petal_segment_count * 2 + 1);
        }

        // compute normals and tangents
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

            // add face normal to vertices
            (*vertices)[i0].nor[0] += face_normal.x;
            (*vertices)[i0].nor[1] += face_normal.y;
            (*vertices)[i0].nor[2] += face_normal.z;
            (*vertices)[i1].nor[0] += face_normal.x;
            (*vertices)[i1].nor[1] += face_normal.y;
            (*vertices)[i1].nor[2] += face_normal.z;
            (*vertices)[i2].nor[0] += face_normal.x;
            (*vertices)[i2].nor[1] += face_normal.y;
            (*vertices)[i2].nor[2] += face_normal.z;

            // approximate tangent as direction along width (x axis), assuming grass blade vertical along y
            Vector3 tangent = Vector3::Normalize(Vector3(edge1.x, 0.0f, edge1.z));
            (*vertices)[i0].tan[0] += tangent.x;
            (*vertices)[i0].tan[1] += tangent.y;
            (*vertices)[i0].tan[2] += tangent.z;
            (*vertices)[i1].tan[0] += tangent.x;
            (*vertices)[i1].tan[1] += tangent.y;
            (*vertices)[i1].tan[2] += tangent.z;
            (*vertices)[i2].tan[0] += tangent.x;
            (*vertices)[i2].tan[1] += tangent.y;
            (*vertices)[i2].tan[2] += tangent.z;
        }

        // normalize normals and tangents per vertex
        for (auto& v : *vertices)
        {
            Vector3 n(v.nor[0], v.nor[1], v.nor[2]);
            n = Vector3::Normalize(n);
            v.nor[0] = n.x; v.nor[1] = n.y; v.nor[2] = n.z;
            Vector3 t(v.tan[0], v.tan[1], v.tan[2]);
            t = Vector3::Normalize(t);
            v.tan[0] = t.x; v.tan[1] = t.y; v.tan[2] = t.z;
        }
    }
}
