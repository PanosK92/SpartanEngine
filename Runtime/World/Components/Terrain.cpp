/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =====================
#include "Terrain.h"
#include "..\..\RHI\RHI_Texture.h"
#include "..\..\Logging\Log.h"
#include "..\..\Math\Vector3.h"
#include "..\..\Math\MathHelper.h"
#include "..\..\RHI\RHI_Vertex.h"
#include "..\..\Rendering\Model.h"
#include "..\..\IO\FileStream.h"
#include "Renderable.h"
#include "..\Entity.h"
//================================

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
        stream->Write(m_indices);
        stream->Write(m_vertices);

        FreeMemory();
    }

    void Terrain::Deserialize(FileStream* stream)
    {
        stream->Read(&m_indices);
        stream->Read(&m_vertices);

        UpdateModel(m_indices, m_vertices);
        FreeMemory();
    }

    void Terrain::Generate()
    {
        ClearGeometry();

        if (!m_height_map)
            return;

        // Get height map data
        vector<std::byte> data  = m_height_map->GetMipmap(0);
        m_height                = m_height_map->GetHeight();
        m_width                 = m_height_map->GetWidth();
        uint32_t bpp            = m_height_map->GetBpp();

        // Read height map
        vector<Vector3> points(m_height * m_width);
        uint32_t index  = 0;
        uint32_t k      = 0;
        for (uint32_t y = 0; y < m_height; y++)
        {
            for (uint32_t x = 0; x < m_width; x++)
            {
                // Read height and scale it to [0,1]
                float height = (static_cast<float>(data[k]) / 255.0f);

                // Construct position
                uint32_t index = y * m_width + x;
                points[index].x = static_cast<float>(x) - m_width * 0.5f;   // center it by offsetting to the left
                points[index].z = static_cast<float>(y) - m_height * 0.5f;  // center it by offsetting backwards
                points[index].y = Lerp(m_min_z, m_max_z, height) / m_smoothness;

                k += 4;
            }
        }
      
        uint32_t vertex_count   = m_height * m_width;
        uint32_t face_count     = (m_height - 1) * (m_width - 1) * 2;

        // Pre-allocate memory in advance
        m_vertices  = vector<RHI_Vertex_PosTexNorTan>(vertex_count);
        m_indices   = vector<uint32_t>(face_count * 3);

        // Create the vertices and the indices of the grid
        index               = 0;
        k                   = 0;
        uint32_t u_index    = 0;
        uint32_t v_index    = 0;
        {
            for (uint32_t y = 0; y < m_height - 1; y++)
            {
                for (uint32_t x = 0; x < m_width - 1; x++)
                {
                    uint32_t index_bottom_left  = y * m_width + x;
                    uint32_t index_bottom_right = y * m_width + x + 1;
                    uint32_t index_top_left     = (y + 1) * m_width + x;
                    uint32_t index_top_right    = (y + 1) * m_width + x + 1;

                    // Bottom right of quad
                    index               = index_bottom_right;
                    m_indices[k]        = index;
                    m_vertices[index]   = RHI_Vertex_PosTexNorTan(points[index], Vector2(u_index + 1.0f, v_index + 1.0f));

                    // Bottom left of quad
                    index               = index_bottom_left;
                    m_indices[k + 1]    = index;
                    m_vertices[index]   = RHI_Vertex_PosTexNorTan(points[index], Vector2(u_index + 0.0f, v_index + 1.0f));

                    // Top left of quad
                    index               = index_top_left;
                    m_indices[k + 2]    = index;
                    m_vertices[index]   = RHI_Vertex_PosTexNorTan(points[index], Vector2(u_index + 0.0f, v_index + 0.0f));

                    // Bottom right of quad
                    index               = index_bottom_right;
                    m_indices[k + 3]    = index;
                    m_vertices[index]   = RHI_Vertex_PosTexNorTan(points[index], Vector2(u_index + 1.0f, v_index + 1.0f));

                    // Top left of quad
                    index               = index_top_left;
                    m_indices[k + 4]    = index;
                    m_vertices[index]   = RHI_Vertex_PosTexNorTan(points[index], Vector2(u_index + 0.0f, v_index + 0.0f));

                    // Top right of quad
                    index               = index_top_right;
                    m_indices[k + 5]    = index;
                    m_vertices[index]   = RHI_Vertex_PosTexNorTan(points[index], Vector2(u_index + 1.0f, v_index + 0.0f));

                    k += 6; // next quad

                    u_index++;
                }
                u_index = 0;
                v_index++;
            }
        }

        ComputeNormals(m_indices, m_vertices);
        UpdateModel(m_indices, m_vertices);
    }

    void Terrain::ComputeNormals(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        // Normals are computed by normal averaging, which can be crazy slow

        uint32_t face_count     = static_cast<uint32_t>(indices.size()) / 3;
        uint32_t vertex_count   = static_cast<uint32_t>(vertices.size());

        // Compute face normals
        vector<Vector3> temp_normals(face_count);
        {
            for (uint32_t i = 0; i < face_count; ++i)
            {
                // Get the vector describing one edge of our triangle (edge 0,2)
                Vector3 edge1 = Vector3(
                    vertices[indices[(i * 3)]].pos[0] - vertices[indices[(i * 3) + 2]].pos[0],
                    vertices[indices[(i * 3)]].pos[1] - vertices[indices[(i * 3) + 2]].pos[1],
                    vertices[indices[(i * 3)]].pos[2] - vertices[indices[(i * 3) + 2]].pos[2]
                );

                // Get the vector describing another edge of our triangle (edge 2,1)
                Vector3 edge2 = Vector3(
                    vertices[indices[(i * 3) + 2]].pos[0] - vertices[indices[(i * 3) + 1]].pos[0],
                    vertices[indices[(i * 3) + 2]].pos[1] - vertices[indices[(i * 3) + 1]].pos[1],
                    vertices[indices[(i * 3) + 2]].pos[2] - vertices[indices[(i * 3) + 1]].pos[2]
                );

                // Cross multiply the two edge vectors to get the unnormalized face normal
                temp_normals[i] = Vector3::Cross(edge1, edge2);
            }
        }

        // Compute vertex normals (normal averaging)
        {
            Vector3 normal_sum = Vector3::Zero;
            float faces_using = 0;

            // Go through each vertex
            for (uint32_t i = 0; i < vertex_count; ++i)
            {
                // Check which triangles use this vertex
                for (uint32_t j = 0; j < face_count; ++j)
                {
                    if (indices[j * 3] == i ||
                        indices[(j * 3) + 1] == i ||
                        indices[(j * 3) + 2] == i)
                    {
                        float tX = normal_sum.x + temp_normals[j].x;
                        float tY = normal_sum.y + temp_normals[j].y;
                        float tZ = normal_sum.z + temp_normals[j].z;

                        normal_sum = Vector3(tX, tY, tZ); // If a face is using the vertex, add the unnormalized face normal to the normal_sum
                        faces_using++;
                    }
                }

                // Get the actual normal by dividing the normal_sum by the number of faces sharing the vertex
                normal_sum = normal_sum / faces_using;

                // Normalize the normalSum vector
                normal_sum = normal_sum.Normalized();

                // Normal
                vertices[i].nor[0] = normal_sum.x;
                vertices[i].nor[1] = normal_sum.y;
                vertices[i].nor[2] = normal_sum.z;

                // Tangent
                Vector3 tangent1    = Vector3::Cross(normal_sum, Vector3::Forward);
                Vector3 tangent2    = Vector3::Cross(normal_sum, Vector3::Up);
                Vector3 tangent     = tangent1.LengthSquared() > tangent2.LengthSquared() ? tangent1 : tangent2;
                vertices[i].tan[0]  = tangent.x;
                vertices[i].tan[1]  = tangent.y;
                vertices[i].tan[2]  = tangent.z;

                // Clear normalSum and faces_using for next vertex
                normal_sum  = Vector3::Zero;
                faces_using = 0.0f;
            }
        }
    }

    void Terrain::UpdateModel(const vector<uint32_t>& indices, vector<RHI_Vertex_PosTexNorTan>& vertices)
    {
        if (indices.empty() || vertices.empty())
        {
            m_model.reset();
            if (shared_ptr<Renderable>& renderable = m_entity->AddComponent<Renderable>())
            {
                renderable->GeometryClear();
            }
            return;
        }

        // Create model
        m_model = make_shared<Model>(m_context);
        m_model->GeometryAppend(indices, vertices);
        m_model->GeometryUpdate();

        // Add renderable and pass the model to it
        shared_ptr<Renderable>& renderable = m_entity->AddComponent<Renderable>();
        renderable->GeometrySet(
            "Terrain",
            0,                                      // index offset
            static_cast<uint32_t>(indices.size()),  // index count
            0,                                      // vertex offset
            static_cast<uint32_t>(indices.size()),  // vertex count
            BoundingBox(vertices),
            m_model.get()
        );
        renderable->UseDefaultMaterial();
    }

    void Terrain::ClearGeometry()
    {
        FreeMemory();
        UpdateModel(m_indices, m_vertices);
    }

    void Terrain::FreeMemory()
    {
        // Free memory
        m_indices.clear();
        m_indices.shrink_to_fit();
        m_vertices.clear();
        m_vertices.shrink_to_fit();
    }
}
