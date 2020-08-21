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

//= INCLUDES ================================
#include "Spartan.h"
#include "Grid.h"
#include "../../World/Components/Transform.h"
#include "../../RHI/RHI_VertexBuffer.h"
#include "../../RHI/RHI_IndexBuffer.h"
#include "../../RHI/RHI_Vertex.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    Grid::Grid(shared_ptr<RHI_Device> rhi_device)
    {
        m_indexCount    = 0;
        m_terrainHeight = 200;
        m_terrainWidth    = 200;

        vector<RHI_Vertex_PosCol> vertices;
        vector<unsigned> indices;
        BuildGrid(&vertices, &indices);
        CreateBuffers(vertices, indices, rhi_device);
    }

    const Matrix& Grid::ComputeWorldMatrix(Transform* camera)
    {
        // To get the grid to feel infinite, it has to follow the camera,
        // but only by increments of the grid's spacing size. This gives the illusion 
        // that the grid never moves and if the grid is large enough, the user can't tell.
        const auto gridSpacing = 1.0f;
        const auto translation = Vector3
        (
            static_cast<int>(camera->GetPosition().x / gridSpacing) * gridSpacing, 
            0.0f, 
            static_cast<int>(camera->GetPosition().z / gridSpacing) * gridSpacing
        );
    
        m_world = Matrix::CreateScale(gridSpacing) * Matrix::CreateTranslation(translation);

        return m_world;
    }

    void Grid::BuildGrid(vector<RHI_Vertex_PosCol>* vertices, vector<uint32_t>* indices)
    {
        const auto halfSizeW = int(m_terrainWidth * 0.5f);
        const auto halfSizeH = int(m_terrainHeight * 0.5f);

        for (auto j = -halfSizeH; j < halfSizeH; j++)
        {
            for (auto i = -halfSizeW; i < halfSizeW; i++)
            {
                // Become more transparent, the further out we go
                const auto alphaWidth    = 1.0f - static_cast<float>(Helper::Abs(j)) / static_cast<float>(halfSizeH);
                const auto alphaHeight    = 1.0f - static_cast<float>(Helper::Abs(i)) / static_cast<float>(halfSizeW);
                auto alpha                = (alphaWidth + alphaHeight) * 0.5f;
                alpha                    = Helper::Pow(alpha, 10.0f);

                // LINE 1
                // Upper left.
                auto positionX = static_cast<float>(i);
                auto positionZ = static_cast<float>(j + 1);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

                // Upper right.
                positionX = static_cast<float>(i + 1);
                positionZ = static_cast<float>(j + 1);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

                // LINE 2
                // Upper right.
                positionX = static_cast<float>(i + 1);
                positionZ = static_cast<float>(j + 1);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

                // Bottom right.
                positionX = static_cast<float>(i + 1);
                positionZ = static_cast<float>(j);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

                // LINE 3
                // Bottom right.
                positionX = static_cast<float>(i + 1);
                positionZ = static_cast<float>(j);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

                // Bottom left.
                positionX = static_cast<float>(i);
                positionZ = static_cast<float>(j);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

                // LINE 4
                // Bottom left.
                positionX = static_cast<float>(i);
                positionZ = static_cast<float>(j);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

                // Upper left.
                positionX = static_cast<float>(i);
                positionZ = static_cast<float>(j + 1);
                vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));
            }
        }

        for (uint32_t i = 0; i < vertices->size(); i++)
        {
            indices->emplace_back(i);
        }
        m_indexCount = static_cast<uint32_t>(indices->size());
    }

    bool Grid::CreateBuffers(vector<RHI_Vertex_PosCol>& vertices, vector<unsigned>& indices, shared_ptr<RHI_Device>& rhi_device)
    {
        m_vertexBuffer = make_shared<RHI_VertexBuffer>(rhi_device);
        if (!m_vertexBuffer->Create(vertices))
        {
            LOG_ERROR("Failed to create vertex buffer.");
            return false;
        }

        m_indexBuffer = make_shared<RHI_IndexBuffer>(rhi_device);
        if (!m_indexBuffer->Create(indices))
        {
            LOG_ERROR("Failed to create index buffer.");
            return false;
        }

        return true;
    }
}
