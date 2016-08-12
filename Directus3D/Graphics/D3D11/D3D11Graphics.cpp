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
#include "../../IO/Log.h"
#include "../../Core/Globals.h"
//==============================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

D3D11Graphics::D3D11Graphics()
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
	m_videoCardMemory = 0;
}

D3D11Graphics::~D3D11Graphics()
{

}

void D3D11Graphics::Initialize(HWND handle)
{
	//= DEFAULT RATERIZER STATE =============================================
	m_rasterizerDesc.FillMode = D3D11_FILL_SOLID;
	m_rasterizerDesc.CullMode = D3D11_CULL_BACK;
	m_rasterizerDesc.FrontCounterClockwise = false;
	m_rasterizerDesc.DepthBias = 0;
	m_rasterizerDesc.SlopeScaledDepthBias = 0.0f;
	m_rasterizerDesc.DepthBiasClamp = 0.0f;
	m_rasterizerDesc.DepthClipEnable = true;
	m_rasterizerDesc.ScissorEnable = false;
	m_rasterizerDesc.MultisampleEnable = false;
	m_rasterizerDesc.AntialiasedLineEnable = false;
	//=======================================================================

	//= DEFAULT DEPTH STENCIL STATE =========================================
	// Depth test parameters
	m_depthStencilDesc.DepthEnable = true;
	m_depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	m_depthStencilDesc.DepthFunc = D3D11_COMPARISON_LESS;

	// Stencil test parameters
	m_depthStencilDesc.StencilEnable = true;
	m_depthStencilDesc.StencilReadMask = 0xFF;
	m_depthStencilDesc.StencilWriteMask = 0xFF;

	// Stencil operations if pixel is front-facing
	m_depthStencilDesc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	m_depthStencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
	m_depthStencilDesc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	m_depthStencilDesc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

	// Stencil operations if pixel is back-facing
	m_depthStencilDesc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
	m_depthStencilDesc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
	m_depthStencilDesc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
	m_depthStencilDesc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;
	//=======================================================================

	//= DEFAULT BLEND STATE =================================================
	m_blendStateDesc.RenderTarget[0].BlendEnable = int(true);
	m_blendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	m_blendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	m_blendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	m_blendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	m_blendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	m_blendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	m_blendStateDesc.RenderTarget[0].RenderTargetWriteMask = 0x0f;
	//=======================================================================

	/*------------------------------------------------------------------------------
							[Graphics Interface Factory]
	------------------------------------------------------------------------------*/
	// Create a DirectX graphics interface factory.
	IDXGIFactory* factory;
	HRESULT hResult = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory));
	if (FAILED(hResult))
		LOG_ERROR("Failed to create a DirectX graphics interface factory.");

	/*------------------------------------------------------------------------------
										[Adapter]
	------------------------------------------------------------------------------*/
	// Use the factory to create an adapter for the primary graphics interface (video card).
	IDXGIAdapter* adapter;
	hResult = factory->EnumAdapters(0, &adapter);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create a primary graphics interface adapter.");

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
		LOG_ERROR("Failed to enumerate the primary adapter output.");

	// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
	hResult = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &numModes, nullptr);
	if (FAILED(hResult))
		LOG_ERROR("Failed to get adapter's display modes.");

	/*------------------------------------------------------------------------------
							[Display Mode Description]
	------------------------------------------------------------------------------*/
	// Create a list to hold all the possible display modes for this monitor/video card combination.
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

	/*------------------------------------------------------------------------------
							[Adapter Description]
	------------------------------------------------------------------------------*/
	DXGI_ADAPTER_DESC adapterDesc;
	// Get the adapter (video card) description.
	hResult = adapter->GetDesc(&adapterDesc);
	if (FAILED(hResult))
		LOG_ERROR("Failed to get the adapter's description.");

	// Release the adapter.
	adapter->Release();

	// Store the dedicated video card memory in megabytes.
	m_videoCardMemory = static_cast<int>(adapterDesc.DedicatedVideoMemory / 1024 / 1024);

	/*------------------------------------------------------------------------------
									[Feature Level]
	------------------------------------------------------------------------------*/
	// Set the feature level to DirectX 11.
	D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	DXGI_SWAP_CHAIN_DESC swapChainDesc = GetSwapchainDesc(handle);
	hResult = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, &featureLevel, 1, D3D11_SDK_VERSION, &swapChainDesc, &m_swapChain, &m_device, nullptr, &m_deviceContext);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the swap chain, Direct3D device, and Direct3D device context.");

	/*------------------------------------------------------------------------------
					[Create a render target view for the back buffer]
	------------------------------------------------------------------------------*/
	ID3D11Texture2D* backBufferPtr;
	// Get the pointer to the back buffer.
	hResult = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<LPVOID*>(&backBufferPtr));
	if (FAILED(hResult))
		LOG_ERROR("Failed to get the pointer to the back buffer.");

	// Create the render target view with the back buffer pointer.
	hResult = m_device->CreateRenderTargetView(backBufferPtr, nullptr, &m_renderTargetView);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the render target view.");

	// Release pointer to the back buffer
	backBufferPtr->Release();
	backBufferPtr = nullptr;

	// Depth Stencil Buffer
	CreateDepthStencilBuffer();

	// Depth-Stencil
	CreateDepthStencil();

	// Depth-Stencil View
	CreateDepthStencilView();

	/*------------------------------------------------------------------------------
							[Rasterizer Description]
	------------------------------------------------------------------------------*/
	// Create a rasterizer state with back face CullMode
	m_rasterizerDesc.CullMode = D3D11_CULL_BACK;
	hResult = m_device->CreateRasterizerState(&m_rasterizerDesc, &m_rasterStateCullBack);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the rasterizer state.");

	// Create a rasterizer state with front face CullMode
	m_rasterizerDesc.CullMode = D3D11_CULL_FRONT;
	hResult = m_device->CreateRasterizerState(&m_rasterizerDesc, &m_rasterStateCullFront);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the rasterizer state.");

	// Create a rasterizer state with no face CullMode
	m_rasterizerDesc.CullMode = D3D11_CULL_NONE;
	hResult = m_device->CreateRasterizerState(&m_rasterizerDesc, &m_rasterStateCullNone);
	if (FAILED(hResult))
		LOG_ERROR("Failed to create the rasterizer state.");

	// set the default rasterizer state
	m_deviceContext->RSSetState(m_rasterStateCullBack);

	/*------------------------------------------------------------------------------
							[Blend State Description]
	------------------------------------------------------------------------------*/
	// Create a blending state with alpha blending enabled
	m_blendStateDesc.RenderTarget[0].BlendEnable = int(true);
	HRESULT result = m_device->CreateBlendState(&m_blendStateDesc, &m_alphaBlendingStateEnabled);
	if (FAILED(result))
		LOG_ERROR("Failed to create the blend state.");

	// Create a blending state with alpha blending disabled
	m_blendStateDesc.RenderTarget[0].BlendEnable = int(false);
	result = m_device->CreateBlendState(&m_blendStateDesc, &m_alphaBlendingStateDisabled);
	if (FAILED(result))
		LOG_ERROR("Failed to create the blend state.");

	SetViewport(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);
}

