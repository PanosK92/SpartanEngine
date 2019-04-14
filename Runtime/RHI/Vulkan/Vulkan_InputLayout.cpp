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
#include "../../Logging/Log.h"
#include "../../Core/EngineDefs.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector4.h"
//================================

//==================
using namespace std;
//==================

namespace Spartan
{
	RHI_InputLayout::RHI_InputLayout(const shared_ptr<RHI_Device>& rhi_device)
	{
		m_rhi_device = rhi_device;
	}

	RHI_InputLayout::~RHI_InputLayout()
	{
		auto buffer = static_cast<VkPipelineVertexInputStateCreateInfo*>(m_buffer);
		safe_delete(buffer);
		m_buffer = nullptr;
	}

	bool RHI_InputLayout::Create(void* vertex_shader_blob, const RHI_Vertex_Attribute_Type vertex_attributes_flag)
	{
		m_vertex_attributes = vertex_attributes_flag;

		vector<VkVertexInputAttributeDescription> vertex_attributes;
		uint32_t vertex_buffer_bind_id = 0;
		if (vertex_attributes_flag == Vertex_Attributes_Position2dTextureColor8)
		{
			vertex_attributes.emplace_back(VkVertexInputAttributeDescription{ vertex_buffer_bind_id, 0, VK_FORMAT_R32G32_SFLOAT, 0 });
			vertex_attributes.emplace_back(VkVertexInputAttributeDescription{ vertex_buffer_bind_id, 1, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 });
			vertex_attributes.emplace_back(VkVertexInputAttributeDescription{ vertex_buffer_bind_id, 2, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4 });
		}

		VkVertexInputBindingDescription binding_description = {};
		binding_description.binding							= 0;
		binding_description.stride							= sizeof(float) * 4;
		binding_description.inputRate						= VK_VERTEX_INPUT_RATE_VERTEX;

		auto vertex_input_info									= new VkPipelineVertexInputStateCreateInfo();
		vertex_input_info->sType								= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_info->vertexBindingDescriptionCount		= 1;
		vertex_input_info->pVertexBindingDescriptions			= &binding_description;
		vertex_input_info->vertexAttributeDescriptionCount		= static_cast<uint32_t>(vertex_attributes.size());
		vertex_input_info->pVertexAttributeDescriptions			= vertex_attributes.data();

		m_buffer = static_cast<void*>(vertex_input_info);
		return true;
	}
}
#endif