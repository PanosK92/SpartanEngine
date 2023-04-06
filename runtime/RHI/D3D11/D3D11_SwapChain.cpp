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
    RHI_SwapChain::RHI_SwapChain(
        void* sdl_window,
        const uint32_t width,
        const uint32_t height,
        const RHI_Format format     /*= Format_R8G8B8A8_UNORM*/,
        const uint32_t buffer_count /*= 2 */,
        const uint32_t flags        /*= Present_Immediate */,
        const char* name            /*= nullptr */
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
        if (!Renderer::GetRhiDevice()->IsValidResolution(width, height))
        {
            SP_LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        // Get factory
        IDXGIFactory* dxgi_factory = nullptr;
        if (const auto& adapter = Renderer::GetRhiDevice()->GetPrimaryPhysicalDevice())
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
        m_format       = format;
        m_buffer_count = buffer_count;
        m_windowed     = true;
        m_width        = width;
        m_height       = height;
        m_flags        = d3d11_utility::swap_chain::validate_flags(flags);
        m_name         = name;

        // Create swap chain
        {
            DXGI_SWAP_CHAIN_DESC desc = {};
            desc.BufferCount          = static_cast<UINT>(buffer_count);
            desc.BufferDesc.Width     = static_cast<UINT>(width);
            desc.BufferDesc.Height    = static_cast<UINT>(height);
            desc.BufferDesc.Format    = d3d11_format[rhi_format_to_index(format)];
            desc.BufferUsage          = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            desc.OutputWindow         = hwnd;
            desc.SampleDesc.Count     = 1;
            desc.SampleDesc.Quality   = 0;
            desc.Windowed             = m_windowed ? TRUE : FALSE;
            desc.SwapEffect           = d3d11_utility::swap_chain::get_swap_effect(m_flags);
            desc.Flags                = d3d11_utility::swap_chain::get_flags(m_flags);

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

        // Validate resolution
        m_present_enabled = Renderer::GetRhiDevice()->IsValidResolution(width, height);

        if (!m_present_enabled)
            return false;

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
        if (m_flags & RHI_SwapChain_Allow_Mode_Switch)
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

            const UINT d3d11_flags = d3d11_utility::swap_chain::get_flags(d3d11_utility::swap_chain::validate_flags(m_flags));
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
        SP_ASSERT(m_present_enabled && "Can't present, presenting has been disabled");

        // Present parameters
        const bool tearing_allowed = m_flags & RHI_Present_Immediate;
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
}
