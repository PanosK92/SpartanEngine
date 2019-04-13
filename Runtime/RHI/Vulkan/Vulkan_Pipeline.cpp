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
#include "../../Logging/Log.h"
#include "../../Math/Matrix.h"
#include "../RHI_Texture.h"
#include "../RHI_Sampler.h"
#include "../RHI_InputLayout.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	// This entire pipeline is temporary just so I can play with Vulkan a bit.

	static unique_ptr<RHI_ConstantBuffer> g_constant_buffer;

	RHI_Pipeline::~RHI_Pipeline()
	{	
		vkDestroyPipeline(m_rhi_device->GetContext()->device, static_cast<VkPipeline>(m_graphics_pipeline), nullptr);
		vkDestroyPipelineLayout(m_rhi_device->GetContext()->device, static_cast<VkPipelineLayout>(m_pipeline_layout), nullptr);
		vkDestroyRenderPass(m_rhi_device->GetContext()->device, static_cast<VkRenderPass>(m_render_pass), nullptr);
		vkDestroyDescriptorPool(m_rhi_device->GetContext()->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
		vkDestroyDescriptorSetLayout(m_rhi_device->GetContext()->device, static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout), nullptr);

		m_graphics_pipeline		= nullptr;
		m_pipeline_layout		= nullptr;
		m_render_pass			= nullptr;
		m_descriptor_pool		= nullptr;	
	}

	inline void CreateDescriptorSet(void* sampler, void* texture, const VkDevice& device, void*& descriptor_set_layout_out, void*& descriptor_set_out, void*& descriptor_pool_out)
	{
		uint32_t fix_this = 1;//static_cast<uint32_t>(swapChainImages.size());

		// Descriptor pool
		VkDescriptorPoolSize poolSize	= {};
		poolSize.type					= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		poolSize.descriptorCount		= fix_this;

		VkDescriptorPoolCreateInfo pool_create_info = {};
		pool_create_info.sType						= VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_create_info.flags						= VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_create_info.poolSizeCount				= 1;
		pool_create_info.pPoolSizes					= &poolSize;
		pool_create_info.maxSets					= fix_this;

		VkDescriptorPool descriptor_pool = nullptr;
		if (vkCreateDescriptorPool(device, &pool_create_info, nullptr, &descriptor_pool) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create descriptor layout");
			return;
		}
		descriptor_pool_out = static_cast<void*>(descriptor_pool);

		// Descriptor set layout - THIS MUST BE AUTOMATED
		// Create binding and layout for the following, matching contents of shader (this must happen automatically)	
		//   binding 0 = uniform buffer 
		//   binding 2 = sampler
		//   binding 1 = texture2D
		VkDescriptorSetLayoutBinding layout;
		vector<VkDescriptorSetLayoutBinding> resource_bindings;
		
		layout.binding				= 0;
		layout.descriptorType		= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		layout.descriptorCount		= 1;
		layout.stageFlags			= VK_SHADER_STAGE_VERTEX_BIT;
		layout.pImmutableSamplers	= nullptr;
		resource_bindings.push_back(layout);

		layout.binding				= 1;
		layout.descriptorType		= VK_DESCRIPTOR_TYPE_SAMPLER;
		layout.descriptorCount		= 1;
		layout.stageFlags			= VK_SHADER_STAGE_FRAGMENT_BIT;
		layout.pImmutableSamplers	= nullptr;
		resource_bindings.push_back(layout);

		layout.binding				= 2;
		layout.descriptorType		= VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
		layout.descriptorCount		= 1;
		layout.stageFlags			= VK_SHADER_STAGE_FRAGMENT_BIT;
		layout.pImmutableSamplers	= nullptr;
		resource_bindings.push_back(layout);

		VkDescriptorSetLayoutCreateInfo resource_layout_info = {};
		resource_layout_info.sType			= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		resource_layout_info.pNext			= nullptr;
		resource_layout_info.bindingCount	= static_cast<uint32_t>(resource_bindings.size());
		resource_layout_info.pBindings		= resource_bindings.data();

		VkDescriptorSetLayout descriptor_set_layout;
		if (vkCreateDescriptorSetLayout(device, &resource_layout_info, nullptr, &descriptor_set_layout) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create descriptor layout");
			return;
		}
		descriptor_set_layout_out = static_cast<VkDescriptorSetLayout>(descriptor_set_layout);

		// Descriptor set
		VkDescriptorSetAllocateInfo allocInfo	= {};
		allocInfo.sType							= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocInfo.descriptorPool				= static_cast<VkDescriptorPool>(descriptor_pool);
		allocInfo.descriptorSetCount			= 1;
		allocInfo.pSetLayouts					= &descriptor_set_layout;

		VkDescriptorSet descriptor_set = nullptr;
		if (vkAllocateDescriptorSets(device, &allocInfo, &descriptor_set) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to allocate descriptor set");
			return;
		}
		descriptor_set_out = static_cast<void*>(descriptor_set);

		for (size_t i = 0; i < fix_this; i++) 
		{
			VkDescriptorBufferInfo bufferInfo	= {};
			bufferInfo.buffer					= static_cast<VkBuffer>(g_constant_buffer->GetBufferView());
			bufferInfo.offset					= 0;
			bufferInfo.range					= g_constant_buffer->GetSize();

			VkDescriptorImageInfo image_info	= {};
			image_info.imageLayout				= VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			image_info.imageView				= static_cast<VkImageView>(texture);
			image_info.sampler					= static_cast<VkSampler>(sampler);

			std::array<VkWriteDescriptorSet, 3> descriptorWrites = {};

			descriptorWrites[0].sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[0].dstSet			= descriptor_set;
			descriptorWrites[0].dstBinding		= 0;
			descriptorWrites[0].dstArrayElement = 0;
			descriptorWrites[0].descriptorType	= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			descriptorWrites[0].descriptorCount = 1;
			descriptorWrites[0].pBufferInfo		= &bufferInfo;

			descriptorWrites[1].sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[1].dstSet			= descriptor_set;
			descriptorWrites[1].dstBinding		= 1;
			descriptorWrites[1].dstArrayElement = 0;
			descriptorWrites[1].descriptorType	= VK_DESCRIPTOR_TYPE_SAMPLER;
			descriptorWrites[1].descriptorCount = 1;
			descriptorWrites[1].pImageInfo		= &image_info;

			descriptorWrites[2].sType			= VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			descriptorWrites[2].dstSet			= descriptor_set;
			descriptorWrites[2].dstBinding		= 2;
			descriptorWrites[2].dstArrayElement = 0;
			descriptorWrites[2].descriptorType	= VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			descriptorWrites[2].descriptorCount = 1;
			descriptorWrites[2].pImageInfo		= &image_info;

			vkUpdateDescriptorSets(device, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);
		}
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

		VkSubpassDependency dependency	= {};
		dependency.srcSubpass			= VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass			= 0;
		dependency.srcStageMask			= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask		= 0;
		dependency.dstStageMask			= VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask		= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo render_pass_info = {};
		render_pass_info.sType					= VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		render_pass_info.attachmentCount		= 1;
		render_pass_info.pAttachments			= &color_attachment;
		render_pass_info.subpassCount			= 1;
		render_pass_info.pSubpasses				= &subpass;
		render_pass_info.dependencyCount		= 1;
		render_pass_info.pDependencies			= &dependency;

		VkRenderPass render_pass = nullptr;
		if (vkCreateRenderPass(device, &render_pass_info, nullptr, &render_pass) != VK_SUCCESS)
		{
			LOG_ERROR("Failed to create render pass");
		}

		return render_pass;
	}

	bool RHI_Pipeline::Create()
	{
		bool dynamic_viewport_scissor = false;

		// State deduction
		if (!m_viewport.IsDefined())
		{
			dynamic_viewport_scissor = true;
		}

		// Uniform buffer
		g_constant_buffer = make_unique<RHI_ConstantBuffer>(m_rhi_device, sizeof(Math::Matrix));
		CreateDescriptorSet(m_sampler->GetBufferView(), m_texture->GetBufferView(), m_rhi_device->GetContext()->device, m_descriptor_set_layout, m_descriptor_set, m_descriptor_pool);
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
		shader_vertex_stage_info.module								= static_cast<VkShaderModule>(m_shader_vertex->GetVertexShaderBuffer());
		shader_vertex_stage_info.pName								= m_shader_vertex->GetVertexEntryPoint().c_str();

		// Pixel shader
		VkPipelineShaderStageCreateInfo shader_pixel_stage_info = {};
		shader_pixel_stage_info.sType							= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		shader_pixel_stage_info.stage							= VK_SHADER_STAGE_FRAGMENT_BIT;
		shader_pixel_stage_info.module							= static_cast<VkShaderModule>(m_shader_pixel->GetPixelShaderBuffer());
		shader_pixel_stage_info.pName							= m_shader_pixel->GetPixelEntryPoint().c_str();

		// Shader stages
		VkPipelineShaderStageCreateInfo shader_stages[] = { shader_vertex_stage_info, shader_pixel_stage_info };

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
		blend_state_attachments.blendEnable							= VK_FALSE;
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

		// Pipeline layout
		VkPipelineLayoutCreateInfo pipeline_layout_info	= {};
		pipeline_layout_info.sType						= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.pushConstantRangeCount		= 0;
		pipeline_layout_info.setLayoutCount				= 1;
		pipeline_layout_info.pSetLayouts				= &static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout);

		VkPipelineLayout pipeline_layout;
		if (vkCreatePipelineLayout(m_rhi_device->GetContext()->device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to create pipeline layout");
			return false;
		}
		m_pipeline_layout = static_cast<void*>(pipeline_layout);

		VkGraphicsPipelineCreateInfo pipeline_info	= {};
		pipeline_info.sType							= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.stageCount					= 2;
		pipeline_info.pStages						= shader_stages;
		pipeline_info.pVertexInputState				= static_cast<VkPipelineVertexInputStateCreateInfo*>(m_input_layout->GetBuffer());
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
}
#endif