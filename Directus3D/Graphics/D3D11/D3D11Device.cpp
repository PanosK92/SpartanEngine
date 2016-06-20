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

//= INCLUDES ===================
#include "D3D11Device.h"
#include <string>
#include "../../Core/Settings.h"
#include "../../IO/Log.h"
//==============================

D3D11Device::D3D11Device()
{
	m_swapChain = nullptr;
	m_device = nullptr;
	m_deviceContext = nullptr;
	m_renderTargetView = nullptr;
	m_depthStencilBuffer = nullptr;
	m_depthStencilStateEnabled = nullptr;
	m_depthStencilStateDisabled = nullptr;
	m_depthStencilView = nullptr;
	m_rasterStateCullFront = nullptr;
	m_rasterStateCullBack = nullptr;
	m_rasterStateCullNone = nullptr;
	m_alphaBlendingStateEnabled = nullptr;
	m_alphaBlendingStateDisabled = nullptr;
	currentCullMode = D3D11_CULL_BACK;
	m_videoCardMemory = 0;
}

D3D11Device::~D3D11Device()
{
}

void D3D11Device::Initialize(HWND handle)
{
	unsigned int screenWidth = (unsigned int)RESOLUTION_WIDTH;
	unsigned int screenHeight = (unsigned int)RESOLUTION_HEIGHT;

	/*------------------------------------------------------------------------------
							[Graphics Interface Factory]
	------------------------------------------------------------------------------*/
	// Create a DirectX graphics interface factory.
	IDXGIFactory* factory;
	HRESULT hResult = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
	if (FAILED(hResult))
	LOG("Failed to create a DirectX graphics interface factory.", Log::Error);

	/*------------------------------------------------------------------------------
										[Adapter]
	------------------------------------------------------------------------------*/
	// Use the factory to create an adapter for the primary graphics interface (video card).
	IDXGIAdapter* adapter;
	hResult = factory->EnumAdapters(0, &adapter);
	if (FAILED(hResult))
	LOG("Failed to create a primary graphics interface adapter.", Log::Error);

	// Release the factory.
	factory->Release();

	/*------------------------------------------------------------------------------
									[Adapter Output]
	------------------------------------------------------------------------------*/
	IDXGIOutput* adapterOutput;
	unsigned int numModes;

	// Enumerate the primary adapter output (monitor).
	hResult = adapter->EnumOutputs(0, &adapterOutput);
	if (FAILED(hResult))
	LOG("Failed to enumerate the primary adapter output.", Log::Error);

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	hResult = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);
	if (FAILED(hResult))
	LOG("Failed to get adapter's display modes.", Log::Error);

	/*------------------------------------------------------------------------------
							[Display Mode Description]
	------------------------------------------------------------------------------*/
	unsigned int i, stringLength;
	unsigned int numerator = 0, denominator = 1;

	// Create a list to hold all the possible display modes for this monitor/video card combination.
	DXGI_MODE_DESC* m_displayModeList = new DXGI_MODE_DESC[numModes];
	if (!m_displayModeList)
	LOG("Failed to create a display mode list.", Log::Error);

	// Now fill the display mode list structures.
	hResult = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, m_displayModeList);
	if (FAILED(hResult))
	LOG("Failed to fill the display mode list structures.", Log::Error);

	// Release the adapter output.
	adapterOutput->Release();

	// Now go through all the display modes and find the one that matches the screen width and height.
	// When a match is found store the numerator and denominator of the refresh rate for that monitor.
	for (i = 0; i < numModes; i++)
	{
		if (m_displayModeList[i].Width == (unsigned int)screenWidth)
		{
			if (m_displayModeList[i].Height == (unsigned int)screenHeight)
			{
				numerator = m_displayModeList[i].RefreshRate.Numerator;
				denominator = m_displayModeList[i].RefreshRate.Denominator;
			}
		}
	}

	/*------------------------------------------------------------------------------
							[Adapter Description]
	------------------------------------------------------------------------------*/
	DXGI_ADAPTER_DESC adapterDesc;
	// Get the adapter (video card) description.
	hResult = adapter->GetDesc(&adapterDesc);
	if (FAILED(hResult))
	LOG("Failed to get the adapter's description.", Log::Error);

	// Release the adapter.
	adapter->Release();

	// Store the dedicated video card memory in megabytes.
	m_videoCardMemory = static_cast<int>(adapterDesc.DedicatedVideoMemory / 1024 / 1024);

	// Convert the name of the video card to a character array and store it.
	char cardDescription[128];
	int error = wcstombs_s(&stringLength, cardDescription, 128, adapterDesc.Description, 128);
	m_videoCardDescription = cardDescription;
	if (error != 0)
	LOG("Failed to convert the adapter's name.", Log::Error);

	/*------------------------------------------------------------------------------
							[Swap Chain Description]
	------------------------------------------------------------------------------*/
	DXGI_SWAP_CHAIN_DESC swapChainDesc;

	// Initialize the swap chain description.
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

	swapChainDesc.BufferCount = 2; // Set to a single back buffer.
	swapChainDesc.BufferDesc.Width = screenWidth; // Set the width of the back buffer.
	swapChainDesc.BufferDesc.Height = screenHeight; // Set the height of the back buffer.
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Set regular 32-bit surface for the back buffer.

	// Set the refresh rate of the back buffer.
	/*if (Settings::GetInstance().IsVsyncEnabled())
	{
		swapChainDesc.BufferDesc.RefreshRate.Numerator = numerator;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = denominator;
	}
	else
	{
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 0;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
	}
*/
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // Set the usage of the back buffer.	
	swapChainDesc.OutputWindow = handle; // Set the handle for the window to render to.
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	// Set to full screen or windowed mode.
	if (Settings::GetInstance().IsFullScreen())
		swapChainDesc.Windowed = false;
	else
		swapChainDesc.Windowed = true;

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = 0;

	/*------------------------------------------------------------------------------
									[Feature Level]
	------------------------------------------------------------------------------*/
	// Set the feature level to DirectX 11.
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	hResult = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1, D3D11_SDK_VERSION, &swapChainDesc, &m_swapChain, &m_device, nullptr, &m_deviceContext);
	if (FAILED(hResult))
	LOG("Failed to create the swap chain, Direct3D device, and Direct3D device context.", Log::Error);

	/*------------------------------------------------------------------------------
					[Create a render target view for the back buffer]
	------------------------------------------------------------------------------*/
	ID3D11Texture2D* backBufferPtr;
	// Get the pointer to the back buffer.
	hResult = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&backBufferPtr));
	if (FAILED(hResult))
	{
		LOG("Failed to get the pointer to the back buffer.", Log::Error);
	}

	// Create the render target view with the back buffer pointer.
	hResult = m_device->CreateRenderTargetView(backBufferPtr, nullptr, &m_renderTargetView);
	if (FAILED(hResult))
	{
		LOG("Failed to create the render target view.", Log::Error);
	}

	// Release pointer to the back buffer as we no longer need it.
	backBufferPtr->Release();
	backBufferPtr = nullptr;

	/*------------------------------------------------------------------------------
							[Depth Buffer Description]
	------------------------------------------------------------------------------*/
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	// Initialize the description of the depth buffer.
	ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));

	// Set up the description of the depth buffer.
	depthBufferDesc.Width = screenWidth;
	depthBufferDesc.Height = screenHeight;
	depthBufferDesc.MipLevels = 1;
	depthBufferDesc.ArraySize = 1;
	depthBufferDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthBufferDesc.SampleDesc.Count = 1;
	depthBufferDesc.SampleDesc.Quality = 0;
	depthBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	depthBufferDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depthBufferDesc.CPUAccessFlags = 0;
	depthBufferDesc.MiscFlags = 0;

	// Create the texture for the depth buffer using the filled out description.
	hResult = m_device->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);
	if (FAILED(hResult))
	LOG("Failed to create the texture for the depth buffer.", Log::Error);

	/*------------------------------------------------------------------------------
									[Depth-Stencil]
	------------------------------------------------------------------------------*/
	// Create a depth stencil state with depth enabled
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc = GetDepthStencilDesc(true);
	hResult = m_device->CreateDepthStencilState(&depthStencilDesc, &m_depthStencilStateEnabled);
	if (FAILED(hResult))
	LOG("Failed to create depth stencil state.", Log::Error);

	// Create a depth stencil state with depth disabled
	depthStencilDesc = GetDepthStencilDesc(false);
	HRESULT result = m_device->CreateDepthStencilState(&depthStencilDesc, &m_depthStencilStateDisabled);
	if (FAILED(result))
	LOG("Failed to create depth stencil state.", Log::Error);

	// Set the default depth stencil state
	m_deviceContext->OMSetDepthStencilState(m_depthStencilStateEnabled, 1);

	/*------------------------------------------------------------------------------
						[Depth-Stencil View Description]
	------------------------------------------------------------------------------*/
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	// Initialize the depth stencil view.
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	hResult = m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
	if (FAILED(hResult))
	LOG("Failed to create the depth stencil view.", Log::Error);

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);

	/*------------------------------------------------------------------------------
							[Rasterizer Description]
	------------------------------------------------------------------------------*/
	// Create a rasterizer state with back face culling
	D3D11_RASTERIZER_DESC rasterDesc = GetRasterizerDesc(D3D11_CULL_BACK);
	hResult = m_device->CreateRasterizerState(&rasterDesc, &m_rasterStateCullBack);
	if (FAILED(hResult))
	LOG("Failed to create the rasterizer state.", Log::Error);

	// Create a rasterizer state with front face culling
	rasterDesc = GetRasterizerDesc(D3D11_CULL_FRONT);
	hResult = m_device->CreateRasterizerState(&rasterDesc, &m_rasterStateCullFront);
	if (FAILED(hResult))
	LOG("Failed to create the rasterizer state.", Log::Error);

	// Create a rasterizer state with no face culling
	rasterDesc = GetRasterizerDesc(D3D11_CULL_NONE);
	hResult = m_device->CreateRasterizerState(&rasterDesc, &m_rasterStateCullNone);
	if (FAILED(hResult))
	LOG("Failed to create the rasterizer state.", Log::Error);

	// set the default rasterizer state
	m_deviceContext->RSSetState(m_rasterStateCullBack);

	/*------------------------------------------------------------------------------
							[Blend State Description]
	------------------------------------------------------------------------------*/
	// Create a blending state with alpha blending enabled
	D3D11_BLEND_DESC blendStateDescription = GetBlendDesc(true);
	result = m_device->CreateBlendState(&blendStateDescription, &m_alphaBlendingStateEnabled);
	if (FAILED(result))
	LOG("Failed to create the blend state.", Log::Error);

	// Create a blending state with alpha blending disabled
	blendStateDescription = GetBlendDesc(false);
	result = m_device->CreateBlendState(&blendStateDescription, &m_alphaBlendingStateDisabled);
	if (FAILED(result))
	LOG("Failed to create the blend state.", Log::Error);

	/*------------------------------------------------------------------------------
									[Misc]
	------------------------------------------------------------------------------*/
	// Setup the viewport for rendering
	m_viewport.Width = (float)screenWidth;
	m_viewport.Height = (float)screenHeight;
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;

	// Create the viewport
	m_deviceContext->RSSetViewports(1, &m_viewport);

	//LOG(m_videoCardDescription + " " + Log::IntToString(m_videoCardMemory) + " MB", Log::Info);
}

