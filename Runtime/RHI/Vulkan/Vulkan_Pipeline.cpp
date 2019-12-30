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

//= INCLUDES ========================
#include "../RHI_Pipeline.h"
#include "../RHI_Device.h"
#include "../RHI_Shader.h"
#include "../RHI_Texture.h"
#include "../RHI_Sampler.h"
#include "../RHI_SwapChain.h"
#include "../RHI_BlendState.h"
#include "../RHI_InputLayout.h"
#include "../RHI_PipelineState.h"
#include "../RHI_ConstantBuffer.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_DepthStencilState.h"
#include "../../Logging/Log.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_Pipeline::RHI_Pipeline(const std::shared_ptr<RHI_Device>& rhi_device, RHI_PipelineState& pipeline_state)
	{
		m_rhi_device    = rhi_device;
		m_state         = pipeline_state;
        m_state.CreateFrameResources(rhi_device->GetContextRhi());

        // Viewport & Scissor
        vector<VkDynamicState> dynamic_states;
        VkPipelineDynamicStateCreateInfo dynamic_state      = {};   
        VkViewport vkViewport                               = {};
        VkRect2D scissor                                    = {};
        VkPipelineViewportStateCreateInfo viewport_state    = {};
        {
            // If no viewport has been provided, assume dynamic
            if (!m_state.viewport.IsDefined())
            {
                dynamic_states.emplace_back(VK_DYNAMIC_STATE_VIEWPORT);
            }

            if (m_state.scissor_dynamic)
            {
                dynamic_states.emplace_back(VK_DYNAMIC_STATE_SCISSOR);
            }

            // Dynamic states
		    dynamic_state.sType				= VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		    dynamic_state.pNext				= nullptr;
		    dynamic_state.flags				= 0;
		    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
		    dynamic_state.pDynamicStates	= dynamic_states.data();

		    // Viewport 
		    vkViewport.x			= m_state.viewport.x;
		    vkViewport.y			= m_state.viewport.y;
		    vkViewport.width		= m_state.viewport.width;
		    vkViewport.height		= m_state.viewport.height;
		    vkViewport.minDepth		= m_state.viewport.depth_min;
		    vkViewport.maxDepth		= m_state.viewport.depth_max;

		    // Scissor	   
            if (!m_state.scissor.IsDefined())
            {
                scissor.offset          = { 0, 0 };
                scissor.extent.width    = static_cast<uint32_t>(vkViewport.width);
                scissor.extent.height   = static_cast<uint32_t>(vkViewport.height);
            }
            else
            {
                scissor.offset          = { static_cast<int32_t>(m_state.scissor.x), static_cast<int32_t>(m_state.scissor.y) };
                scissor.extent.width    = static_cast<uint32_t>(m_state.scissor.width);
                scissor.extent.height   = static_cast<uint32_t>(m_state.scissor.height);
            }

		    // Viewport state
		    viewport_state.sType		    = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		    viewport_state.viewportCount    = 1;
		    viewport_state.pViewports	    = &vkViewport;
		    viewport_state.scissorCount	    = 1;
		    viewport_state.pScissors	    = &scissor;
        }

        // Shader stages
        vector<VkPipelineShaderStageCreateInfo> shader_stages;

		// Vertex shader
        if (m_state.shader_vertex)
        {
            VkPipelineShaderStageCreateInfo shader_vertex_stage_info    = {};
		    shader_vertex_stage_info.sType	                            = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		    shader_vertex_stage_info.stage	                            = VK_SHADER_STAGE_VERTEX_BIT;
		    shader_vertex_stage_info.module	                            = static_cast<VkShaderModule>(m_state.shader_vertex->GetResource());
		    shader_vertex_stage_info.pName	                            = m_state.shader_vertex->GetEntryPoint();

            if (!shader_vertex_stage_info.module || !shader_vertex_stage_info.pName)
            {
                LOG_ERROR("Vertex shader is invalid");
                return;
            }

            shader_stages.push_back(shader_vertex_stage_info);
        }
        else
        {
            LOG_ERROR("Vertex shader is invalid");
            return;
        }

		// Pixel shader
        if (m_state.shader_pixel)
        {
            VkPipelineShaderStageCreateInfo shader_pixel_stage_info = {};
		    shader_pixel_stage_info.sType	                        = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		    shader_pixel_stage_info.stage	                        = VK_SHADER_STAGE_FRAGMENT_BIT;
		    shader_pixel_stage_info.module	                        = static_cast<VkShaderModule>(m_state.shader_pixel->GetResource());
		    shader_pixel_stage_info.pName	                        = m_state.shader_pixel->GetEntryPoint();

            if (!shader_pixel_stage_info.module || !shader_pixel_stage_info.pName)
            {
                LOG_ERROR("Pixel shader is invalid");
                return;
            }

            shader_stages.push_back(shader_pixel_stage_info);
        }

		// Binding description
		VkVertexInputBindingDescription binding_description = {};
		binding_description.binding		= 0;
		binding_description.inputRate	= VK_VERTEX_INPUT_RATE_VERTEX;
		binding_description.stride		= m_state.vertex_buffer_stride;

		// Vertex attributes description
        vector<VkVertexInputAttributeDescription> vertex_attribute_descs;
        if (m_state.shader_vertex)
        {
            if (RHI_InputLayout* input_layout = m_state.shader_vertex->GetInputLayout().get())
            {
                vertex_attribute_descs.reserve(input_layout->GetAttributeDescriptions().size());
                for (const auto& desc : input_layout->GetAttributeDescriptions())
                {
                    vertex_attribute_descs.push_back
                    ({
                        desc.location,              // location
                        desc.binding,               // binding
                        vulkan_format[desc.format], // format
                        desc.offset                 // offset
                        });
                }
            }
        }

		// Vertex input state
		VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
        {
		    vertex_input_state.sType							= VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		    vertex_input_state.vertexBindingDescriptionCount	= 1;
		    vertex_input_state.pVertexBindingDescriptions		= &binding_description;
		    vertex_input_state.vertexAttributeDescriptionCount  = static_cast<uint32_t>(vertex_attribute_descs.size());
		    vertex_input_state.pVertexAttributeDescriptions		= vertex_attribute_descs.data();
        }

		// Input assembly
		VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
        {
		    input_assembly_state.sType					= VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		    input_assembly_state.topology				= vulkan_primitive_topology[m_state.primitive_topology];
		    input_assembly_state.primitiveRestartEnable = VK_FALSE;
        }

		// Rasterizer state
		VkPipelineRasterizationStateCreateInfo rasterizer_state	= {};
        {
		    rasterizer_state.sType					    = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		    rasterizer_state.depthClampEnable		    = VK_FALSE;
		    rasterizer_state.rasterizerDiscardEnable    = VK_FALSE;
		    rasterizer_state.polygonMode			    = vulkan_polygon_mode[m_state.rasterizer_state->GetFillMode()];
		    rasterizer_state.lineWidth				    = 1.0f;
		    rasterizer_state.cullMode				    = vulkan_cull_mode[m_state.rasterizer_state->GetCullMode()];
		    rasterizer_state.frontFace				    = VK_FRONT_FACE_CLOCKWISE;
		    rasterizer_state.depthBiasEnable		    = VK_FALSE;
		    rasterizer_state.depthBiasConstantFactor    = 0.0f;
		    rasterizer_state.depthBiasClamp			    = 0.0f;
		    rasterizer_state.depthBiasSlopeFactor	    = 0.0f;
        }

		// Mutlisampling
		VkPipelineMultisampleStateCreateInfo multisampling_state = {};
        {
		    multisampling_state.sType				    = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		    multisampling_state.sampleShadingEnable	    = m_state.rasterizer_state->GetMultiSampleEnabled() ? VK_TRUE : VK_FALSE;
		    multisampling_state.rasterizationSamples    = VK_SAMPLE_COUNT_1_BIT;
        }

		VkPipelineColorBlendStateCreateInfo color_blend_state = {};
        vector<VkPipelineColorBlendAttachmentState> blend_state_attachments;
        {
            // Blend state attachments
            {
                // Same blend state for all
                VkPipelineColorBlendAttachmentState blend_state_attachment  = {};
                blend_state_attachment.colorWriteMask	                    = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                blend_state_attachment.blendEnable		                    = m_state.blend_state->GetBlendEnabled() ? VK_TRUE : VK_FALSE;
                blend_state_attachment.srcColorBlendFactor                  = vulkan_blend_factor[m_state.blend_state->GetSourceBlend()];
                blend_state_attachment.dstColorBlendFactor                  = vulkan_blend_factor[m_state.blend_state->GetDestBlend()];
                blend_state_attachment.colorBlendOp                         = vulkan_blend_operation[m_state.blend_state->GetBlendOp()];
                blend_state_attachment.srcAlphaBlendFactor                  = vulkan_blend_factor[m_state.blend_state->GetSourceBlendAlpha()];
                blend_state_attachment.dstAlphaBlendFactor                  = vulkan_blend_factor[m_state.blend_state->GetDestBlendAlpha()];
                blend_state_attachment.alphaBlendOp                         = vulkan_blend_operation[m_state.blend_state->GetBlendOpAlpha()];

                // Swapchain
                if (m_state.render_target_swapchain)
                {
                    blend_state_attachments.push_back(blend_state_attachment);
                }

                // Render target(s)
                for (uint32_t i = 0; i < state_max_render_target_count; i++)
                {
                    if (m_state.render_target_color_textures[i] != nullptr)
                    {
                        blend_state_attachments.push_back(blend_state_attachment);
                    }
                }
            }
            
            color_blend_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            color_blend_state.logicOpEnable     = VK_FALSE;
            color_blend_state.logicOp           = VK_LOGIC_OP_COPY;
            color_blend_state.attachmentCount   = static_cast<uint32_t>(blend_state_attachments.size());
            color_blend_state.pAttachments      = blend_state_attachments.data();
		    color_blend_state.blendConstants[0] = m_state.blend_state->GetBlendFactor();
		    color_blend_state.blendConstants[1] = m_state.blend_state->GetBlendFactor();
		    color_blend_state.blendConstants[2] = m_state.blend_state->GetBlendFactor();
		    color_blend_state.blendConstants[3] = m_state.blend_state->GetBlendFactor();
        }

        // Depth-stencil state
        VkPipelineDepthStencilStateCreateInfo depth_stencil_state = {};
        {
            depth_stencil_state.sType               = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil_state.depthTestEnable     = m_state.depth_stencil_state->GetDepthTestEnabled();
            depth_stencil_state.depthWriteEnable    = m_state.depth_stencil_state->GetDepthWriteEnabled();
            depth_stencil_state.depthCompareOp      = vulkan_compare_operator[m_state.depth_stencil_state->GetDepthFunction()];
            depth_stencil_state.front               = depth_stencil_state.back;
            depth_stencil_state.back.compareOp      = VK_COMPARE_OP_ALWAYS;
        }

        // Pipeline layout
        auto pipeline_layout = reinterpret_cast<VkPipelineLayout*>(&m_pipeline_layout);
		VkPipelineLayoutCreateInfo pipeline_layout_info	= {};
        {
            // Create descriptor pool and descriptor set layout
            CreateDescriptorPool();
            ReflectShaders();
            CreateDescriptorSetLayout();

            auto vk_descriptor_set_layout = static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout);

		    pipeline_layout_info.sType						= VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		    pipeline_layout_info.pushConstantRangeCount		= 0;
		    pipeline_layout_info.setLayoutCount				= 1;		
		    pipeline_layout_info.pSetLayouts				= &vk_descriptor_set_layout;

            if (!vulkan_common::error::check_result(vkCreatePipelineLayout(m_rhi_device->GetContextRhi()->device, &pipeline_layout_info, nullptr, pipeline_layout)))
			    return;
        }

        // Pipeline
        VkGraphicsPipelineCreateInfo pipeline_info = {};
        {
		    pipeline_info.sType							= VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		    pipeline_info.stageCount					= static_cast<uint32_t>(shader_stages.size());
		    pipeline_info.pStages						= shader_stages.data();
		    pipeline_info.pVertexInputState				= &vertex_input_state;
		    pipeline_info.pInputAssemblyState			= &input_assembly_state;
		    pipeline_info.pDynamicState					= dynamic_states.empty() ? nullptr : &dynamic_state;
		    pipeline_info.pViewportState				= &viewport_state;
		    pipeline_info.pRasterizationState			= &rasterizer_state;
		    pipeline_info.pMultisampleState				= &multisampling_state;
		    pipeline_info.pColorBlendState				= &color_blend_state;
            pipeline_info.pDepthStencilState            = &depth_stencil_state;
		    pipeline_info.layout						= *pipeline_layout;
		    pipeline_info.renderPass					= static_cast<VkRenderPass>(m_state.GetRenderPass());

            auto pipeline = reinterpret_cast<VkPipeline*>(&m_pipeline);
            vulkan_common::error::check_result(vkCreateGraphicsPipelines(m_rhi_device->GetContextRhi()->device, nullptr, 1, &pipeline_info, nullptr, pipeline));
        }
	}

	RHI_Pipeline::~RHI_Pipeline()
	{
        // Wait in case the buffer is still in use by the graphics queue
        vkQueueWaitIdle(m_rhi_device->GetContextRhi()->queue_graphics);

		vkDestroyPipeline(m_rhi_device->GetContextRhi()->device, static_cast<VkPipeline>(m_pipeline), nullptr);
		m_pipeline = nullptr;

		vkDestroyPipelineLayout(m_rhi_device->GetContextRhi()->device, static_cast<VkPipelineLayout>(m_pipeline_layout), nullptr);
		m_pipeline_layout = nullptr;

		vkDestroyDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout), nullptr);
		m_descriptor_set_layout = nullptr;

		vkDestroyDescriptorPool(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
		m_descriptor_pool = nullptr;
	}

	void* RHI_Pipeline::CreateDescriptorSet(std::size_t hash)
	{
		// Early exit if the descriptor cache is full
		if (m_descriptor_resources.size() == m_descriptor_capacity)
			return nullptr;

		const auto descriptor_pool	= static_cast<VkDescriptorPool>(m_descriptor_pool);
		auto descriptor_set_layout	= static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout);

		// Allocate descriptor set
		VkDescriptorSet descriptor_set;
		{
			// Allocate info
			VkDescriptorSetAllocateInfo allocate_info	= {};
			allocate_info.sType							= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocate_info.descriptorPool				= descriptor_pool;
			allocate_info.descriptorSetCount			= 1;
			allocate_info.pSetLayouts					= &descriptor_set_layout;

			// Allocate		
			if (!vulkan_common::error::check_result(vkAllocateDescriptorSets(m_rhi_device->GetContextRhi()->device, &allocate_info, &descriptor_set)))
				return nullptr;

            string name = (m_state.shader_vertex ? m_state.shader_vertex->GetName() : "null") + "-" + (m_state.shader_pixel ? m_state.shader_pixel->GetName() : "null");
            vulkan_common::debug::set_descriptor_set_name(m_rhi_device->GetContextRhi()->device, descriptor_set, name.c_str());
		}

		// Update descriptor sets
        {
            vector<VkDescriptorImageInfo> image_infos;
            image_infos.reserve(m_descriptors.size());

            vector<VkDescriptorBufferInfo> buffer_infos;
            buffer_infos.reserve(m_descriptors.size());

            vector<VkWriteDescriptorSet> write_descriptor_sets;
            write_descriptor_sets.reserve(m_descriptors.size());

            for (RHI_Descriptor& resource_blueprint : m_descriptors)
            {
                // Ignore null resources (this is legal, as a render pass can choose to not use one or more resources)
                if (!resource_blueprint.resource)
                    continue;

                // Texture or Sampler
                image_infos.push_back
                ({
                    (resource_blueprint.type == RHI_Descriptor_Sampler) ? static_cast<VkSampler>(resource_blueprint.resource)    : nullptr, // sampler
                    (resource_blueprint.type == RHI_Descriptor_Texture) ? static_cast<VkImageView>(resource_blueprint.resource)  : nullptr, // imageView
                    vulkan_image_layout[resource_blueprint.layout]                                                                          // imageLayout
                });

                // Constant/Uniform buffer
                buffer_infos.push_back
                ({
                    (resource_blueprint.type == RHI_Descriptor_ConstantBuffer) ? static_cast<VkBuffer>(resource_blueprint.resource) : nullptr,  // buffer
                    0,                                                                                                                          // offset
                    (resource_blueprint.type == RHI_Descriptor_ConstantBuffer) ? resource_blueprint.size : 0,                                   // range                
                });

                write_descriptor_sets.push_back
                ({
                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,	            // sType
                    nullptr,								            // pNext
                    descriptor_set,							            // dstSet
                    resource_blueprint.slot,				            // dstBinding
                    0,									                // dstArrayElement
                    1,									                // descriptorCount
                    vulkan_descriptor_type[resource_blueprint.type],	// descriptorType
                    &image_infos.back(),                                // pImageInfo 
                    &buffer_infos.back(),                               // pBufferInfo
                    nullptr									            // pTexelBufferView
                });
            }

            vkUpdateDescriptorSets(m_rhi_device->GetContextRhi()->device, static_cast<uint32_t>(write_descriptor_sets.size()), write_descriptor_sets.data(), 0, nullptr);
		}
        
        void* descriptor = static_cast<void*>(descriptor_set);

        // Cache descriptor
        m_descriptor_resources[hash] = descriptor;

        return descriptor;
	}

    void RHI_Pipeline::OnCommandListConsumed()
	{
		// If the descriptor pool is full, re-allocate with double size
		if (m_descriptor_resources.size() < m_descriptor_capacity)
			return;

        if (!m_rhi_device || !m_rhi_device->GetContextRhi())
        {
            LOG_ERROR_INVALID_INTERNALS();
            return;
        }

		// Destroy layout
		vkDestroyDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorSetLayout>(m_descriptor_set_layout), nullptr);
		m_descriptor_set_layout = nullptr;

		// Destroy pool
		vkDestroyDescriptorPool(m_rhi_device->GetContextRhi()->device, static_cast<VkDescriptorPool>(m_descriptor_pool), nullptr);
		m_descriptor_pool = nullptr;

		// Clear cache (as it holds sets belonging to the destroyed pool)
        m_descriptor_resources.clear();

		// Re-allocate everything with double size
		m_descriptor_capacity *= 2;
		CreateDescriptorPool();
		CreateDescriptorSetLayout();
	}

    bool RHI_Pipeline::CreateDescriptorPool()
	{
		// Pool sizes
		VkDescriptorPoolSize pool_sizes[3]	= {};
		pool_sizes[0].type					= VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_sizes[0].descriptorCount       = m_constant_buffer_max;
		pool_sizes[1].type					= VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        pool_sizes[1].descriptorCount       = m_texture_max;
		pool_sizes[2].type					= VK_DESCRIPTOR_TYPE_SAMPLER;
        pool_sizes[2].descriptorCount       = m_sampler_max;
		
		// Create info
		VkDescriptorPoolCreateInfo pool_create_info = {};
		pool_create_info.sType						= VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_create_info.flags						= 0;
		pool_create_info.poolSizeCount				= static_cast<uint32_t>((sizeof(pool_sizes) / sizeof(*pool_sizes)));
		pool_create_info.pPoolSizes					= pool_sizes;
		pool_create_info.maxSets					= m_descriptor_capacity;
		
		// Pool
		const auto descriptor_pool = reinterpret_cast<VkDescriptorPool*>(&m_descriptor_pool);
        if (!vulkan_common::error::check_result(vkCreateDescriptorPool(m_rhi_device->GetContextRhi()->device, &pool_create_info, nullptr, descriptor_pool)))
            return false;

		return true;
	}

	bool RHI_Pipeline::CreateDescriptorSetLayout()
	{
		// Layout bindings
		vector<VkDescriptorSetLayoutBinding> layout_bindings;
		{
			for (const RHI_Descriptor& descriptor_blueprint : m_descriptors)
			{
                // Stage flags
                VkShaderStageFlags stage_flags = 0;
                stage_flags |= (descriptor_blueprint.stage & Shader_Vertex)     ? VK_SHADER_STAGE_VERTEX_BIT    : 0;
                stage_flags |= (descriptor_blueprint.stage & Shader_Pixel)      ? VK_SHADER_STAGE_FRAGMENT_BIT  : 0;
                stage_flags |= (descriptor_blueprint.stage & Shader_Compute)    ? VK_SHADER_STAGE_COMPUTE_BIT   : 0;

                layout_bindings.push_back
                ({
                    descriptor_blueprint.slot,							// binding
                    vulkan_descriptor_type[descriptor_blueprint.type],	// descriptorType
                    1,										            // descriptorCount
                    stage_flags,							            // stageFlags
                    nullptr									            // pImmutableSamplers
                });
			}
		}

		// Create info
		VkDescriptorSetLayoutCreateInfo create_info = {};
		create_info.sType							= VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		create_info.flags							= 0;
		create_info.pNext							= nullptr;
		create_info.bindingCount					= static_cast<uint32_t>(layout_bindings.size());
		create_info.pBindings						= layout_bindings.data();

		// Descriptor set layout
		auto descriptor_set_layout = reinterpret_cast<VkDescriptorSetLayout*>(&m_descriptor_set_layout);
        if (!vulkan_common::error::check_result(vkCreateDescriptorSetLayout(m_rhi_device->GetContextRhi()->device, &create_info, nullptr, descriptor_set_layout)))
			return false;

        string name = (m_state.shader_vertex ? m_state.shader_vertex->GetName() : "null") + "-" + (m_state.shader_pixel ? m_state.shader_pixel->GetName() : "null");
        vulkan_common::debug::set_descriptor_set_layout_name(m_rhi_device->GetContextRhi()->device, *descriptor_set_layout, name.c_str());

		return true;
	}
}
#endif
