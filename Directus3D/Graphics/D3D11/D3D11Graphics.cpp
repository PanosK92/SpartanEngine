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
#include "D3D11Graphics.h"
#include <string>
#include "../../Core/Settings.h"
#include "../../Logging/Log.h"
#include "../../Core/Helper.h"
//==============================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

D3D11Graphics::D3D11Graphics()
{	
	m_device = nullptr;
	m_deviceContext = nullptr;
	m_swapChain = nullptr;
	m_driverType = D3D_DRIVER_TYPE_HARDWARE;
	m_featureLevel = D3D_FEATURE_LEVEL_11_0;
	m_renderTargetView = nullptr;
	m_depthStencilBuffer = nullptr;
	m_depthStencilStateEnabled = nullptr;
	m_depthStencilStateDisabled = nullptr;
	m_depthStencilView = nullptr;
	m_rasterStateCullFront = nullptr;
	m_rasterStateCullBack = nullptr;
	m_rasterStateCullNone = nullptr;
	m_blendStateAlphaEnabled = nullptr;
	m_blendStateAlphaDisabled = nullptr;
	m_displayModeList = nullptr;
	m_videoCardMemory = 0;
}

D3D11Graphics::~D3D11Graphics()
{

}

void D3D11Graphics::Initialize(HWND handle)
{
	//= GRAPHICS INTERFACE FACTORY =================================================
	IDXGIFactory* factory;
	HRESULT hResult = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&factory));
	if (FAILED(hResult))
		LOG_ERROR("Failed to create a DirectX graphics interface factory.");
	//==============================================================================

	//= ADAPTER ====================================================================
	IDXGIAdapter* adapter;
	hResult = factory->EnumAdapters(0, &adapter);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create a primary graphics interface adapter.");

	factory->Release();
	//==============================================================================

	//= ADAPTER OUTPUT =============================================================
	IDXGIOutput* adapterOutput;
	unsigned int numModes;

	// Enumerate the primary adapter output (monitor).
	hResult = adapter->EnumOutputs(0, &adapterOutput);
	if (FAILED(hResult))
		LOG_ERROR("Failed to enumerate the primary adapter output.");

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	hResult = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);
	if (FAILED(hResult))
		LOG_ERROR("Failed to get adapter's display modes.");
	//==============================================================================

	//= DISPLAY MODE DESCRIPTION ===================================================
	DXGI_MODE_DESC* m_displayModeList = new DXGI_MODE_DESC[numModes];
	if (!m_displayModeList)
		LOG_ERROR("Failed to create a display mode list.");

	// Now fill the display mode list structures.
	hResult = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, m_displayModeList);
	if (FAILED(hResult))
		LOG_ERROR("Failed to fill the display mode list structures.");

	// Release the adapter output.
	adapterOutput->Release();

	// Go through all the display modes and find the one that matches the screen width and height.
	unsigned int numerator = 0, denominator = 1;
	for (auto i = 0; i < numModes; i++)
	{
		if (m_displayModeList[i].Width == (unsigned int)RESOLUTION_WIDTH && m_displayModeList[i].Height == (unsigned int)RESOLUTION_HEIGHT)
		{
			numerator = m_displayModeList[i].RefreshRate.Numerator;
			denominator = m_displayModeList[i].RefreshRate.Denominator;
		}
	}
	//==============================================================================

	//= ADAPTER DESCRIPTION ========================================================
	DXGI_ADAPTER_DESC adapterDesc;
	// Get the adapter (video card) description.
	hResult = adapter->GetDesc(&adapterDesc);
	if (FAILED(hResult))
		LOG_ERROR("Failed to get the adapter's description.");

	// Release the adapter.
	adapter->Release();

	// Store the dedicated video card memory in megabytes.
	m_videoCardMemory = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024);
	//==============================================================================
	
	//= SWAP CHAIN =================================================================
	DXGI_SWAP_CHAIN_DESC swapChainDesc;
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

	swapChainDesc.BufferCount = 2;
	swapChainDesc.BufferDesc.Width = RESOLUTION_WIDTH;
	swapChainDesc.BufferDesc.Height = RESOLUTION_HEIGHT;
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.OutputWindow = handle;
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	// Set to full screen or windowed mode.
	swapChainDesc.Windowed = (BOOL)!FULLSCREEN;

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // alt + enter fullscreen

	// Create the swap chain, Direct3D device, and Direct3D device context.
	hResult = D3D11CreateDeviceAndSwapChain(nullptr, m_driverType, nullptr, 0, &m_featureLevel, 1, D3D11_SDK_VERSION, &swapChainDesc, &m_swapChain, &m_device, nullptr, &m_deviceContext);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the swap chain, Direct3D device, and Direct3D device context.");
	//==============================================================================

	//= RENDER TARGET VIEW =========================================================
	ID3D11Texture2D* backBufferPtr;
	// Get the pointer to the back buffer.
	hResult = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)(&backBufferPtr));
	if (FAILED(hResult))
		LOG_ERROR("Failed to get the pointer to the back buffer.");

	// Create the render target view with the back buffer pointer.
	hResult = m_device->CreateRenderTargetView(backBufferPtr, nullptr, &m_renderTargetView);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the render target view.");

	// Release pointer to the back buffer
	backBufferPtr->Release();
	backBufferPtr = nullptr;
	//==============================================================================

	// Depth Stencil Buffer
	CreateDepthStencilBuffer();

	// Depth-Stencil
	CreateDepthStencil();

	// DEPTH-STENCIL VIEW ==========================================================
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	hResult = m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the depth stencil view.");

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);
	//==============================================================================

	//= RASTERIZER =================================================================
	D3D11_RASTERIZER_DESC rasterizerDesc;
	rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	rasterizerDesc.FrontCounterClockwise = false;
	rasterizerDesc.DepthBias = 0;
	rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	rasterizerDesc.DepthBiasClamp = 0.0f;
	rasterizerDesc.DepthClipEnable = true;
	rasterizerDesc.ScissorEnable = false;
	rasterizerDesc.MultisampleEnable = false;
	rasterizerDesc.AntialiasedLineEnable = false;

	// Create a rasterizer state with back face CullMode
	rasterizerDesc.CullMode = D3D11_CULL_BACK;
	hResult = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterStateCullBack);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the rasterizer cull back state.");

	// Create a rasterizer state with front face CullMode
	rasterizerDesc.CullMode = D3D11_CULL_FRONT;
	hResult = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterStateCullFront);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the rasterizer cull front state.");

	// Create a rasterizer state with no face CullMode
	rasterizerDesc.CullMode = D3D11_CULL_NONE;
	hResult = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterStateCullNone);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create rasterizer cull none state.");

	// set the default rasterizer state
	m_deviceContext->RSSetState(m_rasterStateCullBack);
	//==============================================================================
	
	//= BLEND STATE ================================================================
	D3D11_BLEND_DESC blendStateDesc;
	ZeroMemory(&blendStateDesc, sizeof(blendStateDesc));
	blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)true;
	blendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDesc.RenderTarget[0].RenderTargetWriteMask = 0x0f;

	// Create a blending state with alpha blending enabled
	blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)true;
	HRESULT result = m_device->CreateBlendState(&blendStateDesc, &m_blendStateAlphaEnabled);
	if (FAILED(result))
		LOG_ERROR("Failed to create blend state.");

	// Create a blending state with alpha blending disabled
	blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)false;
	result = m_device->CreateBlendState(&blendStateDesc, &m_blendStateAlphaDisabled);
	if (FAILED(result))
		LOG_ERROR("Failed to create blend state.");
	//==============================================================================

	SetViewport(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);
}