void D3D11Device::Release()
{
	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.
	if (m_swapChain)
	{
		m_swapChain->SetFullscreenState(false, nullptr);
	}

	m_alphaBlendingStateEnabled->Release();
	m_alphaBlendingStateDisabled->Release();
	m_rasterStateCullFront->Release();
	m_rasterStateCullBack->Release();
	m_rasterStateCullNone->Release();
	m_depthStencilView->Release();
	m_depthStencilStateEnabled->Release();
	m_depthStencilStateDisabled->Release();
	m_depthStencilBuffer->Release();
	m_renderTargetView->Release();
	m_deviceContext->Release();
	m_device->Release();
	m_swapChain->Release();

	delete[] m_displayModeList;
	m_displayModeList = nullptr;
}

void D3D11Device::Begin()
{
	float clearColor[4];

	// Setup the color to clear the buffer to.
	clearColor[0] = 0.0f;
	clearColor[1] = 0.0f;
	clearColor[2] = 0.0f;
	clearColor[3] = 0.0f;

	// Clear the back buffer.
	m_deviceContext->ClearRenderTargetView(m_renderTargetView, clearColor);

	// Clear the depth buffer.
	m_deviceContext->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void D3D11Device::End()
{
	VSync vSync = Settings::GetInstance().GetVSync();
	m_swapChain->Present(vSync, 0);
}

ID3D11Device* D3D11Device::GetDevice()
{
	return m_device;
}

ID3D11DeviceContext* D3D11Device::GetDeviceContext()
{
	return m_deviceContext;
}

void D3D11Device::TurnZBufferOn()
{
	m_deviceContext->OMSetDepthStencilState(m_depthStencilStateEnabled, 1);
}

void D3D11Device::TurnZBufferOff()
{
	m_deviceContext->OMSetDepthStencilState(m_depthStencilStateDisabled, 1);
}

void D3D11Device::TurnOnAlphaBlending()
{
	float blendFactor[4];

	// Setup the blend factor.
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	// Turn on the alpha blending.
	m_deviceContext->OMSetBlendState(m_alphaBlendingStateEnabled, blendFactor, 0xffffffff);
}

void D3D11Device::TurnOffAlphaBlending()
{
	float blendFactor[4];

	// Setup the blend factor.
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	// Turn off the alpha blending.
	m_deviceContext->OMSetBlendState(m_alphaBlendingStateDisabled, blendFactor, 0xffffffff);
}

void D3D11Device::SetBackBufferRenderTarget()
{
	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);
}

