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

    bool Terrain::SetHeightMap(const shared_ptr<RHI_Texture>& height_map)
    {
        // Get height map data
        m_height_map            = height_map;
        vector<std::byte> data  = height_map->GetMipmap(0);
        m_height                = height_map->GetHeight();
        m_width                 = height_map->GetWidth();
        uint32_t bpp            = height_map->GetBpp();

        // Pre-allocate enough space
        vector<Vector3> points(m_height * m_width, Vector3::Zero);

        // Read height map
        for (uint32_t y = 0; y < m_height; y++)
        {
            for (uint32_t x = 0; x < m_width; x++)
            {
                // Read height and scale it to [0,1]
                uint32_t index = y * m_width + x;
                float height = static_cast<float>(data[index]) / 255.0f;

                // Construct position
                points[index].x = static_cast<float>(x);
                points[index].z = static_cast<float>(y);
                points[index].y = Lerp(m_min_z, m_max_z, height);
            }
        }

        // Create the vertices and the indices of the grid
        vector<RHI_Vertex_PosTexNorTan> vertices(m_height * m_width);
        uint32_t face_count = (m_height - 1) * (m_width - 1) * 2;
        vector<uint32_t> indices(face_count * 3);
        uint32_t k = 0;
        uint32_t texUIndex = 0;
        uint32_t texVIndex = 0;
        {
            for (uint32_t y = 0; y < m_height - 1; y++)
            {
                for (uint32_t x = 0; x < m_width - 1; x++)
                {
                    // Bottom left of quad
                    indices[k] = y * m_width + x;
                    vertices[y * m_width + x] = RHI_Vertex_PosTexNorTan
                    {
                        points[y * m_width + x],
                        Vector2(texUIndex + 0.0f, texVIndex + 1.0f),
                        Vector3(0, 1, 0),
                        Vector3(1, 0, 0)
                    };

                    // Bottom right of quad
                    indices[k + 1] = y * m_width + x + 1;
                    vertices[y * m_width + x + 1] = RHI_Vertex_PosTexNorTan
                    {
                        points[y * m_width + x + 1],
                        Vector2(texUIndex + 1.0f, texVIndex + 1.0f),
                        Vector3(0, 1, 0),
                        Vector3(1, 0, 0)
                    };

                    // Top left of quad
                    indices[k + 2] = (y + 1) * m_width + x;
                    vertices[(y + 1) * m_width + x] = RHI_Vertex_PosTexNorTan
                    {
                        points[(y + 1) * m_width + x],
                        Vector2(texUIndex + 0.0f, texVIndex + 0.0f),
                        Vector3(0, 1, 0),
                        Vector3(1, 0, 0)
                    };

                    // Top left of quad
                    indices[k + 3] = (y + 1) * m_width + x;
                    vertices[(y + 1) * m_width + x] = RHI_Vertex_PosTexNorTan
                    {
                        points[(y + 1) * m_width + x],
                        Vector2(texUIndex + 0.0f, texVIndex + 0.0f),
                        Vector3(0, 1, 0),
                        Vector3(1, 0, 0)
                    };

                    // Bottom right of quad
                    indices[k + 4] = y * m_width + x + 1;
                    vertices[y * m_width + x + 1] = RHI_Vertex_PosTexNorTan
                    {
                        points[y * m_width + x + 1],
                        Vector2(texUIndex + 1.0f, texVIndex + 1.0f),
                        Vector3(0, 1, 0),
                        Vector3(1, 0, 0)
                    };

                    // Top right of quad
                    indices[k + 5] = (y + 1) * m_width + x + 1;
                    vertices[(y + 1) * m_width + x + 1] = RHI_Vertex_PosTexNorTan
                    {
                        points[(y + 1) * m_width + x + 1],
                        Vector2(texUIndex + 1.0f, texVIndex + 0.0f),
                        Vector3(0, 1, 0),
                        Vector3(1, 0, 0)
                    };

                    k += 6; // next quad

                    texUIndex++;
                }
                texUIndex = 0;
                texVIndex++;
            }
        }

        // Create model
        m_model = make_shared<Model>(m_context);
        m_model->GeometryAppend(indices, vertices);
        m_model->GeometryUpdate();

        // Add renderable and pass the model to it
        Renderable* renderable = m_entity->AddComponent<Renderable>().get();
        renderable->GeometrySet(
            "Terrain",
            0,
            static_cast<uint32_t>(indices.size()),
            0,
            static_cast<uint32_t>(vertices.size()),
            BoundingBox(vertices),
            m_model.get()
        );
        renderable->UseDefaultMaterial();

        return true;
    }
}