bool D3D11Graphics::CreateDepthStencilBuffer()
{
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));

	// Set up the description of the depth buffer.
	depthBufferDesc.Width = RESOLUTION_WIDTH;
	depthBufferDesc.Height = RESOLUTION_HEIGHT;
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
	HRESULT hResult = m_device->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create the texture for the depth buffer.");
		return false;
	}

	return true;
}

bool D3D11Graphics::CreateDepthStencil()
{
	D3D11_DEPTH_STENCIL_DESC depthStencilDesc;
	ZeroMemory(&depthStencilDesc, sizeof(depthStencilDesc));

	// Depth test parameters
	depthStencilDesc.DepthEnable = true;
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

	// Create a depth stencil state with depth enabled
	depthStencilDesc.DepthEnable = true;
	HRESULT hResult = m_device->CreateDepthStencilState(&depthStencilDesc, &m_depthStencilStateEnabled);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create depth stencil enabled state.");
		return false;
	}

	// Create a depth stencil state with depth disabled
	depthStencilDesc.DepthEnable = false;
	HRESULT result = m_device->CreateDepthStencilState(&depthStencilDesc, &m_depthStencilStateDisabled);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create depth stencil disabled state.");
		return false;
	}

	// Set the default depth stencil state
	m_deviceContext->OMSetDepthStencilState(m_depthStencilStateEnabled, 1);

	return true;
}

