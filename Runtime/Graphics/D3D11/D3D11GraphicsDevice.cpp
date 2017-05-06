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

//= INCLUDES ===========================
#include "D3D11GraphicsDevice.h"
#include "../../Logging/Log.h"
#include "../../Core/Helper.h"
#include "../../Core/Settings.h"
#include "../../FileSystem/FileSystem.h"
//======================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

namespace Directus
{
	//= ENUMERATIONS ===============================================
	// This helps convert engine enumerations to d3d11 specific
	// enumerations, however the order of the d3d11 enumerations
	// must match the order of the engine's enumerations.
	static const D3D11_CULL_MODE d3dCullMode[] =
	{
		D3D11_CULL_NONE,
		D3D11_CULL_BACK,
		D3D11_CULL_FRONT
	};

	static const D3D11_PRIMITIVE_TOPOLOGY d3dPrimitiveTopology[] =
	{
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		D3D11_PRIMITIVE_TOPOLOGY_LINELIST
	};
	//===============================================================

	D3D11GraphicsDevice::D3D11GraphicsDevice(Context* context) : IGraphicsDevice(context)
	{
		m_inputLayout = PositionTextureNormalTangent;
		m_cullMode = CullBack;
		m_primitiveTopology = TriangleList;
		m_zBufferEnabled = true;
		m_alphaBlendingEnabled = false;

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
	}

	D3D11GraphicsDevice::~D3D11GraphicsDevice()
	{
		// Before shutting down set to windowed mode or 
		// upon releasing the swap chain it will throw an exception.
		if (m_swapChain) {
			m_swapChain->SetFullscreenState(false, nullptr);
		}

		SafeRelease(m_blendStateAlphaEnabled);
		SafeRelease(m_blendStateAlphaDisabled);
		SafeRelease(m_rasterStateCullFront);
		SafeRelease(m_rasterStateCullBack);
		SafeRelease(m_rasterStateCullNone);
		SafeRelease(m_depthStencilView);
		SafeRelease(m_depthStencilStateEnabled);
		SafeRelease(m_depthStencilStateDisabled);
		SafeRelease(m_depthStencilBuffer);
		SafeRelease(m_renderTargetView);
		SafeRelease(m_deviceContext);
		SafeRelease(m_device);
		SafeRelease(m_swapChain);

		if (m_displayModeList)
		{
			delete[] m_displayModeList;
			m_displayModeList = nullptr;
		}
	}

