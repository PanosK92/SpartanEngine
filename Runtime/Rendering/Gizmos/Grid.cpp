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

//= INCLUDES ================================
#include "Grid.h"
#include "../../Logging/Log.h"
#include "../../World/Components/Transform.h"
#include "../../RHI/RHI_VertexBuffer.h"
#include "../../RHI/RHI_IndexBuffer.h"
#include "../../RHI/RHI_Vertex.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Grid::Grid(shared_ptr<RHI_Device> rhiDevice)
	{
		m_indexCount	= 0;
		m_terrainHeight = 200;
		m_terrainWidth	= 200;

		vector<RHI_Vertex_PosCol> vertices;
		vector<unsigned> indices;
		BuildGrid(&vertices, &indices);
		CreateBuffers(vertices, indices, rhiDevice);
	}

	const Matrix& Grid::ComputeWorldMatrix(Transform* camera)
	{
		// To get the grid to feel infinite, it has to follow the camera,
		// but only by increments of the grid's spacing size. This gives the illusion 
		// that the grid never moves and if the grid is large enough, the user can't tell.
		float gridSpacing = 1.0f;
		Vector3 translation = Vector3
		(
			(int)(camera->GetPosition().x / gridSpacing) * gridSpacing, 
			0.0f, 
			(int)(camera->GetPosition().z / gridSpacing) * gridSpacing
		);
	
		m_world = Matrix::CreateScale(gridSpacing) * Matrix::CreateTranslation(translation);

		return m_world;
	}

	void Grid::BuildGrid(vector<RHI_Vertex_PosCol>* vertices, vector<unsigned int>* indices)
	{
		int halfSizeW = int(m_terrainWidth * 0.5f);
		int halfSizeH = int(m_terrainHeight * 0.5f);

		for (int j = -halfSizeH; j < halfSizeH; j++)
		{
			for (int i = -halfSizeW; i < halfSizeW; i++)
			{
				// Become more transparent, the further out we go
				float alphaWidth	= 1.0f - ((float)Math::Helper::Abs(j) / (float)halfSizeH);
				float alphaHeight	= 1.0f - ((float)Math::Helper::Abs(i) / (float)halfSizeW);
				float alpha			= (alphaWidth + alphaHeight) * 0.5f;
				alpha				= Math::Helper::Pow(alpha, 10.0f);

				// LINE 1
				// Upper left.
				auto positionX = (float)i;
				auto positionZ = (float)(j + 1);
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

				// Upper right.
				positionX = (float)(i + 1);
				positionZ = (float)(j + 1);
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

				// LINE 2
				// Upper right.
				positionX = (float)(i + 1);
				positionZ = (float)(j + 1);
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

				// Bottom right.
				positionX = (float)(i + 1);
				positionZ = (float)j;
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

				// LINE 3
				// Bottom right.
				positionX = (float)(i + 1);
				positionZ = (float)j;
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

				// Bottom left.
				positionX = (float)i;
				positionZ = (float)j;
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

				// LINE 4
				// Bottom left.
				positionX = (float)i;
				positionZ = (float)j;
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));

				// Upper left.
				positionX = (float)i;
				positionZ = (float)(j + 1);
				vertices->emplace_back(Vector3(positionX, 0.0f, positionZ), Vector4(1.0f, 1.0f, 1.0f, alpha));
			}
		}

		for (unsigned int i = 0; i < vertices->size(); i++)
		{
			indices->emplace_back(i);
		}
		m_indexCount = (unsigned int)indices->size();
	}

	bool Grid::CreateBuffers(vector<RHI_Vertex_PosCol>& vertices, vector<unsigned>& indices, shared_ptr<RHI_Device> rhiDevice)
	{
		m_vertexBuffer.reset();
		m_indexBuffer.reset();

		m_vertexBuffer = make_shared<RHI_VertexBuffer>(rhiDevice);
		if (!m_vertexBuffer->Create(vertices))
		{
			LOG_ERROR("Grid::CreateBuffers: Failed to create vertex buffer.");
			return false;
		}

		m_indexBuffer = make_shared<RHI_IndexBuffer>(rhiDevice);
		if (!m_indexBuffer->Create(indices))
		{
			LOG_ERROR("Failed to create index buffer.");
			return false;
		}

		return true;
	}
}
