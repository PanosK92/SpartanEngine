/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ============
#include <vector>
#include "../IGraphics.h"
//=======================

namespace Directus
{
	class D3D11Graphics : public IGraphics
	{
	public:
		D3D11Graphics(Context* context);
		~D3D11Graphics();	

		//= Sybsystem ============
		bool Initialize() override;
		//========================

		bool CreateBlendStates();

		//= IGraphics ========================================================
		void Clear(const Math::Vector4& color) override;
		void Present() override;
		void SetBackBufferAsRenderTarget() override;

		// Depth
		bool CreateDepthStencilState(void* depthStencilState, bool depthEnabled, bool writeEnabled) override;
		bool CreateDepthStencilBuffer() override;
		bool CreateDepthStencilView() override;
		void EnableDepth(bool enable) override;

		void EnableAlphaBlending(bool enable) override;
		void SetInputLayout(InputLayout inputLayout) override;
		CullMode GetCullMode() override { return m_cullMode; }
		void SetCullMode(CullMode cullMode) override;
		void SetPrimitiveTopology(PrimitiveTopology primitiveTopology) override;

		// Viewport
		bool SetResolution(int width, int height) override;
		const Viewport& GetViewport() override { return m_backBuffer_viewport; }
		void SetViewport(float width, float height) override;
		void SetViewport() override;
		float GetMaxDepth() override { return m_maxDepth; }

		bool IsInitialized() override { return m_initialized; }

		ID3D11DepthStencilView* GetDepthStencilView() { return m_depthStencilView; }

		//= EVENTS =======================================
		void EventBegin(const std::string& name) override;
		void EventEnd() override;
		//================================================

		//======================================================================

		ID3D11Device* GetDevice() { return m_device; }
		ID3D11DeviceContext* GetDeviceContext() { return m_deviceContext; }

	private:
		//= HELPER FUNCTIONS =================================================================================================
		bool CreateDeviceAndSwapChain(ID3D11Device** device, ID3D11DeviceContext** deviceContext, IDXGISwapChain** swapchain);
		bool CreateRasterizerState(CullMode cullMode, FillMode fillMode, ID3D11RasterizerState** rasterizer);
		std::vector<IDXGIAdapter*> GetAvailableAdapters(IDXGIFactory* factory);	
		IDXGIAdapter* GetAdapterWithTheHighestVRAM(IDXGIFactory* factory);
		IDXGIAdapter* GetAdapterByVendorID(IDXGIFactory* factory, unsigned int vendorID);
		std::string GetAdapterDescription(IDXGIAdapter* adapter);
		//====================================================================================================================

		ID3D11Device* m_device;
		ID3D11DeviceContext* m_deviceContext;
		IDXGISwapChain* m_swapChain;
		ID3D11RenderTargetView* m_renderTargetView;	
		unsigned int m_displayModeCount;
		unsigned int m_refreshRateNumerator;
		unsigned int m_refreshRateDenominator;
		DXGI_MODE_DESC* m_displayModeList;	
		ID3D11Texture2D* m_depthStencilBuffer;
		ID3D11DepthStencilState* m_depthStencilStateEnabled;
		ID3D11DepthStencilState* m_depthStencilStateDisabled;
		ID3D11DepthStencilView* m_depthStencilView;
		ID3D11RasterizerState* m_rasterStateCullFront;
		ID3D11RasterizerState* m_rasterStateCullBack;
		ID3D11RasterizerState* m_rasterStateCullNone;
		ID3D11BlendState* m_blendStateAlphaEnabled;
		ID3D11BlendState* m_blendStateAlphaDisabled;
		bool m_initialized;
		ID3DUserDefinedAnnotation* m_eventReporter;
	};
}
