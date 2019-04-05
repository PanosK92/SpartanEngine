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
		Font(Context* context, const std::string& file_path, int font_size, const Math::Vector4& color);
		~Font() = default;

		//= RESOURCE INTERFACE =================================
		bool SaveToFile(const std::string& file_path) override;
		bool LoadFromFile(const std::string& file_path) override;
		//======================================================

		void SetText(const std::string& text, const Math::Vector2& position);
		void SetSize(unsigned int size);

		const auto& GetColor() const				{ return m_fontColor; }
		void SetColor(const Math::Vector4& color)	{ m_fontColor = color; }
		const auto& GetTexture() const				{ return m_texture_atlas; }
		const auto& GetIndexBuffer() const			{ return m_index_buffer_; }
		const auto& GetVertexBuffer() const			{ return m_vertex_buffer; }
		unsigned int GetIndexCount() const			{ return static_cast<unsigned int>(m_indices.size()); }
			
	private:	
		bool UpdateBuffers(std::vector<RHI_Vertex_PosUv>& vertices, std::vector<unsigned int>& indices) const;

		std::map<unsigned int, Glyph> m_glyphs;
		std::shared_ptr<RHI_Texture> m_texture_atlas;
		unsigned int m_font_size{};
		unsigned int m_char_max_width;
		unsigned int m_char_max_height;
		Math::Vector4 m_fontColor;
		std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer;
		std::shared_ptr<RHI_IndexBuffer> m_index_buffer_;
		std::vector<RHI_Vertex_PosUv> m_vertices;
		std::vector<unsigned int> m_indices;
		std::string m_current_text;
		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}