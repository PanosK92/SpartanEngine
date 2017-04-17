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
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
//===============================

//= INCLUDES ==================
#include "../IGraphicsDevice.h"
#include <d3d11.h>
//=============================

class D3D11GraphicsDevice : public IGraphicsDevice
{
public:
	D3D11GraphicsDevice(Context* context);
	~D3D11GraphicsDevice();

	//==========================================================================
	virtual bool Initialize(HWND windowHandle);
	virtual void Clear(const Directus::Math::Vector4& color);
	virtual void Present();
	virtual void SetBackBufferAsRenderTarget();
	//==========================================================================

	//= DEPTH =================================================================
	virtual bool CreateDepthStencil();
	virtual bool CreateDepthStencilBuffer();
	virtual bool CreateDepthStencilView();
	virtual void EnableZBuffer(bool enable);
	//=========================================================================

	//=========================================================================
	virtual void EnableAlphaBlending(bool enable);
	virtual void SetInputLayout(InputLayout inputLayout);
	virtual void SetCullMode(CullMode cullMode);
	virtual void SetPrimitiveTopology(PrimitiveTopology primitiveTopology);
	//=========================================================================

	//= VIEWPORT ==============================================================
	virtual bool SetResolution(int width, int height);
	virtual void SetViewport(float width, float height);
	virtual void ResetViewport();
	//=========================================================================

	virtual bool IsInitialized() { return m_initializedSuccessfully; }

	ID3D11Device* GetDevice() { return m_device; }
	ID3D11DeviceContext* GetDeviceContext() { return m_deviceContext; }

private:
	bool m_initializedSuccessfully;
	ID3D11Device* m_device;
	ID3D11DeviceContext* m_deviceContext;
	IDXGISwapChain* m_swapChain;
	ID3D11RenderTargetView* m_renderTargetView;
	D3D_DRIVER_TYPE m_driverType;
	D3D_FEATURE_LEVEL m_featureLevel;
	D3D11_VIEWPORT m_viewport;

	UINT m_displayModeCount;
	UINT m_refreshRateNumerator;
	UINT m_refreshRateDenominator;
	DXGI_MODE_DESC* m_displayModeList;

	int m_videoCardMemory;
	char* m_videoCardDescription;

	ID3D11Texture2D* m_depthStencilBuffer;
	ID3D11DepthStencilState* m_depthStencilStateEnabled;
	ID3D11DepthStencilState* m_depthStencilStateDisabled;
	ID3D11DepthStencilView* m_depthStencilView;

	ID3D11RasterizerState* m_rasterStateCullFront;
	ID3D11RasterizerState* m_rasterStateCullBack;
	ID3D11RasterizerState* m_rasterStateCullNone;

	ID3D11BlendState* m_blendStateAlphaEnabled;
	ID3D11BlendState* m_blendStateAlphaDisabled;
};
