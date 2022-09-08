/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ========================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../RHI_Pipeline.h"
#include "../RHI_Semaphore.h"
#include "../RHI_CommandPool.h"
#include "../../Profiling/Profiler.h"
#include "../../Rendering/Renderer.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static VkSurfaceCapabilitiesKHR get_surface_capabilities(const VkSurfaceKHR surface)
    {
        VkSurfaceCapabilitiesKHR surface_capabilities;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan_utility::globals::rhi_context->device_physical, surface, &surface_capabilities);
        return surface_capabilities;
    }

    static vector<VkPresentModeKHR> get_supported_present_modes(const VkSurfaceKHR surface)
    {
        uint32_t present_mode_count;
        vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_utility::globals::rhi_context->device_physical, surface, &present_mode_count, nullptr);

        vector<VkPresentModeKHR> surface_present_modes(present_mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(vulkan_utility::globals::rhi_context->device_physical, surface, &present_mode_count, &surface_present_modes[0]);
        return surface_present_modes;
    }

    static VkPresentModeKHR get_present_mode(const VkSurfaceKHR surface, const uint32_t flags)
    {
        // Get preferred present mode
        VkPresentModeKHR present_mode_preferred = VK_PRESENT_MODE_FIFO_KHR;
        present_mode_preferred                  = flags & RHI_Present_Immediate                ? VK_PRESENT_MODE_IMMEDIATE_KHR                 : present_mode_preferred;
        present_mode_preferred                  = flags & RHI_Present_Fifo                     ? VK_PRESENT_MODE_MAILBOX_KHR                   : present_mode_preferred;
        present_mode_preferred                  = flags & RHI_Present_FifoRelaxed              ? VK_PRESENT_MODE_FIFO_RELAXED_KHR              : present_mode_preferred;
        present_mode_preferred                  = flags & RHI_Present_SharedDemandRefresh      ? VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR     : present_mode_preferred;
        present_mode_preferred                  = flags & RHI_Present_SharedDContinuousRefresh ? VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR : present_mode_preferred;

        // Check if the preferred mode is supported
        VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR; // as per spec, we can rely on VK_PRESENT_MODE_FIFO_KHR to always be present.
        vector<VkPresentModeKHR> surface_present_modes = get_supported_present_modes(surface);
        for (const auto& supported_present_mode : surface_present_modes)
        {
            if (present_mode_preferred == supported_present_mode)
            {
                present_mode = present_mode_preferred;
                break;
            }
        }

        return present_mode;
    }

    static inline vector<VkSurfaceFormatKHR> get_supported_surface_formats(const VkSurfaceKHR surface)
    {
        uint32_t format_count;
        SP_ASSERT_MSG(vkGetPhysicalDeviceSurfaceFormatsKHR(
            vulkan_utility::globals::rhi_context->device_physical, surface, &format_count, nullptr) == VK_SUCCESS,
            "Failed to get physical device surface format count"
        );

        vector<VkSurfaceFormatKHR> surface_formats(format_count);
        SP_ASSERT_MSG(
            vkGetPhysicalDeviceSurfaceFormatsKHR(vulkan_utility::globals::rhi_context->device_physical, surface, &format_count, &surface_formats[0]) == VK_SUCCESS,
            "Failed to get physical device surfaces"
        );

        return surface_formats;

    }

    static bool is_format_supported(RHI_Device* rhi_device, const VkSurfaceKHR surface, RHI_Format* format, VkColorSpaceKHR& color_space)
    {
        // Nvidia supports RHI_Format_B8R8G8A8_Unorm instead of RHI_Format_R8G8B8A8_Unorm.
        if ((*format) == RHI_Format_R8G8B8A8_Unorm && rhi_device->GetPrimaryPhysicalDevice()->IsNvidia())
        {
            (*format) = RHI_Format_B8R8G8A8_Unorm;
        }

        vector<VkSurfaceFormatKHR> supported_formats = get_supported_surface_formats(surface);
        for (const VkSurfaceFormatKHR& supported_format : supported_formats)
        {
            if (supported_format.format == vulkan_format[(*format)])
            {
                color_space = supported_format.colorSpace;
                return true;
            }
        }

        return false;
    }

    static void create
    (
        RHI_Device* rhi_device,
        uint32_t* width,
        uint32_t* height,
        uint32_t buffer_count,
        RHI_Format* rhi_format,
        array<RHI_Image_Layout, max_buffer_count> layouts,
        uint32_t flags,
        void* window_handle,
        void*& void_ptr_surface,
        void*& void_ptr_swap_chain,
        array<void*, max_buffer_count>& backbuffer_textures,
        array<void*, max_buffer_count>& backbuffer_texture_views,
        array<shared_ptr<RHI_Semaphore>, max_buffer_count>& image_acquired_semaphore
    )
        {
            SP_ASSERT(window_handle != nullptr);
            RHI_Context* rhi_context = rhi_device->GetRhiContext();
            
            // Create surface
            VkSurfaceKHR surface = nullptr;
            {
#if defined(_MSC_VER)
                VkWin32SurfaceCreateInfoKHR create_info = {};
                create_info.sType                       = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
                create_info.pNext                       = nullptr;
                create_info.flags                       = 0;
                create_info.hinstance                   = GetModuleHandle(nullptr);
                create_info.hwnd                        = static_cast<HWND>(window_handle);

                SP_ASSERT_MSG(vkCreateWin32SurfaceKHR(rhi_device->GetRhiContext()->instance, &create_info, nullptr, &surface) == VK_SUCCESS,
                    "Failed to created Win32 surface");
#else
                VkXcbSurfaceCreateInfoKHR create_info = {};
                create_info.sType                     = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
                create_info.pNext                     = nullptr;
                create_info.flags                     = nullptr;
                create_info.connection                = nullptr;
                create_info.window                    = nullptr;

                SP_ASSERT_MSG(false, "Not implemented");
#endif

                VkBool32 present_support = false;

                SP_ASSERT_MSG(vkGetPhysicalDeviceSurfaceSupportKHR(
                        rhi_context->device_physical,
                        rhi_device->GetQueueIndex(RHI_Queue_Type::Graphics),
                        surface,
                        &present_support) == VK_SUCCESS,
                    "Failed to get physical device surface support"
                );

                SP_ASSERT_MSG(present_support, "The device does not support this kind of surface");
            }

            // Get surface capabilities
            VkSurfaceCapabilitiesKHR capabilities = get_surface_capabilities(surface);

            // Compute extent
            *width            = Math::Helper::Clamp(*width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
            *height           = Math::Helper::Clamp(*height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
            VkExtent2D extent = { *width, *height };

            // Ensure that the surface supports the requested format, and if so, get the color space.
            VkColorSpaceKHR color_space = VK_COLOR_SPACE_MAX_ENUM_KHR;
            SP_ASSERT_MSG(is_format_supported(rhi_device, surface, rhi_format, color_space), "The surface doesn't support the requested format");

            // Swap chain
            VkSwapchainKHR swap_chain;
            {
                VkSwapchainCreateInfoKHR create_info = {};
                create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
                create_info.surface                  = surface;
                create_info.minImageCount            = buffer_count;
                create_info.imageFormat              = vulkan_format[*rhi_format];
                create_info.imageColorSpace          = color_space;
                create_info.imageExtent              = extent;
                create_info.imageArrayLayers         = 1;
                create_info.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

                uint32_t queueFamilyIndices[] = { rhi_device->GetQueueIndex(RHI_Queue_Type::Compute), rhi_device->GetQueueIndex(RHI_Queue_Type::Graphics) };
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
                create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
                create_info.presentMode    = get_present_mode(surface, flags);
                create_info.clipped        = VK_TRUE;
                create_info.oldSwapchain   = nullptr;

                SP_ASSERT_MSG(
                    vkCreateSwapchainKHR(rhi_context->device, &create_info, nullptr, &swap_chain) == VK_SUCCESS,
                    "Failed to create swapchain"
                );
            }

            // Images
            uint32_t image_count;
            vector<VkImage> images;
            {
                // Get
                vkGetSwapchainImagesKHR(rhi_context->device, swap_chain, &image_count, nullptr);
                images.resize(image_count);
                vkGetSwapchainImagesKHR(rhi_context->device, swap_chain, &image_count, images.data());

                // Transition layouts to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                if (RHI_CommandList* cmd_list = rhi_device->ImmediateBegin(RHI_Queue_Type::Graphics))
                {
                    for (uint32_t i = 0; i < static_cast<uint32_t>(images.size()); i++)
                    {
                        vulkan_utility::image::set_layout(
                            cmd_list->GetRhiResource(),
                            reinterpret_cast<void*>(images[i]),
                            VK_IMAGE_ASPECT_COLOR_BIT,
                            0,
                            1,
                            1,
                            RHI_Image_Layout::Undefined,
                            RHI_Image_Layout::Color_Attachment_Optimal
                        );
                        layouts[i] = RHI_Image_Layout::Color_Attachment_Optimal;
                    }

                    // End/flush
                    rhi_device->ImmediateSubmit(cmd_list);
                }
            }

            // Image views
            {
                for (uint32_t i = 0; i < image_count; i++)
                {
                    backbuffer_textures[i] = static_cast<void*>(images[i]);

                    // Name the image
                    vulkan_utility::debug::set_object_name(images[i], string(string("swapchain_image_") + to_string(i)).c_str());

                    vulkan_utility::image::view::create
                    (
                        static_cast<void*>(images[i]),
                        backbuffer_texture_views[i],
                        VK_IMAGE_VIEW_TYPE_2D,
                        vulkan_format[*rhi_format],
                        VK_IMAGE_ASPECT_COLOR_BIT,
                        0, 1, 0, 1
                    );
                }
            }

            void_ptr_surface    = static_cast<void*>(surface);
            void_ptr_swap_chain = static_cast<void*>(swap_chain);

            // Semaphores
            for (uint32_t i = 0; i < buffer_count; i++)
            {
                string name = (string("swapchain_image_acquired_") + to_string(i));
                image_acquired_semaphore[i] = make_shared<RHI_Semaphore>(rhi_device, false, name.c_str());
            }
        }
    
    static void destroy(
        RHI_Device* rhi_device,
        uint8_t buffer_count,
        void*& surface,
        void*& swap_chain,
        array<void*, 3>& image_views,
        array<std::shared_ptr<RHI_Semaphore>, max_buffer_count>& image_acquired_semaphore
    )
    {
        RHI_Context* rhi_context = rhi_device->GetRhiContext();

        // Sync objects
        image_acquired_semaphore.fill(nullptr);
    
        // Image views
        vulkan_utility::image::view::destroy(image_views);

        // Swap chain view
        if (swap_chain)
        {
            vkDestroySwapchainKHR(rhi_context->device, static_cast<VkSwapchainKHR>(swap_chain), nullptr);
            swap_chain = nullptr;
        }
    
        // Surface
        if (surface)
        {
            vkDestroySurfaceKHR(rhi_context->instance, static_cast<VkSurfaceKHR>(surface), nullptr);
            surface = nullptr;
        }
    }

    RHI_SwapChain::RHI_SwapChain(
        void* window_handle,
        RHI_Device* rhi_device,
        const uint32_t width,
        const uint32_t height,
        const RHI_Format format,
        const uint32_t buffer_count,
        const uint32_t flags,
        const char* name
    )
    {
        // Verify resolution
        if (!rhi_device->IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        m_acquire_semaphore.fill(nullptr);
        m_rhi_backbuffer_resource.fill(nullptr);
        m_rhi_backbuffer_srv.fill(nullptr);
        m_layouts.fill(RHI_Image_Layout::Undefined);

        // Copy parameters
        m_format        = format;
        m_rhi_device    = rhi_device;
        m_buffer_count  = buffer_count;
        m_width         = width;
        m_height        = height;
        m_window_handle = window_handle;
        m_flags         = flags;
        m_name   = name;

        create
        (
            m_rhi_device,
            &m_width,
            &m_height,
            m_buffer_count,
            &m_format,
            m_layouts,
            m_flags,
            m_window_handle,
            m_surface,
            m_rhi_resource,
            m_rhi_backbuffer_resource,
            m_rhi_backbuffer_srv,
            m_acquire_semaphore
        );

        AcquireNextImage();
    }

    RHI_SwapChain::~RHI_SwapChain()
    {
        // Wait until the GPU is idle
        m_rhi_device->QueueWaitAll();

        destroy
        (
            m_rhi_device,
            m_buffer_count,
            m_surface,
            m_rhi_resource,
            m_rhi_backbuffer_srv,
            m_acquire_semaphore
        );
    }

    bool RHI_SwapChain::Resize(const uint32_t width, const uint32_t height, const bool force /*= false*/)
    {
        // Validate resolution
        m_present_enabled = m_rhi_device->IsValidResolution(width, height);

        if (!m_present_enabled)
        {
            // Return true as when minimizing, a resolution
            // of 0,0 can be passed in, and this is fine.
            return false;
        }

        // Only resize if needed
        if (!force)
        {
            if (m_width == width && m_height == height)
                return false;
        }

        // Wait until the GPU is idle
        m_rhi_device->QueueWaitAll();

        // Save new dimensions
        m_width  = width;
        m_height = height;

        // Destroy previous swap chain
        destroy
        (
            m_rhi_device,
            m_buffer_count,
            m_surface,
            m_rhi_resource,
            m_rhi_backbuffer_srv,
            m_acquire_semaphore
        );

        // Create the swap chain with the new dimensions
        create
        (
            m_rhi_device,
            &m_width,
            &m_height,
            m_buffer_count,
            &m_format,
            m_layouts,
            m_flags,
            m_window_handle,
            m_surface,
            m_rhi_resource,
            m_rhi_backbuffer_resource,
            m_rhi_backbuffer_srv,
            m_acquire_semaphore
        );

        // Reset image index
        m_image_index          = numeric_limits<uint32_t>::max();
        m_image_index_previous = m_image_index;

        AcquireNextImage();

        return true;
    }

    void RHI_SwapChain::AcquireNextImage()
    {
        SP_ASSERT(m_present_enabled && "No need to acquire next image when presenting is disabled");

        // Return if the swapchain has a single buffer and it has already been acquired
        if (m_buffer_count == 1 && m_image_index != numeric_limits<uint32_t>::max())
            return;

        // Get signal semaphore
        m_sync_index = (m_sync_index + 1) % m_buffer_count;
        RHI_Semaphore* signal_semaphore = m_acquire_semaphore[m_sync_index].get();

        // Ensure semaphore state
        SP_ASSERT_MSG(signal_semaphore->GetCpuState() != RHI_Sync_State::Submitted, "The semaphore is already signaled");

        m_image_index_previous = m_image_index;

        // Acquire next image
        SP_ASSERT_MSG(vkAcquireNextImageKHR(
            m_rhi_device->GetRhiContext()->device,                     // device
            static_cast<VkSwapchainKHR>(m_rhi_resource),               // swapchain
            numeric_limits<uint64_t>::max(),                           // timeout
            static_cast<VkSemaphore>(signal_semaphore->GetResource()), // signal semaphore
            nullptr,                                                   // signal fence
            &m_image_index                                             // pImageIndex
        ) == VK_SUCCESS, "Failed to acquire next image");

        // Update semaphore state
        signal_semaphore->SetCpuState(RHI_Sync_State::Submitted);
    }

    void RHI_SwapChain::Present()
    {
        SP_ASSERT_MSG(m_rhi_resource != nullptr,               "The swapchain has not been initialised");
        SP_ASSERT_MSG(m_present_enabled,                       "Presenting is disabled");
        SP_ASSERT_MSG(m_image_index != m_image_index_previous, "No image was acquired");

        // Get the semaphores that present should wait for
        static vector<RHI_Semaphore*> wait_semaphores;
        {
            wait_semaphores.clear();

            // The first is simply the image acquired semaphore
            wait_semaphores.emplace_back(m_acquire_semaphore[m_sync_index].get());

            // The others are all the command lists
            const vector<shared_ptr<RHI_CommandPool>>& cmd_pools = m_rhi_device->GetCommandPools();
            for (const shared_ptr<RHI_CommandPool>& cmd_pool : cmd_pools)
            {
                // The editor supports multiple windows, so we can be dealing with multiple swapchains.
                // Therefore we only want to wait on the command list semaphores, which will be presenting their work to this swapchain.
                if (m_object_id == cmd_pool->GetSwapchainId())
                {
                    RHI_Semaphore* semaphore = cmd_pool->GetCurrentCommandList()->GetSemaphoreProccessed();

                    // Command lists can be discarded (when they reference destroyed memory).
                    // In cases like that, the command lists are not submitted and as a result, the semaphore won't be signaled.
                    if (semaphore->GetCpuState() == RHI_Sync_State::Submitted)
                    {
                        wait_semaphores.emplace_back(semaphore);
                    }
                }
            }
        }

        SP_ASSERT_MSG(!wait_semaphores.empty(), "Present() should wait on at least one semaphore");

        // Present
        m_rhi_device->QueuePresent(m_rhi_resource, &m_image_index, wait_semaphores);

        // Acquire next image
        AcquireNextImage();
    }

    void RHI_SwapChain::SetLayout(const RHI_Image_Layout& layout, RHI_CommandList* cmd_list)
    {
        if (m_layouts[m_image_index] == layout)
            return;

        vulkan_utility::image::set_layout(
            reinterpret_cast<void*>(cmd_list->GetRhiResource()),
            reinterpret_cast<void*>(m_rhi_backbuffer_resource[m_image_index]),
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 1,
            m_layouts[m_image_index],
            layout
        );

        m_layouts[m_image_index] = layout;
    }
}
