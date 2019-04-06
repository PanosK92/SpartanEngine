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

//= INCLUDES ===============
#include "../RHI_Pipeline.h"
//==========================

//= NAMESPACES ================
using namespace std;
//using namespace Directus::Math;
//=============================

namespace Directus
{
	bool RHI_Pipeline::Create()
	{
		VkPipelineShaderStageCreateInfo vertShaderStageInfo = {};
		vertShaderStageInfo.sType	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertShaderStageInfo.stage	= VK_SHADER_STAGE_VERTEX_BIT;
		vertShaderStageInfo.module	= static_cast<VkShaderModule>(m_shader_vertex->GetVertexShaderBuffer());
		vertShaderStageInfo.pName	= m_shader_vertex->GetVertexEntryPoint();

		VkPipelineShaderStageCreateInfo fragShaderStageInfo = {};
		fragShaderStageInfo.sType	= VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragShaderStageInfo.stage	= VK_SHADER_STAGE_FRAGMENT_BIT;
		fragShaderStageInfo.module	= static_cast<VkShaderModule>(m_shader_pixel->GetPixelShaderBuffer());
		fragShaderStageInfo.pName	= m_shader_pixel->GetPixelEntryPoint();

		VkPipelineShaderStageCreateInfo shaderStages[] = { vertShaderStageInfo, fragShaderStageInfo };

		VkPipelineVertexInputStateCreateInfo vertex_input_info	= {};
		vertex_input_info.sType									= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertex_input_info.vertexBindingDescriptionCount			= 0;
		vertex_input_info.vertexAttributeDescriptionCount		= 0;

		VkPipelineInputAssemblyStateCreateInfo input_assembly	= {};
		input_assembly.sType									= VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		input_assembly.topology									= VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		input_assembly.primitiveRestartEnable					= VK_FALSE;

		VkViewport viewport = {};
		viewport.x			= 0.0f;
		viewport.y			= 0.0f;
		viewport.width		= (float)swapChainExtent.width;
		viewport.height		= (float)swapChainExtent.height;
		viewport.minDepth	= 0.0f;
		viewport.maxDepth	= 1.0f;

		VkRect2D scissor	= {};
		scissor.offset		= { 0, 0 };
		scissor.extent		= swapChainExtent;

		VkPipelineViewportStateCreateInfo viewport_state	= {};
		viewport_state.sType								= VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewport_state.viewportCount						= 1;
		viewport_state.pViewports							= &viewport;
		viewport_state.scissorCount							= 1;
		viewport_state.pScissors							= &scissor;

		VkPipelineRasterizationStateCreateInfo rasterizer	= {};
		rasterizer.sType									= VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		rasterizer.depthClampEnable							= VK_FALSE;
		rasterizer.rasterizerDiscardEnable					= VK_FALSE;
		rasterizer.polygonMode								= VK_POLYGON_MODE_FILL;
		rasterizer.lineWidth								= 1.0f;
		rasterizer.cullMode									= VK_CULL_MODE_BACK_BIT;
		rasterizer.frontFace								= VK_FRONT_FACE_CLOCKWISE;
		rasterizer.depthBiasEnable							= VK_FALSE;

		VkPipelineMultisampleStateCreateInfo multisampling	= {};
		multisampling.sType									= VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		multisampling.sampleShadingEnable					= VK_FALSE;
		multisampling.rasterizationSamples					= VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState color_blend_attachment	= {};
		color_blend_attachment.colorWriteMask						= VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
		color_blend_attachment.blendEnable							= VK_FALSE;

		VkPipelineColorBlendStateCreateInfo color_blend_info	= {};
		color_blend_info.sType									= VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		color_blend_info.logicOpEnable							= VK_FALSE;
		color_blend_info.logicOp								= VK_LOGIC_OP_COPY;
		color_blend_info.attachmentCount						= 1;
		color_blend_info.pAttachments							= &color_blend_attachment;
		color_blend_info.blendConstants[0]						= 0.0f;
		color_blend_info.blendConstants[1]						= 0.0f;
		color_blend_info.blendConstants[2]						= 0.0f;
		color_blend_info.blendConstants[3]						= 0.0f;

		VkPipelineLayoutCreateInfo pipeline_layout_info	= {};
		pipeline_layout_info.sType						= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		pipeline_layout_info.setLayoutCount				= 0;
		pipeline_layout_info.pushConstantRangeCount		= 0;

		if (vkCreatePipelineLayout(m_rhi_device->GetContext()->device, &pipeline_layout_info, nullptr, &pipelineLayout) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to create pipeline layout");
			return false;
		}

		VkGraphicsPipelineCreateInfo pipeline_info	= {};
		pipeline_info.sType							= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipeline_info.stageCount					= 2;
		pipeline_info.pStages						= shaderStages;
		pipeline_info.pVertexInputState				= &vertex_input_info;
		pipeline_info.pInputAssemblyState			= &input_assembly;
		pipeline_info.pViewportState				= &viewport_state;
		pipeline_info.pRasterizationState			= &rasterizer;
		pipeline_info.pMultisampleState				= &multisampling;
		pipeline_info.pColorBlendState				= &color_blend_info;
		pipeline_info.layout						= pipelineLayout;
		pipeline_info.renderPass					= renderPass;
		pipeline_info.subpass						= 0;
		pipeline_info.basePipelineHandle			= VK_NULL_HANDLE;

		if (vkCreateGraphicsPipelines(m_rhi_device->GetContext()->device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &graphicsPipeline) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to create graphics pipeline");
			return false;
		}

		//vkDestroyShaderModule(device, fragShaderModule, nullptr);
		//vkDestroyShaderModule(device, vertShaderModule, nullptr);

		return true;
	}

	bool RHI_Pipeline::CreateRenderPass()
	{
		VkAttachmentDescription color_attachment	= {};
		color_attachment.format						= swapChainImageFormat;
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

		if (vkCreateRenderPass(m_rhi_device->GetContext()->device, &render_pass_info, nullptr, &renderPass) != VK_SUCCESS) 
		{
			LOG_ERROR("Failed to create render pass");
			return false;
		}

		return true;
	}
}
#endif