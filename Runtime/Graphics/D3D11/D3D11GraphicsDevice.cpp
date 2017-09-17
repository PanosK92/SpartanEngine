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
using namespace std;
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
		D3D11_CULL_FRONT,
		D3D11_CULL_BACK
	};

	static const D3D11_PRIMITIVE_TOPOLOGY d3dPrimitiveTopology[] =
	{
		D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		D3D11_PRIMITIVE_TOPOLOGY_LINELIST
	};
	//===============================================================

	D3D11GraphicsDevice::D3D11GraphicsDevice(Context* context) : IGraphicsDevice(context)
	{
		m_driverType = D3D_DRIVER_TYPE_HARDWARE;
		m_featureLevel = D3D_FEATURE_LEVEL_11_0;
		m_sdkVersion = D3D11_SDK_VERSION;

		m_inputLayout = PositionTextureTBN;
		m_cullMode = CullBack;
		m_primitiveTopology = TriangleList;
		m_depthEnabled = true;
		m_alphaBlendingEnabled = false;
		m_device = nullptr;
		m_deviceContext = nullptr;
		m_swapChain = nullptr;
		m_renderTargetView = nullptr;
		m_displayModeList = nullptr;
		m_displayModeCount = 0;
		m_refreshRateNumerator = 0;
		m_refreshRateDenominator = 0;
		m_depthStencilBuffer = nullptr;
		m_depthStencilStateEnabled = nullptr;
		m_depthStencilStateDisabled = nullptr;
		m_depthStencilView = nullptr;
		m_rasterStateCullFront = nullptr;
		m_rasterStateCullBack = nullptr;
		m_rasterStateCullNone = nullptr;
		m_blendStateAlphaEnabled = nullptr;
		m_blendStateAlphaDisabled = nullptr;
		m_drawHandle = nullptr;
		m_initialized = false;
		m_maxDepth = 1.0f;
	}

	D3D11GraphicsDevice::~D3D11GraphicsDevice()
	{
		// Before shutting down set to windowed mode or 
		// upon releasing the swap chain it will throw an exception.
		if (m_swapChain) 
		{
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
		if (!IsWindow(m_drawHandle))
		{
			LOG_ERROR("Aborting D3D11 initialization. Invalid draw handle.");
			return false;
		}

		//= GRAPHICS INTERFACE FACTORY =================================================
		IDXGIFactory* factory;
		HRESULT result = CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)(&factory));
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create a DirectX graphics interface factory.");
			return false;
		}
		//==============================================================================

		//= ADAPTER ====================================================================
		IDXGIAdapter* adapter = GetAdapterWithTheHighestVRAM(factory); // usually the dedicaded one
		factory->Release();
		if (!adapter)
		{
			LOG_ERROR("Couldn't find any adapters.");
			return false;
		}
		LOG_INFO("Primary adapter: " + GetAdapterDescription(adapter));
		//==============================================================================

		//= ADAPTER OUTPUT / DISPLAY MODE ==============================================
		{
			// Enumerate the primary adapter output (monitor).
			IDXGIOutput* adapterOutput;
			result = adapter->EnumOutputs(0, &adapterOutput);
			if (FAILED(result))
			{
				LOG_ERROR("Failed to enumerate the primary adapter output.");
				return false;
			}

			// Get the number of modes that fit the DXGI_FORMAT_R8G8B8A8_UNORM display format for the adapter output (monitor).
			result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &m_displayModeCount, nullptr);
			if (FAILED(result))
			{
				LOG_ERROR("Failed to get adapter's display modes.");
				return false;
			}

			// Create display mode list
			m_displayModeList = new DXGI_MODE_DESC[m_displayModeCount];
			if (!m_displayModeList)
			{
				LOG_ERROR("Failed to create a display mode list.");
				return false;
			}

			// Now fill the display mode list structures.
			result = adapterOutput->GetDisplayModeList(DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_ENUM_MODES_INTERLACED, &m_displayModeCount, m_displayModeList);
			if (FAILED(result))
			{
				LOG_ERROR("Failed to fill the display mode list structures.");
				return false;
			}
			// Release the adapter output.
			adapterOutput->Release();

			// Go through all the display modes and find the one that matches the screen width and height.
			for (unsigned int i = 0; i < m_displayModeCount; i++)
			{
				if (m_displayModeList[i].Width == (UINT)RESOLUTION_WIDTH && m_displayModeList[i].Height == (UINT)RESOLUTION_HEIGHT)
				{
					m_refreshRateNumerator = (UINT)m_displayModeList[i].RefreshRate.Numerator;
					m_refreshRateDenominator = (UINT)m_displayModeList[i].RefreshRate.Denominator;
					break;
				}
			}
		}
		//==============================================================================

		// Create swap chain
		if (!CreateDeviceAndSwapChain(&m_device, &m_deviceContext, &m_swapChain))
		{
			return false;
		}

		//= RENDER TARGET VIEW =========================================================
		{
			ID3D11Texture2D* backBufferPtr;
			// Get the pointer to the back buffer.
			result = m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)(&backBufferPtr));
			if (FAILED(result))
			{
				LOG_ERROR("Failed to get the pointer to the back buffer.");
				return false;
			}

			// Create the render target view with the back buffer pointer.
			result = m_device->CreateRenderTargetView(backBufferPtr, nullptr, &m_renderTargetView);
			if (FAILED(result))
			{
				LOG_ERROR("Failed to create the render target view.");
				return false;
			}

			// Release pointer to the back buffer
			backBufferPtr->Release();
			backBufferPtr = nullptr;
		}
		//==============================================================================

		SetViewport((float)RESOLUTION_WIDTH, (float)RESOLUTION_HEIGHT);

		//= DEPTH ============================================
		if (!CreateDepthStencilState(m_depthStencilStateEnabled, true, true))
		{
			LOG_ERROR("Failed to create depth stencil enabled state.");
			return false;
		}

		if (!CreateDepthStencilState(m_depthStencilStateDisabled, false, false))
		{
			LOG_ERROR("Failed to create depth stencil disabled state.");
			return false;
		}

		if (!CreateDepthStencilBuffer())
		{
			LOG_ERROR("Failed to create depth stencil buffer.");
			return false;
		}

		if (!CreateDepthStencilView())
		{
			LOG_ERROR("Failed to create the rasterizer state.");
			return false;
		}
		//====================================================

		//= RASTERIZERS ========================================================================
		{
			if (!CreateRasterizerState(D3D11_CULL_BACK, D3D11_FILL_SOLID, &m_rasterStateCullBack))
			{
				LOG_ERROR("Failed to create the rasterizer state.");
				return false;
			}

			if (!CreateRasterizerState(D3D11_CULL_FRONT, D3D11_FILL_SOLID, &m_rasterStateCullFront))
			{
				LOG_ERROR("Failed to create the rasterizer state.");
				return false;
			}

			if (!CreateRasterizerState(D3D11_CULL_NONE, D3D11_FILL_SOLID, &m_rasterStateCullNone))
			{
				LOG_ERROR("Failed to create the rasterizer state.");
				return false;
			}

			// Set default rasterizer state
			m_deviceContext->RSSetState(m_rasterStateCullBack);
		}
		//=======================================================================================

		//= BLEND STATE ================================================================
		D3D11_BLEND_DESC blendStateDesc;
		{
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
				return false;
			}
			// Create a blending state with alpha blending disabled
			blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)false;
			result = m_device->CreateBlendState(&blendStateDesc, &m_blendStateAlphaDisabled);
			if (FAILED(result))
			{
				LOG_ERROR("Failed to create blend state.");
				return false;
			}
		}
		//==============================================================================

		m_initialized = true;
		return true;
	}

	void D3D11GraphicsDevice::SetHandle(void* drawHandle)
	{
		m_drawHandle = (HWND)drawHandle;
	}

	//= DEPTH ================================================================================================================
	void D3D11GraphicsDevice::EnableDepth(bool enable)
	{
		if (!m_deviceContext || m_depthEnabled == enable)
			return;

		m_depthEnabled = enable;

		// Set depth stencil state
		m_deviceContext->OMSetDepthStencilState(m_depthEnabled ? m_depthStencilStateEnabled : m_depthStencilStateDisabled, 1);
	}

	bool D3D11GraphicsDevice::CreateDepthStencilState(void* depthStencilState, bool depthEnabled, bool writeEnabled)
	{
		if (!m_device)
			return false;

		D3D11_DEPTH_STENCIL_DESC desc;
		ZeroMemory(&desc, sizeof(desc));

		// Depth test parameters
		desc.DepthEnable = depthEnabled ? TRUE : FALSE;
		desc.DepthWriteMask = writeEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
		desc.DepthFunc = D3D11_COMPARISON_LESS;

		// Stencil test parameters
		desc.StencilEnable = true;
		desc.StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK;
		desc.StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK;

		// Stencil operations if pixel is front-facing
		desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
		desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		// Stencil operations if pixel is back-facing
		desc.BackFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
		desc.BackFace.StencilDepthFailOp = D3D11_STENCIL_OP_DECR;
		desc.BackFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
		desc.BackFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

		// Create a depth stencil state with depth enabled
		ID3D11DepthStencilState* depthStencilStateTyped = (ID3D11DepthStencilState*)depthStencilState;
		HRESULT result = m_device->CreateDepthStencilState(&desc, &depthStencilStateTyped);
		if (FAILED(result))
			return false;

		return true;
	}

	bool D3D11GraphicsDevice::CreateDepthStencilBuffer()
	{
		if (!m_device)
			return false;

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
			return false;

		return true;
	}

	bool D3D11GraphicsDevice::CreateDepthStencilView()
	{
		if (!m_device)
			return false;

		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
		ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
		depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		// Create the depth stencil view.
		HRESULT result = m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
		if (FAILED(result))
			return false;

		return true;
	}
	//========================================================================================================================

	void D3D11GraphicsDevice::Clear(const Vector4& color)
	{
		if (!m_deviceContext)
			return;

		m_deviceContext->ClearRenderTargetView(m_renderTargetView, color.Data()); // back buffer
		if (m_depthEnabled)
		{
			m_deviceContext->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH, m_maxDepth, 0); // depth buffer
		}
	}

	void D3D11GraphicsDevice::Present()
	{
		if (!m_swapChain)
			return;

		m_swapChain->Present(VSYNC, 0);
	}

	void D3D11GraphicsDevice::SetBackBufferAsRenderTarget()
	{
		if (!m_deviceContext)
		{
			LOG_INFO("Cant't set back buffer as render terget, device context is uninitialized");
			return;
		}

		m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthEnabled ? m_depthStencilView : nullptr);
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
		if (!m_swapChain)
			return false;

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
		if (!m_deviceContext)
			return;

		m_viewport.Width = width;
		m_viewport.Height = height;
		m_viewport.MinDepth = 0.0f;
		m_viewport.MaxDepth = m_maxDepth;
		m_viewport.TopLeftX = 0.0f;
		m_viewport.TopLeftY = 0.0f;

		m_deviceContext->RSSetViewports(1, &m_viewport);
	}

	void D3D11GraphicsDevice::SetViewport()
	{
		if (!m_deviceContext)
			return;

		m_deviceContext->RSSetViewports(1, &m_viewport);
	}
	//================================================================

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

	void D3D11GraphicsDevice::SetCullMode(CullMode cullMode)
	{
		if (!m_deviceContext)
		{
			LOG_WARNING("Can't set cull mode, device context is uninitialized.");
			return;
		}

		// Set face CullMode only if not already set
		if (m_cullMode == cullMode)
			return;

		auto mode = d3dCullMode[cullMode];

		if (mode == D3D11_CULL_NONE)
		{
			m_deviceContext->RSSetState(m_rasterStateCullNone);
		}
		else if (mode == D3D11_CULL_FRONT)
		{
			m_deviceContext->RSSetState(m_rasterStateCullFront);
		}
		else if (mode == D3D11_CULL_BACK)
		{
			m_deviceContext->RSSetState(m_rasterStateCullBack);
		}

		// Save the current CullMode mode
		m_cullMode = cullMode;
	}

	//= HELPER FUNCTIONS ================================================================================
	bool D3D11GraphicsDevice::CreateDeviceAndSwapChain(ID3D11Device** device, ID3D11DeviceContext** deviceContext, IDXGISwapChain** swapchain)
	{
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
		HRESULT result = D3D11CreateDeviceAndSwapChain(
			nullptr,
			m_driverType,
			nullptr,
			0,
			&m_featureLevel,
			1,
			m_sdkVersion,
			&swapChainDesc,
			swapchain,
			device,
			nullptr,
			deviceContext
		);

		if (FAILED(result))
		{
			LOG_ERROR("Failed to create swap chain, device and device context.");
			return false;
		}

		return true;
	}

	bool D3D11GraphicsDevice::CreateRasterizerState(D3D11_CULL_MODE cullMode, D3D11_FILL_MODE fillMode, ID3D11RasterizerState** rasterizer)
	{
		if (!m_device)
		{
			LOG_ERROR("Aborting rasterizer state creation, device is not present");
			return false;
		}

		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = fillMode;
		desc.CullMode = cullMode;
		desc.FrontCounterClockwise = FALSE;
		desc.DepthBias = 0;
		desc.DepthBiasClamp = 0.0f;
		desc.SlopeScaledDepthBias = 0.0f;
		desc.DepthClipEnable = TRUE;
		desc.ScissorEnable = FALSE;
		desc.MultisampleEnable = FALSE;
		desc.AntialiasedLineEnable = FALSE;

		HRESULT result = m_device->CreateRasterizerState(&desc, rasterizer);

		if (FAILED(result))
		{
			LOG_ERROR("Failed to rasterizer state.");
			return false;
		}

		return true;
	}

	vector<IDXGIAdapter*> D3D11GraphicsDevice::GetAvailableAdapters(IDXGIFactory* factory)
	{
		UINT i = 0;
		IDXGIAdapter* pAdapter;
		vector <IDXGIAdapter*> adapters;
		while (factory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
		{
			adapters.push_back(pAdapter);
			++i;
		}

		return adapters;
	}

	IDXGIAdapter* D3D11GraphicsDevice::GetAdapterWithTheHighestVRAM(IDXGIFactory* factory)
	{
		IDXGIAdapter* maxAdapter = nullptr;
		size_t maxVRAM = -INFINITY;

		auto adapters = GetAvailableAdapters(factory);
		DXGI_ADAPTER_DESC adapterDesc;
		for (const auto& adapter : adapters)
		{
			adapter->GetDesc(&adapterDesc);

			if (adapterDesc.DedicatedVideoMemory > maxVRAM)
			{
				maxVRAM = adapterDesc.DedicatedVideoMemory;
				maxAdapter = adapter;
			}
		}

		return maxAdapter;
	}

	IDXGIAdapter* D3D11GraphicsDevice::GetAdapterByVendorID(IDXGIFactory* factory, unsigned vendorID)
	{
		// Nvidia: 0x10DE
		// AMD: 0x1002, 0x1022
		// Intel: 0x163C, 0x8086, 0x8087

		auto adapters = GetAvailableAdapters(factory);

		DXGI_ADAPTER_DESC adapterDesc;
		for (const auto& adapter : adapters)
		{
			adapter->GetDesc(&adapterDesc);
			if (vendorID == adapterDesc.VendorId)
				return adapter;
		}

		return nullptr;
	}

	string D3D11GraphicsDevice::GetAdapterDescription(IDXGIAdapter* adapter)
	{
		if (!adapter)
			return NOT_ASSIGNED;

		DXGI_ADAPTER_DESC adapterDesc;
		HRESULT result = adapter->GetDesc(&adapterDesc);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to get adapter description.");
			return NOT_ASSIGNED;
		}

		int adapterVRAM = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024); // MB
		char adapterName[128];
		char DefChar = ' ';
		WideCharToMultiByte(CP_ACP, 0, adapterDesc.Description, -1, adapterName, 128, &DefChar, nullptr);

		return string(adapterName) + " (" + to_string(adapterVRAM) + " MB)";
	}
}
