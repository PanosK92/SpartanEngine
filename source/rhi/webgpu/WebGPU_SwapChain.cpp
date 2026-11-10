/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../RHI_SyncPrimitive.h"
#include "../../core/Event.h"
#include "../../core/Window.h"
#include "../../profiling/Profiler.h"
#include "WebGPU_Internal.h"
SP_WARNINGS_OFF
#include <SDL3/SDL.h>
SP_WARNINGS_ON
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        atomic<WGPUTextureFormat> configured_format = WGPUTextureFormat_RGBA8Unorm;

        WGPUSurface create_surface(void* sdl_window)
        {
            WGPUSurfaceDescriptor descriptor = WGPU_SURFACE_DESCRIPTOR_INIT;
#if defined(__EMSCRIPTEN__)
            WGPUEmscriptenSurfaceSourceCanvasHTMLSelector source = WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
            source.selector                                      = webgpu::label("#canvas");
            descriptor.nextInChain                               = &source.chain;
#else
            WGPUSurfaceSourceWindowsHWND source = WGPU_SURFACE_SOURCE_WINDOWS_HWND_INIT;
            source.hwnd                         = SDL_GetPointerProperty(SDL_GetWindowProperties(static_cast<SDL_Window*>(sdl_window)), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
            source.hinstance                    = SDL_GetPointerProperty(SDL_GetWindowProperties(static_cast<SDL_Window*>(sdl_window)), SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr);
            descriptor.nextInChain              = &source.chain;
#endif
            return wgpuInstanceCreateSurface(RHI_Context::instance, &descriptor);
        }

        void release_images(vector<void*>& textures, vector<void*>& views)
        {
            for (uint32_t i = 0; i < textures.size(); i++)
            {
                if (views[i])
                {
                    wgpuTextureViewRelease(static_cast<WGPUTextureView>(views[i]));
                    views[i] = nullptr;
                }

                if (textures[i])
                {
                    wgpuTextureRelease(static_cast<WGPUTexture>(textures[i]));
                    textures[i] = nullptr;
                }
            }
        }
    }

    WGPUTextureFormat webgpu::surface_format()
    {
        return configured_format.load(memory_order_relaxed);
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
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        if (hdr)
        {
            SP_LOG_WARNING("HDR output is not supported on webgpu");
        }

        m_format       = format_sdr;
        m_buffer_count = buffer_count;
        m_width        = width;
        m_height       = height;
        m_sdl_window   = sdl_window;
        m_object_name  = name;
        m_present_mode = present_mode;
        m_rhi_rt.resize(m_buffer_count, nullptr);  // surface textures
        m_rhi_rtv.resize(m_buffer_count, nullptr); // their views
        m_rendering_complete_semaphore.resize(m_buffer_count);

        m_rhi_surface   = create_surface(sdl_window);
        m_rhi_swapchain = m_rhi_surface;
        SP_ASSERT_MSG(m_rhi_surface != nullptr, "Failed to create a webgpu surface");

        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            m_rendering_complete_semaphore[i] = make_shared<RHI_SyncPrimitive>(RHI_SyncPrimitive_Type::Semaphore, ("swapchain_present_" + to_string(i)).c_str());
        }

        Create();

        // only the main window swapchain tracks os resize
        if (m_sdl_window == Window::GetHandleSDL())
        {
            m_window_resize_event_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WindowResized, SP_EVENT_HANDLER(ResizeToWindowSize));
        }
    }

    void RHI_SwapChain::Create()
    {
        WGPUSurface surface = static_cast<WGPUSurface>(m_rhi_surface);

        WGPUSurfaceCapabilities capabilities = WGPU_SURFACE_CAPABILITIES_INIT;
        wgpuSurfaceGetCapabilities(surface, RHI_Context::adapter, &capabilities);

        // rgba8 matches the engine's swapchain format, bgra8 is what every surface has
        WGPUTextureFormat format = capabilities.formatCount ? capabilities.formats[0] : WGPUTextureFormat_BGRA8Unorm;
        for (size_t i = 0; i < capabilities.formatCount; i++)
        {
            if (capabilities.formats[i] == WGPUTextureFormat_RGBA8Unorm)
            {
                format = WGPUTextureFormat_RGBA8Unorm;
            }
        }

        auto has_present_mode = [&capabilities](const WGPUPresentMode mode)
        {
            for (size_t i = 0; i < capabilities.presentModeCount; i++)
            {
                if (capabilities.presentModes[i] == mode)
                    return true;
            }
            return false;
        };

        WGPUPresentMode present_mode = WGPUPresentMode_Fifo;
        if (m_present_mode != RHI_Present_Mode::Fifo)
        {
            present_mode = has_present_mode(WGPUPresentMode_Immediate) ? WGPUPresentMode_Immediate : has_present_mode(WGPUPresentMode_Mailbox) ? WGPUPresentMode_Mailbox : WGPUPresentMode_Fifo;
        }

        // blits and copies write the backbuffer too
        WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment;
        usage                 |= capabilities.usages & (WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding);
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);

        WGPUSurfaceConfiguration configuration = WGPU_SURFACE_CONFIGURATION_INIT;
        configuration.device                   = RHI_Context::device;
        configuration.format                   = format;
        configuration.usage                    = usage;
        configuration.width                    = m_width;
        configuration.height                   = m_height;
        configuration.alphaMode                = WGPUCompositeAlphaMode_Opaque;
        configuration.presentMode              = present_mode;
        wgpuSurfaceConfigure(surface, &configuration);
        configured_format.store(format, memory_order_relaxed);

        SP_LOG_INFO("Swapchain created with resolution: %dx%d, HDR: disabled (%s), VSync: %s",
            m_width,
            m_height,
            format == WGPUTextureFormat_RGBA8Unorm ? "RGBA8" : "BGRA8",
            present_mode == WGPUPresentMode_Fifo ? "enabled" : "disabled"
        );
    }

    RHI_SwapChain::~RHI_SwapChain()
    {
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WindowResized, m_window_resize_event_handle);
        m_window_resize_event_handle = 0;

        RHI_Device::QueueWaitAll();
        release_images(m_rhi_rt, m_rhi_rtv);

        if (m_rhi_surface)
        {
            wgpuSurfaceUnconfigure(static_cast<WGPUSurface>(m_rhi_surface));
            wgpuSurfaceRelease(static_cast<WGPUSurface>(m_rhi_surface));
            m_rhi_surface   = nullptr;
            m_rhi_swapchain = nullptr;
        }
    }

    void RHI_SwapChain::ResizeToWindowSize()
    {
        Resize(Window::GetWidthInPixels(), Window::GetHeightInPixels());
    }

    void RHI_SwapChain::Resize(const uint32_t width, const uint32_t height)
    {
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        if (m_width == width && m_height == height)
            return;

        m_width  = width;
        m_height = height;

        SP_LOG_INFO("Resolution has been set to %dx%d", m_width, m_height);

        RHI_Device::QueueWaitAll();
        release_images(m_rhi_rt, m_rhi_rtv);
        Create();
    }

    void RHI_SwapChain::AcquireNextImage()
    {
        m_image_acquired = false;

        if (!m_rhi_surface || Window::IsMinimized())
            return;

        ScopedTimeBlock time_block("acquire_image_wait");

        // the slot's previous image was presented, so it can go
        m_image_index = (m_image_index + 1) % m_buffer_count;
        if (m_rhi_rtv[m_image_index])
        {
            wgpuTextureViewRelease(static_cast<WGPUTextureView>(m_rhi_rtv[m_image_index]));
            m_rhi_rtv[m_image_index] = nullptr;
        }
        if (m_rhi_rt[m_image_index])
        {
            wgpuTextureRelease(static_cast<WGPUTexture>(m_rhi_rt[m_image_index]));
            m_rhi_rt[m_image_index] = nullptr;
        }

        WGPUSurfaceTexture surface_texture = WGPU_SURFACE_TEXTURE_INIT;
        wgpuSurfaceGetCurrentTexture(static_cast<WGPUSurface>(m_rhi_surface), &surface_texture);
        if (surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Outdated || surface_texture.status == WGPUSurfaceGetCurrentTextureStatus_Lost)
        {
            if (surface_texture.texture)
            {
                wgpuTextureRelease(surface_texture.texture);
            }
            Create();
            return;
        }

        if ((surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal && surface_texture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) || !surface_texture.texture)
            return;

        WGPUTextureViewDescriptor view_descriptor = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        view_descriptor.label                     = webgpu::label("swapchain");
        m_rhi_rt[m_image_index]                   = surface_texture.texture;
        m_rhi_rtv[m_image_index]                  = wgpuTextureCreateView(surface_texture.texture, &view_descriptor);
        m_image_acquired                          = true;
    }

    void RHI_SwapChain::Present(RHI_CommandList* cmd_list_frame)
    {
        if (!m_image_acquired)
            return;

        {
            ScopedTimeBlock time_block("frame_present");
#if !defined(__EMSCRIPTEN__)
            // the browser presents the canvas when control returns to it
            wgpuSurfacePresent(static_cast<WGPUSurface>(m_rhi_surface));
#endif
        }
        Profiler::MarkPresented(m_image_index, m_rhi_swapchain);

        m_image_acquired = false;
    }

    void RHI_SwapChain::SetHdr(const bool enabled)
    {
        if (enabled)
        {
            SP_LOG_WARNING("HDR output is not supported on webgpu");
        }
    }

    void RHI_SwapChain::SetVsync(const bool enabled)
    {
        const RHI_Present_Mode new_mode = enabled ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate;
        if (new_mode == m_present_mode)
            return;

        m_present_mode = new_mode;
        if (m_rhi_surface)
        {
            RHI_Device::QueueWaitAll();
            release_images(m_rhi_rt, m_rhi_rtv);
            Create();
        }
    }

    bool RHI_SwapChain::GetVsync()
    {
        return m_present_mode == RHI_Present_Mode::Fifo;
    }

    void RHI_SwapChain::SetFrameGeneration(const bool enabled)
    {
        if (enabled)
        {
            SP_LOG_WARNING("FSR frame generation is not supported on webgpu");
        }
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetImageAcquiredSemaphore() const
    {
        // webgpu orders the surface texture against the queue itself
        return nullptr;
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetRenderingCompleteSemaphore() const
    {
        return m_image_acquired ? m_rendering_complete_semaphore[m_image_index].get() : nullptr;
    }
}
