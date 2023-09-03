/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ==========================
#include "pch.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Implementation.h"
#include "../RHI_Shader.h"
#include "../RHI_SwapChain.h"
#include "../RHI_BlendState.h"
#include "../RHI_InputLayout.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_DescriptorSetLayout.h"
#include "../RHI_Device.h"
#include "../RHI_Texture.h"
//=====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    RHI_Pipeline::RHI_Pipeline(RHI_PipelineState& pipeline_state, RHI_DescriptorSetLayout* descriptor_set_layout)
    {
        m_state = pipeline_state;

        // pipeline layout
        {
            // order is important here, as it will be used to index the descriptor sets
            array<void*, 3> layouts =
            {
                descriptor_set_layout->GetRhiResource(),
                RHI_Device::GetDescriptorSetLayout(RHI_Device_Resource::sampler_comparison),
                RHI_Device::GetDescriptorSetLayout(RHI_Device_Resource::sampler_regular)
            };

            // validate descriptor set layouts
            for (void* layout : layouts)
            {
                SP_ASSERT(layout != nullptr);
            }

            // push constant buffers
            vector<VkPushConstantRange> push_constant_ranges;
            for (const RHI_Descriptor& descriptor : descriptor_set_layout->GetDescriptors())
            {
                if (descriptor.type == RHI_Descriptor_Type::PushConstantBuffer)
                { 
                    SP_ASSERT(descriptor.struct_size <= RHI_Device::PropertyGetMaxPushConstantSize());

                    VkPushConstantRange push_constant_range  = {};
                    push_constant_range.offset               = 0;
                    push_constant_range.size                 = descriptor.struct_size;
                    push_constant_range.stageFlags           = (descriptor.stage & RHI_Shader_Stage::RHI_Shader_Vertex)  ? VK_SHADER_STAGE_VERTEX_BIT   : 0;
                    push_constant_range.stageFlags          |= (descriptor.stage & RHI_Shader_Stage::RHI_Shader_Pixel)   ? VK_SHADER_STAGE_FRAGMENT_BIT : 0;
                    push_constant_range.stageFlags          |= (descriptor.stage & RHI_Shader_Stage::RHI_Shader_Compute) ? VK_SHADER_STAGE_COMPUTE_BIT  : 0;

                    push_constant_ranges.emplace_back(push_constant_range);
                }
            }
         
            // pipeline layout
            VkPipelineLayoutCreateInfo pipeline_layout_info = {};
            pipeline_layout_info.sType                      = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipeline_layout_info.pushConstantRangeCount     = 0;
            pipeline_layout_info.setLayoutCount             = static_cast<uint32_t>(layouts.size());
            pipeline_layout_info.pSetLayouts                = reinterpret_cast<VkDescriptorSetLayout*>(layouts.data());
            pipeline_layout_info.pushConstantRangeCount     = static_cast<uint32_t>(push_constant_ranges.size());
            pipeline_layout_info.pPushConstantRanges        = push_constant_ranges.data();

            // create
            SP_VK_ASSERT_MSG(vkCreatePipelineLayout(RHI_Context::device, &pipeline_layout_info, nullptr, reinterpret_cast<VkPipelineLayout*>(&m_resource_pipeline_layout)),
                "Failed to create pipeline layout");

            // name
            RHI_Device::SetResourceName(m_resource_pipeline_layout, RHI_Resource_Type::PipelineLayout, pipeline_state.name);
        }

        // viewport & scissor
        vector<VkDynamicState> dynamic_states            = {};
        VkPipelineDynamicStateCreateInfo dynamic_state   = {};
        VkViewport vkViewport                            = {};
        VkRect2D scissor                                 = {};
        VkPipelineViewportStateCreateInfo viewport_state = {};
        {
            // always allow dynamic viewport
            dynamic_states.emplace_back(VK_DYNAMIC_STATE_VIEWPORT);

            // If this is always on, Vulkan will expect you to set a scissor rectangle dynamically.
            // Because of this, we just rely on dynamic_scissor.
            if (m_state.dynamic_scissor)
            {
                dynamic_states.emplace_back(VK_DYNAMIC_STATE_SCISSOR);
            }

            // dynamic states
            dynamic_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamic_state.pNext             = nullptr;
            dynamic_state.flags             = 0;
            dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
            dynamic_state.pDynamicStates    = dynamic_states.data();
        
            // viewport
            vkViewport.x        = 0;
            vkViewport.y        = 0;
            vkViewport.width    = static_cast<float>(m_state.GetWidth());
            vkViewport.height   = static_cast<float>(m_state.GetHeight());
            vkViewport.minDepth = 0.0f;
            vkViewport.maxDepth = 1.0f;
        
            // scissor
            scissor.offset.x      = 0;
            scissor.offset.y      = 0;
            scissor.extent.width  = static_cast<uint32_t>(vkViewport.width);
            scissor.extent.height = static_cast<uint32_t>(vkViewport.height);
        
            // viewport state
            viewport_state.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewport_state.viewportCount = 1;
            viewport_state.pViewports    = &vkViewport;
            viewport_state.scissorCount  = 1;
            viewport_state.pScissors     = &scissor;
        }
        
        // shader stages
        vector<VkPipelineShaderStageCreateInfo> shader_stages;
        
        // Shader - Vertex
        if (m_state.shader_vertex)
        {
            VkPipelineShaderStageCreateInfo shader_stage_info = {};
            shader_stage_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.stage                           = VK_SHADER_STAGE_VERTEX_BIT;
            shader_stage_info.module                          = static_cast<VkShaderModule>(m_state.shader_vertex->GetRhiResource());
            shader_stage_info.pName                           = m_state.shader_vertex->GetEntryPoint();

            // Validate shader stage
            SP_ASSERT(shader_stage_info.module != nullptr);
            SP_ASSERT(shader_stage_info.pName != nullptr);

            shader_stages.push_back(shader_stage_info);
        }
        
        // Shader - Pixel
        if (m_state.shader_pixel)
        {
            VkPipelineShaderStageCreateInfo shader_stage_info = {};
            shader_stage_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.stage                           = VK_SHADER_STAGE_FRAGMENT_BIT;
            shader_stage_info.module                          = static_cast<VkShaderModule>(m_state.shader_pixel->GetRhiResource());
            shader_stage_info.pName                           = m_state.shader_pixel->GetEntryPoint();

            // Validate shader stage
            SP_ASSERT(shader_stage_info.module != nullptr);
            SP_ASSERT(shader_stage_info.pName != nullptr);

            shader_stages.push_back(shader_stage_info);
        }

        // Shader - Compute
        if (m_state.shader_compute)
        {
            VkPipelineShaderStageCreateInfo shader_stage_info = {};
            shader_stage_info.sType                           = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            shader_stage_info.stage                           = VK_SHADER_STAGE_COMPUTE_BIT;
            shader_stage_info.module                          = static_cast<VkShaderModule>(m_state.shader_compute->GetRhiResource());
            shader_stage_info.pName                           = m_state.shader_compute->GetEntryPoint();

            // Validate shader stage
            SP_ASSERT(shader_stage_info.module != nullptr);
            SP_ASSERT(shader_stage_info.pName != nullptr);

            shader_stages.push_back(shader_stage_info);
        }
        
        // Binding description
        VkVertexInputBindingDescription binding_description = {};
        binding_description.binding                         = 0;
        binding_description.inputRate                       = VK_VERTEX_INPUT_RATE_VERTEX;
        binding_description.stride                          = m_state.shader_vertex ? m_state.shader_vertex->GetVertexSize() : 0;
        
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
                        desc.location,                                   // location
                        desc.binding,                                    // binding
                        vulkan_format[rhi_format_to_index(desc.format)], // format
                        desc.offset                                      // offset
                        });
                }
            }
        }

        // Vertex input state
        VkPipelineVertexInputStateCreateInfo vertex_input_state = {};
        {
            vertex_input_state.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertex_input_state.vertexBindingDescriptionCount   = m_state.can_use_vertex_index_buffers ? 1 : 0;
            vertex_input_state.pVertexBindingDescriptions      = m_state.can_use_vertex_index_buffers ? &binding_description : nullptr;
            vertex_input_state.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertex_attribute_descs.size());
            vertex_input_state.pVertexAttributeDescriptions    = vertex_attribute_descs.data();
        }
        
        // Input assembly
        VkPipelineInputAssemblyStateCreateInfo input_assembly_state = {};
        {
            input_assembly_state.sType                  = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            input_assembly_state.topology               = vulkan_primitive_topology[static_cast<uint32_t>(m_state.primitive_topology)];
            input_assembly_state.primitiveRestartEnable = VK_FALSE;
        }
        
        // Rasterizer state
        VkPipelineRasterizationStateCreateInfo rasterizer_state             = {};
        VkPipelineRasterizationDepthClipStateCreateInfoEXT depth_clip_state = {};
        if (m_state.rasterizer_state)
        {
            depth_clip_state.sType           = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT;
            depth_clip_state.depthClipEnable = m_state.rasterizer_state->GetDepthClipEnabled();
            depth_clip_state.pNext           = nullptr;

            rasterizer_state.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizer_state.pNext                   = &depth_clip_state;
            rasterizer_state.depthClampEnable        = VK_FALSE;
            rasterizer_state.rasterizerDiscardEnable = VK_FALSE;
            rasterizer_state.polygonMode             = vulkan_polygon_mode[static_cast<uint32_t>(m_state.rasterizer_state->GetPolygonMode())];
            rasterizer_state.lineWidth               = m_state.rasterizer_state->GetLineWidth();
            rasterizer_state.cullMode                = vulkan_cull_mode[static_cast<uint32_t>(m_state.rasterizer_state->GetCullMode())];
            rasterizer_state.frontFace               = VK_FRONT_FACE_CLOCKWISE;
            rasterizer_state.depthBiasEnable         = m_state.rasterizer_state->GetDepthBias() != 0.0f ? VK_TRUE : VK_FALSE;
            rasterizer_state.depthBiasConstantFactor = Math::Helper::Floor(m_state.rasterizer_state->GetDepthBias() * (float)(1 << 24));
            rasterizer_state.depthBiasClamp          = m_state.rasterizer_state->GetDepthBiasClamp();
            rasterizer_state.depthBiasSlopeFactor    = m_state.rasterizer_state->GetDepthBiasSlopeScaled();
        }
        
        // Mutlisampling
        VkPipelineMultisampleStateCreateInfo multisampling_state = {};
        {
            multisampling_state.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampling_state.sampleShadingEnable  = VK_FALSE;
            multisampling_state.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        }
        
        VkPipelineColorBlendStateCreateInfo color_blend_state = {};
        vector<VkPipelineColorBlendAttachmentState> blend_state_attachments;
        if (m_state.blend_state)
        {
            // Blend state attachments
            {
                // Same blend state for all
                VkPipelineColorBlendAttachmentState blend_state_attachment = {};
                blend_state_attachment.colorWriteMask                      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
                blend_state_attachment.blendEnable                         = m_state.blend_state->GetBlendEnabled() ? VK_TRUE : VK_FALSE;
                blend_state_attachment.srcColorBlendFactor                 = vulkan_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetSourceBlend())];
                blend_state_attachment.dstColorBlendFactor                 = vulkan_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetDestBlend())];
                blend_state_attachment.colorBlendOp                        = vulkan_blend_operation[static_cast<uint32_t>(m_state.blend_state->GetBlendOp())];
                blend_state_attachment.srcAlphaBlendFactor                 = vulkan_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetSourceBlendAlpha())];
                blend_state_attachment.dstAlphaBlendFactor                 = vulkan_blend_factor[static_cast<uint32_t>(m_state.blend_state->GetDestBlendAlpha())];
                blend_state_attachment.alphaBlendOp                        = vulkan_blend_operation[static_cast<uint32_t>(m_state.blend_state->GetBlendOpAlpha())];

                // Swapchain
                if (m_state.render_target_swapchain)
                {
                    blend_state_attachments.push_back(blend_state_attachment);
                }

                // Render target(s)
                for (uint8_t i = 0; i < rhi_max_render_target_count; i++)
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
        if (m_state.depth_stencil_state)
        {
            depth_stencil_state.sType             = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depth_stencil_state.depthTestEnable   = m_state.depth_stencil_state->GetDepthTestEnabled();
            depth_stencil_state.depthWriteEnable  = m_state.depth_stencil_state->GetDepthWriteEnabled();
            depth_stencil_state.depthCompareOp    = vulkan_compare_operator[static_cast<uint32_t>(m_state.depth_stencil_state->GetDepthComparisonFunction())];
            depth_stencil_state.stencilTestEnable = m_state.depth_stencil_state->GetStencilTestEnabled();
            depth_stencil_state.front.compareOp   = vulkan_compare_operator[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilComparisonFunction())];
            depth_stencil_state.front.failOp      = vulkan_stencil_operation[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilFailOperation())];
            depth_stencil_state.front.depthFailOp = vulkan_stencil_operation[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilDepthFailOperation())];
            depth_stencil_state.front.passOp      = vulkan_stencil_operation[static_cast<uint32_t>(m_state.depth_stencil_state->GetStencilPassOperation())];
            depth_stencil_state.front.compareMask = m_state.depth_stencil_state->GetStencilReadMask();
            depth_stencil_state.front.writeMask   = m_state.depth_stencil_state->GetStencilWriteMask();
            depth_stencil_state.front.reference   = 1;
            depth_stencil_state.back              = depth_stencil_state.front;
        }

        // Pipeline
        {
            VkPipeline* pipeline = reinterpret_cast<VkPipeline*>(&m_resource_pipeline);

            if (pipeline_state.IsGraphics())
            {
                // Enable dynamic rendering - VK_KHR_dynamic_rendering.
                // This means no render passes and no frame buffer objects.
                VkPipelineRenderingCreateInfoKHR pipeline_rendering_create_info = {};
                vector<VkFormat> attachment_formats_color;
                VkFormat attachment_format_depth   = VK_FORMAT_UNDEFINED;
                VkFormat attachment_format_stencil = VK_FORMAT_UNDEFINED;
                {
                    // Swapchain buffer as a render target
                    if (m_state.render_target_swapchain)
                    {
                        attachment_formats_color.push_back(vulkan_format[rhi_format_to_index(m_state.render_target_swapchain->GetFormat())]);
                    }
                    else // Regular render target(s)
                    {
                        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
                        {
                            RHI_Texture* texture = m_state.render_target_color_textures[i];
                            if (texture == nullptr)
                                break;

                            attachment_formats_color.push_back(vulkan_format[rhi_format_to_index(texture->GetFormat())]);
                        }
                    }

                    // Depth
                    if (m_state.render_target_depth_texture)
                    {
                        RHI_Texture* tex_depth    = m_state.render_target_depth_texture;
                        attachment_format_depth   = vulkan_format[rhi_format_to_index(tex_depth->GetFormat())];
                        attachment_format_stencil = tex_depth->IsStencilFormat() ? attachment_format_depth : VK_FORMAT_UNDEFINED;
                    }

                    pipeline_rendering_create_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
                    pipeline_rendering_create_info.colorAttachmentCount    = static_cast<uint32_t>(attachment_formats_color.size());
                    pipeline_rendering_create_info.pColorAttachmentFormats = attachment_formats_color.data();
                    pipeline_rendering_create_info.depthAttachmentFormat   = attachment_format_depth;
                    pipeline_rendering_create_info.stencilAttachmentFormat = attachment_format_stencil;
                }

                // Describe
                VkGraphicsPipelineCreateInfo pipeline_info = {};
                pipeline_info.pNext                        = &pipeline_rendering_create_info;
                pipeline_info.sType                        = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
                pipeline_info.stageCount                   = static_cast<uint32_t>(shader_stages.size());
                pipeline_info.pStages                      = shader_stages.data();
                pipeline_info.pVertexInputState            = &vertex_input_state;
                pipeline_info.pInputAssemblyState          = &input_assembly_state;
                pipeline_info.pDynamicState                = &dynamic_state;
                pipeline_info.pViewportState               = &viewport_state;
                pipeline_info.pRasterizationState          = &rasterizer_state;
                pipeline_info.pMultisampleState            = &multisampling_state;
                pipeline_info.pColorBlendState             = &color_blend_state;
                pipeline_info.pDepthStencilState           = &depth_stencil_state;
                pipeline_info.layout                       = static_cast<VkPipelineLayout>(m_resource_pipeline_layout);
                pipeline_info.renderPass                   = nullptr;
        
                // Create
                SP_VK_ASSERT_MSG(vkCreateGraphicsPipelines(RHI_Context::device, nullptr, 1, &pipeline_info, nullptr, pipeline),
                    "Failed to create graphics pipeline");

                // Disable naming until I can come up with a more meaningful name
                // vulkan_utility::debug::set_name(*pipeline, m_state.pass_name);
            }
            else if (pipeline_state.IsCompute())
            {
                // Describe
                VkComputePipelineCreateInfo pipeline_info = {};
                pipeline_info.sType                       = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
                pipeline_info.layout                      = static_cast<VkPipelineLayout>(m_resource_pipeline_layout);
                pipeline_info.stage                       = shader_stages[0];

                // Create
                SP_VK_ASSERT_MSG(vkCreateComputePipelines(RHI_Context::device, nullptr, 1, &pipeline_info, nullptr, pipeline),
                    "Failed to create compute pipeline");

                // Name
                RHI_Device::SetResourceName(static_cast<void*>(*pipeline), RHI_Resource_Type::Pipeline, pipeline_state.name);
            }
        }
    }
    
    RHI_Pipeline::~RHI_Pipeline()
    {
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::Pipeline, m_resource_pipeline);
        m_resource_pipeline = nullptr;
        
        RHI_Device::DeletionQueueAdd(RHI_Resource_Type::PipelineLayout, m_resource_pipeline_layout);
        m_resource_pipeline_layout = nullptr;
    }
}
