/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= LINKING =====================
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
//===============================

//= INCLUDES ===================
#include <d3d12.h>
#include <dxgi1_4.h>
#include "../../Math/Vector4.h"
#include <string>
#include "../../Core/Settings.h"
//==============================

namespace Directus
{
	class D3D12GraphicsDevice
	{
	public:
		D3D12GraphicsDevice();
		~D3D12GraphicsDevice();

		bool Initialize(HWND handle);
		void Release();

		void Clear(const Directus::Math::Vector4& color);
		void Present() { m_swapChain->Present(VSYNC, 0); }

		ID3D12Device* GetDevice() { return m_device; }
		//ID3D12DeviceContext* GetDeviceContext() { return m_deviceContext; }

		void EnableZBuffer(bool enable);
		void EnabledAlphaBlending(bool enable);

		//void SetFaceCullMode(D3D11_CULL_MODE cull);
		//void SetBackBufferRenderTarget() { m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView); }

		void SetResolution(int width, int height);
		void SetViewport(int width, int height);
		//void ResetViewport() { m_deviceContext->RSSetViewports(1, &m_viewport); }

	private:
		ID3D12Device* m_device;
		ID3D12CommandQueue* m_commandQueue;
		IDXGISwapChain3* m_swapChain;
		ID3D12DescriptorHeap* m_renderTargetViewHeap;
		ID3D12Resource* m_backBufferRenderTarget[2];
		unsigned int m_bufferIndex;
		ID3D12CommandAllocator* m_commandAllocator;
		ID3D12GraphicsCommandList* m_commandList;
		ID3D12PipelineState* m_pipelineState;
		ID3D12Fence* m_fence;
		HANDLE m_fenceEvent;
		unsigned long long m_fenceValue;

		D3D_DRIVER_TYPE m_driverType;
		D3D_FEATURE_LEVEL m_featureLevel;
		D3D12_VIEWPORT m_viewport;

		DXGI_MODE_DESC* m_displayModeList;
		int m_videoCardMemory;
		std::string m_videoCardDescription;

		/*ID3D12Texture2D* m_depthStencilBuffer;
		ID3D12DepthStencilState* m_depthStencilStateEnabled;
		ID3D12DepthStencilState* m_depthStencilStateDisabled;
		ID3D12DepthStencilView* m_depthStencilView;

		ID3D12RasterizerState* m_rasterStateCullFront;
		ID3D12RasterizerState* m_rasterStateCullBack;
		ID3D12RasterizerState* m_rasterStateCullNone;

		ID3D12BlendState* m_blendStateAlphaEnabled;
		ID3D12BlendState* m_blendStateAlphaDisabled;
		*/

	private:
		bool CreateDepthStencilBuffer();
		bool CreateDepthStencil();
	};
}