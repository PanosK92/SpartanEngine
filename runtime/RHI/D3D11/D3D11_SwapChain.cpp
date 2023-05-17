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
#include "../../Rendering/Renderer.h"
#include "../../Profiling/Profiler.h"
#include "sdl/SDL_video.h"
#include "sdl/SDL_syswm.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    // Determines whether tearing support is available for fullscreen borderless windows.
    static bool check_tearing_support()
    {
        // Rather than create the 1.5 factory interface directly, we create the 1.4
        // interface and query for the 1.5 interface. This will enable the graphics
        // debugging tools which might not support the 1.5 factory interface
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory4;
        HRESULT resut = CreateDXGIFactory1(IID_PPV_ARGS(&factory4));
        BOOL allowTearing = FALSE;
        if (SUCCEEDED(resut))
        {
            Microsoft::WRL::ComPtr<IDXGIFactory5> factory5;
            resut = factory4.As(&factory5);
            if (SUCCEEDED(resut))
            {
                resut = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
            }
        }

        const bool fullscreen_borderless_support = SUCCEEDED(resut) && allowTearing;
        const bool vendor_support = !RHI_Device::GetPrimaryPhysicalDevice()->IsIntel(); // Intel, bad

        return fullscreen_borderless_support && vendor_support;
    }

    static RHI_Present_Mode get_supported_present_mode(const RHI_Present_Mode present_mode)
    {
        // If SwapChain_Allow_Tearing was requested
        if (present_mode == RHI_Present_Mode::Immediate)
        {
            // Check if the adapter supports it, if not, disable it (tends to fail with Intel adapters)
            if (!check_tearing_support())
            {

                SP_LOG_WARNING("RHI_Present_Mode::Immediate is not supported, falling back to RHI_Present_Mode::Fifo");
                return RHI_Present_Mode::Fifo;
            }
        }

        return present_mode;
    }

    static UINT get_present_flags(const RHI_Present_Mode present_mode)
    {
        UINT d3d11_flags = 0;

        d3d11_flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        d3d11_flags |= present_mode == RHI_Present_Mode::Immediate ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        return d3d11_flags;
    }

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
        const bool is_hdr,
        const RHI_Present_Mode present_mode,
        const uint32_t buffer_count,
        const char* name
    )
    {
        // Get window handle
        SDL_SysWMinfo win_info;
        SDL_VERSION(&win_info.version);
        SDL_GetWindowWMInfo(static_cast<SDL_Window*>(sdl_window), &win_info);
        HWND hwnd = win_info.info.win.window;

        // Verify window handle
        SP_ASSERT(hwnd != nullptr);
        SP_ASSERT(IsWindow(hwnd));

        // Verify resolution
        if (!RHI_Device::IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Get factory
        IDXGIFactory* dxgi_factory = nullptr;
        if (const auto& adapter = RHI_Device::GetPrimaryPhysicalDevice())
        {
            auto dxgi_adapter = static_cast<IDXGIAdapter*>(adapter->GetData());
            if (dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory)) != S_OK)
            {
                SP_LOG_ERROR("Failed to get adapter's factory");
                return;
            }
        }
        else
        {
            SP_LOG_ERROR("Invalid primary adapter");
            return;
        }

        // Save parameters
        m_format       = is_hdr ? format_hdr : format_sdr;
        m_buffer_count = buffer_count;
        m_windowed     = true;
        m_width        = width;
        m_height       = height;
        m_object_name  = name;
        m_present_mode = present_mode;

        // Create swap chain
        {
            DXGI_SWAP_CHAIN_DESC desc = {};
            desc.BufferCount          = static_cast<UINT>(buffer_count);
            desc.BufferDesc.Width     = static_cast<UINT>(width);
            desc.BufferDesc.Height    = static_cast<UINT>(height);
            desc.BufferDesc.Format    = d3d11_format[rhi_format_to_index(m_format)];
            desc.BufferUsage          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.OutputWindow         = hwnd;
            desc.SampleDesc.Count     = 1;
            desc.SampleDesc.Quality   = 0;
            desc.Windowed             = m_windowed ? TRUE : FALSE;
            desc.SwapEffect           = get_swap_effect();
            desc.Flags                = get_present_flags(m_present_mode);

            if (!d3d11_utility::error_check(dxgi_factory->CreateSwapChain(RHI_Context::device, &desc, reinterpret_cast<IDXGISwapChain**>(&m_rhi_resource))))
            {
                SP_LOG_ERROR("Failed to create swapchain");
                return;
            }
        }

        // Create the render target
        if (IDXGISwapChain* swap_chain = static_cast<IDXGISwapChain*>(m_rhi_resource))
        {
            ID3D11Texture2D* backbuffer = nullptr;
            auto result = swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
            if (FAILED(result))
            {
                SP_LOG_ERROR("%s", d3d11_utility::dxgi_error_to_string(result));
                return;
            }

            result = RHI_Context::device->CreateRenderTargetView(backbuffer, nullptr, reinterpret_cast<ID3D11RenderTargetView**>(&m_rhi_srv));
            backbuffer->Release();
            if (FAILED(result))
            {
                SP_LOG_ERROR("%s", d3d11_utility::dxgi_error_to_string(result));
                return;
            }
        }

        m_sync_index = 0;
    }

    RHI_SwapChain::~RHI_SwapChain()
    {
        IDXGISwapChain* swap_chain = static_cast<IDXGISwapChain*>(m_rhi_resource);

        // Before shutting down set to windowed mode to avoid swap chain exception
        if (swap_chain && !m_windowed)
        {
            swap_chain->SetFullscreenState(false, nullptr);
        }

        swap_chain->Release();
        swap_chain = nullptr;

        d3d11_utility::release<ID3D11RenderTargetView>(m_rhi_srv);
    }

    bool RHI_SwapChain::Resize(const uint32_t width, const uint32_t height, const bool force /*= false*/)
    {
        SP_ASSERT(m_rhi_resource != nullptr);
        SP_ASSERT_MSG(RHI_Device::IsValidResolution(width, height), "Invalid resolution");

        // Only resize if needed
        if (!force)
        {
            if (m_width == width && m_height == height)
                return false;
        }

        // Save new dimensions
        m_width  = width;
        m_height = height;

        IDXGISwapChain* swap_chain = static_cast<IDXGISwapChain*>(m_rhi_resource);

        // Set this flag to enable an application to switch modes by calling IDXGISwapChain::ResizeTarget.
        // When switching from windowed to full-screen mode, the display mode (or monitor resolution)
        // will be changed to match the dimensions of the application window.
        if (true) // DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH
        {
            // Resize swapchain target
            DXGI_MODE_DESC dxgi_mode_desc   = {};
            dxgi_mode_desc.Width            = static_cast<UINT>(width);
            dxgi_mode_desc.Height           = static_cast<UINT>(height);
            dxgi_mode_desc.Format           = d3d11_format[rhi_format_to_index(m_format)];
            dxgi_mode_desc.RefreshRate      = DXGI_RATIONAL{ static_cast<UINT>(Display::GetRefreshRate()), 1 };
            dxgi_mode_desc.Scaling          = DXGI_MODE_SCALING_UNSPECIFIED;
            dxgi_mode_desc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
            
            // Resize swapchain target
            const auto result = swap_chain->ResizeTarget(&dxgi_mode_desc);
            if (FAILED(result))
            {
                SP_LOG_ERROR("Failed to resize swapchain target, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }
        }

        // Resize swapchain buffers
        {
            // Release the previous render target view (to avoid IDXGISwapChain::ResizeBuffers: Swapchain cannot be resized unless all outstanding buffer references have been released)
            ID3D11RenderTargetView* render_target_view = static_cast<ID3D11RenderTargetView*>(m_rhi_srv);
            if (render_target_view)
            {
                render_target_view->Release();
                render_target_view = nullptr;
            }

            const UINT d3d11_flags = get_present_flags(get_supported_present_mode(m_present_mode));
            auto result = swap_chain->ResizeBuffers(m_buffer_count, static_cast<UINT>(width), static_cast<UINT>(height), d3d11_format[rhi_format_to_index(m_format)], d3d11_flags);
            if (FAILED(result))
            {
                SP_LOG_ERROR("Failed to resize swapchain buffers, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }
        }

        // Create render target view
        {
            // Get swapchain back-buffer
            ID3D11Texture2D* backbuffer = nullptr;
            auto result = swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
            if (FAILED(result))
            {
                SP_LOG_ERROR("Failed to get swapchain buffer, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }

            // Create new one
            ID3D11RenderTargetView* render_target_view = static_cast<ID3D11RenderTargetView*>(m_rhi_srv);
            result = RHI_Context::device->CreateRenderTargetView(backbuffer, nullptr, &render_target_view);
            backbuffer->Release();
            backbuffer = nullptr;
            if (FAILED(result))
            {
                SP_LOG_ERROR("Failed to create render target view, %s.", d3d11_utility::dxgi_error_to_string(result));
                return false;
            }

            m_rhi_srv = static_cast<void*>(render_target_view);
        }
        return true;
    }

    void RHI_SwapChain::AcquireNextImage()
    {

    }

    void RHI_SwapChain::Present()
    {
        SP_ASSERT(m_rhi_resource != nullptr && "Can't present, the swapchain has not been initialised");

        // Present parameters
        const bool tearing_allowed = m_present_mode == RHI_Present_Mode::Immediate;
        const UINT sync_interval   = tearing_allowed ? 0 : 1; // sync interval can go up to 4, so this could be improved
        const UINT flags           = (tearing_allowed && m_windowed) ? DXGI_PRESENT_ALLOW_TEARING : 0;

        // Present
        SP_ASSERT(d3d11_utility::error_check(static_cast<IDXGISwapChain*>(m_rhi_resource)->Present(sync_interval, flags)) && "Failed to present");
    }

    void RHI_SwapChain::SetLayout(const RHI_Image_Layout& layout, RHI_CommandList* cmd_list)
    {

    }

    void RHI_SwapChain::SetHdr(const bool enabled)
    {
        SP_LOG_ERROR("Not implemented for D3D11. Please use the Vulkan build.");
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
        return RHI_Image_Layout::Present_Src;
    }
}
