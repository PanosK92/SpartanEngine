/*
Copyright(c) 2015-2026 Panos Karabelas

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

#pragma once

//= INCLUDES =====================
#include <memory>
#include <array>
#include "RHI_Definitions.h"
#include "../Core/SpartanObject.h"
//================================

namespace spartan
{
    class RHI_SwapChain : public SpartanObject
    {
    public:
        RHI_SwapChain() = default;
        RHI_SwapChain(
            void* sdl_window,
            const uint32_t width,
            const uint32_t height,
            const RHI_Present_Mode present_mode,
            const uint32_t buffer_count,
            const bool hdr,
            const char* name
        );
        ~RHI_SwapChain();

        // size
        void Resize(uint32_t width, uint32_t height);
        void ResizeToWindowSize();

        // hdr
        void SetHdr(const bool enabled);
        bool IsHdr() const { return m_format == format_hdr; }

        // vsync
        void SetVsync(const bool enabled);
        bool GetVsync();

        // loop
        void AcquireNextImage();
        void Present(RHI_CommandList* cmd_list_frame);
        bool IsImageAcquired() const { return m_image_acquired; }

        // properties
        uint32_t GetWidth() const            { return m_width; }
        uint32_t GetHeight() const           { return m_height; }
        uint32_t GetBufferCount() const      { return m_buffer_count; }
        RHI_Format GetFormat() const         { return m_format; }
        uint32_t GetImageIndex() const       { return m_image_index; }
        void* GetRhiRt() const               { return m_rhi_rt[m_image_index]; }
        void* GetRhiRtv() const              { return m_rhi_rtv[m_image_index]; }
        void* GetRhiRtRaw(uint32_t i) const  { return m_rhi_rt[i]; }
        void SetRhiRt(uint32_t i, void* ptr) { m_rhi_rt[i] = ptr; }

        // misc
        RHI_SyncPrimitive* GetImageAcquiredSemaphore() const;
        RHI_SyncPrimitive* GetRenderingCompleteSemaphore() const;

        static const uint8_t buffer_count  = 2;
        static const RHI_Format format_sdr = RHI_Format::R8G8B8A8_Unorm;
        static const RHI_Format format_hdr = RHI_Format::R10G10B10A2_Unorm;

    private:
        void Create();

        // main
        bool m_windowed                 = false;
        uint32_t m_buffer_count         = 0;
        uint32_t m_width                = 0;
        uint32_t m_height               = 0;
        RHI_Format m_format             = RHI_Format::Max;
        RHI_Present_Mode m_present_mode = RHI_Present_Mode::Immediate;

        // misc
        bool m_is_dirty          = false;
        bool m_image_acquired    = false;
        uint32_t m_image_index   = 0;
        uint32_t semaphore_index = 0;
        void* m_sdl_window       = nullptr;
        subscription_handle m_window_resize_event_handle;
        std::array<std::shared_ptr<RHI_SyncPrimitive>, buffer_count> m_image_acquired_semaphore;
        std::array<std::shared_ptr<RHI_SyncPrimitive>, buffer_count> m_rendering_complete_semaphore;

        // rhi
        void* m_rhi_swapchain                     = nullptr;
        void* m_rhi_surface                       = nullptr;
        std::array<void*, buffer_count> m_rhi_rt  = { nullptr };
        std::array<void*, buffer_count> m_rhi_rtv = { nullptr };
    };
}
