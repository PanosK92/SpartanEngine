/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =====================
#include "Spartan.h"
#include "../RHI_Implementation.h"
#include "../RHI_PipelineState.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    inline VkAttachmentLoadOp get_color_load_op(const Math::Vector4& color)
    {
        if (color == rhi_color_dont_care)
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    
        if (color == rhi_color_load)
            return VK_ATTACHMENT_LOAD_OP_LOAD;
    
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    inline VkAttachmentLoadOp get_depth_load_op(const float depth)
    {
        if (depth == rhi_depth_dont_care)
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (depth == rhi_depth_load)
            return VK_ATTACHMENT_LOAD_OP_LOAD;

        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    inline VkAttachmentLoadOp get_stencil_load_op(const uint32_t stencil)
    {
        if (stencil == rhi_stencil_dont_care)
            return VK_ATTACHMENT_LOAD_OP_DONT_CARE;

        if (stencil == rhi_stencil_load)
            return VK_ATTACHMENT_LOAD_OP_LOAD;

        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    };

    inline VkAttachmentStoreOp get_stencil_store_op(const RHI_DepthStencilState* depth_stencil_state)
    {
        return depth_stencil_state->GetStencilWriteEnabled() ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    };
    
    inline bool create_render_pass(
        RHI_Context* rhi_context,
        RHI_DepthStencilState* depth_stencil_state,
        RHI_SwapChain* render_target_swapchain,
        array<RHI_Texture*, rhi_max_render_target_count>& render_target_color_textures,
        array<Math::Vector4, rhi_max_render_target_count>& render_target_color_clear,
        RHI_Texture* render_target_depth_texture,
        float clear_value_depth,
        uint32_t clear_value_stencil,
        void*& render_pass
    )
    {
        // Attachments
        vector<VkAttachmentDescription> attachment_descriptions;
        vector<VkAttachmentReference> attachment_references;
        {
            VkAttachmentLoadOp load_op_stencil      = get_stencil_load_op(clear_value_stencil);
            VkAttachmentStoreOp store_op_stencil    = get_stencil_store_op(depth_stencil_state);

            // Color
            {
                // Swapchain
                if (render_target_swapchain)
                {
                    VkImageLayout layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

                    VkAttachmentDescription attachment_desc  = {};
                    attachment_desc.format                   = rhi_context->surface_format;
                    attachment_desc.samples                  = VK_SAMPLE_COUNT_1_BIT;
                    attachment_desc.loadOp                   = get_color_load_op(render_target_color_clear[0]);
                    attachment_desc.storeOp                  = VK_ATTACHMENT_STORE_OP_STORE;
                    attachment_desc.stencilLoadOp            = load_op_stencil;
                    attachment_desc.stencilStoreOp           = store_op_stencil;
                    attachment_desc.initialLayout            = layout;
                    attachment_desc.finalLayout              = layout;

                    // Description
                    attachment_descriptions.push_back(attachment_desc);
                    // Reference
                    attachment_references.push_back({ static_cast<uint32_t>(attachment_references.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
                }
                else // Textures
                {
                    for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
                    {
                        RHI_Texture* texture = render_target_color_textures[i];
                        if (!texture)
                            continue;

                        VkImageLayout layout = vulkan_image_layout[static_cast<uint8_t>(texture->GetLayout())];

                        VkAttachmentDescription attachment_desc  = {};
                        attachment_desc.format                   = vulkan_format[texture->GetFormat()];
                        attachment_desc.samples                  = VK_SAMPLE_COUNT_1_BIT;
                        attachment_desc.loadOp                   = get_color_load_op(render_target_color_clear[i]);
                        attachment_desc.storeOp                  = VK_ATTACHMENT_STORE_OP_STORE;
                        attachment_desc.stencilLoadOp            = load_op_stencil;
                        attachment_desc.stencilStoreOp           = store_op_stencil;
                        attachment_desc.initialLayout            = layout;
                        attachment_desc.finalLayout              = layout;

                        // Description
                        attachment_descriptions.push_back(attachment_desc);
                        // Reference
                        attachment_references.push_back({ static_cast<uint32_t>(attachment_references.size()), layout });
                    }
                }
            }
    
            // Depth
            if (render_target_depth_texture)
            {
                VkImageLayout layout = vulkan_image_layout[static_cast<uint8_t>(render_target_depth_texture->GetLayout())];

                VkAttachmentDescription attachment_desc  = {};
                attachment_desc.format                   = vulkan_format[render_target_depth_texture->GetFormat()];
                attachment_desc.samples                  = VK_SAMPLE_COUNT_1_BIT;
                attachment_desc.loadOp                   = get_depth_load_op(clear_value_depth);
                attachment_desc.storeOp                  = VK_ATTACHMENT_STORE_OP_STORE;
                attachment_desc.stencilLoadOp            = load_op_stencil;
                attachment_desc.stencilStoreOp           = store_op_stencil;
                attachment_desc.initialLayout            = layout;
                attachment_desc.finalLayout              = layout;

                // Description
                attachment_descriptions.push_back(attachment_desc);
                // Reference
                attachment_references.push_back({ static_cast<uint32_t>(attachment_references.size()), layout });
            }
        }
    
        // Subpass
        VkSubpassDescription subpass    = {};
        subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount    = static_cast<uint32_t>(render_target_depth_texture ? attachment_references.size() - 1 : attachment_references.size());
        subpass.pColorAttachments       = attachment_references.data();
        subpass.pDepthStencilAttachment = render_target_depth_texture ? &attachment_references.back() : nullptr;
    
        VkRenderPassCreateInfo render_pass_info = {};
        render_pass_info.sType                  = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        render_pass_info.attachmentCount        = static_cast<uint32_t>(attachment_descriptions.size());
        render_pass_info.pAttachments           = attachment_descriptions.data();
        render_pass_info.subpassCount           = 1;
        render_pass_info.pSubpasses             = &subpass;
        render_pass_info.dependencyCount        = 0;
        render_pass_info.pDependencies          = nullptr;
    
        return vulkan_utility::error::check(vkCreateRenderPass(rhi_context->device, &render_pass_info, nullptr, reinterpret_cast<VkRenderPass*>(&render_pass)));
    }

    inline bool create_frame_buffer(RHI_Context* rhi_context, void* render_pass, const std::vector<void*>& attachments, const uint32_t width, const uint32_t height, void*& frame_buffer)
    {
        VkFramebufferCreateInfo create_info = {};
        create_info.sType                   = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        create_info.renderPass              = static_cast<VkRenderPass>(render_pass);
        create_info.attachmentCount         = static_cast<uint32_t>(attachments.size());
        create_info.pAttachments            = reinterpret_cast<const VkImageView*>(attachments.data());
        create_info.width                   = width;
        create_info.height                  = height;
        create_info.layers                  = 1;

        return vulkan_utility::error::check(vkCreateFramebuffer(rhi_context->device, &create_info, nullptr, reinterpret_cast<VkFramebuffer*>(&frame_buffer)));
    }
  
    void* RHI_PipelineState::GetFrameBuffer() const
    {
        // If this is a swapchain, return the appropriate buffer
        if (render_target_swapchain)
        {
            if (render_target_swapchain->GetImageIndex() >= rhi_max_render_target_count)
            {
                LOG_ERROR("Invalid image index, %d", render_target_swapchain->GetImageIndex());
                return nullptr;
            }

            return m_frame_buffers[render_target_swapchain->GetImageIndex()];
        }

        // If this is a render texture, return the first buffer 
        return m_frame_buffers[0];
    }

    bool RHI_PipelineState::CreateFrameResources(const RHI_Device* rhi_device)
    {
        if (IsCompute())
            return true;

        m_rhi_device = rhi_device;

        const uint32_t render_target_width  = GetWidth();
        const uint32_t render_target_height = GetHeight();

        // Destroy existing frame resources
        DestroyFrameResources();

        // Create a render pass
        if (!create_render_pass(m_rhi_device->GetContextRhi(), depth_stencil_state, render_target_swapchain, render_target_color_textures, clear_color, render_target_depth_texture, clear_depth, clear_stencil, m_render_pass))
            return false;

        // Name the render pass
        string name = render_target_swapchain ? ("render_pass_swapchain_" + to_string(m_hash)) : ("render_pass_texture_" + to_string(m_hash));
        vulkan_utility::debug::set_name(static_cast<VkRenderPass>(m_render_pass), name.c_str());

        // Create frame buffer
        if (render_target_swapchain)
        {
            // Create one frame buffer per image
            for (uint32_t i = 0; i < render_target_swapchain->GetBufferCount(); i++)
            {
                vector<void*> attachments = { render_target_swapchain->Get_Resource_View(i) };
                if (!create_frame_buffer(m_rhi_device->GetContextRhi(), m_render_pass, attachments, render_target_width, render_target_height, m_frame_buffers[i]))
                    return false;

                // Name the frame buffer
                vulkan_utility::debug::set_name(static_cast<VkFramebuffer>(m_frame_buffers[i]), "frame_bufer_swapchain");
            }

            return true;
        }
        else
        {
            vector<void*> attachments;
            
            // Color
            for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
            {
                if (RHI_Texture* texture = render_target_color_textures[i])
                {
                    attachments.emplace_back(texture->Get_Resource_View_RenderTarget(render_target_color_texture_array_index));
                }
            }
            
            // Depth
            if (render_target_depth_texture)
            {
                attachments.emplace_back(render_target_depth_texture->Get_Resource_View_DepthStencil(render_target_depth_stencil_texture_array_index));
            }

            // Create a frame buffer
            if (!create_frame_buffer(m_rhi_device->GetContextRhi(), m_render_pass, attachments, render_target_width, render_target_height, m_frame_buffers[0]))
                return false;
            
            // Name the frame buffer
            vulkan_utility::debug::set_name(static_cast<VkFramebuffer>(m_frame_buffers[0]), "frame_bufer_texture");
            
            return true;
        }

        return true;
    }

    void RHI_PipelineState::DestroyFrameResources()
    {
        if (!m_rhi_device)
            return;

        // Wait in case the buffer is still in use by the graphics queue
        m_rhi_device->Queue_Wait(RHI_Queue_Graphics);

        for (uint32_t i = 0; i < rhi_max_render_target_count; i++)
        {
            if (void* frame_buffer = m_frame_buffers[i])
            {
                vkDestroyFramebuffer(m_rhi_device->GetContextRhi()->device, static_cast<VkFramebuffer>(frame_buffer), nullptr);
            }
        }
        m_frame_buffers.fill(nullptr);

        // Destroy render pass
        vkDestroyRenderPass(m_rhi_device->GetContextRhi()->device, static_cast<VkRenderPass>(m_render_pass), nullptr);
        m_render_pass = nullptr;
    }
}
