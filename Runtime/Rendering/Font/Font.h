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

#pragma once

//= INCLUDES ========================
#include <memory>
#include <map>
#include "Glyph.h"
#include "../../RHI/RHI_Definition.h"
#include "../../Core/EngineDefs.h"
#include "../../Resource/IResource.h"
#include "../../Math/Vector4.h"
//===================================

namespace Directus
{
	namespace Math
	{
		class Vector2;
	}

	class ENGINE_CLASS Font : IResource
	{
	public:
		Font(Context* context, const std::string& filePath, int fontSize, const Math::Vector4& color);
		~Font();

		//= RESOURCE INTERFACE =================================
		bool SaveToFile(const std::string& filePath) override;
		bool LoadFromFile(const std::string& filePath) override;
		//======================================================

		void SetText(const std::string& text, const Math::Vector2& position);
		void SetSize(int size);

		const Math::Vector4& GetColor()				{ return m_fontColor; }
		void SetColor(const Math::Vector4& color)	{ m_fontColor = color; }

		const std::shared_ptr<RHI_Texture>& GetTexture()	{ return m_textureAtlas; }
		std::shared_ptr<RHI_IndexBuffer> GetIndexBuffer()	{ return m_indexBuffer; }
		std::shared_ptr<RHI_VertexBuffer> GetVertexBuffer()	{ return m_vertexBuffer; }
		unsigned int GetIndexCount()						{ return (unsigned int)m_indices.size(); }
			
	private:	
		bool UpdateBuffers(std::vector<RHI_Vertex_PosUV>& vertices, std::vector<unsigned int>& indices);

		std::map<unsigned int, Glyph> m_glyphs;
		std::shared_ptr<RHI_Texture> m_textureAtlas;
		int m_fontSize;
		int m_charMaxWidth;
		int m_charMaxHeight;
		Math::Vector4 m_fontColor;
		std::shared_ptr<RHI_VertexBuffer> m_vertexBuffer;
		std::shared_ptr<RHI_IndexBuffer> m_indexBuffer;
		std::vector<RHI_Vertex_PosUV> m_vertices;
		std::vector<unsigned int> m_indices;
		std::string m_currentText;
		std::shared_ptr<RHI_Device> m_rhiDevice;
	};
}