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

//= IMPLEMENTATION ===============
#ifdef API_GRAPHICS_VULKAN
#include "../RHI_Implementation.h"
//================================

//= INCLUDES ====================
#include "../RHI_PipelineState.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
    void* RHI_PipelineState::GetFrameBuffer() const
    {
        // If this is a swapchain, return the appropriate buffer
        if (render_target_swapchain)
        {
            return m_frame_buffers[render_target_swapchain->GetImageIndex()];
        }

        // If this is a render texture, return the first buffer 
        return m_frame_buffers[0];
    }

    bool RHI_PipelineState::CreateFrameResources(const RHI_Device* rhi_device)
    {
        m_rhi_device                = rhi_device;
        RHI_Context* rhi_context    = rhi_device->GetContextRhi();

        // Detect all used render target color textures
        vector<RHI_Texture*> used_render_target_color;
        for (uint8_t i = 0; i < state_max_render_target_count; i++)
        {
            if (render_target_color_textures[i])
            {
                used_render_target_color.emplace_back(render_target_color_textures[i]);
            }
        }

        const bool is_swapchain                     = render_target_swapchain != nullptr;
        const uint32_t render_target_color_count    = is_swapchain ? 1 : static_cast<uint32_t>(used_render_target_color.size());
        const uint32_t render_target_depth_count    = render_target_depth_texture ? 1 : 0;
        const uint32_t render_target_width          = is_swapchain ? render_target_swapchain->GetWidth()    : render_target_color_count != 0 ? used_render_target_color[0]->GetWidth()  : ( render_target_depth_texture ? render_target_depth_texture->GetWidth() : 0);
        const uint32_t render_target_height         = is_swapchain ? render_target_swapchain->GetHeight()   : render_target_color_count != 0 ? used_render_target_color[0]->GetHeight() : ( render_target_depth_texture ? render_target_depth_texture->GetHeight() : 0);
        RHI_Texture** target_array                  = is_swapchain ? nullptr : used_render_target_color.data();

        // Destroy existing render pass and frame buffer (if any)
        DestroyFrameResources();

        // Create a render pass
        if (!vulkan_common::render_pass::create(rhi_context, target_array, clear_color, render_target_color_count, render_target_depth_texture, clear_depth, clear_stencil, is_swapchain, m_render_pass))
            return false;

        // Name the render pass
        vulkan_common::debug::set_render_pass_name(rhi_context->device, static_cast<VkRenderPass>(m_render_pass), is_swapchain ? "swapchain" : "texture");

        // Create frame buffer
        if (is_swapchain)
        {
            // Create one frame buffer per image
            for (uint32_t i = 0; i < render_target_swapchain->GetBufferCount(); i++)
            {
                vector<void*> attachments = { render_target_swapchain->GetResource_ShaderView(i) };
                if (!vulkan_common::frame_buffer::create(rhi_context, m_render_pass, attachments, render_target_width, render_target_height, m_frame_buffers[i]))
                    return false;

                // Name the frame buffer
                vulkan_common::debug::set_framebuffer_name(rhi_context->device, static_cast<VkFramebuffer>(m_frame_buffers[i]), "swapchain");
            }

            return true;
        }
        else
        {
            vector<void*> attachments(render_target_color_count + render_target_depth_count);
            
            // Color
            for (uint32_t i = 0; i < render_target_color_count; i++)
            {
                attachments[i] = used_render_target_color[i]->Get_View_Texture();
            }
            
            // Depth
            if (render_target_depth_texture)
            {
                attachments.back() = render_target_depth_texture->Get_View_Texture();
            }

            // Create a frame buffer
            if (!vulkan_common::frame_buffer::create(rhi_context, m_render_pass, attachments, render_target_width, render_target_height, m_frame_buffers[0]))
                return false;
            
            // Name the frame buffer
            vulkan_common::debug::set_framebuffer_name(rhi_context->device, static_cast<VkFramebuffer>(m_frame_buffers[0]), "texture");
            
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

        for (auto& frame_buffer : m_frame_buffers)
        {
            vulkan_common::frame_buffer::destroy(m_rhi_device->GetContextRhi(), frame_buffer);
        }
        vulkan_common::render_pass::destroy(m_rhi_device->GetContextRhi(), m_render_pass);
    }
}
#endif
