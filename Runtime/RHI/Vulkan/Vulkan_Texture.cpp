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

//= INCLUDES =====================
#include "../RHI_Device.h"
#include "../RHI_Texture2D.h"
#include "../RHI_TextureCube.h"
#include "../../Math/MathHelper.h"
#include "../RHI_CommandList.h"
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    RHI_Texture2D::~RHI_Texture2D()
    {
        if (!m_rhi_device->IsInitialized())
            return;

        m_rhi_device->Queue_WaitAll();
        m_data.clear();
        const auto rhi_context = m_rhi_device->GetContextRhi();
        vulkan_common::image::view::destroy(rhi_context, m_resource_view[0]);
        vulkan_common::image::view::destroy(rhi_context, m_resource_view[1]);
        for (uint32_t i = 0; i < state_max_render_target_count; i++)
        {
            vulkan_common::image::view::destroy(rhi_context, m_resource_view_depthStencil[i]);
            vulkan_common::image::view::destroy(rhi_context, m_resource_view_renderTarget[i]);
        }
        vulkan_common::image::destroy(rhi_context, m_resource);
		vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_resource_memory);
	}

    void RHI_Texture::SetLayout(const RHI_Image_Layout new_layout, RHI_CommandList* command_list /*= nullptr*/)
    {
        if (m_layout == new_layout)
            return;

         // If a command list is provided, this means we should insert a pipeline barrier
        if (command_list)
        {
            if (!vulkan_common::image::set_layout(m_rhi_device.get(), static_cast<VkCommandBuffer>(command_list->GetResource_CommandBuffer()), this, new_layout))
                return;
        }

        m_layout = new_layout;
    }

	bool RHI_Texture2D::CreateResourceGpu()
	{
        const RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Create image
        {
            if (!vulkan_common::image::create(rhi_context, this, m_resource))
            {
                LOG_ERROR("Failed to create image");
                return false;
            }

            if (!vulkan_common::image::allocate_bind(rhi_context, m_resource, m_resource_memory))
            {
                LOG_ERROR("Failed to allocate and bind image memory");
                return false;
            }
        }

        // If the texture has any data, stage it
        if (HasData())
        {
            if (!vulkan_common::image::stage(m_rhi_device.get(), this))
                return false;
        }

        // Transition to target layout
        if (VkCommandBuffer cmd_buffer = vulkan_common::command_buffer_immediate::begin(m_rhi_device.get(), RHI_Queue_Graphics))
        {    
            RHI_Image_Layout target_layout = RHI_Image_Preinitialized;
        
            if (IsSampled() && IsColorFormat())
                target_layout = RHI_Image_Shader_Read_Only_Optimal;
        
            if (IsRenderTargetColor())
                target_layout = RHI_Image_Color_Attachment_Optimal;
        
            if (IsRenderTargetDepthStencil())
                target_layout = RHI_Image_Depth_Stencil_Attachment_Optimal;
        
            // Transition to the final layout
            if (!vulkan_common::image::set_layout(m_rhi_device.get(), cmd_buffer, this, target_layout))
                return false;
        
            // Flush
            if (!vulkan_common::command_buffer_immediate::end(RHI_Queue_Graphics))
                return false;

            // Update this texture with the new layout
            m_layout = target_layout;
        }

        // Create image views
        {
            // Shader resource views
            if (IsSampled())
            {
                if (IsColorFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view[0], this))
                        return false;
                }

                if (IsDepthFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view[0], this, 0, m_array_size, true, false))
                        return false;
                }

                if (IsStencilFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view[1], this, 0, m_array_size, false, true))
                        return false;
                }
            }

            // Render target views
            for (uint32_t i = 0; i < m_array_size; i++)
            {
                if (IsRenderTargetColor())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view_renderTarget[i], this, i, 1))
                        return false;
                }

                if (IsRenderTargetDepthStencil())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view_depthStencil[i], this, i, 1, true))
                        return false;
                }
            }

            // Name the image and image view(s)
            {
                string name = GetResourceName();

                if (IsSampled())
                {
                    name += name.empty() ? "sampled" : "-sampled";
                }

                if (IsRenderTargetColor())
                {
                    name += name.empty() ? "render_target_color" : "-render_target_color";
                }

                if (IsRenderTargetDepthStencil())
                {
                    name += name.empty() ? "render_target_depth" : "-render_target_depth";
                }

                vulkan_common::debug::set_image_name(rhi_context->device, static_cast<VkImage>(m_resource), name.c_str());
                vulkan_common::debug::set_image_view_name(rhi_context->device, static_cast<VkImageView>(m_resource_view[0]), name.c_str());
                if (IsSampled() && IsStencilFormat())
                {
                    vulkan_common::debug::set_image_view_name(rhi_context->device, static_cast<VkImageView>(m_resource_view[1]), name.c_str());
                }
            }
        }

		return true;
	}

	// TEXTURE CUBE

	RHI_TextureCube::~RHI_TextureCube()
	{
        if (!m_rhi_device->IsInitialized())
            return;

        m_rhi_device->Queue_WaitAll();
        m_data.clear();
        const auto rhi_context = m_rhi_device->GetContextRhi();
        vulkan_common::image::view::destroy(rhi_context, m_resource_view[0]);
        vulkan_common::image::view::destroy(rhi_context, m_resource_view[1]);
        for (uint32_t i = 0; i < state_max_render_target_count; i++)
        {
            vulkan_common::image::view::destroy(rhi_context, m_resource_view_depthStencil[i]);
            vulkan_common::image::view::destroy(rhi_context, m_resource_view_renderTarget[i]);
        }
        vulkan_common::image::destroy(rhi_context, m_resource);
        vulkan_common::memory::free(m_rhi_device->GetContextRhi(), m_resource_memory);
	}

	bool RHI_TextureCube::CreateResourceGpu()
	{
        const RHI_Context* rhi_context = m_rhi_device->GetContextRhi();

        // Create image
        {
            if (!vulkan_common::image::create(rhi_context, this, m_resource))
            {
                LOG_ERROR("Failed to create image");
                return false;
            }

            if (!vulkan_common::image::allocate_bind(rhi_context, m_resource, m_resource_memory))
            {
                LOG_ERROR("Failed to allocate and bind image memory");
                return false;
            }
        }

        // If the texture has any data, stage it
        if (HasData())
        {
            if (!vulkan_common::image::stage(m_rhi_device.get(), this))
                return false;
        }

        // Transition to target layout
        if (VkCommandBuffer cmd_buffer = vulkan_common::command_buffer_immediate::begin(m_rhi_device.get(), RHI_Queue_Graphics))
        {
            RHI_Image_Layout target_layout = RHI_Image_Preinitialized;

            if (IsSampled() && IsColorFormat())
                target_layout = RHI_Image_Shader_Read_Only_Optimal;

            if (IsRenderTargetColor())
                target_layout = RHI_Image_Color_Attachment_Optimal;

            if (IsRenderTargetDepthStencil())
                target_layout = RHI_Image_Depth_Stencil_Attachment_Optimal;

            // Transition to the final layout
            if (!vulkan_common::image::set_layout(m_rhi_device.get(), cmd_buffer, this, target_layout))
                return false;

            // Flush
            if (!vulkan_common::command_buffer_immediate::end(RHI_Queue_Graphics))
                return false;

            // Update this texture with the new layout
            m_layout = target_layout;
        }

        // Create image views
        {
            // Shader resource views
            if (IsSampled())
            {
                if (IsColorFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view[0], this))
                        return false;
                }

                if (IsDepthFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view[0], this, 0, m_array_size, true, false))
                        return false;
                }

                if (IsStencilFormat())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view[1], this, 0, m_array_size, false, true))
                        return false;
                }
            }

            // Render target views
            for (uint32_t i = 0; i < m_array_size; i++)
            {
                if (IsRenderTargetColor())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view_renderTarget[i], this, i, 1))
                        return false;
                }

                if (IsRenderTargetDepthStencil())
                {
                    if (!vulkan_common::image::view::create(rhi_context, m_resource, m_resource_view_depthStencil[i], this, i, 1, true))
                        return false;
                }
            }

            // Name the image and image view(s)
            {
                string name = GetResourceName();

                if (IsSampled())
                {
                    name += name.empty() ? "sampled" : "-sampled";
                }

                if (IsRenderTargetColor())
                {
                    name += name.empty() ? "render_target_color" : "-render_target_color";
                }

                if (IsRenderTargetDepthStencil())
                {
                    name += name.empty() ? "render_target_depth" : "-render_target_depth";
                }

                vulkan_common::debug::set_image_name(rhi_context->device, static_cast<VkImage>(m_resource), name.c_str());
                vulkan_common::debug::set_image_view_name(rhi_context->device, static_cast<VkImageView>(m_resource_view[0]), name.c_str());
                if (IsSampled() && IsStencilFormat())
                {
                    vulkan_common::debug::set_image_view_name(rhi_context->device, static_cast<VkImageView>(m_resource_view[1]), name.c_str());
                }
            }
        }

        return true;
	}
}
#endif
