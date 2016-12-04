/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===========================
#include <d3d11.h>
#include <vector>
#include "../../FileSystem/FileSystem.h"
//======================================

class GraphicsAPI
{
	friend class Graphics;

public:
	GraphicsAPI()
	{
		m_device = nullptr;
		m_deviceContext = nullptr;
		m_swapChain = nullptr;
		m_renderTargetView = nullptr;
		m_driverType = D3D_DRIVER_TYPE_HARDWARE;
		m_featureLevel = D3D_FEATURE_LEVEL_11_0;
		m_displayModeList = nullptr;
		m_displayModeCount = 0;
		m_refreshRateNumerator = 0;
		m_refreshRateDenominator = 0;
		m_videoCardMemory = 0;
		m_videoCardDescription = DATA_NOT_ASSIGNED;
		m_depthStencilBuffer = nullptr;
		m_depthStencilStateEnabled = nullptr;
		m_depthStencilStateDisabled = nullptr;
		m_depthStencilView = nullptr;
		m_rasterStateCullFront = nullptr;
		m_rasterStateCullBack = nullptr;
		m_rasterStateCullNone = nullptr;
		m_blendStateAlphaEnabled = nullptr;
		m_blendStateAlphaDisabled = nullptr;
	};

	ID3D11Device* GetDevice() { return m_device; }
	ID3D11DeviceContext* GetDeviceContext() { return m_deviceContext; }

private:
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
	std::string m_videoCardDescription;

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
