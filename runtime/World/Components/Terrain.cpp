/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ============================
#include "pch.h"
#include "Terrain.h"
#include "Renderable.h"
#include "../Entity.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Mesh.h"
#include "../../Core/ThreadPool.h"
#include "ProgressTracker.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static void generate_positions(vector<Vector3>& positions, const vector<std::byte>& height_map, const uint32_t width, const uint32_t height, float min_x, float max_y)
    {
        SP_ASSERT_MSG(!height_map.empty(), "Height map is empty");

        uint32_t index = 0;
        uint32_t k     = 0;

        for (uint32_t y = 0; y < height; y++)
        {
            for (uint32_t x = 0; x < width; x++)
            {
                // Read height and scale it to a [0, 1] range
                const float height = (static_cast<float>(height_map[k]) / 255.0f);

                // Construct position
                const uint32_t index = y * width + x;
                positions[index].x = static_cast<float>(x) - width * 0.5f;  // center on the X axis
                positions[index].z = static_cast<float>(y) - height * 0.5f; // center on the Z axis
                positions[index].y = Helper::Lerp(min_x, max_y, height);

                k += 4;
            }
        }
    }

    static void generate_vertices_and_indices(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const vector<Vector3>& positions, const uint32_t width, const uint32_t height)
    {
        SP_ASSERT_MSG(!positions.empty(), "Positions are empty");

        uint32_t index   = 0;
        uint32_t k       = 0;
        uint32_t u_index = 0;
        uint32_t v_index = 0;

        for (uint32_t y = 0; y < height - 1; y++)
        {
            for (uint32_t x = 0; x < width - 1; x++)
            {
                const uint32_t index_bottom_left  = y * width + x;
                const uint32_t index_bottom_right = y * width + x + 1;
                const uint32_t index_top_left     = (y + 1) * width + x;
                const uint32_t index_top_right    = (y + 1) * width + x + 1;

                // Bottom right of quad
                index           = index_bottom_right;
                indices[k]      = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u_index + 1.0f, v_index + 1.0f));

                // Bottom left of quad
                index           = index_bottom_left;
                indices[k + 1]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u_index + 0.0f, v_index + 1.0f));

                // Top left of quad
                index           = index_top_left;
                indices[k + 2]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u_index + 0.0f, v_index + 0.0f));

                // Bottom right of quad
                index           = index_bottom_right;
                indices[k + 3]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u_index + 1.0f, v_index + 1.0f));

                // Top left of quad
                index           = index_top_left;
                indices[k + 4]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u_index + 0.0f, v_index + 0.0f));

                // Top right of quad
                index           = index_top_right;
                indices[k + 5]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u_index + 1.0f, v_index + 0.0f));

                k += 6; // next quad

                u_index++;
            }

            u_index = 0;
            v_index++;
        }
    }

    static void generate_normals_and_tangents(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        SP_ASSERT_MSG(!indices.empty(),  "Indices are empty");
        SP_ASSERT_MSG(!vertices.empty(), "Vertices are empty");

        // Pre-allocate everything needed by the loop
        uint32_t triangle_count = static_cast<uint32_t>(indices.size()) / 3;
        vector<Vector3> face_normals(triangle_count);
        face_normals.reserve(triangle_count);
        vector<Vector3> face_tangents(triangle_count);
        face_tangents.reserve(triangle_count);
        Vector3 edge_a;
        Vector3 edge_b;

        // Compute the normal and tangent for each face
        for (uint32_t i = 0; i < triangle_count; ++i)
        {
            // Normal
            {
                // Get the vector describing one edge of our triangle (edge 0, 1)
                edge_a.x = vertices[indices[(i * 3)]].pos[0] - vertices[indices[(i * 3) + 1]].pos[0];
                edge_a.y = vertices[indices[(i * 3)]].pos[1] - vertices[indices[(i * 3) + 1]].pos[1];
                edge_a.z = vertices[indices[(i * 3)]].pos[2] - vertices[indices[(i * 3) + 1]].pos[2];

                // Get the vector describing another edge of our triangle (edge 2,1)
                edge_b.x = vertices[indices[(i * 3) + 1]].pos[0] - vertices[indices[(i * 3) + 2]].pos[0];
                edge_b.y = vertices[indices[(i * 3) + 1]].pos[1] - vertices[indices[(i * 3) + 2]].pos[1];
                edge_b.z = vertices[indices[(i * 3) + 1]].pos[2] - vertices[indices[(i * 3) + 2]].pos[2];

                // Cross multiply the two edge vectors to get the unnormalized face normal
                face_normals[i] = Vector3::Cross(edge_a, edge_b);
            }

            // Tangent
            {
                // find first texture coordinate edge 2d vector
                const float tc_u1 = vertices[indices[(i * 3)]].tex[0] - vertices[indices[(i * 3) + 1]].tex[0];
                const float tc_v1 = vertices[indices[(i * 3)]].tex[1] - vertices[indices[(i * 3) + 1]].tex[1];

                // find second texture coordinate edge 2d vector
                const float tc_u2 = vertices[indices[(i * 3) + 1]].tex[0] - vertices[indices[(i * 3) + 2]].tex[0];
                const float tc_v2 = vertices[indices[(i * 3) + 1]].tex[1] - vertices[indices[(i * 3) + 2]].tex[1];

                // find tangent using both tex coord edges and position edges
                face_tangents[i].x = (tc_v1 * edge_a.x - tc_v2 * edge_b.x * (1.0f / (tc_u1 * tc_v2 - tc_u2 * tc_v1)));
                face_tangents[i].y = (tc_v1 * edge_a.y - tc_v2 * edge_b.y * (1.0f / (tc_u1 * tc_v2 - tc_u2 * tc_v1)));
                face_tangents[i].z = (tc_v1 * edge_a.z - tc_v2 * edge_b.z * (1.0f / (tc_u1 * tc_v2 - tc_u2 * tc_v1)));
            }
        }

        // Compute vertex normals and tangents (normals averaging) - This is very expensive show we split it into multiple threads below
        const auto compute_vertex_normals_tangents = [&vertices, &indices, &face_normals , &face_tangents, triangle_count](uint32_t start_index, uint32_t range)
        {
            uint32_t product = 0;
            uint32_t index_0 = 0;
            uint32_t index_1 = 0;
            uint32_t index_2 = 0;

            for (uint32_t i = start_index; i < range; i++)
            {
                Vector3 normal_average  = Vector3::Zero;
                Vector3 tangent_average = Vector3::Zero;
                float face_usage_count  = 0;

                // Check which triangles use this vertex
                for (uint32_t j = 0; j < triangle_count; ++j)
                {
                    //= MOST EXPENSIVE PART ============
                    // The cost comes mainly from misses
                    product = (j << 1) + j; // j * 3;
                    index_0 = indices[product];
                    index_1 = indices[product + 1];
                    index_2 = indices[product + 2];
                    //==================================

                    if (index_0 == i || index_1 == i || index_2 == i)
                    {
                        // If a face is using the vertex, accumulate the face normal/tangent
                        normal_average  += face_normals[j];
                        tangent_average += face_tangents[j];

                        face_usage_count++;
                    }
                }

                // Compute actual normal
                normal_average /= face_usage_count;
                normal_average.Normalize();

                // Compute actual tangent
                tangent_average /= face_usage_count;
                tangent_average.Normalize();

                // Write normal to vertex
                vertices[i].nor[0] = normal_average.x;
                vertices[i].nor[1] = normal_average.y;
                vertices[i].nor[2] = normal_average.z;

                // Write tangent to vertex
                vertices[i].tan[0] = tangent_average.x;
                vertices[i].tan[1] = tangent_average.y;
                vertices[i].tan[2] = tangent_average.z;

                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
        };

        uint32_t vertex_count = static_cast<uint32_t>(vertices.size());
        ThreadPool::ParallelLoop(compute_vertex_normals_tangents, vertex_count);
    }

    Terrain::Terrain(weak_ptr<Entity> entity) : IComponent(entity)
    {

    }

    void Terrain::Serialize(FileStream* stream)
    {
        const string no_path;

        stream->Write(m_height_map ? m_height_map->GetResourceFilePathNative() : no_path);
        stream->Write(m_mesh ? m_mesh->GetObjectName() : no_path);
        stream->Write(m_min_y);
        stream->Write(m_max_y);
    }

    void Terrain::Deserialize(FileStream* stream)
    {
        m_height_map = ResourceCache::GetByPath<RHI_Texture2D>(stream->ReadAs<string>());
        m_mesh       = ResourceCache::GetByName<Mesh>(stream->ReadAs<string>());
        stream->Read(&m_min_y);
        stream->Read(&m_max_y);

        UpdateFromMesh(m_mesh);
    }

    void Terrain::SetHeightMap(const shared_ptr<RHI_Texture>& height_map)
    {
        m_height_map = ResourceCache::Cache<RHI_Texture>(height_map);
    }

    void Terrain::GenerateAsync()
    {
        if (m_is_generating)
        {
            SP_LOG_WARNING("Terrain is already being generated, please wait...");
            return;
        }

        if (!m_height_map)
        {
            SP_LOG_WARNING("You need to assign a height map before trying to generate a terrain.");

            ResourceCache::Remove(m_mesh);
            m_mesh = nullptr;
            if (Renderable* renderable = m_entity_ptr->AddComponent<Renderable>())
            {
                renderable->Clear();
            }
            
            return;
        }

        ThreadPool::AddTask([this]()
        {
            m_is_generating = true;

            // Get height map data
            vector<std::byte> height_data;
            {
                height_data = m_height_map->GetMip(0, 0).bytes;

                // If not the data is not there, load it
                if (height_data.empty())
                {
                    if (m_height_map->LoadFromFile(m_height_map->GetResourceFilePath()))
                    {
                        height_data = m_height_map->GetMip(0, 0).bytes;

                        if (height_data.empty())
                        {
                            SP_LOG_ERROR("Failed to load height map");
                            m_is_generating = false;
                            return;
                        }
                    }
                }
            }

            // Deduce some stuff
            uint32_t width   = m_height_map->GetWidth();
            uint32_t height  = m_height_map->GetHeight();
            m_height_samples = width * height;
            m_vertex_count   = m_height_samples;
            m_index_count    = m_vertex_count * 6;
            m_triangle_count = m_index_count / 3;

            uint32_t job_count =
                1 +              // 1. generate_positions()
                1 +              // 2. generate_vertices_and_indices()
                m_vertex_count + // 3. generate_normals_and_tangents()
                1;               // 4. create mesh

            // Star progress tracking
            ProgressTracker::GetProgress(ProgressType::Terrain).Start(job_count, "Generating terrain...");

            // Pre-allocate memory for the calculations that follow
            vector<Vector3> positions(m_height_samples);
            positions.reserve(m_height_samples);
            vector<RHI_Vertex_PosTexNorTan> vertices(m_vertex_count);
            vertices.reserve(m_vertex_count);
            vector<uint32_t> indices(m_index_count);
            indices.reserve(m_index_count);

            // 1. Generate positions by reading the height map
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Generating positions...");
            generate_positions(positions, height_data, width, height, m_min_y, m_max_y);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 2. Compute vertices and indices
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Generating vertices and indices...");
            generate_vertices_and_indices(vertices, indices, positions, width, height);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            // 3. Compute normals and tangents
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Generating normals and tangents...");
            generate_normals_and_tangents(indices, vertices);
            // Jobs done are tracked internally here because this is the most expensive function

            // 4. Create mesh
            ProgressTracker::GetProgress(ProgressType::Terrain).SetText("Creating mesh...");
            UpdateFromVertices(indices, vertices);
            ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();

            m_is_generating = false;
        });
    }

    void Terrain::UpdateFromMesh(const shared_ptr<Mesh> mesh) const
    {
        if (Renderable* renderable = m_entity_ptr->AddComponent<Renderable>())
        {
            renderable->SetGeometry(
                "Terrain",
                0,                      // index offset
                mesh->GetIndexCount(),  // index count
                0,                      // vertex offset
                mesh->GetVertexCount(), // vertex count
                mesh->GetAabb(),
                mesh
            );

            renderable->SetDefaultMaterial();
        }
    }

    void Terrain::UpdateFromVertices(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        // Add vertices and indices into a mesh struct (and cache that)
        if (!m_mesh)
        {
            // Create new model
            m_mesh = make_shared<Mesh>();

            // Set geometry
            m_mesh->AddIndices(indices);
            m_mesh->AddVertices(vertices);
            m_mesh->CreateGpuBuffers();
            m_mesh->ComputeNormalizedScale();
            m_mesh->ComputeAabb();

            // Set a file path so the model can be used by the resource cache
            m_mesh->SetResourceFilePath(ResourceCache::GetProjectDirectory() + m_entity_ptr->GetObjectName() + "_terrain_" + to_string(m_object_id) + string(EXTENSION_MODEL));
            m_mesh = ResourceCache::Cache(m_mesh);
        }
        else
        {
            // Update with new geometry
            m_mesh->Clear();
            m_mesh->AddIndices(indices);
            m_mesh->AddVertices(vertices);
            m_mesh->CreateGpuBuffers();
            m_mesh->ComputeNormalizedScale();
            m_mesh->ComputeAabb();
        }

        UpdateFromMesh(m_mesh);
    }
}
