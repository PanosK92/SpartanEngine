/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "Spartan.h"
#include "Terrain.h"
#include "Renderable.h"
#include "..\Entity.h"
#include "..\..\RHI\RHI_Texture2D.h"
#include "..\..\RHI\RHI_Vertex.h"
#include "..\..\Rendering\Model.h"
#include "..\..\IO\FileStream.h"
#include "..\..\Resource\ResourceCache.h"
#include "..\..\Rendering\Mesh.h"
#include "..\..\Threading\Threading.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Terrain::Terrain(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {
        
    }

    void Terrain::OnInitialize()
    {
        
    }

    void Terrain::Serialize(FileStream* stream)
    {
        const string no_path;

        stream->Write(m_height_map ? m_height_map->GetResourceFilePathNative() : no_path);
        stream->Write(m_model ? m_model->GetResourceName() : no_path);
        stream->Write(m_min_y);
        stream->Write(m_max_y);
    }

    void Terrain::Deserialize(FileStream* stream)
    {
        ResourceCache* resource_cache = m_context->GetSubsystem<ResourceCache>();
        m_height_map    = resource_cache->GetByPath<RHI_Texture2D>(stream->ReadAs<string>());
        m_model         = resource_cache->GetByName<Model>(stream->ReadAs<string>());
        stream->Read(&m_min_y);
        stream->Read(&m_max_y);

        UpdateFromModel(m_model);
    }

    void Terrain::SetHeightMap(const shared_ptr<RHI_Texture2D>& height_map)
    {
        // In order for the component to guarantee serialization/deserialization, we cache the height_map
        m_height_map = m_context->GetSubsystem<ResourceCache>()->Cache<RHI_Texture2D>(height_map);
    }

    void Terrain::GenerateAsync()
    {
        if (m_is_generating)
        {
            LOG_WARNING("Terrain is already being generated, please wait...");
            return;
        }

        if (!m_height_map)
        {
            LOG_WARNING("You need to assign a height map before trying to generate a terrain.");

            m_context->GetSubsystem<ResourceCache>()->Remove(m_model);
            m_model.reset();
            if (Renderable* renderable = m_entity->AddComponent<Renderable>())
            {
                renderable->GeometryClear();
            }
            
            return;
        }

        m_context->GetSubsystem<Threading>()->AddTask([this]()
        {
            m_is_generating = true;

            // Get height map data
            const vector<std::byte> height_map_data = m_height_map->GetOrLoadMip(0);
            if (height_map_data.empty())
            {
                LOG_ERROR("Height map has no data");
            }

            // Deduce some stuff
            m_height                            = m_height_map->GetHeight();
            m_width                             = m_height_map->GetWidth();
            m_vertex_count                      = m_height * m_width;
            m_face_count                        = (m_height - 1) * (m_width - 1) * 2;
            m_progress_jobs_done                = 0;
            m_progress_job_count                = m_vertex_count * 2 + m_face_count + m_vertex_count * m_face_count;

            // Pre-allocate memory for the calculations that follow
            vector<Vector3> positions                 = vector<Vector3>(m_height * m_width);
            vector<RHI_Vertex_PosTexNorTan> vertices  = vector<RHI_Vertex_PosTexNorTan>(m_vertex_count);
            vector<uint32_t> indices                  = vector<uint32_t>(m_face_count * 3);

            // Read height map and construct positions
            m_progress_desc = "Generating positions...";
            if (GeneratePositions(positions, height_map_data))
            {
                // Compute the vertices (without the normals) and the indices
                m_progress_desc = "Generating terrain vertices and indices...";
                if (GenerateVerticesIndices(positions, indices, vertices))
                {
                    m_progress_desc = "Generating normals and tangents...";
                    positions.clear();
                    positions.shrink_to_fit();

                    // Compute the normals by doing normal averaging (very expensive)
                    if (GenerateNormalTangents(indices, vertices))
                    {
                        // Create a model and set it to the renderable component
                        UpdateFromVertices(indices, vertices);
                    }
                }
            }

            // Clear progress stats
            m_progress_jobs_done = 0;
            m_progress_job_count = 1;
            m_progress_desc.clear();

            m_is_generating = false;
        });
    }

    bool Terrain::GeneratePositions(vector<Vector3>& positions, const vector<std::byte>& height_map)
    {
        if (height_map.empty())
        {
            LOG_ERROR("Height map is empty");
            return false;
        }

        uint32_t index  = 0;
        uint32_t k      = 0;

        for (uint32_t y = 0; y < m_height; y++)
        {
            for (uint32_t x = 0; x < m_width; x++)
            {
                // Read height and scale it to a [0, 1] range
                const float height = (static_cast<float>(height_map[k]) / 255.0f);

                // Construct position
                const uint32_t index  = y * m_width + x;
                positions[index].x    = static_cast<float>(x) - m_width * 0.5f;     // center on the X axis
                positions[index].z    = static_cast<float>(y) - m_height * 0.5f;    // center on the Z axis
                positions[index].y    = Helper::Lerp(m_min_y, m_max_y, height);

                k += 4;

                // track progress
                m_progress_jobs_done++;
            }
        }

        return true;
    }

    bool Terrain::GenerateVerticesIndices(const vector<Vector3>& positions, vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        if (positions.empty())
        {
            LOG_ERROR("Positions are empty");
            return false;
        }

        uint32_t index      = 0;
        uint32_t k          = 0;
        uint32_t u_index    = 0;
        uint32_t v_index    = 0;

        for (uint32_t y = 0; y < m_height - 1; y++)
        {
            for (uint32_t x = 0; x < m_width - 1; x++)
            {
                const uint32_t index_bottom_left  = y * m_width + x;
                const uint32_t index_bottom_right = y * m_width + x + 1;
                const uint32_t index_top_left     = (y + 1) * m_width + x;
                const uint32_t index_top_right    = (y + 1) * m_width + x + 1;

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

                // track progress
                m_progress_jobs_done++;
            }
            u_index = 0;
            v_index++;
        }

        return true;
    }

    bool Terrain::GenerateNormalTangents(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        if (indices.empty())
        {
            LOG_ERROR("Indices are empty");
            return false;
        }

        if (vertices.empty())
        {
            LOG_ERROR("Vertices are empty");
            return false;
        }

        // Normals are computed by normal averaging, which can be crazy slow
        uint32_t face_count     = static_cast<uint32_t>(indices.size()) / 3;
        uint32_t vertex_count   = static_cast<uint32_t>(vertices.size());

        // Compute face normals and tangents
        vector<Vector3> face_normals(face_count);
        vector<Vector3> face_tangents(face_count);
        {
            for (uint32_t i = 0; i < face_count; ++i)
            {
                Vector3 edge_a;
                Vector3 edge_b;

                // Normal
                {
                    // Get the vector describing one edge of our triangle (edge 0, 1)
                    edge_a = Vector3(
                        vertices[indices[(i * 3)]].pos[0] - vertices[indices[(i * 3) + 1]].pos[0],
                        vertices[indices[(i * 3)]].pos[1] - vertices[indices[(i * 3) + 1]].pos[1],
                        vertices[indices[(i * 3)]].pos[2] - vertices[indices[(i * 3) + 1]].pos[2]
                    );

                    // Get the vector describing another edge of our triangle (edge 2,1)
                    edge_b = Vector3(
                        vertices[indices[(i * 3) + 1]].pos[0] - vertices[indices[(i * 3) + 2]].pos[0],
                        vertices[indices[(i * 3) + 1]].pos[1] - vertices[indices[(i * 3) + 2]].pos[1],
                        vertices[indices[(i * 3) + 1]].pos[2] - vertices[indices[(i * 3) + 2]].pos[2]
                    );

                    // Cross multiply the two edge vectors to get the unnormalized face normal
                    face_normals[i] = Vector3::Cross(edge_a, edge_b);
                }

                // Tangent
                {
                    // find first texture coordinate edge 2d vector
                    const float tcU1 = vertices[indices[(i * 3)]].tex[0] - vertices[indices[(i * 3) + 1]].tex[0];
                    const float tcV1 = vertices[indices[(i * 3)]].tex[1] - vertices[indices[(i * 3) + 1]].tex[1];

                    // find second texture coordinate edge 2d vector
                    const float tcU2 = vertices[indices[(i * 3) + 1]].tex[0] - vertices[indices[(i * 3) + 2]].tex[0];
                    const float tcV2 = vertices[indices[(i * 3) + 1]].tex[1] - vertices[indices[(i * 3) + 2]].tex[1];

                    // find tangent using both tex coord edges and position edges
                    face_tangents[i].x = (tcV1 * edge_a.x - tcV2 * edge_b.x * (1.0f / (tcU1 * tcV2 - tcU2 * tcV1)));
                    face_tangents[i].y = (tcV1 * edge_a.y - tcV2 * edge_b.y * (1.0f / (tcU1 * tcV2 - tcU2 * tcV1)));
                    face_tangents[i].z = (tcV1 * edge_a.z - tcV2 * edge_b.z * (1.0f / (tcU1 * tcV2 - tcU2 * tcV1)));
                }

                // track progress
                m_progress_jobs_done++;
            }
        }

        
        // Compute vertex normals and tangents (normals averaging) - This is very expensive show we split it into multiple threads below
        const auto compute_vertex_normals_tangents = [this, &face_normals , &face_tangents, &vertices, &indices, &vertex_count, &face_count](uint32_t i_start, uint32_t i_end)
        {
            Vector3 normal_sum  = Vector3::Zero;
            Vector3 tangent_sum = Vector3::Zero;
            float faces_using   = 0;

            for (uint32_t i = i_start; i < i_end; ++i)
            {
                // Check which triangles use this vertex
                for (uint32_t j = 0; j < face_count; ++j)
                {
                    if (indices[j * 3] == i || indices[(j * 3) + 1] == i || indices[(j * 3) + 2] == i)
                    {
                        // If a face is using the vertex, accumulate the face normal/tangent
                        normal_sum  += face_normals[j];
                        tangent_sum += face_tangents[j];
                        faces_using++;
                    }

                    // Track progress but only every time the inner loop (face_count) finished.
                    // This is because different threads increment the progress tracker which is also an atomic so it locks and unlocks.
                    if (j == face_count < 1)
                    {
                        m_progress_jobs_done += face_count;
                    }
                }

                // Compute actual normal
                normal_sum /= faces_using;
                normal_sum.Normalize();

                // Compute actual tangent
                tangent_sum /= faces_using;
                tangent_sum.Normalize();

                // Write normal to vertex
                vertices[i].nor[0] = normal_sum.x;
                vertices[i].nor[1] = normal_sum.y;
                vertices[i].nor[2] = normal_sum.z;

                // Write tangent to vertex
                vertices[i].tan[0] = tangent_sum.x;
                vertices[i].tan[1] = tangent_sum.y;
                vertices[i].tan[2] = tangent_sum.z;

                // Reset
                normal_sum  = Vector3::Zero;
                tangent_sum = Vector3::Zero;
                faces_using = 0;
            }
        };

        m_context->GetSubsystem<Threading>()->AddTaskLoop(compute_vertex_normals_tangents, vertex_count);

        return true;
    }

    void Terrain::UpdateFromModel(const shared_ptr<Model>& model) const
    {
        if (!model)
        {
            LOG_ERROR_INVALID_PARAMETER();
            return;
        }

        if (Renderable* renderable = m_entity->AddComponent<Renderable>())
        {
            renderable->GeometrySet(
                "Terrain",
                0,                                  // index offset
                model->GetMesh()->Indices_Count(),  // index count
                0,                                  // vertex offset
                model->GetMesh()->Vertices_Count(), // vertex count
                model->GetAabb(),
                model.get()
            );

            renderable->UseDefaultMaterial();
        }
    }

    void Terrain::UpdateFromVertices(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        // Add vertices and indices into a model struct (and cache that)
        if (!m_model)
        {
            // Create new model
            m_model = make_shared<Model>(m_context);

            // Set geometry
            m_model->AppendGeometry(indices, vertices);
            m_model->UpdateGeometry();

            // Set a file path so the model can be used by the resource cache
            ResourceCache* resource_cache = m_context->GetSubsystem<ResourceCache>();
            m_model->SetResourceFilePath(resource_cache->GetProjectDirectory() + m_entity->GetName() + "_terrain_" + to_string(m_id) + string(EXTENSION_MODEL));
            m_model = resource_cache->Cache(m_model);
        }
        else
        {
            // Update with new geometry
            m_model->Clear();
            m_model->AppendGeometry(indices, vertices);
            m_model->UpdateGeometry();
        }

        UpdateFromModel(m_model);
    }
}
