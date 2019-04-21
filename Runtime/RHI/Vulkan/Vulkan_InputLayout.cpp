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
		safe_delete(m_buffer);
	}

	bool RHI_InputLayout::Create(void* vertex_shader_blob, const RHI_Vertex_Attribute_Type vertex_attributes)
	{
		// Binding description
		VkVertexInputBindingDescription binding_description = {};
		binding_description.binding							= 0;
		binding_description.inputRate						= VK_VERTEX_INPUT_RATE_VERTEX;
		binding_description.stride							= sizeof(float) * 8; // size of the vertex must be known here

		// Vertex attributes description
		uint32_t vertex_buffer_bind_id = 0;
		vector<VkVertexInputAttributeDescription> vertex_attribute_descs;
		vertex_attribute_descs.emplace_back(VkVertexInputAttributeDescription{ 0, vertex_buffer_bind_id, VK_FORMAT_R32G32_SFLOAT, 0 });
		vertex_attribute_descs.emplace_back(VkVertexInputAttributeDescription{ 1, vertex_buffer_bind_id, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 });
		vertex_attribute_descs.emplace_back(VkVertexInputAttributeDescription{ 2, vertex_buffer_bind_id, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4 });
		
		// Vertex input state
		auto* vertex_input_state = new VkPipelineVertexInputStateCreateInfo();
		vertex_input_state->sType								= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_state->vertexBindingDescriptionCount		= 1;
		vertex_input_state->pVertexBindingDescriptions			= &binding_description;
		vertex_input_state->vertexAttributeDescriptionCount		= static_cast<uint32_t>(vertex_attribute_descs.size());
		vertex_input_state->pVertexAttributeDescriptions		= reinterpret_cast<const VkVertexInputAttributeDescription*>(vertex_attribute_descs.data());

		// Save
		m_buffer = static_cast<void*>(vertex_input_state);
		
		return true;
	}
}
#endif