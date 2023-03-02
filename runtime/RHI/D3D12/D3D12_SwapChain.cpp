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
    RHI_SwapChain::RHI_SwapChain(
        void* sdl_window,
        RHI_Device* rhi_device,
        const uint32_t width,
        const uint32_t height,
        const RHI_Format format	    /*= Format_R8G8B8A8_UNORM*/,
        const uint32_t buffer_count	/*= 2 */,
        const uint32_t flags	    /*= Present_Immediate */,
        const char* name            /*= nullptr */
    )
    {
        SP_ASSERT(rhi_device != nullptr);

        // Verify window handle
        const HWND hwnd = static_cast<HWND>(sdl_window);
        SP_ASSERT(hwnd != nullptr);
        SP_ASSERT(IsWindow(hwnd));

        // Verify resolution
        if (!rhi_device->IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Get factory
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        {
            INT dxgiFactoryFlags = 0;
            if (!rhi_device->GetRhiContext()->validation)
            {
                dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }

            if (!d3d12_utility::error::check(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory))))
                return;
        }

        // Copy parameters
        m_format       = format;
        m_rhi_device   = rhi_device;
        m_buffer_count = buffer_count;
        m_width        = width;
        m_height       = height;
        m_sdl_window   = sdl_window;
        m_flags        = flags;
        m_name         = name;

        // Describe and create the swap chain.
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        swap_chain_desc.BufferCount           = m_buffer_count;
        swap_chain_desc.Width                 = m_width;
        swap_chain_desc.Height                = m_height;
        swap_chain_desc.Format                = d3d12_format[format];
        swap_chain_desc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.SwapEffect            = d3d12_utility::swap_chain::get_swap_effect(m_flags);
        swap_chain_desc.SampleDesc.Count      = 1;

        IDXGISwapChain1* swap_chain;
        d3d12_utility::error::check(factory->CreateSwapChainForHwnd(
            static_cast<ID3D12CommandQueue*>(m_rhi_device->GetQueue(RHI_Queue_Type::Graphics)), // Swap chain needs the queue so that it can force a flush on it.
            hwnd,
            &swap_chain_desc,
            nullptr,
            nullptr,
            &swap_chain
        ));

        m_rhi_resource = static_cast<void*>(swap_chain);
        m_image_index  = static_cast<IDXGISwapChain3*>(m_rhi_resource)->GetCurrentBackBufferIndex();
    }
    
    RHI_SwapChain::~RHI_SwapChain()
    {
        d3d12_utility::release<IDXGISwapChain3>(m_rhi_resource);
    }
    
    bool RHI_SwapChain::Resize(const uint32_t width, const uint32_t height, const bool force /*= false*/)
    {
        return true;
    }
    
    void RHI_SwapChain::AcquireNextImage()
    {
        m_image_index = static_cast<IDXGISwapChain3*>(m_rhi_resource)->GetCurrentBackBufferIndex();
    }
    
    void RHI_SwapChain::Present()
    {
        SP_ASSERT(m_rhi_resource != nullptr && "Can't present, the swapchain has not been initialised");
        SP_ASSERT(m_present_enabled && "Can't present, presenting has been disabled");

        // Present parameters
        const bool tearing_allowed = m_flags & RHI_Present_Immediate;
        const UINT sync_interval   = tearing_allowed ? 0 : 1; // sync interval can go up to 4, so this could be improved
        const UINT flags           = (tearing_allowed && m_windowed) ? DXGI_PRESENT_ALLOW_TEARING : 0;

        // Present
        SP_ASSERT(d3d12_utility::error::check(static_cast<IDXGISwapChain3*>(m_rhi_resource)->Present(sync_interval, flags))
            && "Failed to present");

        AcquireNextImage();
    }

    void RHI_SwapChain::SetLayout(const RHI_Image_Layout& layout, RHI_CommandList* cmd_list)
    {

    }
}
