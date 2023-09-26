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
    static bool load_and_normalize_height_data(vector<float>& height_data_out, shared_ptr<RHI_Texture> height_texture, float min_y, float max_y)
    {
        vector<std::byte> height_data = height_texture->GetMip(0, 0).bytes;

        // if the data is not there, load it
        if (height_data.empty())
        {
            if (height_texture->LoadFromFile(height_texture->GetResourceFilePath()))
            {
                height_data = height_texture->GetMip(0, 0).bytes;

                if (height_data.empty())
                {
                    SP_LOG_ERROR("Failed to load height map");
                    return false;
                }
            }
        }

        // bytes per pixel
        uint32_t bytes_per_pixel = (height_texture->GetChannelCount() * height_texture->GetBitsPerChannel()) / 8;

        // normalize and scale height data
        height_data_out.resize(height_data.size() / bytes_per_pixel);
        for (uint32_t i = 0; i < height_data.size(); i += bytes_per_pixel)
        {
            // assuming the height is stored in the red channel (first channel)
            height_data_out[i / bytes_per_pixel] = min_y + (static_cast<float>(height_data[i]) / 255.0f) * (max_y - min_y);
        }

        return true;
    }

    static void generate_positions(vector<Vector3>& positions, const vector<float>& height_map, const uint32_t width, const uint32_t height)
    {
        SP_ASSERT_MSG(!height_map.empty(), "Height map is empty");

        for (uint32_t y = 0; y < height; y++)
        {
            for (uint32_t x = 0; x < width; x++)
            {
                uint32_t index = y * width + x;

                // center on the X and Z axis
                float centered_x = static_cast<float>(x) - width * 0.5f;
                float centered_z = static_cast<float>(y) - height * 0.5f;

                // get height from height_map
                float height_value = height_map[index]; 

                positions[index] = Vector3(centered_x, height_value, centered_z);
            }
        }
    }

    static void generate_vertices_and_indices(vector<RHI_Vertex_PosTexNorTan>& vertices, vector<uint32_t>& indices, const vector<Vector3>& positions, const uint32_t width, const uint32_t height)
    {
        SP_ASSERT_MSG(!positions.empty(), "Positions are empty");

        Vector3 offset = Vector3::Zero;
        {
            // calculate offsets to center the terrain
            float offset_x   = -static_cast<float>(width) * 0.5f;
            float offset_z   = -static_cast<float>(height) * 0.5f;
            float min_height = FLT_MAX;

            // find the minimum height to align the lower part of the terrain at y = 0
            for (const Vector3& pos : positions)
            {
                if (pos.y < min_height)
                {
                    min_height = pos.y;
                }
            }

            offset = Vector3(offset_x, -min_height, offset_z);
        }

        uint32_t index = 0;
        uint32_t k     = 0;
        for (uint32_t y = 0; y < height - 1; y++)
        {
            for (uint32_t x = 0; x < width - 1; x++)
            {
                Vector3 position = positions[index] + offset;

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
                index = index_bottom_left;
                indices[k + 1] = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v + 1.0f / (height - 1)));

                // Top left of quad
                index = index_top_left;
                indices[k + 2] = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u, v));

                // Bottom right of quad
                index = index_bottom_right;
                indices[k + 3] = index;
                vertices[index] = RHI_Vertex_PosTexNorTan(positions[index], Vector2(u + 1.0f / (width - 1), v + 1.0f / (height - 1)));

                // Top left of quad
                index = index_top_left;
                indices[k + 4] = index;
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
        m_height_texture = nullptr;
    }

    void Terrain::Serialize(FileStream* stream)
    {
        const string no_path;

        stream->Write(m_height_texture ? m_height_texture->GetResourceFilePathNative() : no_path);
        stream->Write(m_mesh ? m_mesh->GetObjectName() : no_path);
        stream->Write(m_min_y);
        stream->Write(m_max_y);
    }

    void Terrain::Deserialize(FileStream* stream)
    {
        m_height_texture = ResourceCache::GetByPath<RHI_Texture2D>(stream->ReadAs<string>());
        m_mesh       = ResourceCache::GetByName<Mesh>(stream->ReadAs<string>());
        stream->Read(&m_min_y);
        stream->Read(&m_max_y);

        UpdateFromMesh(m_mesh);
    }

    void Terrain::SetHeightMap(const shared_ptr<RHI_Texture>& height_map)
    {
        m_height_texture = height_map;
    }

    void Terrain::GenerateAsync(std::function<void()> on_complete)
    {
        if (m_is_generating)
        {
            SP_LOG_WARNING("Terrain is already being generated, please wait...");
            return;
        }

        if (!m_height_texture)
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

        ThreadPool::AddTask([this, on_complete]()
        {
            m_is_generating = true;

            if (!load_and_normalize_height_data(m_height_data, m_height_texture, m_min_y, m_max_y))
            {
                m_is_generating = false;
                return;
            }

            // deduce some stuff
            uint32_t width   = m_height_texture->GetWidth();
            uint32_t height  = m_height_texture->GetHeight();
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
            generate_positions(positions, m_height_data, width, height);
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

            if (on_complete)
            {
                on_complete();
            }

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
            material->SetTexture(MaterialTexture::Color, "project\\terrain\\grass.jpg");
            material->SetTexture(MaterialTexture::Color2, "project\\terrain\\rock.jpg");
            material->SetProperty(MaterialProperty::IsTerrain, 1.0f);
            material->SetProperty(MaterialProperty::UvTilingX, 100.0f);
            material->SetProperty(MaterialProperty::UvTilingY, 100.0f);

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

            // set a file path so the model can be used by the resource cache
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
