/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES =========================
#include "Rectangle.h"
#include "Renderer.h"
#include "../Core/Settings.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_IndexBuffer.h"
#include "../RHI/RHI_Vertex.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Rectangle::Rectangle(Context* context)
	{
		m_rhiDevice			= context->GetSubsystem<Renderer>()->GetRHIDevice();
		m_x					= 0;
		m_y					= 0;
		m_width				= 0;
		m_height			= 0;
		m_resolutionWidth	= Settings::Get().GetResolutionWidth();
		m_resolutionHeight	= Settings::Get().GetResolutionHeight();
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
			m_resolutionWidth == Settings::Get().GetResolutionWidth() && 
			m_resolutionHeight == Settings::Get().GetResolutionHeight()
			)
			return true;

		m_x = x;
		m_y = y;
		m_width = width;
		m_height = height;
		m_resolutionWidth = Settings::Get().GetResolutionWidth();
		m_resolutionHeight = Settings::Get().GetResolutionHeight();

		// Compute screen coordinates
		float left = -m_resolutionWidth * 0.5f + m_x;
		float right = left + m_width;
		float top = m_resolutionHeight * 0.5f - m_y;
		float bottom = top - m_height;

		// Create index and vertex arrays
		vector<RHI_Vertex_PosUV> vertices;
		vector<unsigned int> indices;

		// First triangle
		// Top left
		vertices.emplace_back(RHI_Vertex_PosUV(Vector3(left, top, 0.0f), Vector2(0.0f, 0.0f)));

		// Bottom right
		vertices.emplace_back(RHI_Vertex_PosUV(Vector3(right, bottom, 0.0f), Vector2(1.0f, 1.0f)));

		// Bottom left
		vertices.emplace_back(RHI_Vertex_PosUV(Vector3(left, bottom, 0.0f), Vector2(0.0f, 1.0f)));

		// Second triangle
		// Top left
		vertices.emplace_back(RHI_Vertex_PosUV(Vector3(left, top, 0.0f), Vector2(0.0f, 0.0f)));

		// Top right
		vertices.emplace_back(RHI_Vertex_PosUV(Vector3(right, top, 0.0f), Vector2(1.0f, 0.0f)));

		// Bottom right
		vertices.emplace_back(RHI_Vertex_PosUV(Vector3(right, bottom, 0.0f), Vector2(1.0f, 1.0f)));

		// Load the index array with data.
		for (unsigned int i = 0; i < vertices.size(); i++)
		{
			indices.push_back(i);
		}

		m_vertexBuffer = make_shared<RHI_VertexBuffer>(m_rhiDevice);
		if (!m_vertexBuffer->Create(vertices))
		{
			LOG_ERROR("Rectangle: Failed to create vertex buffer.");
			return false;
		}

		m_indexBuffer = make_shared<RHI_IndexBuffer>(m_rhiDevice);
		if (!m_indexBuffer->Create(indices))
		{
			LOG_ERROR("Rectangle: Failed to create index buffer.");
			return false;
		}

		return true;
	}
}