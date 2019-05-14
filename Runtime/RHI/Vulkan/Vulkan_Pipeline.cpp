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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_VULKAN
//================================

//= INCLUDES ======================
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_BlendState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_Texture.h"
#include "../RHI_Sampler.h"
#include "../RHI_InputLayout.h"
#include "../../Logging/Log.h"
#include "../../Math/Matrix.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_Pipeline::~RHI_Pipeline()
	{
		vkDestroyPipeline(m_rhi_device->GetContext()->device, static_cast<VkPipeline>(m_graphics_pipeline), nullptr);
		m_graphics_pipeline = nullptr;

		vkDestroyPipelineLayout(m_rhi_device->GetContext()->device, static_cast<VkPipelineLayout>(m_pipeline_layout), nullptr);
		m_pipeline_layout = nullptr;

		vkDestroyRenderPass(m_rhi_device->GetContext()->device, static_cast<VkRenderPass>(m_render_pass), nullptr);
		m_render_pass = nullptr;

		vkDestroyDescriptorSetLayout(m_rhi_device->GetContext()->device, static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout), nullptr);
		m_descriptor_set_layout = nullptr;
	}

	inline VkRenderPass CreateRenderPass(const VkDevice& device)
	{
		VkAttachmentDescription color_attachment	= {};
		color_attachment.format						= VkFormat::VK_FORMAT_B8G8R8A8_UNORM; // this has to come from the swapchain
		color_attachment.samples					= VK_SAMPLE_COUNT_1_BIT;
		color_attachment.loadOp						= VK_ATTACHMENT_LOAD_OP_CLEAR;
		color_attachment.storeOp					= VK_ATTACHMENT_STORE_OP_STORE;
		color_attachment.stencilLoadOp				= VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		color_attachment.stencilStoreOp				= VK_ATTACHMENT_STORE_OP_DONT_CARE;
		color_attachment.initialLayout				= VK_IMAGE_LAYOUT_UNDEFINED;
		color_attachment.finalLayout				= VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference color_attachment_ref	= {};
		color_attachment_ref.attachment				= 0;
		color_attachment_ref.layout					= VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass	= {};
		subpass.pipelineBindPoint		= VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount	= 1;
		subpass.pColorAttachments		= &color_attachment_ref;

		// Sub-pass dependencies for layout transitions
		vector<VkSubpassDependency> dependencies
		{
			VkSubpassDependency
			{
				VK_SUBPASS_EXTERNAL,																							// uint32_t srcSubpass;
				0,																												// uint32_t dstSubpass;
				VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,													// PipelineStageFlags srcStageMask;
				VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,											// PipelineStageFlags dstStageMask;
				VkAccessFlagBits::VK_ACCESS_MEMORY_READ_BIT,																	// AccessFlags srcAccessMask;
				VkAccessFlagBits::VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VkAccessFlagBits::VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags dstAccessMask;
				VkDependencyFlagBits::VK_DEPENDENCY_BY_REGION_BIT																// DependencyFlags dependencyFlags;
			},

			VkSubpassDependency
			{
				0,																												// uint32_t srcSubpass;
				VK_SUBPASS_EXTERNAL,																							// uint32_t dstSubpass;
				VkPipelineStageFlagBits::VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,											// PipelineStageFlags srcStageMask;
				VkPipelineStageFlagBits::VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,													// PipelineStageFlags dstStageMask;
				VkAccessFlagBits::VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VkAccessFlagBits::VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,	// AccessFlags srcAccessMask;
				VkAccessFlagBits::VK_ACCESS_MEMORY_READ_BIT,																	// AccessFlags dstAccessMask;
				VkDependencyFlagBits::VK_DEPENDENCY_BY_REGION_BIT																// DependencyFlags dependencyFlags;
			},
		};

		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType					= VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount		= 1;
		render_pass_info.pAttachments			= &color_attachment;
		render_pass_info.subpassCount			= 1;
		render_pass_info.pSubpasses				= &subpass;
		render_pass_info.dependencyCount		= static_cast<uint32_t>(dependencies.size());
		render_pass_info.pDependencies			= dependencies.data();

		VkRenderPass render_pass = nullptr;
		if (vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create render pass");
		}

		return render_pass;
	}

	inline void CreateDescriptorSetLayout(const VkDevice& device, const map<string, Shader_Resource>& shader_resources, void*& descriptor_set_layout_out)
	{
		// Layout bindings
		vector<VkDescriptorSetLayoutBinding> layout_bindings;
		{
			for (const auto& resource : shader_resources)
			{
				VkShaderStageFlags stage_flags = (resource.second.shader_type == Shader_Vertex) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;

				layout_bindings.push_back
				({
					resource.second.slot,							// binding
					vulkan_descriptor_type[resource.second.type],	// descriptorType
					1,												// descriptorCount
					stage_flags,									// stageFlags
					nullptr											// pImmutableSamplers
					});
			}
		}
		
		// Create info
		VkDescriptorSetLayoutCreateInfo create_info	= {};
		create_info.sType							= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		create_info.flags							= 0;
		create_info.pNext							= nullptr;
		create_info.bindingCount					= static_cast<uint32_t>(layout_bindings.size());
		create_info.pBindings						= layout_bindings.data();

		// Descriptor set layout
		VkDescriptorSetLayout descriptor_set_layout;
		auto result = vkCreateDescriptorSetLayout(device, &create_info, nullptr, &descriptor_set_layout);
		if (result != VK_SUCCESS)
		{
			LOGF_ERROR("Failed to create descriptor layout, %s", Vulkan_Common::result_to_string(result));
			return;
		}
		descriptor_set_layout_out = static_cast<void*>(descriptor_set_layout);
	}

	bool RHI_Pipeline::Create()
	{
		bool dynamic_viewport_scissor = false;

		// State deduction
		if (!m_viewport.IsDefined())
		{
			dynamic_viewport_scissor = true;
		}

		// Create render pass
		m_render_pass = static_cast<void*>(CreateRenderPass(m_rhi_device->GetContext()->device));

		// Dynamic viewport and scissor states
		vector<VkDynamicState> dynamic_states =
		{
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR
		};

		VkPipelineDynamicStateCreateInfo dynamic_state;
		dynamic_state.sType				= VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic_state.pNext				= nullptr;
		dynamic_state.flags				= 0;
		dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
		dynamic_state.pDynamicStates	= dynamic_states.data();

		// Viewport
		VkViewport vkViewport	= {};
		vkViewport.x			= m_viewport.GetX();
		vkViewport.y			= m_viewport.GetY();
		vkViewport.width		= m_viewport.GetWidth();
		vkViewport.height		= m_viewport.GetHeight();
		vkViewport.minDepth		= m_viewport.GetMinDepth();
		vkViewport.maxDepth		= m_viewport.GetMaxDepth();

		// Scissor
		VkRect2D scissor		= {};
		scissor.offset			= { 0, 0 };
		scissor.extent.width	= static_cast<uint32_t>(vkViewport.width);
		scissor.extent.height	= static_cast<uint32_t>(vkViewport.height);

		// Viewport state
		VkPipelineViewportStateCreateInfo viewport_state	= {};
		viewport_state.sType								= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount						= 1;
		viewport_state.pViewports							= &vkViewport;
		viewport_state.scissorCount							= 1;
		viewport_state.pScissors							= &scissor;

		// Vertex shader
		VkPipelineShaderStageCreateInfo shader_vertex_stage_info	= {};
		shader_vertex_stage_info.sType								= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shader_vertex_stage_info.stage								= VK_SHADER_STAGE_VERTEX_BIT;
		shader_vertex_stage_info.module								= static_cast<VkShaderModule>(m_shader_vertex->GetResource_VertexShader());
		shader_vertex_stage_info.pName								= m_shader_vertex->GetVertexEntryPoint().c_str();

		// Pixel shader
		VkPipelineShaderStageCreateInfo shader_pixel_stage_info = {};
		shader_pixel_stage_info.sType							= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shader_pixel_stage_info.stage							= VK_SHADER_STAGE_FRAGMENT_BIT;
		shader_pixel_stage_info.module							= static_cast<VkShaderModule>(m_shader_pixel->GetResource_PixelShader());
		shader_pixel_stage_info.pName							= m_shader_pixel->GetPixelEntryPoint().c_str();

		// Shader stages
		vector<VkPipelineShaderStageCreateInfo> shader_stages = { shader_vertex_stage_info, shader_pixel_stage_info };

		ReflectShaders();

		// Descriptor set layout and descriptor sets
		CreateDescriptorSetLayout(m_rhi_device->GetContext()->device, m_shader_resources, m_descriptor_set_layout);

		// Vertex input state
		//auto vertex_input_state = static_cast<VkPipelineVertexInputStateCreateInfo*>(m_shader_vertex->GetInputLayout()->GetBuffer());

		// Binding description
		VkVertexInputBindingDescription binding_description = {};
		binding_description.binding		= 0;
		binding_description.inputRate	= VK_VERTEX_INPUT_RATE_VERTEX;
		binding_description.stride		= sizeof(float) * 4 + sizeof(uint32_t); // size of the vertex must be known here

		// Vertex attributes description
		uint32_t vertex_buffer_bind_id = 0;
		vector<VkVertexInputAttributeDescription> vertex_attribute_descs;
		vertex_attribute_descs.emplace_back(VkVertexInputAttributeDescription{ 0, vertex_buffer_bind_id, VK_FORMAT_R32G32_SFLOAT, 0 });
		vertex_attribute_descs.emplace_back(VkVertexInputAttributeDescription{ 1, vertex_buffer_bind_id, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 });
		vertex_attribute_descs.emplace_back(VkVertexInputAttributeDescription{ 2, vertex_buffer_bind_id, VK_FORMAT_R8G8B8A8_UNORM, sizeof(float) * 4 });

		// Vertex input state
		VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
		vertex_input_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_state.vertexBindingDescriptionCount = 1;
		vertex_input_state.pVertexBindingDescriptions = &binding_description;
		vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attribute_descs.size());
		vertex_input_state.pVertexAttributeDescriptions = vertex_attribute_descs.data();

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
		input_assembly_state.sType									= VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly_state.topology								= vulkan_primitive_topology[m_primitive_topology];
		input_assembly_state.primitiveRestartEnable					= VK_FALSE;

		// Rasterizer state
		VkPipelineRasterizationStateCreateInfo rasterizer_state	= {};
		rasterizer_state.sType									= VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer_state.depthClampEnable						= VK_FALSE;
		rasterizer_state.rasterizerDiscardEnable				= VK_FALSE;
		rasterizer_state.polygonMode							= vulkan_polygon_Mode[m_rasterizer_state->GetFillMode()];
		rasterizer_state.lineWidth								= 1.0f;
		rasterizer_state.cullMode								= vulkan_cull_Mode[m_rasterizer_state->GetCullMode()];
		rasterizer_state.frontFace								= VK_FRONT_FACE_CLOCKWISE;
		rasterizer_state.depthBiasEnable						= VK_FALSE;
		rasterizer_state.depthBiasConstantFactor				= 0.0f;
		rasterizer_state.depthBiasClamp							= 0.0f;
		rasterizer_state.depthBiasSlopeFactor					= 0.0f;

		// Mutlisampling
		VkPipelineMultisampleStateCreateInfo multisampling_state	= {};
		multisampling_state.sType									= VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling_state.sampleShadingEnable						= m_rasterizer_state->GetMultiSampleEnabled() ? VK_TRUE : VK_FALSE;
		multisampling_state.rasterizationSamples					= VK_SAMPLE_COUNT_1_BIT;

		// Blend state
		VkPipelineColorBlendAttachmentState blend_state_attachments = {};
		blend_state_attachments.colorWriteMask						= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		blend_state_attachments.blendEnable							= m_blend_state->GetBlendEnabled() ? VK_TRUE : VK_FALSE;
		blend_state_attachments.srcColorBlendFactor					= vulkan_blend_factor[m_blend_state->GetSourceBlend()];
		blend_state_attachments.dstColorBlendFactor					= vulkan_blend_factor[m_blend_state->GetDestBlend()];
		blend_state_attachments.colorBlendOp						= vulkan_blend_operation[m_blend_state->GetBlendOp()];
		blend_state_attachments.srcAlphaBlendFactor					= vulkan_blend_factor[m_blend_state->GetSourceBlendAlpha()];
		blend_state_attachments.dstAlphaBlendFactor					= vulkan_blend_factor[m_blend_state->GetDestBlendAlpha()];
		blend_state_attachments.alphaBlendOp						= vulkan_blend_operation[m_blend_state->GetBlendOpAlpha()];

		VkPipelineColorBlendStateCreateInfo color_blend_State	= {};
		color_blend_State.sType									= VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend_State.logicOpEnable							= VK_FALSE;
		color_blend_State.logicOp								= VK_LOGIC_OP_COPY;
		color_blend_State.attachmentCount						= 1;
		color_blend_State.pAttachments							= &blend_state_attachments;
		color_blend_State.blendConstants[0]						= 0.0f;
		color_blend_State.blendConstants[1]						= 0.0f;
		color_blend_State.blendConstants[2]						= 0.0f;
		color_blend_State.blendConstants[3]						= 0.0f;

		// Pipeline layout create info
		auto vk_descriptor_set_layout = static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout);
		VkPipelineLayoutCreateInfo pipeline_layout_info	= {};
		pipeline_layout_info.sType						= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.pushConstantRangeCount		= 0;
		pipeline_layout_info.setLayoutCount				= 1;		
		pipeline_layout_info.pSetLayouts				= &vk_descriptor_set_layout;

		// Pipeline layout
		VkPipelineLayout pipeline_layout;
		if (vkCreatePipelineLayout(m_rhi_device->GetContext()->device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to create pipeline layout");
			return false;
		}
		m_pipeline_layout = static_cast<void*>(pipeline_layout);

		VkGraphicsPipelineCreateInfo pipeline_info	= {};
		pipeline_info.sType							= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.stageCount					= static_cast<uint32_t>(shader_stages.size());
		pipeline_info.pStages						= shader_stages.data();
		pipeline_info.pVertexInputState				= &vertex_input_state;
		pipeline_info.pInputAssemblyState			= &input_assembly_state;
		pipeline_info.pDynamicState					= dynamic_viewport_scissor ? &dynamic_state : nullptr;
		pipeline_info.pViewportState				= dynamic_viewport_scissor ? &viewport_state : nullptr;
		pipeline_info.pRasterizationState			= &rasterizer_state;
		pipeline_info.pMultisampleState				= &multisampling_state;
		pipeline_info.pColorBlendState				= &color_blend_State;
		pipeline_info.layout						= pipeline_layout;
		pipeline_info.renderPass					= static_cast<VkRenderPass>(m_render_pass);
		pipeline_info.subpass						= 0;
		pipeline_info.basePipelineHandle			= nullptr;

		VkPipeline graphics_pipeline = nullptr;
		if (vkCreateGraphicsPipelines(m_rhi_device->GetContext()->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphics_pipeline) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to create graphics pipeline");
			return false;
		}

		m_graphics_pipeline = static_cast<void*>(graphics_pipeline);

		return true;
	}

	void RHI_Pipeline::UpdateDescriptorSets(RHI_Texture* texture /*= nullptr*/)
	{
		if (!texture || !texture->GetResource_Texture())
			return;

		// Early exit if descriptor set already exists
		if (m_descriptor_sets.count(texture->RHI_GetID()))
			return;

		auto descriptor_set_layout = static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout);

		// Allocate descriptor set
		VkDescriptorSet descriptor_set;
		{
			// Allocate info
			VkDescriptorSetAllocateInfo allocate_info	= {};
			allocate_info.sType							= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocate_info.descriptorPool				= m_rhi_device->GetContext()->descriptor_pool;
			allocate_info.descriptorSetCount			= 1;
			allocate_info.pSetLayouts					= &descriptor_set_layout;

			// Allocate		
			auto result = vkAllocateDescriptorSets(m_rhi_device->GetContext()->device, &allocate_info, &descriptor_set);
			if (result != VK_SUCCESS)
			{
				LOGF_ERROR("Failed to allocate descriptor set, %s", Vulkan_Common::result_to_string(result));
				return;
			}
		}

		// Update descriptor sets
		{
			VkDescriptorImageInfo image_info	= {};
			image_info.imageLayout				= VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			image_info.imageView				= texture ? static_cast<VkImageView>(texture->GetResource_Texture()) : nullptr;
			image_info.sampler					= m_sampler ? static_cast<VkSampler>(m_sampler->GetResource()) : nullptr;

			VkDescriptorBufferInfo buffer_info	= {};
			buffer_info.buffer					= m_constant_buffer ? static_cast<VkBuffer>(m_constant_buffer->GetResource()) : nullptr;
			buffer_info.offset					= 0;
			buffer_info.range					= m_constant_buffer ? m_constant_buffer->GetSize() : 0;

			vector<VkWriteDescriptorSet> write_descriptor_sets;
			for (const auto& resource : m_shader_resources)
			{
				write_descriptor_sets.push_back
				({
					VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,			// sType
					nullptr,										// pNext
					descriptor_set,									// dstSet
					resource.second.slot,							// dstBinding
					0,												// dstArrayElement
					1,												// descriptorCount
					vulkan_descriptor_type[resource.second.type],	// descriptorType
					&image_info,									// pImageInfo 
					&buffer_info,									// pBufferInfo
					nullptr											// pTexelBufferView
				});
			}
			vkUpdateDescriptorSets(m_rhi_device->GetContext()->device, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
		}

		m_descriptor_sets[texture->RHI_GetID()] = static_cast<void*>(descriptor_set);
	}

	void RHI_Pipeline::ReflectShaders()
	{
		// Wait for shaders to finish compilation
		while (m_shader_vertex->GetCompilationState() == Compilation_State::Shader_Compiling || m_shader_pixel->GetCompilationState() == Compilation_State::Shader_Compiling) {} 

		// Merge vertex & index shader resources into map (to ensure unique values)
		for (const auto& resource : m_shader_vertex->GetResources())	m_shader_resources[resource.name] = resource;
		for (const auto& resource : m_shader_pixel->GetResources())		m_shader_resources[resource.name] = resource;
	}
}
#endif