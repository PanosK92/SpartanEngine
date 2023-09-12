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
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Mesh.h"
#include "../../Core/ThreadPool.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        uint32_t tessellation_factor = 1;
    }

    static void generate_positions(vector<Vector3>& positions, const vector<byte>& height_map, uint32_t width, uint32_t height, float min_x, float max_y)
    {
        SP_ASSERT_MSG(!height_map.empty(), "Height map is empty");

        uint32_t new_width = (width - 1) * tessellation_factor + 1;
        uint32_t new_height = (height - 1) * tessellation_factor + 1;

        positions.resize(new_width * new_height);

        for (uint32_t y = 0; y < new_height; ++y)
        {
            for (uint32_t x = 0; x < new_width; ++x)
            {
                uint32_t orig_x = x / tessellation_factor;
                uint32_t orig_y = y / tessellation_factor;

                uint32_t next_x = std::min(orig_x + 1, width - 1);
                uint32_t next_y = std::min(orig_y + 1, height - 1);

                float lerp_x = (x % tessellation_factor) / static_cast<float>(tessellation_factor);
                float lerp_y = (y % tessellation_factor) / static_cast<float>(tessellation_factor);

                float height1 = static_cast<float>(height_map[(orig_y * width + orig_x) * 4]) / 255.0f;
                float height2 = static_cast<float>(height_map[(orig_y * width + next_x) * 4]) / 255.0f;
                float height3 = static_cast<float>(height_map[(next_y * width + orig_x) * 4]) / 255.0f;
                float height4 = static_cast<float>(height_map[(next_y * width + next_x) * 4]) / 255.0f;

                float height_x1 = Helper::Lerp(height1, height2, lerp_x);
                float height_x2 = Helper::Lerp(height3, height4, lerp_x);

                float final_height = Helper::Lerp(height_x1, height_x2, lerp_y);

                uint32_t index     = y * new_width + x;
                positions[index].x = x - new_width * 0.5f;
                positions[index].z = y - new_height * 0.5f;
                positions[index].y = Helper::Lerp(min_x, max_y, final_height);
            }
        }
    }

    static void generate_vertices_and_indices(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const vector<Vector3>& positions, const uint32_t width, const uint32_t height)
    {
        SP_ASSERT_MSG(!positions.empty(), "Positions are empty");
    
        uint32_t index = 0;
        uint32_t k = 0;
    
        for (uint32_t y = 0; y < height - 1; y++)
        {
            for (uint32_t x = 0; x < width - 1; x++)
            {
                float u = static_cast<float>(x) / static_cast<float>(width - 1);
                float v = static_cast<float>(y) / static_cast<float>(height - 1);
    
                const uint32_t index_bottom_left  = y * width + x;
                const uint32_t index_bottom_right = y * width + x + 1;
                const uint32_t index_top_left     = (y + 1) * width + x;
                const uint32_t index_top_right    = (y + 1) * width + x + 1;
    
                // Bottom right of quad
                index           = index_bottom_right;
                indices[k]      = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u + 1.0f / (width - 1), v + 1.0f / (height - 1)));
    
                // Bottom left of quad
                index           = index_bottom_left;
                indices[k + 1]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v + 1.0f / (height - 1)));
    
                // Top left of quad
                index           = index_top_left;
                indices[k + 2]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v));
    
                // Bottom right of quad
                index           = index_bottom_right;
                indices[k + 3]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u + 1.0f / (width - 1), v + 1.0f / (height - 1)));
    
                // Top left of quad
                index           = index_top_left;
                indices[k + 4]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v));
    
                // Top right of quad
                index           = index_top_right;
                indices[k + 5]  = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u + 1.0f / (width - 1), v));
    
                k += 6; // next quad
            }
        }
    }

    static void generate_normals_and_tangents(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        SP_ASSERT_MSG(!indices.empty(), "Indices are empty");
        SP_ASSERT_MSG(!vertices.empty(), "Vertices are empty");

        uint32_t triangle_count = static_cast<uint32_t>(indices.size()) / 3;
        vector<Vector3> face_normals(triangle_count);
        vector<Vector3> face_tangents(triangle_count);
        Vector3 edge_a, edge_b;

        unordered_map<uint32_t, vector<uint32_t>> vertex_to_triangle_map;

        for (uint32_t i = 0; i < triangle_count; ++i)
        {
            uint32_t index_a = indices[i * 3];
            uint32_t index_b = indices[i * 3 + 1];
            uint32_t index_c = indices[i * 3 + 2];

            vertex_to_triangle_map[index_a].push_back(i);
            vertex_to_triangle_map[index_b].push_back(i);
            vertex_to_triangle_map[index_c].push_back(i);

            edge_a.x = vertices[index_a].pos[0] - vertices[index_b].pos[0];
            edge_a.y = vertices[index_a].pos[1] - vertices[index_b].pos[1];
            edge_a.z = vertices[index_a].pos[2] - vertices[index_b].pos[2];

            edge_b.x = vertices[index_b].pos[0] - vertices[index_c].pos[0];
            edge_b.y = vertices[index_b].pos[1] - vertices[index_c].pos[1];
            edge_b.z = vertices[index_b].pos[2] - vertices[index_c].pos[2];

            face_normals[i] = Vector3::Cross(edge_a, edge_b);

            const float tc_u1 = vertices[index_a].tex[0] - vertices[index_b].tex[0];
            const float tc_v1 = vertices[index_a].tex[1] - vertices[index_b].tex[1];
            const float tc_u2 = vertices[index_b].tex[0] - vertices[index_c].tex[0];
            const float tc_v2 = vertices[index_b].tex[1] - vertices[index_c].tex[1];

            float coef = 1.0f / (tc_u1 * tc_v2 - tc_u2 * tc_v1);

            face_tangents[i].x = (tc_v1 * edge_a.x - tc_v2 * edge_b.x) * coef;
            face_tangents[i].y = (tc_v1 * edge_a.y - tc_v2 * edge_b.y) * coef;
            face_tangents[i].z = (tc_v1 * edge_a.z - tc_v2 * edge_b.z) * coef;
        }

        const auto compute_vertex_normals_tangents = [&vertices, &vertex_to_triangle_map, &face_normals, &face_tangents](uint32_t start_index, uint32_t range)
        {
            for (uint32_t i = start_index; i < range; i++)
            {
                Vector3 normal_average = Vector3::Zero;
                Vector3 tangent_average = Vector3::Zero;
                float face_usage_count = 0;

                for (uint32_t j : vertex_to_triangle_map[i])
                {
                    normal_average += face_normals[j];
                    tangent_average += face_tangents[j];
                    face_usage_count++;
                }

                normal_average /= face_usage_count;
                tangent_average /= face_usage_count;

                normal_average.Normalize();
                tangent_average.Normalize();

                vertices[i].nor[0] = normal_average.x;
                vertices[i].nor[1] = normal_average.y;
                vertices[i].nor[2] = normal_average.z;

                vertices[i].tan[0] = tangent_average.x;
                vertices[i].tan[1] = tangent_average.y;
                vertices[i].tan[2] = tangent_average.z;

                ProgressTracker::GetProgress(ProgressType::Terrain).JobDone();
            }
        };

        uint32_t vertex_count = static_cast<uint32_t>(vertices.size());
        ThreadPool::ParallelLoop(compute_vertex_normals_tangents, vertex_count);
    }

    Terrain::Terrain(weak_ptr<Entity> entity) : Component(entity)
    {

    }

    Terrain::~Terrain()
    {
        m_height_map = nullptr;
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
            SP_LOG_WARNING("You need to assign a height map before trying to generate a terrain");

            ResourceCache::Remove(m_mesh);
            m_mesh = nullptr;
            if (shared_ptr<Renderable> renderable = m_entity_ptr->AddComponent<Renderable>())
            {
                renderable->SetGeometry(nullptr);
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
        if (shared_ptr<Renderable> renderable = m_entity_ptr->AddComponent<Renderable>())
        {
            renderable->SetGeometry(
                mesh.get(),
                mesh->GetAabb(),
                0,                     // index offset
                mesh->GetIndexCount(), // index count
                0,                     // vertex offset
                mesh->GetVertexCount() // vertex count
            );

            shared_ptr<Material> material = make_shared<Material>();
            material->SetResourceFilePath(string("project\\terrain\\material_terrain") + string(EXTENSION_MATERIAL));
            material->SetTexture(MaterialTexture::Color, "project\\terrain\\flat.jpg");
            material->SetTexture(MaterialTexture::Color2, "project\\terrain\\slope.jpg");
            material->SetProperty(MaterialProperty::IsTerrain, 1.0f);
            material->SetProperty(MaterialProperty::UvTilingX, 20.0f);
            material->SetProperty(MaterialProperty::UvTilingY, 20.0f);

            m_entity_ptr->GetComponent<Renderable>()->SetMaterial(material);
        }
    }

    void Terrain::UpdateFromVertices(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        // add vertices and indices into a mesh struct (and cache that)
        if (!m_mesh)
        {
            // create new model
            m_mesh = make_shared<Mesh>();
            m_mesh->SetObjectName("Terrain");

            // set geometry
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
            // update with new geometry
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
