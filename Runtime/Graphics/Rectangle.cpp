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

//= INCLUDES =======================
#include "Rectangle.h"
#include "D3D11/D3D11IndexBuffer.h"
#include "D3D11/D3D11VertexBuffer.h"
#include "../Core/EngineDefs.h"
#include "../Graphics/Vertex.h"
#include "../Logging/Log.h"
#include "../Core/Context.h"
#include "../Core/Settings.h"
//==================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Rectangle::Rectangle(Context* context)
	{
		m_graphics = context->GetSubsystem<Graphics>();
		m_x = 0;
		m_y = 0;
		m_width = 0;
		m_height = 0;
		m_resolutionWidth = RESOLUTION_WIDTH;
		m_resolutionHeight = RESOLUTION_HEIGHT;
	}

	Rectangle::~Rectangle()
	{

	}

	bool Rectangle::Create(float x, float y, float width, float height)
	{
		// Don't update if it's not needed
		if (m_x == x && 
			m_y == y && 
			m_width == width && 
			m_height == height && 
			m_resolutionWidth == RESOLUTION_WIDTH && 
			m_resolutionHeight == RESOLUTION_HEIGHT
			)
			return true;

		m_x = x;
		m_y = y;
		m_width = width;
		m_height = height;
		m_resolutionWidth = RESOLUTION_WIDTH;
		m_resolutionHeight = RESOLUTION_HEIGHT;

		// Compute screen coordinates
		float left = -m_resolutionWidth * 0.5f + m_x;
		float right = left + m_width;
		float top = m_resolutionHeight * 0.5f - m_y;
		float bottom = top - m_height;

		// Create index and vertex arrays
		vector<VertexPosTex> vertices;
		vector<unsigned int> indices;

		// Load the vertex array with data.		
		VertexPosTex vertex;

		// First triangle
		// Top left
		vertex.position = Vector3(left, top, 0.0f); 
		vertex.uv = Vector2(0.0f, 0.0f);
		vertices.push_back(vertex);

		// Bottom right
		vertex.position = Vector3(right, bottom, 0.0f); 
		vertex.uv = Vector2(1.0f, 1.0f);
		vertices.push_back(vertex);

		// Bottom left
		vertex.position = Vector3(left, bottom, 0.0f); 
		vertex.uv = Vector2(0.0f, 1.0f);
		vertices.push_back(vertex);

		// Second triangle
		// Top left
		vertex.position = Vector3(left, top, 0.0f); 
		vertex.uv = Vector2(0.0f, 0.0f);
		vertices.push_back(vertex);

		// Top right
		vertex.position = Vector3(right, top, 0.0f);
		vertex.uv = Vector2(1.0f, 0.0f);
		vertices.push_back(vertex);

		// Bottom right
		vertex.position = Vector3(right, bottom, 0.0f); 
		vertex.uv = Vector2(1.0f, 1.0f);
		vertices.push_back(vertex);

		// Load the index array with data.
		for (int i = 0; i < vertices.size(); i++)
		{
			indices.push_back(i);
		}

		m_vertexBuffer = make_unique<D3D11VertexBuffer>(m_graphics);
		if (!m_vertexBuffer->Create(vertices))
		{
			LOG_ERROR("Rectangle: Failed to create vertex buffer.");
			return false;
		}

		m_indexBuffer = make_unique<D3D11IndexBuffer>(m_graphics);
		if (!m_indexBuffer->Create(indices))
		{
			LOG_ERROR("Rectangle: Failed to create index buffer.");
			return false;
		}

		return true;
	}

	bool Rectangle::SetBuffer()
	{
		if (!m_graphics || !m_vertexBuffer || !m_indexBuffer)
			return false;

		m_vertexBuffer->SetIA();
		m_indexBuffer->SetIA();
		m_graphics->SetPrimitiveTopology(TriangleList);

		return true;
	}
}