void D3D11Device::ResetViewport()
{
	// Set the viewport.
	m_deviceContext->RSSetViewports(1, &m_viewport);
}

void D3D11Device::SetResolution(unsigned int width, unsigned int height)
{
	m_displayModeList->Width = width;
	m_displayModeList->Height = height;

	//// 1. Clear the existing references to the backbuffer
	//ID3D11RenderTargetView* nullViews[] = { nullptr };
	//m_deviceContext->OMSetRenderTargets(ARRAYSIZE(nullViews), nullViews, nullptr);
	//m_renderTargetView->Release(); 
	//m_depthStencilView->Release();
	//m_deviceContext->Flush();

	//// 2. Resize the existing swapchain
	//hr = m_swapChain->ResizeBuffers(2, backBufferWidth, backBufferHeight, backBufferFormat, 0);

	m_swapChain->ResizeTarget(m_displayModeList);
}

void D3D11Device::SetFaceCulling(D3D11_CULL_MODE cull)
{
	if (currentCullMode == cull)
		return;

	if (cull == D3D11_CULL_FRONT)
	{
		m_deviceContext->RSSetState(m_rasterStateCullFront);
		currentCullMode = cull;
		return;
	}

	if (cull == D3D11_CULL_BACK)
	{
		m_deviceContext->RSSetState(m_rasterStateCullBack);
		currentCullMode = cull;
		return;
	}

	if (cull == D3D11_CULL_NONE)
	{
		m_deviceContext->RSSetState(m_rasterStateCullNone);
		currentCullMode = cull;
	}
}

