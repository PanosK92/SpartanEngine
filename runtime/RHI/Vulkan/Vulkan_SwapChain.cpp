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

//= INCLUDES =====================
#include "pch.h"
#include "Window.h"
#include "../RHI_Device.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Implementation.h"
#include "../RHI_Semaphore.h"
#include "../RHI_CommandPool.h"
#include "../Display/Display.h"
SP_WARNINGS_OFF
#include <SDL_vulkan.h>
SP_WARNINGS_ON
//================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    { 
        static VkColorSpaceKHR get_color_space(bool is_hdr)
        {
            // VK_COLOR_SPACE_HDR10_ST2084_EXT represents the HDR10 color space with the ST.2084 (PQ)electro - optical transfer function.
            // This is the most common HDR format used for HDR TVs and monitors.

            // VK_COLOR_SPACE_SRGB_NONLINEAR_KHR represents the sRGB color space.
            // This is the standard color space for the web and is supported by most modern displays.
            // sRGB is a nonlinear color space, which means that the values stored in an image are not directly proportional to the perceived brightness of the colors.
            // When displaying an image in sRGB, the values must be converted to linear space before they are displayed.

            return is_hdr ? VK_COLOR_SPACE_HDR10_ST2084_EXT : VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        }

        static VkSurfaceCapabilitiesKHR get_surface_capabilities(const VkSurfaceKHR surface)
        {
            VkSurfaceCapabilitiesKHR surface_capabilities;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHI_Context::device_physical, surface, &surface_capabilities);
            return surface_capabilities;
        }

        static vector<VkPresentModeKHR> get_supported_present_modes(const VkSurfaceKHR surface)
        {
            uint32_t present_mode_count;
            vkGetPhysicalDeviceSurfacePresentModesKHR(RHI_Context::device_physical, surface, &present_mode_count, nullptr);

            vector<VkPresentModeKHR> surface_present_modes(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(RHI_Context::device_physical, surface, &present_mode_count, &surface_present_modes[0]);
            return surface_present_modes;
        }

        static VkPresentModeKHR get_present_mode(const VkSurfaceKHR surface, const RHI_Present_Mode present_mode)
        {
            // Convert RHI_Present_Mode to VkPresentModeKHR
            VkPresentModeKHR vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
            if (present_mode == RHI_Present_Mode::Immediate)
            {
                vk_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            else if (present_mode == RHI_Present_Mode::Mailbox)
            {
                vk_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            }

            // Return the present mode as is if the surface supports it
            vector<VkPresentModeKHR> surface_present_modes = get_supported_present_modes(surface);
            for (const VkPresentModeKHR supported_present_mode : surface_present_modes)
            {
                if (vk_present_mode == supported_present_mode)
                {
                    return vk_present_mode;
                }
            }

            // At this point we call back to VK_PRESENT_MODE_FIFO_KHR, which as per spec is always present
            SP_LOG_WARNING("Requested present mode is not supported. Falling back to VK_PRESENT_MODE_FIFO_KHR");
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        static vector<VkSurfaceFormatKHR> get_supported_surface_formats(const VkSurfaceKHR surface)
        {
            uint32_t format_count;
            SP_VK_ASSERT_MSG(vkGetPhysicalDeviceSurfaceFormatsKHR(RHI_Context::device_physical, surface, &format_count, nullptr),
                "Failed to get physical device surface format count");

            vector<VkSurfaceFormatKHR> surface_formats(format_count);
            SP_VK_ASSERT_MSG(vkGetPhysicalDeviceSurfaceFormatsKHR(RHI_Context::device_physical, surface, &format_count, &surface_formats[0]),
                "Failed to get physical device surfaces");

            return surface_formats;
        }

        static bool is_format_and_color_space_supported(const VkSurfaceKHR surface, RHI_Format* format, VkColorSpaceKHR color_space)
        {
            // Get supported surface formats
            vector<VkSurfaceFormatKHR> supported_formats = get_supported_surface_formats(surface);

            // NV supports RHI_Format::B8R8G8A8_Unorm instead of RHI_Format::R8G8B8A8_Unorm.
            if ((*format) == RHI_Format::R8G8B8A8_Unorm && RHI_Device::GetPrimaryPhysicalDevice()->IsNvidia())
            {
                (*format) = RHI_Format::B8R8G8A8_Unorm;
            }

            for (const VkSurfaceFormatKHR& supported_format : supported_formats)
            {
                bool support_format      = supported_format.format == vulkan_format[rhi_format_to_index(*format)];
                bool support_color_space = supported_format.colorSpace == color_space;

                if (support_format && support_color_space)
                    return true;
            }

            return false;
        }

        static VkCompositeAlphaFlagBitsKHR get_supported_composite_alpha_format(const VkSurfaceKHR surface)
        {
            vector<VkCompositeAlphaFlagBitsKHR> composite_alpha_flags =
            {
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            };

            // Get physical device surface capabilities
            VkSurfaceCapabilitiesKHR surface_capabilities;
            SP_VK_ASSERT_MSG(
                vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHI_Context::device_physical, surface, &surface_capabilities),
                "Failed to get surface capabilities");

            // Simply select the first composite alpha format available
            for (VkCompositeAlphaFlagBitsKHR& composite_alpha : composite_alpha_flags)
            {
                if (surface_capabilities.supportedCompositeAlpha & composite_alpha)
                {
                    return composite_alpha;
                };
            }

            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }
    }

    RHI_SwapChain::RHI_SwapChain(
        void* sdl_window,
        const uint32_t width,
        const uint32_t height,
        const RHI_Present_Mode present_mode,
        const uint32_t buffer_count,
        const char* name
    )
    {
        SP_ASSERT_MSG(RHI_Device::IsValidResolution(width, height), "Invalid resolution");

        // Copy parameters
        m_format       = format_sdr; // for now, we use SDR by default as HDR doesn't look rigth - Display::GetHdr() ? format_hdr : format_sdr;
        m_buffer_count = buffer_count;
        m_width        = width;
        m_height       = height;
        m_sdl_window   = sdl_window;
        m_object_name  = name;
        m_present_mode = present_mode;

        Create();
        AcquireNextImage();

        SP_SUBSCRIBE_TO_EVENT(EventType::WindowResized, SP_EVENT_HANDLER(ResizeToWindowSize));
        SP_SUBSCRIBE_TO_EVENT(EventType::WindowFullscreen, SP_EVENT_HANDLER(ResizeToWindowSize));
    }

    RHI_SwapChain::~RHI_SwapChain()
    {
        Destroy();
    }

    void RHI_SwapChain::Create()
    {
        SP_ASSERT(m_sdl_window != nullptr);

        // Create surface
        VkSurfaceKHR surface = nullptr;
        {
            SP_ASSERT_MSG(
                SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(m_sdl_window), RHI_Context::instance, &surface),
                "Failed to created window surface");

            VkBool32 present_support = false;
            SP_VK_ASSERT_MSG(vkGetPhysicalDeviceSurfaceSupportKHR(
                RHI_Context::device_physical,
                RHI_Device::QueueGetIndex(RHI_Queue_Type::Graphics),
                surface,
                &present_support),
                "Failed to get physical device surface support");

            SP_ASSERT_MSG(present_support, "The device does not support this kind of surface");
        }

        // Get surface capabilities
        VkSurfaceCapabilitiesKHR capabilities = get_surface_capabilities(surface);

        // Ensure that the surface supports the requested format and color space
        VkColorSpaceKHR color_space = get_color_space(IsHdr());
        SP_ASSERT_MSG(is_format_and_color_space_supported(surface, &m_format, color_space), "The surface doesn't support the requested format");

        // Clamp size between the supported min and max
        m_width  = Math::Helper::Clamp(m_width,  capabilities.minImageExtent.width,  capabilities.maxImageExtent.width);
        m_height = Math::Helper::Clamp(m_height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        // Swap chain
        VkSwapchainKHR swap_chain;
        {
            VkSwapchainCreateInfoKHR create_info  = {};
            create_info.sType                     = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
            create_info.surface                   = surface;
            create_info.minImageCount             = m_buffer_count;
            create_info.imageFormat               = vulkan_format[rhi_format_to_index(m_format)];
            create_info.imageColorSpace           = color_space;
            create_info.imageExtent               = { m_width, m_height };
            create_info.imageArrayLayers          = 1;
            create_info.imageUsage                = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT; // fer rendering on it
            create_info.imageUsage               |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;     // for blitting to it

            uint32_t queueFamilyIndices[] = { RHI_Device::QueueGetIndex(RHI_Queue_Type::Compute), RHI_Device::QueueGetIndex(RHI_Queue_Type::Graphics) };
            if (queueFamilyIndices[0] != queueFamilyIndices[1])
            {
                create_info.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
                create_info.queueFamilyIndexCount = 2;
                create_info.pQueueFamilyIndices   = queueFamilyIndices;
            }
            else
            {
                create_info.imageSharingMode      = VK_SHARING_MODE_EXCLUSIVE;
                create_info.queueFamilyIndexCount = 0;
                create_info.pQueueFamilyIndices   = nullptr;
            }

            create_info.preTransform   = capabilities.currentTransform;
            create_info.compositeAlpha = get_supported_composite_alpha_format(surface);
            create_info.presentMode    = get_present_mode(surface, m_present_mode);
            create_info.clipped        = VK_TRUE;
            create_info.oldSwapchain   = nullptr;

            SP_VK_ASSERT_MSG(vkCreateSwapchainKHR(RHI_Context::device, &create_info, nullptr, &swap_chain),
                "Failed to create swapchain");
        }

        // Images
        {
            uint32_t image_count = 0;
            SP_VK_ASSERT_MSG(vkGetSwapchainImagesKHR(RHI_Context::device, swap_chain, &image_count, nullptr), "Failed to get swapchain image count");
            SP_VK_ASSERT_MSG(vkGetSwapchainImagesKHR(RHI_Context::device, swap_chain, &image_count, reinterpret_cast<VkImage*>(m_rhi_rt.data())), "Failed to get swapchain image count");

            // Transition layouts to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
            if (RHI_CommandList* cmd_list = RHI_Device::CmdImmediateBegin(RHI_Queue_Type::Graphics))
            {
                for (uint32_t i = 0; i < m_buffer_count; i++)
                {
                    cmd_list->InsertMemoryBarrierImage(
                        m_rhi_rt[i],
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        0,
                        1,
                        1,
                        RHI_Image_Layout::Undefined,
                        RHI_Image_Layout::Color_Attachment_Optimal
                    );

                    m_layouts[i] = RHI_Image_Layout::Color_Attachment_Optimal;
                }

                // End/flush
                RHI_Device::CmdImmediateSubmit(cmd_list);
            }
        }

        // image views
        {
            for (uint32_t i = 0; i < m_buffer_count; i++)
            {
                RHI_Device::SetResourceName(m_rhi_rt[i], RHI_Resource_Type::Texture, string(string("swapchain_image_") + to_string(i)));

                VkImageViewCreateInfo create_info           = {};
                create_info.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
                create_info.image                           = static_cast<VkImage>(m_rhi_rt[i]);
                create_info.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
                create_info.format                          = vulkan_format[rhi_format_to_index(m_format)];
                create_info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
                create_info.subresourceRange.baseMipLevel   = 0;
                create_info.subresourceRange.levelCount     = 1;
                create_info.subresourceRange.baseArrayLayer = 0;
                create_info.subresourceRange.layerCount     = 1;
                create_info.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
                create_info.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;

                SP_ASSERT_MSG(vkCreateImageView(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkImageView*>(&m_rhi_rtv[i])) == VK_SUCCESS, "Failed to create swapchain RTV");
            }
        }

        m_rhi_surface   = static_cast<void*>(surface);
        m_rhi_swapchain = static_cast<void*>(swap_chain);

        // Semaphores
        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            string name = (string("swapchain_image_acquired_") + to_string(i));
            m_acquire_semaphore[i] = make_shared<RHI_Semaphore>(false, name.c_str());
        }
    }

    void RHI_SwapChain::Destroy()
    {
        for (void* image_view : m_rhi_rtv)
        {
            if (image_view)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::TextureView, image_view);
            }
        }

        m_rhi_rtv.fill(nullptr);
        m_acquire_semaphore.fill(nullptr);

        RHI_Device::QueueWaitAll();

        vkDestroySwapchainKHR(RHI_Context::device, static_cast<VkSwapchainKHR>(m_rhi_swapchain), nullptr);
        m_rhi_swapchain = nullptr;

        vkDestroySurfaceKHR(RHI_Context::instance, static_cast<VkSurfaceKHR>(m_rhi_surface), nullptr);
        m_rhi_surface = nullptr;
    }

    void RHI_SwapChain::Resize(const uint32_t width, const uint32_t height, const bool force /*= false*/)
    {
        SP_ASSERT(RHI_Device::IsValidResolution(width, height));

        // Only resize if needed
        if (!force)
        {
            if (m_width == width && m_height == height)
                return;
        }

        // Save new dimensions
        m_width  = width;
        m_height = height;

        // Reset image index
        m_image_index          = numeric_limits<uint32_t>::max();
        m_image_index_previous = m_image_index;

        Destroy();
        Create();
        AcquireNextImage();

        SP_LOG_INFO("Resolution has been set to %dx%d", width, height);
    }

    void RHI_SwapChain::ResizeToWindowSize()
    {
        Resize(Window::GetWidth(), Window::GetHeight());
    }

    void RHI_SwapChain::AcquireNextImage()
    {
        // This is a blocking function, it will stop execution until an image becomes available
        constexpr uint64_t timeout = numeric_limits<uint64_t>::max();

        // Return if the swapchain has a single buffer and it has already been acquired
        if (m_buffer_count == 1 && m_image_index != numeric_limits<uint32_t>::max())
            return;

        // Get signal semaphore
        m_sync_index = (m_sync_index + 1) % m_buffer_count;
        RHI_Semaphore* signal_semaphore = m_acquire_semaphore[m_sync_index].get();

        // Ensure semaphore state
        SP_ASSERT_MSG(signal_semaphore->GetStateCpu() != RHI_Sync_State::Submitted, "The semaphore is already signaled");

        m_image_index_previous = m_image_index;

        // Acquire next image
        SP_VK_ASSERT_MSG(vkAcquireNextImageKHR(
            RHI_Context::device,                                          // device
            static_cast<VkSwapchainKHR>(m_rhi_swapchain),                 // swapchain
            timeout,                                                      // timeout
            static_cast<VkSemaphore>(signal_semaphore->GetRhiResource()), // signal semaphore
            nullptr,                                                      // signal fence
            &m_image_index                                                // pImageIndex
        ), "Failed to acquire next image");

        // Update semaphore state
        signal_semaphore->SetStateCpu(RHI_Sync_State::Submitted);
    }

    void RHI_SwapChain::Present()
    {
        SP_ASSERT_MSG(!(SDL_GetWindowFlags(static_cast<SDL_Window*>(m_sdl_window)) & SDL_WINDOW_MINIMIZED), "Present should not be called for a minimized window");
        SP_ASSERT_MSG(m_rhi_swapchain != nullptr,                                                           "Invalid swapchain");
        SP_ASSERT_MSG(m_image_index != m_image_index_previous,                                              "No image was acquired");
        SP_ASSERT_MSG(m_layouts[m_image_index] == RHI_Image_Layout::Present_Src,                            "Invalid layout");

        // Get the semaphores that present should wait for
        m_wait_semaphores.clear();
        {
            // Semaphores which are signaled when command lists have finished executing
            for (const shared_ptr<RHI_CommandPool>& cmd_pool : RHI_Device::GetCommandPools())
            {
                // The editor supports multiple windows, so we can be dealing with multiple swapchains
                if (m_object_id == cmd_pool->GetSwapchainId())
                {
                    RHI_Semaphore* semaphore_cmd_list = cmd_pool->GetCurrentCommandList()->GetSemaphoreProccessed();
                    if (semaphore_cmd_list->GetStateCpu() == RHI_Sync_State::Submitted)
                    {
                        m_wait_semaphores.emplace_back(semaphore_cmd_list);
                    }
                }
            }
            SP_ASSERT_MSG(!m_wait_semaphores.empty(), "Present() present should not be called if no work is to be presented");

            // Semaphore that's signaled when the image is acquired
            RHI_Semaphore* semaphore_image_aquired = m_acquire_semaphore[m_sync_index].get();
            SP_ASSERT(semaphore_image_aquired->GetStateCpu() == RHI_Sync_State::Submitted);
            m_wait_semaphores.emplace_back(semaphore_image_aquired);
        }

        RHI_Device::QueuePresent(m_rhi_swapchain, &m_image_index, m_wait_semaphores);
        AcquireNextImage();
    }

    void RHI_SwapChain::SetLayout(const RHI_Image_Layout& layout, RHI_CommandList* cmd_list)
    {
        if (m_layouts[m_image_index] == layout)
            return;

        cmd_list->InsertMemoryBarrierImage(
            m_rhi_rt[m_image_index],
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1,
            m_layouts[m_image_index],
            layout
        );

        m_layouts[m_image_index] = layout;
    }

    void RHI_SwapChain::SetHdr(const bool enabled)
    {
        if (enabled)
        {
            SP_ASSERT_MSG(Display::GetHdr(), "This display doesn't support HDR");
        }

        RHI_Format new_format = enabled ? format_hdr : format_sdr;

        if (new_format != m_format)
        {
            m_format = new_format;
            Resize(m_width, m_height, true);
            SP_LOG_INFO("HDR has been %s", enabled ? "enabled" : "disabled");
        }
    }

    void RHI_SwapChain::SetVsync(const bool enabled)
    {
        // For v-sync, we could Mailbox for lower latency, but Fifo is always supported, so we'll assume that

        if ((m_present_mode == RHI_Present_Mode::Fifo) != enabled)
        {
            m_present_mode = enabled ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate;
            Resize(m_width, m_height, true);
            Timer::OnVsyncToggled(enabled);
            SP_LOG_INFO("VSync has been %s", enabled ? "enabled" : "disabled");
        }
    }

    bool RHI_SwapChain::GetVsync()
    {
        // For v-sync, we could Mailbox for lower latency, but Fifo is always supported, so we'll assume that
        return m_present_mode == RHI_Present_Mode::Fifo;
    }

    RHI_Image_Layout RHI_SwapChain::GetLayout() const
    {
        return m_layouts[m_image_index];
    }
}
