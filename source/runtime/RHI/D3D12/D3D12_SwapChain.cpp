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

//= INCLUDES =====================
#include "pch.h"
#include "../RHI_Implementation.h"
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../RHI_CommandList.h"
#include "../RHI_Queue.h"
#include "../Core/Debugging.h"
#include <wrl/client.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>
//================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

// forward declarations from d3d12_device.cpp
namespace spartan::d3d12_descriptors
{
    ID3D12DescriptorHeap* GetRtvHeap();
    uint32_t GetRtvDescriptorSize();
    uint32_t AllocateRtv();
    D3D12_CPU_DESCRIPTOR_HANDLE GetRtvHandle(uint32_t index);
}

namespace spartan
{
    // local storage for rtv indices (not exposed in header)
    static std::array<uint32_t, RHI_SwapChain::buffer_count> s_rtv_indices = { 0 };

    // helper to get native hwnd from sdl_window
    static HWND get_hwnd_from_sdl_window(void* sdl_window)
    {
        SDL_Window* window = static_cast<SDL_Window*>(sdl_window);
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        return static_cast<HWND>(SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    }

    static void destroy_swapchain_resources(RHI_SwapChain* swapchain)
    {
        // release backbuffers
        for (uint32_t i = 0; i < swapchain->GetBufferCount(); i++)
        {
            if (swapchain->GetRhiRtRaw(i))
            {
                static_cast<ID3D12Resource*>(swapchain->GetRhiRtRaw(i))->Release();
                swapchain->SetRhiRt(i, nullptr);
            }
        }
    }

    static DXGI_SWAP_EFFECT get_swap_effect()
    {
        #if !defined(_WIN32_WINNT)
            return DXGI_SWAP_EFFECT_DISCARD;
        #endif

        if (RHI_Device::GetPrimaryPhysicalDevice() && RHI_Device::GetPrimaryPhysicalDevice()->IsIntel())
        {
            SP_LOG_WARNING("Using DXGI_SWAP_EFFECT_DISCARD for Intel adapter");
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
        // extract native hwnd from sdl window
        const HWND hwnd = get_hwnd_from_sdl_window(sdl_window);
        SP_ASSERT_MSG(hwnd != nullptr, "Failed to get HWND from SDL window");
        SP_ASSERT(IsWindow(hwnd));

        // verify resolution
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // copy parameters
        m_format       = hdr ? format_hdr : format_sdr;
        m_buffer_count = buffer_count;
        m_width        = width;
        m_height       = height;
        m_sdl_window   = sdl_window;
        m_object_name  = name;
        m_present_mode = present_mode;

        Create();
    }

    void RHI_SwapChain::Create()
    {
        // destroy existing resources
        destroy_swapchain_resources(this);

        const HWND hwnd = get_hwnd_from_sdl_window(m_sdl_window);

        // get factory
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;
        {
            UINT dxgi_factory_flags = 0;
            if (Debugging::IsValidationLayerEnabled())
            {
                dxgi_factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
            }

            if (!d3d12_utility::error::check(CreateDXGIFactory2(dxgi_factory_flags, IID_PPV_ARGS(&factory))))
            {
                SP_LOG_ERROR("Failed to create DXGI factory");
                return;
            }
        }

        // describe and create the swap chain
        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        swap_chain_desc.BufferCount           = m_buffer_count;
        swap_chain_desc.Width                 = m_width;
        swap_chain_desc.Height                = m_height;
        swap_chain_desc.Format                = d3d12_format[rhi_format_to_index(m_format)];
        swap_chain_desc.BufferUsage           = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.SwapEffect            = get_swap_effect();
        swap_chain_desc.SampleDesc.Count      = 1;
        swap_chain_desc.Flags                 = (m_present_mode == RHI_Present_Mode::Immediate) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        // get the graphics queue
        ID3D12CommandQueue* command_queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(RHI_Queue_Type::Graphics));
        if (!command_queue)
        {
            SP_LOG_ERROR("Graphics command queue is null");
            return;
        }

        // create swap chain
        IDXGISwapChain1* swap_chain_temp = nullptr;
        if (!d3d12_utility::error::check(factory->CreateSwapChainForHwnd(
            command_queue,
            hwnd,
            &swap_chain_desc,
            nullptr,
            nullptr,
            &swap_chain_temp
        )))
        {
            SP_LOG_ERROR("Failed to create swap chain");
            return;
        }

        // upgrade to swapchain3
        if (!d3d12_utility::error::check(swap_chain_temp->QueryInterface(IID_PPV_ARGS(reinterpret_cast<IDXGISwapChain3**>(&m_rhi_swapchain)))))
        {
            SP_LOG_ERROR("Failed to query IDXGISwapChain3");
            swap_chain_temp->Release();
            return;
        }
        swap_chain_temp->Release();

        // disable alt+enter fullscreen toggle
        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        // create render target views for each backbuffer
        ID3D12DescriptorHeap* rtv_heap = d3d12_descriptors::GetRtvHeap();
        uint32_t rtv_descriptor_size = d3d12_descriptors::GetRtvDescriptorSize();

        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            // get backbuffer
            ID3D12Resource* backbuffer = nullptr;
            if (!d3d12_utility::error::check(static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetBuffer(i, IID_PPV_ARGS(&backbuffer))))
            {
                SP_LOG_ERROR("Failed to get backbuffer %d", i);
                continue;
            }

            // allocate rtv slot
            uint32_t rtv_index = d3d12_descriptors::AllocateRtv();
            
            // create rtv
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            rtv_handle.ptr += rtv_index * rtv_descriptor_size;

            RHI_Context::device->CreateRenderTargetView(backbuffer, nullptr, rtv_handle);

            // store backbuffer and rtv info
            m_rhi_rt[i]       = backbuffer;
            s_rtv_indices[i]  = rtv_index;
        }

        // get the first backbuffer index
        m_image_index    = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetCurrentBackBufferIndex();
        m_image_acquired = true;

        SP_LOG_INFO("Swapchain created with resolution: %dx%d, HDR: %s (%s), VSync: %s",
            m_width,
            m_height,
            m_format == format_hdr ? "enabled" : "disabled",
            rhi_format_to_string(m_format),
            m_present_mode == RHI_Present_Mode::Fifo ? "enabled" : "disabled"
        );
    }