D3D11_RASTERIZER_DESC D3D11Device::GetRasterizerDesc(D3D11_CULL_MODE cullMode)
{
	// A rasterizer state determines determines
	// how and what polygons will be drawn.
	D3D11_RASTERIZER_DESC rasterDesc;

	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.CullMode = cullMode;
	rasterDesc.FrontCounterClockwise = false;
	rasterDesc.DepthBias = 0;
	rasterDesc.SlopeScaledDepthBias = 0.0f;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = true;
	rasterDesc.ScissorEnable = false;
	rasterDesc.MultisampleEnable = false;
	rasterDesc.AntialiasedLineEnable = false;

	return rasterDesc;
}

D3D11_DEPTH_STENCIL_DESC D3D11Device::GetDepthStencilDesc(bool depthEnable)
{
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;

	// Initialize the description of the stencil state.
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));

	// Depth test parameters
	depthStencilDesc.DepthEnable = depthEnable;
	depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

	// Stencil test parameters
	depthStencilDesc.StencilEnable = true;
	depthStencilDesc.StencilReadMask = 0xFF;
	depthStencilDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing
	depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing
	depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	return depthStencilDesc;
}

D3D11_BLEND_DESC D3D11Device::GetBlendDesc(bool blendEnable)
{
	D3D11_BLEND_DESC blendStateDescription;
	// Clear the blend state description.
	ZeroMemory(&blendStateDescription, sizeof(D3D11_BLEND_DESC));

	// Create an alpha enabled blend state description.
	blendStateDescription.RenderTarget[0].BlendEnable = (int)blendEnable;
	blendStateDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].RenderTargetWriteMask = 0x0f;

	return blendStateDescription;
}
