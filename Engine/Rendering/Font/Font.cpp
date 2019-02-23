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

//= INCLUDES ============================
#include "Font.h"
#include "Glyph.h"
#include "../Renderer.h"
#include "../../Core/Stopwatch.h"
#include "../../RHI/RHI_Implementation.h"
#include "../../RHI/RHI_VertexBuffer.h"
#include "../../RHI/RHI_IndexBuffer.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../RHI/RHI_Texture.h"
#include "../../Resource/ResourceCache.h"
//=======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

#define ASCII_TAB		9
#define ASCII_NEW_LINE	10
#define ASCII_SPACE		32

namespace Directus
{
	Font::Font(Context* context, const string& filePath, int fontSize, const Vector4& color) : IResource(context, Resource_Font)
	{
		m_rhiDevice			= m_context->GetSubsystem<Renderer>()->GetRHIDevice();
		m_vertexBuffer		= make_shared<RHI_VertexBuffer>(m_rhiDevice);
		m_indexBuffer		= make_shared<RHI_IndexBuffer>(m_rhiDevice);
		m_charMaxWidth		= 0;
		m_charMaxHeight		= 0;
		m_fontColor			= color;
		
		SetSize(fontSize);
		LoadFromFile(filePath);
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

		Stopwatch timer;

		// Load font
		vector<std::byte> atlasBuffer;
		unsigned int texAtlasWidth = 0;
		unsigned int texAtlasHeight = 0;
		if (!m_context->GetSubsystem<ResourceCache>()->GetFontImporter()->LoadFromFile(filePath, m_fontSize, atlasBuffer, texAtlasWidth, texAtlasHeight, m_glyphs))
		{
			LOGF_ERROR("Failed to load font \"%s\"", filePath.c_str());
			atlasBuffer.clear();
			return false;
		}

		// Find max character height (todo, actually get spacing from FreeType)
		for (const auto& charInfo : m_glyphs)
		{
			m_charMaxWidth	= Max<int>(charInfo.second.width, m_charMaxWidth);
			m_charMaxHeight = Max<int>(charInfo.second.height, m_charMaxHeight);
		}

		// Create a font texture atlas form the provided data
		m_textureAtlas = make_shared<RHI_Texture>(m_context);
		bool needsMipChain = false;
		if (!m_textureAtlas->ShaderResource_Create2D(texAtlasWidth, texAtlasHeight, 1, Format_R8_UNORM, atlasBuffer, needsMipChain))
		{
			LOG_ERROR("Failed to create shader resource.");
		}
		LOG_INFO("Loading \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\" took " + to_string((int)timer.GetElapsedTimeMs()) + " ms");

		return true;
	}

	void Font::SetText(const string& text, const Vector2& position)
	{
		if (text == m_currentText)
			return;

		Vector2 pen = position;
		m_currentText = text;
		m_vertices.clear();
	
		// Draw each letter onto a quad.
		for (char textChar : m_currentText)
		{
			auto glyph = m_glyphs[textChar];

			if (textChar == ASCII_TAB)
			{
				int spaceOffset = m_glyphs[ASCII_SPACE].horizontalOffset;
				int spaceCount = 8; // spaces in a typical terminal
				int tabSpacing = spaceOffset * spaceCount;
				int columnHeader = int(pen.x - position.x); // -position.x because it has to be zero based so we can do the mod below
				int offsetToNextTabStop = tabSpacing - (columnHeader % (tabSpacing != 0 ? tabSpacing : 1));
				pen.x += offsetToNextTabStop;
				continue;
			}

			if (textChar == ASCII_NEW_LINE)
			{
				pen.y = pen.y - m_charMaxHeight;
				pen.x = position.x;
				continue;
			}

			if (textChar == ASCII_SPACE)
			{
				pen.x += glyph.horizontalOffset;
				continue;
			}

			// First triangle in quad.		
			m_vertices.emplace_back(pen.x,					pen.y - glyph.descent,					0.0f, glyph.uvXLeft, glyph.uvYTop);		// Top left
			m_vertices.emplace_back((pen.x + glyph.width),	(pen.y - glyph.height - glyph.descent), 0.0f, glyph.uvXRight, glyph.uvYBottom);	// Bottom right
			m_vertices.emplace_back(pen.x,					(pen.y - glyph.height - glyph.descent), 0.0f, glyph.uvXLeft, glyph.uvYBottom);	// Bottom left
			// Second triangle in quad.
			m_vertices.emplace_back(pen.x,					pen.y - glyph.descent,					0.0f, glyph.uvXLeft, glyph.uvYTop);		// Top left
			m_vertices.emplace_back((pen.x	+ glyph.width),	pen.y - glyph.descent,					0.0f, glyph.uvXRight, glyph.uvYTop);	// Top right
			m_vertices.emplace_back((pen.x	+ glyph.width),	(pen.y - glyph.height - glyph.descent), 0.0f, glyph.uvXRight, glyph.uvYBottom);	// Bottom right

			// Update the x location for drawing by the size of the letter and one pixel.
			pen.x = pen.x + glyph.width;
		}
		m_vertices.shrink_to_fit();		
		
		m_indices.clear();
		for (unsigned int i = 0; i < m_vertices.size(); i++)
		{
			m_indices.emplace_back(i);
		}

		UpdateBuffers(m_vertices, m_indices);
	}

	void Font::SetSize(int size)
	{
		m_fontSize = Clamp<int>(size, 8, 50);
	}

	bool Font::UpdateBuffers(vector<RHI_Vertex_PosUV>& vertices, vector<unsigned int>& indices)
	{
		if (!m_context || !m_vertexBuffer || !m_indexBuffer)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Grow buffers (if needed)
		if (vertices.size() > m_vertexBuffer->GetVertexCount())
		{
			// Vertex buffer
			if (!m_vertexBuffer->CreateDynamic(sizeof(RHI_Vertex_PosUV), (unsigned int)vertices.size()))
			{
				LOG_ERROR("Failed to update vertex buffer.");
				return false;
			}

			// Index buffer
			if (!m_indexBuffer->CreateDynamic(sizeof(unsigned int), (unsigned int)indices.size()))
			{
				LOG_ERROR("Failed to update index buffer.");
				return false;
			}
		}

		auto vertexBuffer = (RHI_Vertex_PosUV*)m_vertexBuffer->Map();
		copy(vertices.begin(), vertices.end(), vertexBuffer);
		m_vertexBuffer->Unmap();

		auto indexBuffer = (unsigned int*)m_indexBuffer->Map();
		copy(indices.begin(), indices.end(), indexBuffer);
		m_indexBuffer->Unmap();

		return true;
	}
}
