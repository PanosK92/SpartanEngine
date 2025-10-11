/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "../RHI_SyncPrimitive.h"
#include "../RHI_Queue.h"
#include "../Display/Display.h"
#include "../Rendering/Renderer.h"
#ifdef _WIN32
#include <tlhelp32.h>
#endif
SP_WARNINGS_OFF
#include <SDL3/SDL_vulkan.h>
SP_WARNINGS_ON
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        VkColorSpaceKHR get_color_space(const RHI_Format format)
        {
            VkColorSpaceKHR color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;                                                       // SDR
            color_space                 = format == RHI_Format::R10G10B10A2_Unorm ? VK_COLOR_SPACE_HDR10_ST2084_EXT : color_space; // HDR

            return color_space;
        }

        void set_hdr_metadata(const VkSwapchainKHR& swapchain)
        {
            VkHdrMetadataEXT hdr_metadata          = {};
            hdr_metadata.sType                     = VK_STRUCTURE_TYPE_HDR_METADATA_EXT;
            hdr_metadata.displayPrimaryRed.x       = 0.708f;
            hdr_metadata.displayPrimaryRed.y       = 0.292f;
            hdr_metadata.displayPrimaryGreen.x     = 0.170f;
            hdr_metadata.displayPrimaryGreen.y     = 0.797f;
            hdr_metadata.displayPrimaryBlue.x      = 0.131f;
            hdr_metadata.displayPrimaryBlue.y      = 0.046f;
            hdr_metadata.whitePoint.x              = 0.3127f;
            hdr_metadata.whitePoint.y              = 0.3290f;
            const float nits_to_lumin              = 10000.0f;
            hdr_metadata.maxLuminance              = Display::GetLuminanceMax() * nits_to_lumin;
            hdr_metadata.minLuminance              = 0.001f * nits_to_lumin;
            hdr_metadata.maxContentLightLevel      = 2000.0f;
            hdr_metadata.maxFrameAverageLightLevel = 500.0f;

            PFN_vkSetHdrMetadataEXT pfnVkSetHdrMetadataEXT = (PFN_vkSetHdrMetadataEXT)vkGetDeviceProcAddr(RHI_Context::device , "vkSetHdrMetadataEXT");
            SP_ASSERT(pfnVkSetHdrMetadataEXT != nullptr);
            pfnVkSetHdrMetadataEXT(RHI_Context::device, 1, &swapchain, &hdr_metadata);
        }

        VkSurfaceCapabilitiesKHR get_surface_capabilities(const VkSurfaceKHR surface)
        {
            VkSurfaceCapabilitiesKHR surface_capabilities;
            vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHI_Context::device_physical, surface, &surface_capabilities);
            return surface_capabilities;
        }

        vector<VkPresentModeKHR> get_supported_present_modes(const VkSurfaceKHR surface)
        {
            uint32_t present_mode_count;
            vkGetPhysicalDeviceSurfacePresentModesKHR(RHI_Context::device_physical, surface, &present_mode_count, nullptr);

            vector<VkPresentModeKHR> surface_present_modes(present_mode_count);
            vkGetPhysicalDeviceSurfacePresentModesKHR(RHI_Context::device_physical, surface, &present_mode_count, &surface_present_modes[0]);
            return surface_present_modes;
        }

        VkPresentModeKHR get_present_mode(const VkSurfaceKHR surface, const RHI_Present_Mode present_mode)
        {
            VkPresentModeKHR vk_present_mode = VK_PRESENT_MODE_FIFO_KHR;
            if (present_mode == RHI_Present_Mode::Immediate)
            {
                vk_present_mode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
            else if (present_mode == RHI_Present_Mode::Mailbox)
            {
                vk_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
            }

            // return the present mode as is if the surface supports it
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

        vector<VkSurfaceFormatKHR> get_supported_surface_formats(const VkSurfaceKHR surface)
        {
            uint32_t format_count;
            SP_ASSERT_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(RHI_Context::device_physical, surface, &format_count, nullptr));

            vector<VkSurfaceFormatKHR> surface_formats(format_count);
            SP_ASSERT_VK(vkGetPhysicalDeviceSurfaceFormatsKHR(RHI_Context::device_physical, surface, &format_count, &surface_formats[0]));

            return surface_formats;
        }

        bool is_format_and_color_space_supported(const VkSurfaceKHR surface, RHI_Format* format, VkColorSpaceKHR color_space)
        {
            vector<VkSurfaceFormatKHR> supported_formats = get_supported_surface_formats(surface);

            // NV supports RHI_Format::B8R8G8A8_Unorm instead of RHI_Format::R8G8B8A8_Unorm
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

        VkCompositeAlphaFlagBitsKHR get_supported_composite_alpha_format(const VkSurfaceKHR surface)
        {
            vector<VkCompositeAlphaFlagBitsKHR> composite_alpha_flags =
            {
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
                VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            };

            // get physical device surface capabilities
            VkSurfaceCapabilitiesKHR surface_capabilities;
            SP_ASSERT_VK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(RHI_Context::device_physical, surface, &surface_capabilities));

            // simply select the first composite alpha format available
            for (VkCompositeAlphaFlagBitsKHR& composite_alpha : composite_alpha_flags)
            {
                if (surface_capabilities.supportedCompositeAlpha & composite_alpha)
                {
                    return composite_alpha;
                };
            }

            return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        }

        bool is_process_running(const char* process_name)
        {
        #ifdef _WIN32
            HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (snapshot == INVALID_HANDLE_VALUE) {
                return false;
            }

            PROCESSENTRY32W pe32 = {0}; // Use PROCESSENTRY32W for Unicode
            pe32.dwSize = sizeof(PROCESSENTRY32W);

            if (!Process32FirstW(snapshot, &pe32)) { // Use Process32FirstW for Unicode
                CloseHandle(snapshot);
                return false; // Failed to get first process
            }

            // Convert processName to wide-character string for comparison
            size_t convertedChars = 0;
            wchar_t wProcessName[MAX_PATH];
            mbstowcs_s(&convertedChars, wProcessName, process_name, strlen(process_name) + 1);

            // Iterate through all processes
            do {
                if (_wcsicmp(pe32.szExeFile, wProcessName) == 0) { // Case-insensitive wide-character comparison
                    CloseHandle(snapshot);
                    return true; // Process found
                }
            } while (Process32NextW(snapshot, &pe32)); // Use Process32NextW for Unicode

            CloseHandle(snapshot);
            return false; // Process not found

        #elif defined(__linux__)
            // Linux implementation
            DIR* dir = opendir("/proc");
            if (!dir) {
                return false; // Failed to open /proc directory
            }

            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                // Check if the directory is a PID (starts with a digit)
                if (entry->d_type == DT_DIR && isdigit(entry->d_name[0])) {
                    char commPath[256];
                    snprintf(commPath, sizeof(commPath), "/proc/%s/comm", entry->d_name);

                    FILE* commFile = fopen(commPath, "r");
                    if (commFile) {
                        char comm[256];
                        if (fgets(comm, sizeof(comm), commFile)) {
                            comm[strcspn(comm, "\n")] = 0; // Remove newline
                            if (strcmp(comm, processName) == 0) { // Case-sensitive comparison
                                fclose(commFile);
                                closedir(dir);
                                return true; // Process found
                            }
                        }
                        fclose(commFile);
                    }
                }
            }

            closedir(dir);
            return false; // Process not found

        #else
            // Unsupported platform
            return false;
        #endif
        }
    }

    RHI_SwapChain::RHI_SwapChain(
        void* sdl_window,
        const uint32_t width,
        const uint32_t height,
        const RHI_Present_Mode present_mode,
        const uint32_t buffer_count,
        const bool hdr,
        const char* name
    )
    {
        SP_ASSERT_MSG(RHI_Device::IsValidResolution(width, height), "Invalid resolution");
        SP_ASSERT_MSG(buffer_count >= 2, "Buffer count can't be less than 2");

        m_format       = hdr ? format_hdr : format_sdr;
        m_buffer_count = buffer_count;
        m_width        = width;
        m_height       = height;
        m_sdl_window   = sdl_window;
        m_object_name  = name;
        m_present_mode = present_mode;

        // create surface once
        {
            SP_ASSERT_MSG(SDL_Vulkan_CreateSurface(static_cast<SDL_Window*>(m_sdl_window), RHI_Context::instance, nullptr, reinterpret_cast<VkSurfaceKHR*>(&m_rhi_surface)), "Failed to create window surface");

            VkBool32 present_support = false;
            SP_ASSERT_VK(vkGetPhysicalDeviceSurfaceSupportKHR(
                RHI_Context::device_physical,
                RHI_Device::GetQueueIndex(RHI_Queue_Type::Graphics),
                static_cast<VkSurfaceKHR>(m_rhi_surface),
                &present_support
            ));
        }

        Create();

        SP_SUBSCRIBE_TO_EVENT(EventType::WindowResized, SP_EVENT_HANDLER(ResizeToWindowSize));
    }

    RHI_SwapChain::~RHI_SwapChain()
    {
        for (void*& image_view : m_rhi_rtv)
        {
            if (image_view)
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, image_view);
                image_view = nullptr;
            }
        }

        if (m_rhi_swapchain)
        {
            vkDestroySwapchainKHR(RHI_Context::device, static_cast<VkSwapchainKHR>(m_rhi_swapchain), nullptr);
            m_rhi_swapchain = nullptr;
        }

        if (m_rhi_surface)
        {
            vkDestroySurfaceKHR(RHI_Context::instance, static_cast<VkSurfaceKHR>(m_rhi_surface), nullptr);
            m_rhi_surface = nullptr;
        }
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetImageAcquiredSemaphore() const
    {
        // when minimized, image acquisition is not needed, so return nullptr
        return Window::IsMinimized() ? nullptr : m_image_acquired_semaphore[m_image_index].get();
    }

    void RHI_SwapChain::Create()
    {
        SP_ASSERT(m_sdl_window != nullptr);
        SP_ASSERT(m_rhi_surface != nullptr);

        // get surface capabilities
        VkSurfaceCapabilitiesKHR capabilities = get_surface_capabilities(static_cast<VkSurfaceKHR>(m_rhi_surface));

        // skip if window is minimized
        if (capabilities.currentExtent.width == 0 || capabilities.currentExtent.height == 0)
        {
            SP_LOG_WARNING("Window is minimized, swapchain creation skipped");
            return;
        }

        // check surface supports the requested format and color space, fall back to SDR if not supported
        VkColorSpaceKHR color_space = get_color_space(m_format);
        if (!is_format_and_color_space_supported(static_cast<VkSurfaceKHR>(m_rhi_surface), &m_format, color_space))
        {
            SP_LOG_WARNING("HDR format (%s, %d) not supported by surface. Falling back to SDR."
                           "On NVIDIA Optimus laptops, switch to 'High-performance NVIDIA processor' in NVIDIA Control Panel or Windows Graphics Settings,"
                           "or update to NVIDIA driver 551.xx+ for Vulkan HDR support.", rhi_format_to_string(m_format), color_space);

            Renderer::GetRenderOptionsPool().SetOption(Renderer_Option::Hdr, 0.0f);
            m_format    = format_sdr;
            color_space = get_color_space(m_format);
        }

        RHI_Device::QueueWaitAll();

        // clamp size
        m_width  = clamp(m_width,  capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        m_height = clamp(m_height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);

        // create new swapchain
        VkSwapchainCreateInfoKHR create_info = {};
        create_info.sType                    = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        create_info.surface                  = static_cast<VkSurfaceKHR>(m_rhi_surface);
        create_info.minImageCount            = m_buffer_count;
        create_info.imageFormat              = vulkan_format[rhi_format_to_index(m_format)];
        create_info.imageColorSpace          = color_space;
        create_info.imageExtent              = { m_width, m_height };
        create_info.imageArrayLayers         = 1;
        create_info.imageUsage               = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        create_info.imageSharingMode         = VK_SHARING_MODE_EXCLUSIVE;
        create_info.preTransform             = capabilities.currentTransform;
        create_info.compositeAlpha           = get_supported_composite_alpha_format(static_cast<VkSurfaceKHR>(m_rhi_surface));
        create_info.presentMode              = get_present_mode(static_cast<VkSurfaceKHR>(m_rhi_surface), m_present_mode);
        create_info.clipped                  = VK_TRUE;
        create_info.oldSwapchain             = static_cast<VkSwapchainKHR>(m_rhi_swapchain);

        // check for RivaTuner overlay interference, it adds bit flags without checking for compatibility with existing ones (so it can break stuff)
        if (is_process_running("RTSS.exe"))
        {
            SP_ERROR_WINDOW("RivaTuner is running and may crash the engine. Please close RivaTuner and restart the engine.");
        }

        SP_ASSERT_VK(vkCreateSwapchainKHR(RHI_Context::device, &create_info, nullptr, reinterpret_cast<VkSwapchainKHR*>(&m_rhi_swapchain)));

        // destroy old swapchain if it existed
        if (create_info.oldSwapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(RHI_Context::device, create_info.oldSwapchain, nullptr);
        }

        // get new images
        uint32_t image_count = 0;
        SP_ASSERT_VK(vkGetSwapchainImagesKHR(RHI_Context::device, static_cast<VkSwapchainKHR>(m_rhi_swapchain), &image_count, nullptr));
        SP_ASSERT_VK(vkGetSwapchainImagesKHR(RHI_Context::device, static_cast<VkSwapchainKHR>(m_rhi_swapchain), &image_count, reinterpret_cast<VkImage*>(m_rhi_rt.data())));

        // create new image views
        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            // delete old one, if it exists
            if (m_rhi_rtv[i])
            {
                RHI_Device::DeletionQueueAdd(RHI_Resource_Type::ImageView, m_rhi_rtv[i]);
            }

            VkImageViewCreateInfo view_info = {};
            view_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            view_info.image                 = static_cast<VkImage>(m_rhi_rt[i]);
            view_info.viewType              = VK_IMAGE_VIEW_TYPE_2D;
            view_info.format                = vulkan_format[rhi_format_to_index(m_format)];
            view_info.subresourceRange      = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            view_info.components            = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            SP_ASSERT_VK(vkCreateImageView(RHI_Context::device, &view_info, nullptr, reinterpret_cast<VkImageView*>(&m_rhi_rtv[i])));
        }

        // sync primitives
        for (uint32_t i = 0; i < static_cast<uint32_t>( m_image_acquired_semaphore.size()); i++)
        {
            m_image_acquired_semaphore[i] = make_shared<RHI_SyncPrimitive>(RHI_SyncPrimitive_Type::Semaphore, ("swapchain_" + to_string(i)).c_str());
        }

        // set HDR metadata only if HDR is enabled
        if (m_format == format_hdr)
        {
            set_hdr_metadata(static_cast<VkSwapchainKHR>(m_rhi_swapchain));
        }

         SP_LOG_INFO(
            "Swapchain created with resolution: %dx%d, HDR: %s (%s), VSync: %s",
            m_width,
            m_height,
            m_format == format_hdr ? "enabled" : "disabled",
            rhi_format_to_string(m_format),
            m_present_mode == RHI_Present_Mode::Fifo ? "enabled" : "disabled"
        );

        m_image_index = 0;
    }

    void RHI_SwapChain::Resize(const uint32_t width, const uint32_t height)
    {
        SP_ASSERT(RHI_Device::IsValidResolution(width, height));

        if (m_width == width && m_height == height)
            return;

        m_width  = width;
        m_height = height;

        Create();

        SP_LOG_INFO("Resolution has been set to %dx%d", width, height);
    }

    void RHI_SwapChain::ResizeToWindowSize()
    {
        Resize(Window::GetWidth(), Window::GetHeight());
    }

    void RHI_SwapChain::AcquireNextImage()
    {
        // when the window is minimized acquisition will fail and it's not necessery either
        if (Window::IsMinimized())
            return;

        // get semaphore
        RHI_SyncPrimitive* signal_semaphore = m_image_acquired_semaphore[semaphore_index].get();

        // ensure the semaphore is free; with enough semaphores, waits are rare as command lists typically complete before reuse
        if (RHI_CommandList* cmd_list = signal_semaphore->GetUserCmdList())
        {
            if (cmd_list->GetState() == RHI_CommandListState::Submitted)
            {
                cmd_list->WaitForExecution();
            }
            SP_ASSERT(cmd_list->GetState() == RHI_CommandListState::Idle);
        }

        // vk_not_ready can happen if the swapchain is not ready yet, possible during window events
        // it can happen often on some gpus/drivers and less and on others, regardless, it has to be handled
        uint32_t retry_count     = 0;
        const uint32_t retry_max = 10;
        while (retry_count < retry_max)
        {
            VkResult result = vkAcquireNextImageKHR(
                RHI_Context::device,
                static_cast<VkSwapchainKHR>(m_rhi_swapchain),
                16000000, // 16ms
                static_cast<VkSemaphore>(signal_semaphore->GetRhiResource()),
                nullptr,
                &m_image_index
            );

            if (result == VK_SUCCESS)
            {
                // associate the semaphore with the acquired image index
                m_image_acquired_semaphore[m_image_index] = m_image_acquired_semaphore[semaphore_index];
                semaphore_index                           = (semaphore_index + 1) % m_image_acquired_semaphore.size(); // rotate through semaphores
                return;
            }
            else if (result == VK_NOT_READY || result == VK_SUBOPTIMAL_KHR)
            {
                this_thread::sleep_for(chrono::milliseconds(16));
                retry_count++;
            }
            else if (result == VK_ERROR_OUT_OF_DATE_KHR)
            {
                Create();
            }
            else
            {
                SP_ASSERT_VK(result);
            }
        }
    }

    void RHI_SwapChain::Present(RHI_CommandList* cmd_list_frame)
    {
        // when the window is minimized acquisition isn't happening, so neither is presentation
        if (Window::IsMinimized())
            return;

        cmd_list_frame->GetQueue()->Present(m_rhi_swapchain, m_image_index, cmd_list_frame->GetRenderingCompleteSemaphore());

        // recreate the swapchain if needed - we do it here so that no semaphores are being destroyed while they are being waited for
        if (m_is_dirty)
        {
            Create();
            m_is_dirty = false;
        }
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
            m_format   = new_format;
            m_is_dirty = true;
        }
    }

    void RHI_SwapChain::SetVsync(const bool enabled)
    {
        if ((m_present_mode == RHI_Present_Mode::Fifo) != enabled)
        {
            m_present_mode = enabled ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate;
            m_is_dirty     = true;
            Timer::OnVsyncToggled(enabled);
        }
    }

    bool RHI_SwapChain::GetVsync()
    {
        // for v-sync, we could Mailbox for lower latency, but fifo is always supported, so we'll assume that
        return m_present_mode == RHI_Present_Mode::Fifo;
    }
}
