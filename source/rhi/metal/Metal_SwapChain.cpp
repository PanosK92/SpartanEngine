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
#include "Metal_Internal.h"
#include <CoreGraphics/CGColorSpace.h>
SP_WARNINGS_OFF
#include <SDL3/SDL_metal.h>
SP_WARNINGS_ON
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan
{
    namespace
    {
        CA::MetalLayer* get_layer(void* swapchain)
        {
            return static_cast<CA::MetalLayer*>(swapchain);
        }

        void release_drawables(RHI_SwapChain* swapchain, vector<void*>& drawables)
        {
            for (uint32_t i = 0; i < drawables.size(); i++)
            {
                if (drawables[i])
                {
                    static_cast<CA::MetalDrawable*>(drawables[i])->release();
                    drawables[i] = nullptr;
                }
                swapchain->SetRhiRt(i, nullptr);
            }
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
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        m_format       = hdr ? format_hdr : format_sdr;
        m_buffer_count = buffer_count;
        m_width        = width;
        m_height       = height;
        m_sdl_window   = sdl_window;
        m_object_name  = name;
        m_present_mode = present_mode;
        m_rhi_rt.resize(m_buffer_count, nullptr);
        m_rhi_rtv.resize(m_buffer_count, nullptr); // holds the acquired drawables
        m_rendering_complete_semaphore.resize(m_buffer_count);

        // sdl owns the view, the layer comes with it
        m_rhi_surface = SDL_Metal_CreateView(static_cast<SDL_Window*>(sdl_window));
        SP_ASSERT_MSG(m_rhi_surface != nullptr, "Failed to create metal view");
        m_rhi_swapchain = SDL_Metal_GetLayer(m_rhi_surface);

        CA::MetalLayer* layer = get_layer(m_rhi_swapchain);
        layer->setDevice(RHI_Context::device);
        layer->setFramebufferOnly(false); // blits and copies write the backbuffer too

        // the drawables the layer hands out must be resident for the queue to write them
        metal_queue::Get()->addResidencySet(layer->residencySet());

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
        CA::MetalLayer* layer = get_layer(m_rhi_swapchain);
        const bool hdr        = m_format == format_hdr;

        layer->setPixelFormat(metal_swapchain::layer_format(m_format));
        layer->setDrawableSize(CGSize{ static_cast<CGFloat>(m_width), static_cast<CGFloat>(m_height) });
        layer->setMaximumDrawableCount(clamp<uint32_t>(m_buffer_count, 2, 3));
        layer->setDisplaySyncEnabled(m_present_mode == RHI_Present_Mode::Fifo);

        // hdr10 output, the same pq encoding the vulkan path hands to the wsi
        CGColorSpaceRef color_space = CGColorSpaceCreateWithName(hdr ? kCGColorSpaceITUR_2100_PQ : kCGColorSpaceSRGB);
        layer->setColorspace(color_space);
        CGColorSpaceRelease(color_space);

        SP_LOG_INFO("Swapchain created with resolution: %dx%d, HDR: %s (%s), VSync: %s",
            m_width,
            m_height,
            hdr ? "enabled" : "disabled",
            rhi_format_to_string(m_format),
            m_present_mode == RHI_Present_Mode::Fifo ? "enabled" : "disabled"
        );
    }

    RHI_SwapChain::~RHI_SwapChain()
    {
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WindowResized, m_window_resize_event_handle);
        m_window_resize_event_handle = 0;

        RHI_Device::QueueWaitAll();
        release_drawables(this, m_rhi_rtv);

        if (m_rhi_swapchain)
        {
            metal_queue::Get()->removeResidencySet(get_layer(m_rhi_swapchain)->residencySet());
            m_rhi_swapchain = nullptr;
        }

        if (m_rhi_surface)
        {
            SDL_Metal_DestroyView(m_rhi_surface);
            m_rhi_surface = nullptr;
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

        // drawables already handed out keep their old size, only new ones change
        RHI_Device::QueueWaitAll();
        get_layer(m_rhi_swapchain)->setDrawableSize(CGSize{ static_cast<CGFloat>(m_width), static_cast<CGFloat>(m_height) });
    }

    void RHI_SwapChain::AcquireNextImage()
    {
        m_image_acquired = false;

        if (!m_rhi_swapchain || Window::IsMinimized())
            return;

        ScopedTimeBlock time_block("acquire_image_wait");

        // the previous holder of this slot was presented, so its drawable can go
        m_image_index = (m_image_index + 1) % m_buffer_count;
        if (m_rhi_rtv[m_image_index])
        {
            static_cast<CA::MetalDrawable*>(m_rhi_rtv[m_image_index])->release();
            m_rhi_rtv[m_image_index] = nullptr;
        }

        // nextDrawable autoreleases, keep our own reference and drain the pool right away
        NS::AutoreleasePool* pool   = NS::AutoreleasePool::alloc()->init();
        CA::MetalDrawable* drawable = get_layer(m_rhi_swapchain)->nextDrawable();
        if (drawable)
        {
            drawable->retain();
        }
        pool->release();

        if (!drawable)
            return;

        // everything committed from now on waits until the display is done reading this drawable
        {
            lock_guard<mutex> lock(metal_queue::GetMutex());
            metal_queue::Get()->wait(drawable);
        }

        m_rhi_rtv[m_image_index] = drawable;
        m_rhi_rt[m_image_index]  = drawable->texture();
        m_image_acquired         = true;
    }

    void RHI_SwapChain::Present(RHI_CommandList* cmd_list_frame)
    {
        if (!m_image_acquired)
            return;

        CA::MetalDrawable* drawable = static_cast<CA::MetalDrawable*>(m_rhi_rtv[m_image_index]);
        {
            ScopedTimeBlock time_block("frame_present");
            lock_guard<mutex> lock(metal_queue::GetMutex());
            metal_queue::Get()->signalDrawable(drawable);
            drawable->present();
        }
        Profiler::MarkPresented(m_image_index, m_rhi_swapchain);

        m_image_acquired = false;
    }

    void RHI_SwapChain::SetHdr(const bool enabled)
    {
        const RHI_Format target_format = enabled ? format_hdr : format_sdr;
        if (target_format == m_format || !m_rhi_swapchain)
            return;

        RHI_Device::QueueWaitAll();
        m_format = target_format;
        Create();
    }

    void RHI_SwapChain::SetVsync(const bool enabled)
    {
        const RHI_Present_Mode new_mode = enabled ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate;
        if (new_mode == m_present_mode)
            return;

        m_present_mode = new_mode;
        if (m_rhi_swapchain)
        {
            get_layer(m_rhi_swapchain)->setDisplaySyncEnabled(enabled);
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
            SP_LOG_WARNING("FSR frame generation is not supported on metal");
        }
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetImageAcquiredSemaphore() const
    {
        // the queue waits on the drawable itself, see AcquireNextImage
        return nullptr;
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetRenderingCompleteSemaphore() const
    {
        return m_image_acquired ? m_rendering_complete_semaphore[m_image_index].get() : nullptr;
    }
}
