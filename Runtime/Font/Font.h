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

//= INCLUDES ====================
#include "../Core/Helper.h"
#include "../Resource/Resource.h"
#include <memory>
#include <map>
#include "../Math/Vector4.h"
//===============================

namespace Directus
{
	namespace Math
	{
		class Vector2;
	}

	struct Glyph;
	struct VertexPosTex;
	class Texture;
	class D3D11VertexBuffer;
	class D3D11IndexBuffer;

	class DLL_API Font : Resource
	{
	public:
		Font(Context* context);
		~Font();

		//= RESOURCE INTERFACE =================================
		bool SaveToFile(const std::string& filePath) override;
		bool LoadFromFile(const std::string& filePath) override;
		//======================================================

		void SetText(const std::string& text, const Math::Vector2& position);
		void SetSize(int size);

		const Math::Vector4& GetColor() { return m_fontColor; }
		void SetColor(const Math::Vector4& color) { m_fontColor = color; }

		void** GetShaderResource();
		bool SetBuffer();
		unsigned int GetIndexCount() { return m_indices.size(); }
			
	private:	
		bool UpdateBuffers(std::vector<VertexPosTex>& vertices, std::vector<unsigned int>& indices);

		std::map<unsigned int, Glyph> m_glyphs;
		std::unique_ptr<Texture> m_textureAtlas;
		int m_fontSize;
		int m_charMaxWidth;
		int m_charMaxHeight;
		Math::Vector4 m_fontColor;
		std::shared_ptr<D3D11VertexBuffer> m_vertexBuffer;
		std::shared_ptr<D3D11IndexBuffer> m_indexBuffer;
		std::vector<VertexPosTex> m_vertices;
		std::vector<unsigned int> m_indices;
		std::string m_currentText;
	};
}