bool D3D11Graphics::CreateDepthStencilBuffer()
{
	D3D11_TEXTURE2D_DESC depthBufferDesc;
	// Initialize the description of the depth buffer.
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
	// Create a depth stencil state with depth enabled
	m_depthStencilDesc.DepthEnable = true;
	HRESULT hResult = m_device->CreateDepthStencilState(&m_depthStencilDesc, &m_depthStencilStateEnabled);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create depth stencil state.");
		return false;
	}

	// Create a depth stencil state with depth disabled
	m_depthStencilDesc.DepthEnable = false;
	HRESULT result = m_device->CreateDepthStencilState(&m_depthStencilDesc, &m_depthStencilStateDisabled);
	if (FAILED(result))
	{
		LOG_ERROR("Failed to create depth stencil state.");
		return false;
	}

	// Set the default depth stencil state
	m_deviceContext->OMSetDepthStencilState(m_depthStencilStateEnabled, 1);

	return true;
}

bool D3D11Graphics::CreateDepthStencilView()
{
	D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
	// Initialize the depth stencil view.
	ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));

	// Set up the depth stencil view description.
	depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
	depthStencilViewDesc.Texture2D.MipSlice = 0;

	// Create the depth stencil view.
	HRESULT hResult = m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create the depth stencil view.");
		return false;
	}

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);

	return true;
}

void D3D11Graphics::Release()
{
	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.
	if (m_swapChain)
		m_swapChain->SetFullscreenState(false, nullptr);

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

void D3D11Graphics::Clear(const Vector4& color)
{
	float clearColor[4];

	// Setup the color to clear the buffer to.
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
	VSync vSync = VSYNC;
	m_swapChain->Present(vSync, 0);
}

ID3D11Device* D3D11Graphics::GetDevice()
{
	return m_device;
}

ID3D11DeviceContext* D3D11Graphics::GetDeviceContext()
{
	return m_deviceContext;
}

void D3D11Graphics::TurnZBufferOn()
{
	m_deviceContext->OMSetDepthStencilState(m_depthStencilStateEnabled, 1);
}

void D3D11Graphics::TurnZBufferOff()
{
	m_deviceContext->OMSetDepthStencilState(m_depthStencilStateDisabled, 1);
}

void D3D11Graphics::TurnOnAlphaBlending()
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

void D3D11Graphics::TurnOffAlphaBlending()
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
	HRESULT hResult = m_swapChain->ResizeBuffers(1, width, height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
	ID3D11Texture2D* backBuffer;
	hResult = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backBuffer));
	hResult = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
	SafeRelease(backBuffer);

	CreateDepthStencilBuffer();
	CreateDepthStencil();

	SetViewport(width, height);
}

void D3D11Graphics::SetViewport(int width, int height)
{
	// Setup the viewport
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

DXGI_SWAP_CHAIN_DESC D3D11Graphics::GetSwapchainDesc(HWND handle)
{
	DXGI_SWAP_CHAIN_DESC swapChainDesc;

	// Initialize the swap chain description.
	ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

	swapChainDesc.BufferCount = 2; // Set to a single back buffer.
	swapChainDesc.BufferDesc.Width = RESOLUTION_WIDTH; // Set the width of the back buffer.
	swapChainDesc.BufferDesc.Height = RESOLUTION_HEIGHT; // Set the height of the back buffer.
	swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // Set regular 32-bit surface for the back buffer.
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // Set the usage of the back buffer.	
	swapChainDesc.OutputWindow = handle; // Set the handle for the window to render to.
	swapChainDesc.SampleDesc.Count = 1;
	swapChainDesc.SampleDesc.Quality = 0;

	// Set to full screen or windowed mode.
	if (FULLSCREEN)
		swapChainDesc.Windowed = false;
	else
		swapChainDesc.Windowed = true;

	// Set the scan line ordering and scaling to unspecified.
	swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapChainDesc.Flags = 0;

	return swapChainDesc;
}