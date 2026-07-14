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
#include "../RHI_SyncPrimitive.h"
#include "../Core/Debugging.h"
#include "../Display/Display.h"
#include "../../Profiling/Breadcrumbs.h"
#include "../Core/Event.h"
#include "../Core/Window.h"
#include "../Rendering/Renderer.h"
#include "D3D12_Internal.h"
#include <wrl/client.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_properties.h>
#include <unordered_map>
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
                ID3D12Resource* resource = static_cast<ID3D12Resource*>(swapchain->GetRhiRtRaw(i));
                d3d12_state::RemoveState(resource);
                resource->Release();
                swapchain->SetRhiRt(i, nullptr);
            }
        }
    }

    // cache last applied color space per swapchain so resize does not re-spam SetHDRMetaData
    static unordered_map<IDXGISwapChain3*, DXGI_COLOR_SPACE_TYPE> s_color_space_by_swapchain;
    static unordered_map<IDXGISwapChain3*, bool> s_hdr10_metadata_active;

    static void clear_hdr_metadata(IDXGISwapChain3* swapchain)
    {
        const auto it = s_hdr10_metadata_active.find(swapchain);
        if (it == s_hdr10_metadata_active.end() || !it->second)
        {
            return;
        }

        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
        if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4))))
        {
            return;
        }
        swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_NONE, 0, nullptr);
        s_hdr10_metadata_active[swapchain] = false;
    }

    static void set_hdr_metadata(IDXGISwapChain3* swapchain)
    {
        Microsoft::WRL::ComPtr<IDXGISwapChain4> swapchain4;
        if (FAILED(swapchain->QueryInterface(IID_PPV_ARGS(&swapchain4))))
        {
            return;
        }

        // chromaticity in units of 0.00002, luminance in nits
        DXGI_HDR_METADATA_HDR10 meta = {};
        meta.RedPrimary[0]               = static_cast<UINT16>(0.708f * 50000.0f);
        meta.RedPrimary[1]               = static_cast<UINT16>(0.292f * 50000.0f);
        meta.GreenPrimary[0]             = static_cast<UINT16>(0.170f * 50000.0f);
        meta.GreenPrimary[1]             = static_cast<UINT16>(0.797f * 50000.0f);
        meta.BluePrimary[0]              = static_cast<UINT16>(0.131f * 50000.0f);
        meta.BluePrimary[1]              = static_cast<UINT16>(0.046f * 50000.0f);
        meta.WhitePoint[0]               = static_cast<UINT16>(0.3127f * 50000.0f);
        meta.WhitePoint[1]               = static_cast<UINT16>(0.3290f * 50000.0f);
        meta.MaxMasteringLuminance       = static_cast<UINT>(Display::GetLuminanceMax());
        meta.MinMasteringLuminance       = static_cast<UINT>(0.001f * 10000.0f);
        meta.MaxContentLightLevel        = 2000;
        meta.MaxFrameAverageLightLevel   = 500;

        HRESULT result = swapchain4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(meta), &meta);
        if (FAILED(result))
        {
            SP_LOG_WARNING("Failed to set DXGI HDR10 metadata");
            return;
        }
        s_hdr10_metadata_active[swapchain] = true;
    }

    static bool set_swapchain_color_space(IDXGISwapChain3* swapchain, const RHI_Format format)
    {
        if (!swapchain)
        {
            return false;
        }

        const DXGI_COLOR_SPACE_TYPE color_space = format == RHI_SwapChain::format_hdr ?
            (format == RHI_Format::R16G16B16A16_Float ?
                DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709 :
                DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020) :
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;

        UINT support = 0;
        HRESULT result = swapchain->CheckColorSpaceSupport(color_space, &support);
        if (FAILED(result) || !(support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT))
        {
            SP_LOG_WARNING("DXGI color space %d is not supported for presentation", static_cast<int>(color_space));
            return false;
        }

        const auto it = s_color_space_by_swapchain.find(swapchain);
        if (it != s_color_space_by_swapchain.end() && it->second == color_space)
        {
            return true;
        }

        result = swapchain->SetColorSpace1(color_space);
        if (FAILED(result))
        {
            SP_LOG_WARNING("Failed to set DXGI color space %d", static_cast<int>(color_space));
            return false;
        }
        s_color_space_by_swapchain[swapchain] = color_space;

        // hdr10 metadata is only meaningful for pq swapchains, scrgb stays metadata-free
        if (format == RHI_Format::R10G10B10A2_Unorm)
        {
            set_hdr_metadata(swapchain);
        }
        else
        {
            clear_hdr_metadata(swapchain);
        }

        return true;
    }

    static void forget_swapchain_color_space(IDXGISwapChain3* swapchain)
    {
        if (swapchain)
        {
            s_color_space_by_swapchain.erase(swapchain);
            s_hdr10_metadata_active.erase(swapchain);
        }
    }

    static void disable_hdr_cvar_silent()
    {
        // avoid on_change recursion back into SetHdr during create/fallback
        if (ConsoleVariable* var = ConsoleRegistry::Get().Find("r.hdr"))
        {
            *var->m_value_ptr = 0.0f;
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

        // only the main window swapchain tracks os resize, imgui child swapchains are resized via their own platform callbacks
        if (m_sdl_window == Window::GetHandleSDL())
        {
            m_window_resize_event_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WindowResized, SP_EVENT_HANDLER(ResizeToWindowSize));
        }
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
        swap_chain_desc.AlphaMode             = DXGI_ALPHA_MODE_IGNORE; // tell dwm not to use the alpha channel for compositing, matches vulkan's composite_alpha_opaque
        swap_chain_desc.Flags                 = (m_present_mode == RHI_Present_Mode::Immediate) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        // get the present queue, dedicated so vsync wait does not stall graphics
        ID3D12CommandQueue* command_queue = static_cast<ID3D12CommandQueue*>(RHI_Device::GetQueueRhiResource(RHI_Queue_Type::Present));
        if (!command_queue)
        {
            SP_LOG_ERROR("Present command queue is null");
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

        // fall back to sdr when the display rejects hdr10 color space, mirrors vulkan surface format check
        if (m_format == format_hdr && !set_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain), m_format))
        {
            SP_LOG_WARNING("HDR format not supported by display. Falling back to SDR.");
            disable_hdr_cvar_silent();
            m_format = format_sdr;
            forget_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain));

            DXGI_SWAP_CHAIN_DESC1 desc = {};
            static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetDesc1(&desc);
            if (!d3d12_utility::error::check(static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->ResizeBuffers(
                m_buffer_count, m_width, m_height, d3d12_format[rhi_format_to_index(m_format)], desc.Flags)))
            {
                SP_LOG_ERROR("Failed to resize swapchain to SDR after HDR fallback");
                return;
            }
            set_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain), m_format);
        }
        else if (m_format != format_hdr)
        {
            set_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain), m_format);
        }

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

            // backbuffers come out of dxgi in present state, seed the tracker so the first transition is correct
            d3d12_state::SetState(backbuffer, D3D12_RESOURCE_STATE_PRESENT);
            d3d12_state::SetSubresourceCount(backbuffer, 1);

            // store backbuffer and rtv info
            m_rhi_rt[i]       = backbuffer;
            s_rtv_indices[i]  = rtv_index;
        }

        // fences so the present queue can wait for graphics to finish writing the backbuffer
        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            m_rendering_complete_semaphore[i] = make_shared<RHI_SyncPrimitive>(RHI_SyncPrimitive_Type::Semaphore, ("swapchain_present_" + to_string(i)).c_str());
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
        SP_UNSUBSCRIBE_FROM_EVENT(EventType::WindowResized, m_window_resize_event_handle);
        m_window_resize_event_handle = 0;

        RHI_Device::QueueWaitAll();

        destroy_swapchain_resources(this);

        if (m_rhi_swapchain)
        {
            forget_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain));
            static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->Release();
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
        {
            return;
        }

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
                return;
            }

            // create rtv at the same index
            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            rtv_handle.ptr += s_rtv_indices[i] * rtv_descriptor_size;

            RHI_Context::device->CreateRenderTargetView(backbuffer, nullptr, rtv_handle);

            d3d12_state::SetState(backbuffer, D3D12_RESOURCE_STATE_PRESENT);
            d3d12_state::SetSubresourceCount(backbuffer, 1);

            m_rhi_rt[i] = backbuffer;
        }

        m_image_index    = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetCurrentBackBufferIndex();
        m_image_acquired = true;

        // resizeBuffers resets the color space, force re-apply
        forget_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain));
        if (!set_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain), m_format))
        {
            SP_LOG_WARNING("Failed to restore color space after resize (%s)", rhi_format_to_string(m_format));
        }
    }
    
    void RHI_SwapChain::AcquireNextImage()
    {
        m_image_acquired = false;

        if (!m_rhi_swapchain)
        {
            return;
        }

        // when the window is minimized acquisition will fail and it's not necessary either
        if (Window::IsMinimized())
        {
            return;
        }

        // d3d12 has no vkAcquireNextImageKHR, the current backbuffer index is owned by dxgi
        m_image_index    = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->GetCurrentBackBufferIndex();
        m_image_acquired = true;
    }
    
    void RHI_SwapChain::Present(RHI_CommandList* cmd_list_frame)
    {
        // only present if we successfully acquired an image
        if (!m_image_acquired)
        {
            return;
        }

        if (!m_rhi_swapchain)
        {
            SP_LOG_ERROR("Can't present, the swapchain has not been initialized");
            return;
        }

        // present queue waits for graphics to finish the backbuffer, then dxgi presents
        RHI_Device::GetQueue(RHI_Queue_Type::Present)->Present(m_rhi_swapchain, m_image_index, GetRenderingCompleteSemaphore());

        // present parameters, tearing can bypass dwm hdr composition so keep it off for hdr
        const bool hdr             = m_format == format_hdr;
        const bool tearing_allowed = m_present_mode == RHI_Present_Mode::Immediate && !hdr;
        const UINT sync_interval   = (m_present_mode == RHI_Present_Mode::Immediate) ? 0 : 1;
        const UINT flags           = tearing_allowed ? DXGI_PRESENT_ALLOW_TEARING : 0;

        // present
        HRESULT result = static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->Present(sync_interval, flags);
        
        if (FAILED(result))
        {
            if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET)
            {
                RHI_Device::SetDeviceLost();
                if (Debugging::IsBreadcrumbsEnabled())
                {
                    Breadcrumbs::OnDeviceLost();
                    SP_LOG_ERROR("GPU crashed. Check 'log.txt' for breadcrumbs report.");
                }
                else
                {
                    SP_LOG_ERROR("Device lost during present");
                }
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
        if (enabled)
        {
            SP_ASSERT_MSG(Display::GetHdr(), "This display doesn't support HDR");
        }

        if (!m_rhi_swapchain)
        {
            return;
        }

        const RHI_Format target_format = enabled ? format_hdr : format_sdr;
        if (target_format == m_format)
        {
            return;
        }

        // wait gpu, recreate buffers in the new format
        RHI_Device::QueueWaitAll();
        destroy_swapchain_resources(this);

        IDXGISwapChain3* dxgi = static_cast<IDXGISwapChain3*>(m_rhi_swapchain);
        DXGI_SWAP_CHAIN_DESC1 desc = {};
        dxgi->GetDesc1(&desc);
        forget_swapchain_color_space(dxgi);

        const DXGI_FORMAT new_format = d3d12_format[rhi_format_to_index(target_format)];
        if (!d3d12_utility::error::check(dxgi->ResizeBuffers(m_buffer_count, m_width, m_height, new_format, desc.Flags)))
        {
            SP_LOG_ERROR("Failed to resize swapchain to %s", enabled ? "HDR" : "SDR");
            return;
        }

        // recreate rtvs at the same indices
        ID3D12DescriptorHeap* rtv_heap = d3d12_descriptors::GetRtvHeap();
        uint32_t rtv_descriptor_size = d3d12_descriptors::GetRtvDescriptorSize();
        for (uint32_t i = 0; i < m_buffer_count; i++)
        {
            ID3D12Resource* backbuffer = nullptr;
            if (!d3d12_utility::error::check(dxgi->GetBuffer(i, IID_PPV_ARGS(&backbuffer))))
            {
                SP_LOG_ERROR("Failed to get backbuffer %d after HDR toggle", i);
                return;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
            rtv_handle.ptr += s_rtv_indices[i] * rtv_descriptor_size;
            RHI_Context::device->CreateRenderTargetView(backbuffer, nullptr, rtv_handle);
            d3d12_state::SetState(backbuffer, D3D12_RESOURCE_STATE_PRESENT);
            d3d12_state::SetSubresourceCount(backbuffer, 1);
            m_rhi_rt[i] = backbuffer;
        }

        m_image_index    = dxgi->GetCurrentBackBufferIndex();
        m_image_acquired = true;

        // hdr shaders must match the swapchain encoding, refuse a half-applied hdr mode
        if (!set_swapchain_color_space(dxgi, target_format))
        {
            SP_LOG_WARNING("HDR color space not supported after format change, falling back to SDR");
            disable_hdr_cvar_silent();
            forget_swapchain_color_space(dxgi);
            destroy_swapchain_resources(this);
            dxgi->GetDesc1(&desc);
            dxgi->ResizeBuffers(m_buffer_count, m_width, m_height, d3d12_format[rhi_format_to_index(format_sdr)], desc.Flags);
            for (uint32_t i = 0; i < m_buffer_count; i++)
            {
                ID3D12Resource* backbuffer = nullptr;
                if (SUCCEEDED(dxgi->GetBuffer(i, IID_PPV_ARGS(&backbuffer))))
                {
                    D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_heap->GetCPUDescriptorHandleForHeapStart();
                    rtv_handle.ptr += s_rtv_indices[i] * rtv_descriptor_size;
                    RHI_Context::device->CreateRenderTargetView(backbuffer, nullptr, rtv_handle);
                    d3d12_state::SetState(backbuffer, D3D12_RESOURCE_STATE_PRESENT);
                    d3d12_state::SetSubresourceCount(backbuffer, 1);
                    m_rhi_rt[i] = backbuffer;
                }
            }
            m_format = format_sdr;
            set_swapchain_color_space(dxgi, m_format);
            SP_LOG_INFO("Swapchain HDR disabled (%s)", rhi_format_to_string(m_format));
            return;
        }

        m_format = target_format;
        SP_LOG_INFO("Swapchain HDR %s (%s)", enabled ? "enabled" : "disabled", rhi_format_to_string(m_format));
    }

    void RHI_SwapChain::SetVsync(const bool enabled)
    {
        const RHI_Present_Mode new_mode = enabled ? RHI_Present_Mode::Fifo : RHI_Present_Mode::Immediate;
        if (new_mode == m_present_mode)
        {
            return;
        }

        m_present_mode = new_mode;

        // toggling tearing requires recreating the swapchain because DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING is a creation-time flag
        // recreate the entire swapchain, the buffers and rtvs are rebuilt by Create()
        if (m_rhi_swapchain)
        {
            RHI_Device::QueueWaitAll();
            destroy_swapchain_resources(this);
            forget_swapchain_color_space(static_cast<IDXGISwapChain3*>(m_rhi_swapchain));
            static_cast<IDXGISwapChain3*>(m_rhi_swapchain)->Release();
            m_rhi_swapchain = nullptr;
            Create();
        }
    }

    bool RHI_SwapChain::GetVsync()
    {
        return m_present_mode == RHI_Present_Mode::Fifo;
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetImageAcquiredSemaphore() const
    {
        // d3d12 present is synchronized by dxgi flip, not binary semaphores
        // returning these into Submit would queue->Wait on fences that are never gpu-signaled for acquire
        return nullptr;
    }

    RHI_SyncPrimitive* RHI_SwapChain::GetRenderingCompleteSemaphore() const
    {
        return m_image_acquired ? m_rendering_complete_semaphore[m_image_index].get() : nullptr;
    }

    // helper function to get rtv handle for a swapchain image (used by command list)
    D3D12_CPU_DESCRIPTOR_HANDLE get_swapchain_rtv_handle(const RHI_SwapChain* swapchain)
    {
        return d3d12_descriptors::GetRtvHandle(s_rtv_indices[swapchain->GetImageIndex()]);
    }
}
