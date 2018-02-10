/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES =============================
#include "Grid.h"
#include "../Core/Context.h"
#include "Vertex.h"
#include "D3D11/D3D11VertexBuffer.h"
#include "D3D11/D3D11IndexBuffer.h"
#include "../Logging/Log.h"
#include "../Scene/Components/Transform.h"
//========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Grid::Grid(Context* context)
	{
		m_context = context;
		m_indexCount = 0;
		m_terrainHeight = 200;
		m_terrainWidth = 200;
	}

	Grid::~Grid()
	{

	}

	void Grid::BuildGrid()
	{
		vector<VertexPosCol> vertices;
		VertexPosCol vertex;
		int halfSizeW = int(m_terrainWidth * 0.5f);
		int halfSizeH = int(m_terrainHeight * 0.5f);

		for (int j = -halfSizeH; j < halfSizeH; j++)
		{
			for (int i = -halfSizeW; i < halfSizeW; i++)
			{
				// LINE 1
				// Upper left.
				float positionX = (float)i;
				float positionZ = (float)(j + 1);
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);

				// Upper right.
				positionX = (float)(i + 1);
				positionZ = (float)(j + 1);
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);

				// LINE 2
				// Upper right.
				positionX = (float)(i + 1);
				positionZ = (float)(j + 1);
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);

				// Bottom right.
				positionX = (float)(i + 1);
				positionZ = (float)j;
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);

				// LINE 3
				// Bottom right.
				positionX = (float)(i + 1);
				positionZ = (float)j;
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);

				// Bottom left.
				positionX = (float)i;
				positionZ = (float)j;
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);

				// LINE 4
				// Bottom left.
				positionX = (float)i;
				positionZ = (float)j;
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);

				// Upper left.
				positionX = (float)i;
				positionZ = (float)(j + 1);
				vertex.position = Vector3(positionX, 0.0f, positionZ);
				vertex.color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
				vertices.push_back(vertex);
			}
		}

		vector<unsigned int> indices;
		for (int i = 0; i < vertices.size(); i++)
		{
			indices.push_back(i);
		}
		m_indexCount = (int)indices.size();

		CreateBuffers(vertices, indices);
	}

	bool Grid::SetBuffer()
	{
		auto graphics = m_context->GetSubsystem<Graphics>();

		if (!graphics || !m_vertexBuffer || !m_indexBuffer)
			return false;

		m_vertexBuffer->SetIA();
		m_indexBuffer->SetIA();

		// Set the type of primitive that should 
		// be rendered from this vertex buffer
		graphics->SetPrimitiveTopology(LineList);

		return true;
	}

	const Matrix& Grid::ComputeWorldMatrix(Transform* camera)
	{
		// To get the grid to feel infinate, it has to follow the camera,
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

	bool Grid::CreateBuffers(vector<VertexPosCol>& vertices, vector<unsigned>& indices)
	{
		if (!m_context)
			return false;

		auto graphics = m_context->GetSubsystem<Graphics>();

		m_vertexBuffer.reset();
		m_indexBuffer.reset();

		m_vertexBuffer = make_shared<D3D11VertexBuffer>(graphics);
		if (!m_vertexBuffer->Create(vertices))
		{
			LOG_ERROR("Font: Failed to create vertex buffer.");
			return false;
		}

		m_indexBuffer = make_shared<D3D11IndexBuffer>(graphics);
		if (!m_indexBuffer->Create(indices))
		{
			LOG_ERROR("Font: Failed to create index buffer.");
			return false;
		}

		return true;
	}
}
