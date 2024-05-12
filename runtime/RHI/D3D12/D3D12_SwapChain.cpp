/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "../../Rendering/Renderer.h"
#include "../../Profiling/Profiler.h"
#include <wrl/client.h>
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    static DXGI_SWAP_EFFECT get_swap_effect()
    {
        #if !defined(_WIN32_WINNT)
            if (flags & RHI_Swap_Flip_Discard)
            {
                LOG_WARNING("Swap_Flip_Discard was requested but it's only supported in by Windows 10 or later, using Swap_Discard instead.");
                return DXGI_SWAP_EFFECT_DISCARD;
            }
        #endif

        if (RHI_Device::GetPrimaryPhysicalDevice()->IsIntel())
        {
            SP_LOG_WARNING("Swap_Flip_Discard was requested but it's not supported by Intel adapters, using Swap_Discard instead.");
            return DXGI_SWAP_EFFECT_DISCARD;
        }

        return DXGI_SWAP_EFFECT_FLIP_DISCARD;
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
        // Verify window handle
        const HWND hwnd = static_cast<HWND>(sdl_window);
        SP_ASSERT(hwnd != nullptr);
        SP_ASSERT(IsWindow(hwnd));

        // Verify resolution
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Get factory
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        {
            INT dxgiFactoryFlags = 0;
            if (!Profiler::IsValidationLayerEnabled())
            {
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }

            if (!d3d12_utility::error::check(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory))))
                return;
        }

        // Copy parameters
        m_format       = hdr ? format_hdr : format_sdr;
        m_buffer_count = buffer_count;
        m_width        = width;
        m_height       = height;
        m_sdl_window   = sdl_window;
        m_object_name  = name;
        m_present_mode = present_mode;

        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        swap_chain_desc.BufferCount           = m_buffer_count;
        swap_chain_desc.Width                 = m_width;
        swap_chain_desc.Height                = m_height;
        swap_chain_desc.Format                = d3d12_format[rhi_format_to_index(m_format)];
        swap_chain_desc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.SwapEffect            = get_swap_effect();
        swap_chain_desc.SampleDesc.Count      = 1;

        SP_ASSERT_MSG(false, "Need to implement RHI_Device::QueueGet(RHI_Queue_Type::Graphics)");
        //IDXGISwapChain1* swap_chain;
        //d3d12_utility::error::check(factory->CreateSwapChainForHwnd(
        //    static_cast<ID3D12CommandQueue*>(RHI_Device::QueueGet(RHI_Queue_Type::Graphics)), // todo: fix this again - swap chain needs the queue so that it can force a flush on it
        //    hwnd,
        //    &swap_chain_desc,
        //    nullptr,
        //    nullptr,
        //    &swap_chain
        //));

       //m_rhi_swapchain = static_cast<void*>(swap_chain);
        m_image_index   = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetCurrentBackBufferIndex();
    }
    
    RHI_SwapChain::~RHI_SwapChain()
    {
        d3d12_utility::release<IDXGISwapChain3>(m_rhi_swapchain);
    }
    
    void RHI_SwapChain::Resize(const uint32_t width, const uint32_t height, const bool force /*= false*/)
    {

    }
    
    void RHI_SwapChain::AcquireNextImage()
    {
        m_image_index = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetCurrentBackBufferIndex();
    }
    
    void RHI_SwapChain::Present()
    {
        SP_ASSERT(m_rhi_swapchain != nullptr && "Can't present, the swapchain has not been initialised");

        // Present parameters
        const bool tearing_allowed = m_present_mode == RHI_Present_Mode::Immediate;
        const UINT sync_interval   = tearing_allowed ? 0 : 1; // sync interval can go up to 4, so this could be improved
        const UINT flags           = (tearing_allowed && m_windowed) ? DXGI_PRESENT_ALLOW_TEARING : 0;

        // Present
        SP_ASSERT(d3d12_utility::error::check(static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->Present(sync_interval, flags))
            && "Failed to present");

        AcquireNextImage();
    }

    void RHI_SwapChain::SetLayout(const RHI_Image_Layout& layout, RHI_CommandList* cmd_list)
    {

    }

    void RHI_SwapChain::SetHdr(const bool enabled)
    {
        SP_LOG_ERROR("Not implemented.");
    }

    void RHI_SwapChain::SetVsync(const bool enabled)
    {
        SP_LOG_ERROR("Not implemented for D3D11. Please use the Vulkan build.");
    }

    bool RHI_SwapChain::GetVsync()
    {
        return false;
    }

    RHI_Image_Layout RHI_SwapChain::GetLayout() const
    {
        return m_layouts[m_image_index];
    }
}
