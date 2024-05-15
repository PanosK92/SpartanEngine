/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include <vector>
#include <array>
#include "RHI_Definitions.h"
#include "../Core/SpartanObject.h"
//================================

namespace Spartan
{
    static const uint8_t max_buffer_count = 3;
    static const RHI_Format format_sdr    = RHI_Format::R8G8B8A8_Unorm;
    static const RHI_Format format_hdr    = RHI_Format::R10G10B10A2_Unorm;

    class SP_CLASS RHI_SwapChain : public SpartanObject
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
        void Resize(uint32_t width, uint32_t height, const bool force = false);
        void ResizeToWindowSize();

        // hdr
        void SetHdr(const bool enabled);
        bool IsHdr() const { return m_format == format_hdr; }

        // vsync
        void SetVsync(const bool enabled);
        bool GetVsync();

        void Present();

        // properties
        uint32_t GetWidth()       const { return m_width; }
        uint32_t GetHeight()      const { return m_height; }
        uint32_t GetBufferCount() const { return m_buffer_count; }
        RHI_Format GetFormat()    const { return m_format; }
        void* GetRhiRt()          const { return m_rhi_rt[m_image_index]; }
        void* GetRhiRtv()         const { return m_rhi_rtv[m_image_index]; }

        // layout
        RHI_Image_Layout GetLayout() const;
        void SetLayout(const RHI_Image_Layout& layout, RHI_CommandList* cmd_list);

    private:
        void Create();
        void Destroy();
        void AcquireNextImage();

        // main
        bool m_windowed                 = false;
        uint32_t m_buffer_count         = 0;
        uint32_t m_width                = 0;
        uint32_t m_height               = 0;
        RHI_Format m_format             = RHI_Format::Max;
        RHI_Present_Mode m_present_mode = RHI_Present_Mode::Immediate;

        // misc
        uint32_t m_sync_index                                    = std::numeric_limits<uint32_t>::max();
        uint32_t m_image_index                                   = std::numeric_limits<uint32_t>::max();
        void* m_sdl_window                                       = nullptr;
        std::array<RHI_Image_Layout, max_buffer_count> m_layouts = { RHI_Image_Layout::Max, RHI_Image_Layout::Max, RHI_Image_Layout::Max };
        std::array<std::shared_ptr<RHI_Semaphore>, max_buffer_count> m_image_acquired_semaphore;
        std::array<std::shared_ptr<RHI_Fence>, max_buffer_count> m_image_acquired_fence;
        std::vector<RHI_Semaphore*> m_wait_semaphores;

        // rhi
        void* m_rhi_swapchain                         = nullptr;
        void* m_rhi_surface                           = nullptr;
        std::array<void*, max_buffer_count> m_rhi_rt  = { nullptr, nullptr, nullptr};
        std::array<void*, max_buffer_count> m_rhi_rtv = { nullptr, nullptr, nullptr};
    };
}