    RHI_SwapChain::~RHI_SwapChain()
    {
        RHI_Device::QueueWaitAll();

        destroy_swapchain_resources(this);

        if (m_rhi_swapchain)
        {
            static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->Release();
            m_rhi_swapchain = nullptr;
        }
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

        // wait for gpu
        RHI_Device::QueueWaitAll();

        // release backbuffers before resizing
        destroy_swapchain_resources(this);

        // resize
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetDesc1(&desc);
        
        if (!d3d12_utility::error::check(static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->ResizeBuffers(
            m_buffer_count,
            m_width,
            m_height,
            desc.Format,
            desc.Flags
        )))
        {
            SP_LOG_ERROR("Failed to resize swapchain");
            return;
        }

        // recreate render target views
        ID3D12DescriptorHeap* rtv_heap = d3d12_descriptors::GetRtvHeap();
        uint32_t rtv_descriptor_size = d3d12_descriptors::GetRtvDescriptorSize();

        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            // get backbuffer
            ID3D12Resource* backbuffer = nullptr;
            if (!d3d12_utility::error::check(static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetBuffer(i, IID_PPV_ARGS(&backbuffer))))
            {
                SP_LOG_ERROR("Failed to get backbuffer %d after resize", i);
                continue;
            }

            // create rtv at the same index
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            rtv_handle.ptr += s_rtv_indices[i] * rtv_descriptor_size;

            RHI_Context::device->CreateRenderTargetView(backbuffer, nullptr, rtv_handle);

            m_rhi_rt[i] = backbuffer;
        }

        m_image_index    = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetCurrentBackBufferIndex();
        m_image_acquired = true;
    }
    
    void RHI_SwapChain::AcquireNextImage()
    {
        m_image_acquired = false;

        if (!m_rhi_swapchain)
            return;

        // d3d12 doesn't have explicit image acquisition like vulkan
        // the current backbuffer index is determined by the swapchain
        m_image_index    = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetCurrentBackBufferIndex();
        m_image_acquired = true;
    }
    
    void RHI_SwapChain::Present(RHI_CommandList* cmd_list_frame)
    {
        // only present if we successfully acquired an image
        if (!m_image_acquired)
            return;

        if (!m_rhi_swapchain)
        {
            SP_LOG_ERROR("Can't present, the swapchain has not been initialized");
            return;
        }

        // note: barrier to present state should be handled by the command list before present

        // present parameters
        const bool tearing_allowed = m_present_mode == RHI_Present_Mode::Immediate;
        const UINT sync_interval   = tearing_allowed ? 0 : 1;
        const UINT flags           = tearing_allowed ? DXGI_PRESENT_ALLOW_TEARING : 0;

        // present
        HRESULT result = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->Present(sync_interval, flags);
        
        if (FAILED(result))
        {
            if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
            {
                SP_LOG_ERROR("Device lost during present");
            }
            else
            {
                SP_LOG_ERROR("Failed to present: %s", d3d12_utility::error::dxgi_error_to_string(result));
            }
        }

        // clear acquisition state after presentation
        m_image_acquired = false;
    }

    void RHI_SwapChain::SetHdr(const bool enabled)
    {
        // todo: implement hdr support
        SP_LOG_WARNING("HDR switching not yet implemented for D3D12");
    }

    void RHI_SwapChain::SetVsync(const bool enabled)
    {
        m_present_mode = enabled ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate;
    }

    bool RHI_SwapChain::GetVsync()
    {
        return m_present_mode == RHI_Present_Mode::Fifo;
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetImageAcquiredSemaphore() const
    {
        // d3d12 doesn't use semaphores for swapchain synchronization
        return nullptr;
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetRenderingCompleteSemaphore() const
    {
        // d3d12 doesn't use semaphores for swapchain synchronization
        return nullptr;
    }

    // helper function to get rtv handle for a swapchain image (used by command list)
    D3D12_CPU_DESCRIPTOR_HANDLE get_swapchain_rtv_handle(const RHI_SwapChain* swapchain)
    {
        return d3d12_descriptors::GetRtvHandle(s_rtv_indices[swapchain->GetImageIndex()]);
    }
}