	bool D3D11GraphicsDevice::Initialize()
	{
		// assume all will go well
		m_initializedSuccessfully = true;

		if (!IsWindow(m_drawHandle))
		{
			LOG_ERROR("Aborting D3D11 initialization. Invalid draw handle.");
			m_initializedSuccessfully = false;
			return false;
		}

		//= GRAPHICS INTERFACE FACTORY =================================================
		IDXGIFactory* factory;
		HRESULT result = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&factory));
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create a DirectX graphics interface factory.");
			m_initializedSuccessfully = false;
			return false;
		}
		//==============================================================================

		//= ADAPTER ====================================================================
		IDXGIAdapter* adapter;
		result = factory->EnumAdapters(0, &adapter);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create a primary graphics interface adapter.");
			m_initializedSuccessfully = false;
			return false;
		}
		factory->Release();
		//==============================================================================

		//= ADAPTER OUTPUT / DISPLAY MODE ==============================================
		IDXGIOutput* adapterOutput;

		// Enumerate the primary adapter output (monitor).
		result = adapter->EnumOutputs(0, &adapterOutput);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to enumerate the primary adapter output.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
		result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &m_displayModeCount, nullptr);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to get adapter's display modes.");
			m_initializedSuccessfully = false;
			return false;
		}

		// Create display mode list
		m_displayModeList = new DXGI_MODE_DESC[m_displayModeCount];
		if (!m_displayModeList)
		{
			LOG_ERROR("Failed to create a display mode list.");
			m_initializedSuccessfully = false;
			return false;
		}

		// Now fill the display mode list structures.
		result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &m_displayModeCount, m_displayModeList);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to fill the display mode list structures.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Release the adapter output.
		adapterOutput->Release();

		// Go through all the display modes and find the one that matches the screen width and height.
		for (unsigned int i = 0; i < m_displayModeCount; i++)
			if (m_displayModeList[i].Width == (UINT)RESOLUTION_WIDTH && m_displayModeList[i].Height == (UINT)RESOLUTION_HEIGHT)
			{
				m_refreshRateNumerator = (UINT)m_displayModeList[i].RefreshRate.Numerator;
				m_refreshRateDenominator = (UINT)m_displayModeList[i].RefreshRate.Denominator;
				break;
			}

		//==============================================================================

		//= ADAPTER DESCRIPTION ========================================================
		DXGI_ADAPTER_DESC adapterDesc;
		// Get the adapter (video card) description.
		result = adapter->GetDesc(&adapterDesc);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to get the adapter's description.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Release the adapter.
		adapter->Release();

		// Store the dedicated video card memory in megabytes.
		m_videoCardMemory = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024);
		//==============================================================================

		//= SWAP CHAIN =================================================================
		DXGI_SWAP_CHAIN_DESC swapChainDesc;
		ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));
		swapChainDesc.BufferCount = 1;
		swapChainDesc.BufferDesc.Width = RESOLUTION_WIDTH;
		swapChainDesc.BufferDesc.Height = RESOLUTION_HEIGHT;
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.OutputWindow = m_drawHandle;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.Windowed = (BOOL)!FULLSCREEN_ENABLED;
		swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // alt + enter fullscreen

																	  // Create the swap chain, Direct3D device, and Direct3D device context.
		result = D3D11CreateDeviceAndSwapChain(nullptr, m_driverType, nullptr, 0,
			&m_featureLevel, 1, D3D11_SDK_VERSION, &swapChainDesc,
			&m_swapChain, &m_device, nullptr, &m_deviceContext);

		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the D3D11 swap chain, device, and device context.");
			m_initializedSuccessfully = false;
			return false;
		}
		//==============================================================================

		//= RENDER TARGET VIEW =========================================================
		ID3D11Texture2D* backBufferPtr;
		// Get the pointer to the back buffer.
		result = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)(&backBufferPtr));
		if (FAILED(result))
		{
			LOG_ERROR("Failed to get the pointer to the back buffer.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Create the render target view with the back buffer pointer.
		result = m_device->CreateRenderTargetView(backBufferPtr, nullptr, &m_renderTargetView);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the render target view.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Release pointer to the back buffer
		backBufferPtr->Release();
		backBufferPtr = nullptr;
		//==============================================================================

		SetViewport(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		//= DEPTH =================
		CreateDepthStencil();
		CreateDepthStencilBuffer();
		CreateDepthStencilView();
		//=========================

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
		result = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterStateCullBack);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the rasterizer cull back state.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Create a rasterizer state with front face CullMode
		rasterizerDesc.CullMode = D3D11_CULL_FRONT;
		result = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterStateCullFront);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the rasterizer cull front state.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Create a rasterizer state with no face CullMode
		rasterizerDesc.CullMode = D3D11_CULL_NONE;
		result = m_device->CreateRasterizerState(&rasterizerDesc, &m_rasterStateCullNone);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create rasterizer cull none state.");
			m_initializedSuccessfully = false;
			return false;
		}
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
		result = m_device->CreateBlendState(&blendStateDesc, &m_blendStateAlphaEnabled);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create blend state.");
			m_initializedSuccessfully = false;
			return false;
		}
		// Create a blending state with alpha blending disabled
		blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)false;
		result = m_device->CreateBlendState(&blendStateDesc, &m_blendStateAlphaDisabled);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create blend state.");
			m_initializedSuccessfully = false;
			return false;
		}
		//==============================================================================

		return m_initializedSuccessfully;
	}

	void D3D11GraphicsDevice::SetHandle(HWND drawHandle)
	{
		m_drawHandle = drawHandle;
	}

	//= DEPTH ======================================================================================================
	void D3D11GraphicsDevice::EnableZBuffer(bool enable)
	{
		if (!m_deviceContext || m_zBufferEnabled == enable) {
			return;
		}

		// Set depth stencil state
		m_deviceContext->OMSetDepthStencilState(enable ? m_depthStencilStateEnabled : m_depthStencilStateDisabled, 1);

		m_zBufferEnabled = enable;
	}

	bool D3D11GraphicsDevice::CreateDepthStencil()
	{
		if (!m_device) {
			return false;
		}

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
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		HRESULT result = m_device->CreateDepthStencilState(&depthStencilDesc, &m_depthStencilStateEnabled);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create depth stencil enabled state.");
			m_initializedSuccessfully = false;
			return false;
		}

		// Create a depth stencil state with depth disabled
		depthStencilDesc.DepthEnable = false;
		depthStencilDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
		result = m_device->CreateDepthStencilState(&depthStencilDesc, &m_depthStencilStateDisabled);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create depth stencil disabled state.");
			m_initializedSuccessfully = false;
			return false;
		}

		// Set the default depth stencil state
		m_deviceContext->OMSetDepthStencilState(m_depthStencilStateEnabled, 1);

		return true;
	}

	bool D3D11GraphicsDevice::CreateDepthStencilBuffer()
	{
		if (!m_device) {
			return false;
		}

		D3D11_TEXTURE2D_DESC depthBufferDesc;
		ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));
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
		HRESULT result = m_device->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the texture for the depth buffer.");
			m_initializedSuccessfully = false;
			return false;
		}

		return true;
	}

	bool D3D11GraphicsDevice::CreateDepthStencilView()
	{
		if (!m_device) {
			return false;
		}

		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
		ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
		depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		// Create the depth stencil view.
		HRESULT result = m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create the depth stencil view.");
			m_initializedSuccessfully = false;
			return false;
		}

		// Bind the render target view and depth stencil buffer to the output render pipeline.
		m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);

		return true;
	}
	//========================================================================================================================

	void D3D11GraphicsDevice::Clear(const Vector4& color)
	{
		if (!m_deviceContext) {
			return;
		}

		// Clear the back buffer.
		float clearColor[4] = { color.x, color.y, color.z, color.w };
		m_deviceContext->ClearRenderTargetView(m_renderTargetView, clearColor);

		// Clear the depth buffer.
		m_deviceContext->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);
	}

	void D3D11GraphicsDevice::Present()
	{
		if (!m_swapChain) {
			return;
		}

		m_swapChain->Present(VSYNC, 0);
	}

	void D3D11GraphicsDevice::EnableAlphaBlending(bool enable)
	{
		if (!m_deviceContext || m_alphaBlendingEnabled == enable)
			return;

		// Set blend state
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_deviceContext->OMSetBlendState(enable ? m_blendStateAlphaEnabled : m_blendStateAlphaDisabled, blendFactor, 0xffffffff);

		m_alphaBlendingEnabled = enable;
	}

	bool D3D11GraphicsDevice::SetResolution(int width, int height)
	{
		if (!m_swapChain) {
			return false;
		}

		//= RELEASE RESLUTION BASED STUFF =======
		SafeRelease(m_renderTargetView);
		SafeRelease(m_depthStencilBuffer);
		SafeRelease(m_depthStencilView);
		//=======================================

		//= RESIZE TARGET ==================================================
		DXGI_MODE_DESC dxgiModeDesc;
		ZeroMemory(&dxgiModeDesc, sizeof(dxgiModeDesc));
		dxgiModeDesc.Width = (UINT)width;
		dxgiModeDesc.Height = (UINT)height;
		dxgiModeDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		dxgiModeDesc.RefreshRate = DXGI_RATIONAL{ m_refreshRateNumerator, m_refreshRateDenominator };
		dxgiModeDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		dxgiModeDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

		HRESULT result = m_swapChain->ResizeTarget(&dxgiModeDesc);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to resize swapchain target.");
			return false;
		}
		//==================================================================

		//= RESIZE BUFFERS =================================================
		// Resize the swap chain and recreate the render target views. 
		result = m_swapChain->ResizeBuffers(1, (UINT)width, (UINT)height, DXGI_FORMAT_R8G8B8A8_UNORM, 0);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to resize swapchain buffers.");
			return false;
		}
		//==================================================================

		//= RENDER TARGET VIEW =============================================
		ID3D11Texture2D* backBuffer = nullptr;
		result = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)(&backBuffer));
		if (FAILED(result))
		{
			LOG_ERROR("Failed to get pointer to the swapchain's back buffer.");
			return false;
		}

		result = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
		SafeRelease(backBuffer);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create render target view.");
			return false;
		}
		//====================================================================

		//= RECREATE RESOLUTION BASED STUFF =
		CreateDepthStencilBuffer();
		CreateDepthStencilView();
		//===================================

		return true;
	}

	//= VIEWPORT =====================================================
	void D3D11GraphicsDevice::SetViewport(float width, float height)
	{
		if (!m_deviceContext) {
			return;
		}

		m_viewport.Width = width;
		m_viewport.Height = height;
		m_viewport.MinDepth = 0.0f;
		m_viewport.MaxDepth = 1.0f;
		m_viewport.TopLeftX = 0.0f;
		m_viewport.TopLeftY = 0.0f;

		m_deviceContext->RSSetViewports(1, &m_viewport);
	}

	void D3D11GraphicsDevice::ResetViewport()
	{
		if (!m_deviceContext) {
			return;
		}

		m_deviceContext->RSSetViewports(1, &m_viewport);
	}
	//================================================================

	void D3D11GraphicsDevice::SetCullMode(CullMode cullMode)
	{
		// Set face CullMode only if not already set
		if (!m_deviceContext || m_cullMode == cullMode)
			return;

		auto mode = d3dCullMode[cullMode];

		if (mode == D3D11_CULL_FRONT)
			m_deviceContext->RSSetState(m_rasterStateCullFront);
		else if (mode == D3D11_CULL_BACK)
			m_deviceContext->RSSetState(m_rasterStateCullBack);
		else if (mode == D3D11_CULL_NONE)
			m_deviceContext->RSSetState(m_rasterStateCullNone);

		// Save the current CullMode mode
		m_cullMode = cullMode;
	}

	void D3D11GraphicsDevice::SetBackBufferAsRenderTarget()
	{
		if (!m_deviceContext) {
			return;
		}

		m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthStencilView);
	}

	void D3D11GraphicsDevice::SetPrimitiveTopology(PrimitiveTopology primitiveTopology)
	{
		// Set PrimitiveTopology only if not already set
		if (!m_deviceContext || m_primitiveTopology == primitiveTopology)
			return;

		// Ser primitive topology
		m_deviceContext->IASetPrimitiveTopology(d3dPrimitiveTopology[primitiveTopology]);

		// Save the current PrimitiveTopology mode
		m_primitiveTopology = primitiveTopology;
	}

	void D3D11GraphicsDevice::SetInputLayout(InputLayout inputLayout)
	{
		if (m_inputLayout == inputLayout)
			return;

		m_inputLayout = inputLayout;
	}
}