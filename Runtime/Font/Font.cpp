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

//= INCLUDES =====================================
#include "Font.h"
#include "../Resource/ResourceManager.h"
#include "../Graphics/Vertex.h"
#include "../Graphics/D3D11/D3D11VertexBuffer.h"
#include "../Graphics/D3D11/D3D11IndexBuffer.h"
#include "../Graphics/D3D11/D3D11GraphicsDevice.h"
#include "../Core/Settings.h"
//================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Graphics* graphics;

	Font::Font(Context* context)
	{
		m_context = context;
		m_fontSize = 12;
		m_charMaxWidth = 0;
		m_charMaxHeight = 0;
		m_indexCount = 0;
		m_fontColor = Vector4(0.0f, 0.0f, 0.0f, 1.0f);
		graphics = m_context->GetSubsystem<Graphics>();
	}

	Font::~Font()
	{
	}

	bool Font::SaveToFile(const string& filePath)
	{
		return true;
	}

	bool Font::LoadFromFile(const string& filePath)
	{
		if (!m_context)
			return false;

		// Load font
		vector<unsigned char> atlasBuffer;
		int texAtlasWidth = 0;
		int texAtlasHeight = 0;
		if (!m_context->GetSubsystem<ResourceManager>()->GetFontImporter()._Get()->LoadFont(filePath, m_fontSize, atlasBuffer, texAtlasWidth, texAtlasHeight, m_characterInfo))
		{
			LOG_ERROR("Font: Failed to load font \"" + filePath + "\"");
			atlasBuffer.clear();
			return false;
		}

		// Find max character height (todo, actually get spacing from freetype)
		for (const auto& charInfo : m_characterInfo)
		{
			m_charMaxWidth = Max<int>(charInfo.second.width, m_charMaxWidth);
			m_charMaxHeight = Max<int>(charInfo.second.height, m_charMaxHeight);
		}

		// Create a font texture atlas form the provided data
		m_textureAtlas = make_unique<Texture>(m_context);
		m_textureAtlas->CreateShaderResource(texAtlasWidth, texAtlasHeight, 1, atlasBuffer, R_8_UNORM);

		return true;
	}

	void Font::SetSize(int size)
	{
		m_fontSize = Clamp<int>(size, 8, 50);
	}

	void** Font::GetShaderResource()
	{
		return m_textureAtlas->GetShaderResource();
	}

	bool Font::SetBuffer()
	{
		if (!graphics || !m_vertexBuffer || !m_indexBuffer)
			return false;

		m_vertexBuffer->SetIA();
		m_indexBuffer->SetIA();

		// Set the type of primitive that should 
		// be rendered from this vertex buffer
		graphics->SetPrimitiveTopology(TriangleList);

		return true;
	}

	void Font::SetText(const string& text, const Vector2& position)
	{
		Vector2 pen = position;
		vector<VertexPosTex> vertices;
		VertexPosTex vertex;

		// Draw each letter onto a quad.
		for (const char& character : text)
		{
			auto charInfo = m_characterInfo[character];

			if (character == 10) // New line
			{
				pen.y = pen.y - m_charMaxHeight;
				pen.x = position.x;
				continue;
			}

			if (character == 32) // Space
			{
				pen.x = pen.x + m_charMaxWidth;
				continue;
			}

			// First triangle in quad.
			// Top left.
			vertex.position = Vector3(pen.x, pen.y - charInfo.descent, 0.0f);
			vertex.uv = Vector2(charInfo.uvXLeft, charInfo.uvYTop);
			vertices.push_back(vertex);

			// Bottom right.
			vertex.position = Vector3((pen.x + charInfo.width), (pen.y - charInfo.height - charInfo.descent), 0.0f);
			vertex.uv = Vector2(charInfo.uvXRight, charInfo.uvYBottom);
			vertices.push_back(vertex);

			// Bottom left.
			vertex.position = Vector3(pen.x, (pen.y - charInfo.height - charInfo.descent), 0.0f);
			vertex.uv = Vector2(charInfo.uvXLeft, charInfo.uvYBottom);
			vertices.push_back(vertex);

			// Second triangle in quad.
			// Top left.
			vertex.position = Vector3(pen.x, pen.y - charInfo.descent, 0.0f);
			vertex.uv = Vector2(charInfo.uvXLeft, charInfo.uvYTop);
			vertices.push_back(vertex);

			// Top right.
			vertex.position = Vector3(pen.x + charInfo.width, pen.y - charInfo.descent, 0.0f);
			vertex.uv = Vector2(charInfo.uvXRight, charInfo.uvYTop);
			vertices.push_back(vertex);

			// Bottom right.
			vertex.position = Vector3((pen.x + charInfo.width), (pen.y - charInfo.height - charInfo.descent), 0.0f);
			vertex.uv = Vector2(charInfo.uvXRight, charInfo.uvYBottom);
			vertices.push_back(vertex);

			// Update the x location for drawing by the size of the letter and one pixel.
			pen.x = pen.x + charInfo.width;
		}

		vector<unsigned int> indices;
		indices.reserve(vertices.size());
		for (int i = 0; i < vertices.size(); i++)
		{
			indices.emplace_back(i);
		}
		m_indexCount = (int)indices.size();

		UpdateBuffers(vertices, indices);
	}

	bool Font::UpdateBuffers(vector<VertexPosTex>& vertices, vector<unsigned int>& indices)
	{
		if (!m_context)
			return false;

		// Vertex buffer
		if (!m_vertexBuffer)
		{
			m_vertexBuffer = make_shared<D3D11VertexBuffer>(graphics);
			if (!m_vertexBuffer->CreateDynamic(sizeof(VertexPosTex), (unsigned int)vertices.size()))
			{
				LOG_ERROR("Font: Failed to create vertex buffer.");
				return false;
			}	
		}
		void* data = m_vertexBuffer->Map();
		memcpy(data, &vertices[0], sizeof(VertexPosTex) * (int)vertices.size());
		m_vertexBuffer->Unmap();

		// Index buffer
		if (!m_indexBuffer)
		{
			m_indexBuffer = make_shared<D3D11IndexBuffer>(graphics);
			if (!m_indexBuffer->CreateDynamic((unsigned int)indices.size()))
			{
				LOG_ERROR("Font: Failed to create index buffer.");
				return false;
			}
		}
		data = m_indexBuffer->Map();
		memcpy(data, &indices[0], sizeof(unsigned int) * (int)indices.size());
		m_indexBuffer->Unmap();

		return true;
	}
}
