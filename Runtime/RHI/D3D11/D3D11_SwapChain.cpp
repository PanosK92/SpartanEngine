/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES ===================
#include "../RHI_SwapChain.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "../../Math/Vector4.h"
#include "../../Core/Settings.h"
//==============================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	RHI_SwapChain::RHI_SwapChain(
		void* window_handle,
		const std::shared_ptr<RHI_Device>& device,
		unsigned int width,
		unsigned int height,
		const  RHI_Format format		/*= Format_R8G8B8A8_UNORM*/,
		RHI_Present_Mode present_mode	/*= Present_Off */,
		const unsigned int buffer_count	/*= 1 */,
		void* render_pass				/*= nullptr */
	)
	{
		const auto hwnd	= static_cast<HWND>(window_handle);
		if (!hwnd || !device || !IsWindow(hwnd))
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Return if resolution is invalid
		if (width == 0 || width > m_max_resolution || height == 0 || height > m_max_resolution)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return;
		}

		// Get device
		if (!device->GetContext()->device)
		{
			LOG_ERROR("Invalid device.");
			return;
		}

		// Get factory
		IDXGIFactory* dxgi_factory = nullptr;
		if (const auto& adapter = device->GetPrimaryAdapter())
		{
			auto dxgi_adapter = static_cast<IDXGIAdapter*>(adapter->data);
			dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));

			if (!dxgi_factory)
			{
				LOG_ERROR("Failed to get adapter's factory");
				return;
			}
		}
		else
		{
			LOG_ERROR("Invalid primary adapter");
			return;
		}

		// Save parameters
		m_format		= format;
		m_rhi_device	= device;
		m_buffer_count	= buffer_count;
		m_windowed		= true;
		m_width			= width;
		m_height		= height;
		m_present_mode	= present_mode;
		m_flags			= D3D11_Common::swap_chain::flag_filter(m_rhi_device.get(), SwapChain_Allow_Tearing);

		// Create swap chain
		{
			DXGI_SWAP_CHAIN_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.BufferCount					= static_cast<UINT>(buffer_count);
			desc.BufferDesc.Width				= static_cast<UINT>(width);
			desc.BufferDesc.Height				= static_cast<UINT>(height);
			desc.BufferDesc.Format				= d3d11_format[format];
			desc.BufferUsage					= DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.OutputWindow					= hwnd;
			desc.SampleDesc.Count				= 1;
			desc.SampleDesc.Quality				= 0;
			desc.Windowed						= m_windowed ? TRUE : FALSE;
			desc.BufferDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			desc.BufferDesc.Scaling				= DXGI_MODE_SCALING_UNSPECIFIED;
			desc.SwapEffect						= D3D11_Common::swap_chain::swap_effect_filter(m_rhi_device.get(), Swap_Flip_Discard);	// Todo, pick whatever emulates Vulkan more closely
			desc.Flags							= D3D11_Common::swap_chain::flag_to_d3d11(m_flags);										// Todo, pick whatever emulates Vulkan more closely

			auto swap_chain		= static_cast<IDXGISwapChain*>(m_swap_chain_view);
			const auto result	= dxgi_factory->CreateSwapChain(m_rhi_device->GetContext()->device, &desc, &swap_chain);
			if (FAILED(result))
			{
				LOGF_ERROR("%s", D3D11_Common::dxgi_error_to_string(result));
				return;
			}
			m_swap_chain_view = static_cast<void*>(swap_chain);
		}

		// Create the render target
		if (auto swap_chain = static_cast<IDXGISwapChain*>(m_swap_chain_view))
		{
			ID3D11Texture2D* backbuffer = nullptr;
			auto result = swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
			if (FAILED(result))
			{
				LOGF_ERROR("%s", D3D11_Common::dxgi_error_to_string(result));
				return;
			}

			auto render_target_view = static_cast<ID3D11RenderTargetView*>(m_render_target_view);
			result = m_rhi_device->GetContext()->device->CreateRenderTargetView(backbuffer, nullptr, &render_target_view);
			backbuffer->Release();
			if (FAILED(result))
			{
				LOGF_ERROR("%s", D3D11_Common::dxgi_error_to_string(result));
				return;
			}
			m_render_target_view = static_cast<void*>(render_target_view);
		}

		m_initialized = true;
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		auto swap_chain = static_cast<IDXGISwapChain*>(m_swap_chain_view);

		// Before shutting down set to windowed mode to avoid swap chain exception
		if (swap_chain && !m_windowed)
		{
			swap_chain->SetFullscreenState(false, nullptr);
		}

		safe_release(swap_chain);
		safe_release(static_cast<ID3D11RenderTargetView*>(m_render_target_view));
	}

	bool RHI_SwapChain::Resize(const unsigned int width, const unsigned int height)
	{	
		if (!m_swap_chain_view)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Return if resolution is invalid
		if (width == 0 || width > m_max_resolution || height == 0 || height > m_max_resolution)
		{
			LOGF_WARNING("%dx%d is an invalid resolution", width, height);
			return false;
		}

		auto swap_chain			= static_cast<IDXGISwapChain*>(m_swap_chain_view);
		auto render_target_view	= static_cast<ID3D11RenderTargetView*>(m_render_target_view);

		// Release previous stuff
		safe_release(render_target_view);

		// Set this flag to enable an application to switch modes by calling IDXGISwapChain::ResizeTarget.
		// When switching from windowed to full-screen mode, the display mode (or monitor resolution)
		// will be changed to match the dimensions of the application window.
		if (m_flags & SwapChain_Allow_Mode_Switch)
		{		
			DisplayMode display_mode;
			if (!m_rhi_device->GetDidsplayModeFastest(&display_mode))
			{
				LOG_ERROR("Failed to get a display mode");
				return false;
			}

			// Resize swapchain target
			DXGI_MODE_DESC dxgi_mode_desc;
			ZeroMemory(&dxgi_mode_desc, sizeof(dxgi_mode_desc));
			dxgi_mode_desc.Width			= static_cast<UINT>(width);
			dxgi_mode_desc.Height			= static_cast<UINT>(height);
			dxgi_mode_desc.Format			= d3d11_format[m_format];
			dxgi_mode_desc.RefreshRate		= DXGI_RATIONAL{ display_mode.refreshRateNumerator, display_mode.refreshRateDenominator };
			dxgi_mode_desc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;
			dxgi_mode_desc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

			// Resize swapchain target
			auto result = swap_chain->ResizeTarget(&dxgi_mode_desc);
			if (FAILED(result))
			{
				LOGF_ERROR("Failed to resize swapchain target, %s.", D3D11_Common::dxgi_error_to_string(result));
				return false;
			}
		}
	
		// Resize swapchain buffers
		UINT d3d11_flags = D3D11_Common::swap_chain::flag_to_d3d11(D3D11_Common::swap_chain::flag_filter(m_rhi_device.get(), m_flags));
		auto result = swap_chain->ResizeBuffers(m_buffer_count, static_cast<UINT>(width), static_cast<UINT>(height), d3d11_format[m_format], d3d11_flags);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to resize swapchain buffers, %s.", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		// Get swapchain back-buffer
		ID3D11Texture2D* backbuffer = nullptr;
		result = swap_chain->GetBuffer(0, IID_PPV_ARGS(&backbuffer));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to get swapchain buffer, %s.", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}

		// Create render target view
		result = m_rhi_device->GetContext()->device->CreateRenderTargetView(backbuffer, nullptr, &render_target_view);
		safe_release(backbuffer);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create render target view, %s.", D3D11_Common::dxgi_error_to_string(result));
			return false;
		}
		m_render_target_view = static_cast<void*>(render_target_view);

		return true;
	}

	bool RHI_SwapChain::Present(void* semaphore_wait)
	{
		if (!m_swap_chain_view)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		bool tearing_allowed	= m_flags & SwapChain_Allow_Tearing;
		UINT flags				= (tearing_allowed && m_windowed) ? DXGI_PRESENT_ALLOW_TEARING : 0;
		auto ptr_swap_chain		= static_cast<IDXGISwapChain*>(m_swap_chain_view);

		ptr_swap_chain->Present(static_cast<UINT>(m_present_mode), flags);
		return true;
	}
}
#endif