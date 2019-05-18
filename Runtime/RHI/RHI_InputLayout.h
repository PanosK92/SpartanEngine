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

//= INCLUDES ==================
#include <memory>
#include <vector>
#include "RHI_Definition.h"
#include "RHI_Vertex.h"
#include "../Core/EngineDefs.h"
//=============================

namespace Spartan
{
	struct VertexAttribute 
	{
		VertexAttribute(const std::string& name, const uint32_t location, const uint32_t binding, const RHI_Format format, const uint32_t offset)
		{
			this->name		= name;
			this->location	= location;
			this->binding	= binding;
			this->format	= format;
			this->offset	= offset;
		}

		std::string name;
		uint32_t location;
		uint32_t binding;
		RHI_Format format;
		uint32_t offset;
	};

	class SPARTAN_CLASS RHI_InputLayout
	{
	public:
		RHI_InputLayout(const std::shared_ptr<RHI_Device>& rhi_device)
		{
			m_rhi_device = rhi_device;
		}

		~RHI_InputLayout();

		template<typename T>
		bool Create(void* vertex_shader_blob = nullptr)
		{
			uint32_t binding = 0;

			if (RHI_Vertex_Type_To_Enum<T>() == RHI_Vertex_Type_Position)
			{
				m_vertex_attributes =
				{
					{ "POSITION", 0, binding, Format_R32G32B32_FLOAT,	offsetof(RHI_Vertex_Pos, pos) }
				};
			}

			if (RHI_Vertex_Type_To_Enum<T>() == RHI_Vertex_Type_PositionTexture)
			{
				m_vertex_attributes =
				{
					{ "POSITION", 0, binding, Format_R32G32B32_FLOAT,	offsetof(RHI_Vertex_PosTex, pos) },
					{ "TEXCOORD", 1, binding, Format_R32G32_FLOAT,		offsetof(RHI_Vertex_PosTex, tex) }
				};
			}

			if (RHI_Vertex_Type_To_Enum<T>() == RHI_Vertex_Type_PositionColor)
			{
				m_vertex_attributes =
				{
					{ "POSITION",	0, binding, Format_R32G32B32_FLOAT,		offsetof(RHI_Vertex_PosCol, pos) },
					{ "COLOR",		1, binding, Format_R32G32B32A32_FLOAT,	offsetof(RHI_Vertex_PosCol, col) }
				};
			}

			if (RHI_Vertex_Type_To_Enum<T>() == RHI_Vertex_Type_Position2dTextureColor8)
			{
				m_vertex_attributes =
				{
					{ "POSITION",	0, binding, Format_R32G32_FLOAT,	offsetof(RHI_Vertex_Pos2dTexCol8, pos) },
					{ "TEXCOORD",	1, binding, Format_R32G32_FLOAT,	offsetof(RHI_Vertex_Pos2dTexCol8, tex) },
					{ "COLOR",		2, binding, Format_R8G8B8A8_UNORM,	offsetof(RHI_Vertex_Pos2dTexCol8, col) }
				};
			}

			if (RHI_Vertex_Type_To_Enum<T>() == RHI_Vertex_Type_PositionTextureNormalTangent)
			{
				m_vertex_attributes =
				{
					{ "POSITION",	0, binding, Format_R32G32B32_FLOAT,	offsetof(RHI_Vertex_PosTexNorTan, pos) },
					{ "TEXCOORD",	1, binding, Format_R32G32_FLOAT,	offsetof(RHI_Vertex_PosTexNorTan, tex) },
					{ "NORMAL",		2, binding, Format_R32G32B32_FLOAT,	offsetof(RHI_Vertex_PosTexNorTan, nor) },
					{ "TANGENT",	3, binding, Format_R32G32B32_FLOAT,	offsetof(RHI_Vertex_PosTexNorTan, tan) }
				};
			}

			if (vertex_shader_blob && !m_vertex_attributes.empty())
			{
				return _CreateResource(vertex_shader_blob);
			}
			return true;
		}

		const auto& GetAttributeDescriptions() const	{ return m_vertex_attributes; }
		auto GetResource() const						{ return m_resource; }

	private:
		// API
		bool _CreateResource(void* vertex_shader_blob);
		std::shared_ptr<RHI_Device> m_rhi_device;
		void* m_resource = nullptr;
		std::vector<VertexAttribute> m_vertex_attributes;
	};
}