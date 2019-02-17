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

#pragma once

//= INCLUDES =====================
#include "../RHI_SwapChain.h"
#include "../RHI_Implementation.h"
#include "../RHI_Device.h"
#include "D3D11_Common.h"
#include "../../Logging/Log.h"
#include "../../Math/Vector4.h"
#include "../../Core/Settings.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	RHI_SwapChain::RHI_SwapChain(
		void* windowHandle,
		std::shared_ptr<RHI_Device> device,
		unsigned int width,
		unsigned int height,
		RHI_Format format			/*= Format_R8G8B8A8_UNORM*/,
		RHI_Swap_Effect swapEffect	/*= Swap_Discard*/,
		unsigned long flags			/*= 0 */,
		unsigned int bufferCount	/*= 1 */
	)
	{
		if (!windowHandle || !device)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto hwnd		= (HWND)windowHandle;
		m_format		= format;
		m_device		= device;
		m_flags			= flags;
		m_bufferCount	= bufferCount;

		// Get factory from device
		IDXGIDevice* dxgi_device	= nullptr;
		IDXGIAdapter* dxgi_adapter	= nullptr;
		IDXGIFactory* dxgi_factory	= nullptr;
		if (m_device->GetDevice<ID3D11Device>()->QueryInterface(IID_PPV_ARGS(&dxgi_device)) == S_OK)
		{
			if (dxgi_device->GetParent(IID_PPV_ARGS(&dxgi_adapter)) == S_OK)
			{
				dxgi_adapter->GetParent(IID_PPV_ARGS(&dxgi_factory));
			}
		}
		SafeRelease(dxgi_device);
		SafeRelease(dxgi_adapter);

		// Create swap chain
		{
			DXGI_SWAP_CHAIN_DESC desc;
			ZeroMemory(&desc, sizeof(desc));
			desc.BufferCount					= bufferCount;
			desc.BufferDesc.Width				= width;
			desc.BufferDesc.Height				= height;
			desc.BufferDesc.Format				= d3d11_dxgi_format[format];
			desc.BufferUsage					= DXGI_USAGE_RENDER_TARGET_OUTPUT;
			desc.OutputWindow					= hwnd;
			desc.SampleDesc.Count				= 1;
			desc.SampleDesc.Quality				= 0;
			desc.Windowed						= TRUE;
			desc.BufferDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			desc.BufferDesc.Scaling				= DXGI_MODE_SCALING_UNSPECIFIED;
			desc.SwapEffect						= d3d11_swap_effect[swapEffect];
			unsigned int d3d11_flags			= 0;
			d3d11_flags							|= flags & SwapChain_Allow_Mode_Switch	? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;
			d3d11_flags							|= flags & SwapChain_Allow_Tearing		? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
			desc.Flags							= d3d11_flags;

			auto swapChain = (IDXGISwapChain*)m_swapChain;
			auto result = dxgi_factory->CreateSwapChain(m_device->GetDevice<ID3D11Device>(), &desc, &swapChain);
			if (FAILED(result))
			{
				LOGF_ERROR("%s", D3D11_Common::DxgiErrorToString(result));
				return;
			}
			m_swapChain = (void*)swapChain;
		}

		// Create the render target
		if (auto swapChain = (IDXGISwapChain*)m_swapChain)
		{
			ID3D11Texture2D* ptr_backBuffer = nullptr;
			auto result = swapChain->GetBuffer(0, IID_PPV_ARGS(&ptr_backBuffer));
			if (FAILED(result))
			{
				LOGF_ERROR("%s", D3D11_Common::DxgiErrorToString(result));
				return;
			}

			auto renderTargetView = (ID3D11RenderTargetView*)m_renderTargetView;
			result = m_device->GetDevice<ID3D11Device>()->CreateRenderTargetView(ptr_backBuffer, nullptr, &renderTargetView);
			ptr_backBuffer->Release();
			if (FAILED(result))
			{
				LOGF_ERROR("%s", D3D11_Common::DxgiErrorToString(result));
			}
			m_renderTargetView = (void*)renderTargetView;
		}
	}

	RHI_SwapChain::~RHI_SwapChain()
	{
		auto swapChain = (IDXGISwapChain*)m_swapChain;

		// Before shutting down set to windowed mode to avoid swap chain exception
		if (swapChain)
		{
			swapChain->SetFullscreenState(false, nullptr);
		}

		SafeRelease(swapChain);
		SafeRelease((ID3D11RenderTargetView*)m_renderTargetView);
	}

	bool RHI_SwapChain::Resize(unsigned int width, unsigned int height)
	{	
		if (width == 0 || height == 0)
		{
			LOGF_ERROR("Size %fx%f is invalid.", width, height);
			return false;
		}

		if (!m_swapChain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto swapChain			= (IDXGISwapChain*)m_swapChain;
		auto renderTargetView	= (ID3D11RenderTargetView*)m_renderTargetView;

		// Release previous stuff
		SafeRelease(renderTargetView);
	
		DisplayMode displayMode;
		if (!Settings::Get().DisplayMode_GetFastest(&displayMode))
		{
			LOG_ERROR("Failed to get a display mode");
			return false;
		}

		// Resize swapchain target
		DXGI_MODE_DESC dxgiModeDesc;
		ZeroMemory(&dxgiModeDesc, sizeof(dxgiModeDesc));
		dxgiModeDesc.Width				= width;
		dxgiModeDesc.Height				= height;
		dxgiModeDesc.Format				= d3d11_dxgi_format[m_format];
		dxgiModeDesc.RefreshRate		= DXGI_RATIONAL{ displayMode.refreshRateNumerator, displayMode.refreshRateDenominator };
		dxgiModeDesc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;
		dxgiModeDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

		// Resize swapchain target
		auto result = swapChain->ResizeTarget(&dxgiModeDesc);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to resize swapchain target, %s.", D3D11_Common::DxgiErrorToString(result));
			return false;
		}

		// Resize swapchain buffers
		unsigned int d3d11_flags = 0;
		d3d11_flags |= m_flags & SwapChain_Allow_Mode_Switch ? DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH : 0;
		d3d11_flags |= m_flags & SwapChain_Allow_Tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
		result = swapChain->ResizeBuffers(m_bufferCount, (UINT)width, (UINT)height, dxgiModeDesc.Format, d3d11_flags);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to resize swapchain buffers, %s.", D3D11_Common::DxgiErrorToString(result));
			return false;
		}

		// Get swapchain back-buffer
		ID3D11Texture2D* pBackBuffer = nullptr;
		result = swapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to get swapchain buffer, %s.", D3D11_Common::DxgiErrorToString(result));
			return false;
		}

		// Create render target view
		result = m_device->GetDevice<ID3D11Device>()->CreateRenderTargetView(pBackBuffer, nullptr, &renderTargetView);
		SafeRelease(pBackBuffer);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create render target view, %s.", D3D11_Common::DxgiErrorToString(result));
			return false;
		}
		m_renderTargetView = (void*)renderTargetView;

		return true;
	}

	bool RHI_SwapChain::SetAsRenderTarget()
	{
		if(!m_device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto context			= m_device->GetDeviceContext<ID3D11DeviceContext>();
		auto renderTargetView	= (ID3D11RenderTargetView*)m_renderTargetView;
		if (!context || !renderTargetView)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		ID3D11DepthStencilView* depthStencil = nullptr;
		context->OMSetRenderTargets(1, &renderTargetView, depthStencil);
		return true;
	}

	bool RHI_SwapChain::Clear(const Vector4& color)
	{
		auto context			= m_device->GetDeviceContext<ID3D11DeviceContext>();
		auto renderTargetView	= (ID3D11RenderTargetView*)m_renderTargetView;
		if (!context || !renderTargetView)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		context->ClearRenderTargetView(renderTargetView, color.Data());
		return true;
	}

	bool RHI_SwapChain::Present(RHI_Present_Mode mode)
	{
		if (!m_swapChain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto ptr_swapChain = (IDXGISwapChain*)m_swapChain;
		ptr_swapChain->Present((UINT)mode, 0);
		return true;
	}
}