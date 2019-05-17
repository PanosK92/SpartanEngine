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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES =====================
#include "../RHI_InputLayout.h"
#include "../RHI_Device.h"
#include "../../Core/EngineDefs.h"
//================================

//==================
using namespace std;
//==================

namespace Spartan
{
	RHI_InputLayout::RHI_InputLayout(const shared_ptr<RHI_Device>& rhi_device)
	{

	}

	RHI_InputLayout::~RHI_InputLayout()
	{
		safe_delete(m_resource);
	}

	bool RHI_InputLayout::Create(void* vertex_shader_blob, const RHI_Vertex_Attribute_Type vertex_attributes)
	{
		uint32_t location = 0;
		uint32_t vertex_buffer_bind_id = 0;

		m_attribute_descs.emplace_back(0, vertex_buffer_bind_id, Format_R32G32_FLOAT,	0);
		m_attribute_descs.emplace_back(1, vertex_buffer_bind_id, Format_R32G32_FLOAT,	sizeof(float) * 2);
		m_attribute_descs.emplace_back(2, vertex_buffer_bind_id, Format_R8G8B8A8_UNORM, sizeof(float) * 4);

		// POSITION
		//{
		//	if (m_vertex_attributes & Vertex_Attribute_Position2d)
		//	{
		//		m_attribute_descs.emplace_back(location, vertex_buffer_bind_id, Format_R32G32_FLOAT, 0);
		//	}
		//	else if (m_vertex_attributes & Vertex_Attribute_Position3d)
		//	{
		//		m_attribute_descs.emplace_back(location, vertex_buffer_bind_id, Format_R32G32B32_FLOAT, 0);
		//	}
		//}

		//// TEXTURE
		//location = 1;
		//if (m_vertex_attributes & Vertex_Attribute_Texture)
		//{
		//	m_attribute_descs.emplace_back(location, vertex_buffer_bind_id, Format_R32G32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0);
		//}

		//// COLOR
		//location = 2;
		//{
		//	if (m_vertex_attributes & Vertex_Attribute_Color8)
		//	{
		//		m_attribute_descs.emplace_back(location, vertex_buffer_bind_id, Format_R8G8B8A8_UNORM, 0);
		//	}

		//	if (m_vertex_attributes & Vertex_Attribute_Color32)
		//	{
		//		m_attribute_descs.emplace_back(location, vertex_buffer_bind_id, Format_R32G32B32A32_FLOAT, 0);
		//	}
		//}

		//// NORMAL & TANGENT
		//location = 3;
		//if (m_vertex_attributes & Vertex_Attribute_NormalTangent)
		//{
		//	m_attribute_descs.emplace_back(location, vertex_buffer_bind_id, Format_R32G32B32A32_FLOAT, 0);
		//	m_attribute_descs.emplace_back(location, vertex_buffer_bind_id, Format_R32G32B32A32_FLOAT, 0);
		//}
			
		return true;
	}
}
#endif