void D3D11Graphics::Release()
{
	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.
	if (m_swapChain)
		m_swapChain->SetFullscreenState(false, nullptr);

	m_blendStateAlphaEnabled->Release();
	m_blendStateAlphaDisabled->Release();
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

void D3D11Graphics::Clear(const Vector4& color)
{
	float clearColor[4];
	clearColor[0] = color.x;
	clearColor[1] = color.y;
	clearColor[2] = color.z;
	clearColor[3] = color.w;

	// Clear the back buffer.
	m_deviceContext->ClearRenderTargetView(m_renderTargetView, clearColor);

	// Clear the depth buffer.
	m_deviceContext->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
}

void D3D11Graphics::Present()
{
	m_swapChain->Present(VSYNC, 0);
}

ID3D11Device* D3D11Graphics::GetDevice()
{
	return m_device;
}

ID3D11DeviceContext* D3D11Graphics::GetDeviceContext()
{
	return m_deviceContext;
}

void D3D11Graphics::EnableZBuffer(bool enable)
{
	if (enable)
		m_deviceContext->OMSetDepthStencilState(m_depthStencilStateEnabled, 1);
	else
		m_deviceContext->OMSetDepthStencilState(m_depthStencilStateDisabled, 1);
}

void D3D11Graphics::EnabledAlphaBlending(bool enable)
{
	// Blend factor.
	float blendFactor[4];
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	if (enable)
		m_deviceContext->OMSetBlendState(m_blendStateAlphaEnabled, blendFactor, 0xffffffff);
	else
		m_deviceContext->OMSetBlendState(m_blendStateAlphaDisabled, blendFactor, 0xffffffff);
}

void D3D11Graphics::SetBackBufferRenderTarget()
{
	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);
}

void D3D11Graphics::SetResolution(int width, int height)
{
	//Release old views and the old depth/stencil buffer.
	SafeRelease(m_renderTargetView);
	SafeRelease(m_depthStencilView);
	SafeRelease(m_depthStencilBuffer);

	//Resize the swap chain and recreate the render target views. 
	HRESULT result = m_swapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	if (FAILED(result))
		LOG_ERROR("Failed to resize swap chain buffers.");

	ID3D11Texture2D* backBuffer;
	result = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)(&backBuffer));
	if (FAILED(result))
		LOG_ERROR("Failed to get pointer to the swap chain's back buffer.");

	result = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
	if (FAILED(result))
		LOG_ERROR("Failed to create render target view.");

	SafeRelease(backBuffer);

	CreateDepthStencilBuffer();
	CreateDepthStencil();

	SetViewport(width, height);
}

void D3D11Graphics::SetViewport(int width, int height)
{
	m_viewport.Width = float(width);
	m_viewport.Height = float(height);
	m_viewport.MinDepth = 0.0f;
	m_viewport.MaxDepth = 1.0f;
	m_viewport.TopLeftX = 0.0f;
	m_viewport.TopLeftY = 0.0f;

	m_deviceContext->RSSetViewports(1, &m_viewport);
}

void D3D11Graphics::ResetViewport()
{
	m_deviceContext->RSSetViewports(1, &m_viewport);
}

void D3D11Graphics::SetFaceCullMode(D3D11_CULL_MODE cull)
{
	if (cull == D3D11_CULL_FRONT)
		m_deviceContext->RSSetState(m_rasterStateCullFront);
	else if (cull == D3D11_CULL_BACK)
		m_deviceContext->RSSetState(m_rasterStateCullBack);
	else if (cull == D3D11_CULL_NONE)
		m_deviceContext->RSSetState(m_rasterStateCullNone);
}