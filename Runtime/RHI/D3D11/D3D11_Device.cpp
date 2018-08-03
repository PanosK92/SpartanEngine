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

//= INCLUDES ======================
#include "../../Logging/Log.h"
#include "../IRHI_Implementation.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace D3D11Settings
{
	const static D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;
	const static unsigned int sdkVersion = D3D11_SDK_VERSION;

	 // Define the ordering of feature levels that Direct3D attempts to create.
	const static D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_1
    };
}

namespace Directus
{
	D3D11_Device::D3D11_Device(Context* context) : IRHI_Device(context)
	{
		m_device					= nullptr;
		m_deviceContext				= nullptr;
		m_swapChain					= nullptr;
		m_renderTargetView			= nullptr;
		m_displayModeList			= nullptr;
		m_displayModeCount			= 0;
		m_refreshRateNumerator		= 0;
		m_refreshRateDenominator	= 0;
		m_depthStencilBuffer		= nullptr;
		m_depthStencilStateEnabled	= nullptr;
		m_depthStencilStateDisabled = nullptr;
		m_depthStencilView			= nullptr;
		m_rasterStateCullFront		= nullptr;
		m_rasterStateCullBack		= nullptr;
		m_rasterStateCullNone		= nullptr;
		m_blendStateAlphaEnabled	= nullptr;
		m_blendStateAlphaDisabled	= nullptr;
		m_drawHandle				= nullptr;
		m_initialized				= false;
		m_eventReporter				= nullptr;
	}

	D3D11_Device::~D3D11_Device()
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

	bool D3D11_Device::Initialize()
	{
		if (!IsWindow((HWND)m_drawHandle))
		{
			LOG_ERROR("Aborting D3D11 initialization. Invalid draw handle.");
			return false;
		}

		//= GRAPHICS INTERFACE FACTORY =================================================
		IDXGIFactory* factory;
		HRESULT result = CreateDXGIFactory(IID_PPV_ARGS(&factory));
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::Initialize: Failed to create a DirectX graphics interface factory.");
			return false;
		}
		//==============================================================================

		//= ADAPTER ====================================================================
		IDXGIAdapter* adapter = GetAdapterWithTheHighestVRAM(factory); // usually the dedicated one
		SafeRelease(factory);
		if (!adapter)
		{
			LOG_ERROR("D3D11_Device::Initialize: Couldn't find any adapters.");
			return false;
		}
		//==============================================================================

		//= ADAPTER OUTPUT / DISPLAY MODE ==============================================
		{
			// Enumerate the primary adapter output (monitor).
			IDXGIOutput* adapterOutput;
			result = adapter->EnumOutputs(0, &adapterOutput);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to enumerate the primary adapter output.");
				return false;
			}

			// Get the number of modes that fit the requested format, for the adapter output (monitor).
			result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_format], DXGI_ENUM_MODES_INTERLACED, &m_displayModeCount, nullptr);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to get adapter's display modes.");
				return false;
			}

			// Create display mode list
			m_displayModeList = new DXGI_MODE_DESC[m_displayModeCount];
			if (!m_displayModeList)
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to create a display mode list.");
				return false;
			}

			// Now fill the display mode list structures.
			result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_format], DXGI_ENUM_MODES_INTERLACED, &m_displayModeCount, m_displayModeList);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to fill the display mode list structures.");
				return false;
			}
			// Release the adapter output.
			adapterOutput->Release();

			// Go through all the display modes and find the one that matches the screen width and height.
			for (unsigned int i = 0; i < m_displayModeCount; i++)
			{	
				if (m_displayModeList[i].Width == (unsigned int)Settings::Get().GetResolutionWidth() && m_displayModeList[i].Height == (unsigned int)Settings::Get().GetResolutionHeight())
				{
					m_refreshRateNumerator		= (unsigned int)m_displayModeList[i].RefreshRate.Numerator;
					m_refreshRateDenominator	= (unsigned int)m_displayModeList[i].RefreshRate.Denominator;
					break;
				}
			}
		}
		//==============================================================================

		//= SWAPCHAIN ==================================================================
		{
			DXGI_SWAP_CHAIN_DESC swapChainDesc;
			ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

			swapChainDesc.BufferCount					= 1;
			swapChainDesc.BufferDesc.Width				= Settings::Get().GetResolutionWidth();
			swapChainDesc.BufferDesc.Height				= Settings::Get().GetResolutionHeight();
			swapChainDesc.BufferDesc.Format				= d3d11_dxgi_format[m_format];
			swapChainDesc.BufferUsage					= DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.OutputWindow					= (HWND)m_drawHandle;
			swapChainDesc.SampleDesc.Count				= 1;
			swapChainDesc.SampleDesc.Quality			= 0;
			swapChainDesc.Windowed						= (BOOL)!Settings::Get().IsFullScreen();
			swapChainDesc.BufferDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			swapChainDesc.BufferDesc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;
			swapChainDesc.SwapEffect					= DXGI_SWAP_EFFECT_DISCARD;
			swapChainDesc.Flags							= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; //| DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // alt + enter fullscreen

			UINT deviceFlags = 0;
			#ifdef DEBUG
			deviceFlags |= D3D11_CREATE_DEVICE_DEBUG; // Enable debug layer
			#endif

			// Create the swap chain, Direct3D device, and Direct3D device context.
			HRESULT result = D3D11CreateDeviceAndSwapChain(
				nullptr,									// specify nullptr to use the default adapter
				D3D11Settings::driverType,
				nullptr,									// specify nullptr because D3D_DRIVER_TYPE_HARDWARE indicates that this...
				deviceFlags,								// ...function uses hardware, optionally set debug and Direct2D compatibility flags
				D3D11Settings::featureLevels,
				ARRAYSIZE(D3D11Settings::featureLevels),
				D3D11Settings::sdkVersion,					// always set this to D3D11_SDK_VERSION
				&swapChainDesc,
				&m_swapChain,
				&m_device,
				nullptr,
				&m_deviceContext
			);

			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::CreateDeviceAndSwapChain: Failed to create swap chain, device and device context.");
				return false;
			}
		}
		//==============================================================================

		//= RENDER TARGET VIEW =========================================================
		{
			ID3D11Texture2D* backBufferPtr;
			// Get the pointer to the back buffer.
			result = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBufferPtr));
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to get the pointer to the back buffer.");
				return false;
			}

			// Create the render target view with the back buffer pointer.
			result = m_device->CreateRenderTargetView(backBufferPtr, nullptr, &m_renderTargetView);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to create the render target view.");
				return false;
			}

			// Release pointer to the back buffer
			backBufferPtr->Release();
			backBufferPtr = nullptr;
		}
		//==============================================================================

		m_backBufferViewport = RHI_Viewport((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight());
		SetViewport(m_backBufferViewport);

		//= DEPTH ======================================================================
		if (!CreateDepthStencilState(m_depthStencilStateEnabled, true, true))
		{
			LOG_ERROR("D3D11_Device::Initialize: Failed to create depth stencil enabled state.");
			return false;
		}

		if (!CreateDepthStencilState(m_depthStencilStateDisabled, false, false))
		{
			LOG_ERROR("D3D11_Device::Initialize: Failed to create depth stencil disabled state.");
			return false;
		}

		if (!CreateDepthStencilBuffer())
		{
			LOG_ERROR("D3D11_Device::Initialize: Failed to create depth stencil buffer.");
			return false;
		}

		if (!CreateDepthStencilView())
		{
			LOG_ERROR("D3D11_Device::Initialize: Failed to create the rasterizer state.");
			return false;
		}
		//===============================================================================

		//= RASTERIZERS =================================================================
		{
			if (!CreateRasterizerState(Cull_Back, Fill_Solid, &m_rasterStateCullBack))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to create the rasterizer state.");
				return false;
			}

			if (!CreateRasterizerState(Cull_Front, Fill_Solid, &m_rasterStateCullFront))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to create the rasterizer state.");
				return false;
			}

			if (!CreateRasterizerState(Cull_None, Fill_Solid, &m_rasterStateCullNone))
			{
				LOG_ERROR("D3D11_Device::Initialize: Failed to create the rasterizer state.");
				return false;
			}

			// Set default rasterizer state
			m_deviceContext->RSSetState(m_rasterStateCullBack);
		}
		//===============================================================================

		//= BLEND STATES ================================================================
		{
			D3D11_BLEND_DESC blendStateDesc;
			ZeroMemory(&blendStateDesc, sizeof(blendStateDesc));
			blendStateDesc.RenderTarget[0].BlendEnable				= (BOOL)true;
			blendStateDesc.RenderTarget[0].RenderTargetWriteMask	= D3D11_COLOR_WRITE_ENABLE_ALL;

			blendStateDesc.RenderTarget[0].SrcBlend		= D3D11_BLEND_SRC_ALPHA;
			blendStateDesc.RenderTarget[0].DestBlend	= D3D11_BLEND_INV_SRC_ALPHA;
			blendStateDesc.RenderTarget[0].BlendOp		= D3D11_BLEND_OP_ADD;

			blendStateDesc.RenderTarget[0].SrcBlendAlpha	= D3D11_BLEND_ZERO;
			blendStateDesc.RenderTarget[0].DestBlendAlpha	= D3D11_BLEND_ONE;
			blendStateDesc.RenderTarget[0].BlendOpAlpha		= D3D11_BLEND_OP_ADD;

			// Create a blending state with alpha blending enabled
			blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)true;
			HRESULT result = m_device->CreateBlendState(&blendStateDesc, &m_blendStateAlphaEnabled);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::CreateBlendStates: Failed to create blend state.");
				return false;
			}

			// Create a blending state with alpha blending disabled
			blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)false;
			result = m_device->CreateBlendState(&blendStateDesc, &m_blendStateAlphaDisabled);
			if (FAILED(result))
			{
				LOG_ERROR("D3D11_Device::CreateBlendStates: Failed to create blend state.");
				return false;
			}
		}
		//===============================================================================

		m_eventReporter = nullptr;
		result = m_deviceContext->QueryInterface(IID_PPV_ARGS(&m_eventReporter));
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::Initialize: Failed to create ID3DUserDefinedAnnotation for event reporting");
			return false;
		}

		// Log feature level and adapter info
		D3D_FEATURE_LEVEL featureLevel = m_device->GetFeatureLevel();
		string featureLevelStr;
		switch (featureLevel)
		{
		case D3D_FEATURE_LEVEL_9_1:
			featureLevelStr = "9.1";
			break;

		case D3D_FEATURE_LEVEL_9_2:
			featureLevelStr = "9.2";
			break;

		case D3D_FEATURE_LEVEL_9_3:
			featureLevelStr = "9.3";
			break;

		case D3D_FEATURE_LEVEL_10_0:
			featureLevelStr = "10.0";
			break;

		case D3D_FEATURE_LEVEL_10_1:
			featureLevelStr = "10.1";
			break;

		case D3D_FEATURE_LEVEL_11_0:
			featureLevelStr = "11.0";
			break;

		case D3D_FEATURE_LEVEL_11_1:
			featureLevelStr = "11.1";
			break;
		}
		LOGF_INFO("D3D11_Device::Initialize: Feature level %s - %s", featureLevelStr.data(), GetAdapterDescription(adapter).data());

		m_initialized = true;
		return true;
	}

	void D3D11_Device::Draw(unsigned int vertexCount)
	{
		if (!m_deviceContext)
			return;

		IRHI_Device::Draw(vertexCount);
		m_deviceContext->Draw(vertexCount, 0);
	}

	void D3D11_Device::DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)
	{
		if (!m_deviceContext)
			return;

		IRHI_Device::DrawIndexed(indexCount, indexOffset, vertexOffset);
		m_deviceContext->DrawIndexed(indexCount, indexOffset, vertexOffset);
	}

	void D3D11_Device::Clear(const Vector4& color)
	{
		if (!m_deviceContext)
			return;

		m_deviceContext->ClearRenderTargetView(m_renderTargetView, color.Data()); // back buffer
		if (m_depthEnabled)
		{
			m_deviceContext->ClearDepthStencilView(m_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, m_backBufferViewport.GetMaxDepth(), 0); // depth buffer
		}
	}

	void D3D11_Device::Present()
	{
		if (!m_swapChain)
			return;

		m_swapChain->Present(Settings::Get().GetVSync(), 0);
	}

	void D3D11_Device::Bind_BackBufferAsRenderTarget()
	{
		if (!m_deviceContext)
			return;

		m_deviceContext->OMSetRenderTargets(1, &m_renderTargetView, m_depthEnabled ? m_depthStencilView : nullptr);
	}

	void D3D11_Device::Bind_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil)
	{
		if (!m_deviceContext)
			return;

		m_deviceContext->OMSetRenderTargets(renderTargetCount, (ID3D11RenderTargetView**)&renderTargets[0], (ID3D11DepthStencilView*)depthStencil);
	}

	void D3D11_Device::Bind_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources)
	{
		if (!m_deviceContext)
			return;

		m_deviceContext->PSSetShaderResources(startSlot, resourceCount, (ID3D11ShaderResourceView**)shaderResources);
	}

	bool D3D11_Device::SetResolution(int width, int height)
	{
		if (!IRHI_Device::SetResolution(width, height))
			return false;

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
		dxgiModeDesc.Width = (unsigned int)width;
		dxgiModeDesc.Height = (unsigned int)height;
		dxgiModeDesc.Format = d3d11_dxgi_format[m_format];
		dxgiModeDesc.RefreshRate = DXGI_RATIONAL{ m_refreshRateNumerator, m_refreshRateDenominator };
		dxgiModeDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		dxgiModeDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

		HRESULT result = m_swapChain->ResizeTarget(&dxgiModeDesc);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::SetResolution: Failed to resize swapchain target.");
			return false;
		}
		//==================================================================

		//= RESIZE BUFFERS =================================================
		// Resize the swap chain and recreate the render target views. 
		result = m_swapChain->ResizeBuffers(1, (unsigned int)width, (unsigned int)height, dxgiModeDesc.Format, 0);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::SetResolution: Failed to resize swapchain buffers.");
			return false;
		}
		//==================================================================

		//= RENDER TARGET VIEW =============================================
		ID3D11Texture2D* backBuffer = nullptr;
		result = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::SetResolution: Failed to get pointer to the swapchain's back buffer.");
			return false;
		}

		result = m_device->CreateRenderTargetView(backBuffer, nullptr, &m_renderTargetView);
		SafeRelease(backBuffer);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::SetResolution: Failed to create render target view.");
			return false;
		}
		//====================================================================

		//= RECREATE RESOLUTION BASED STUFF =
		CreateDepthStencilBuffer();
		CreateDepthStencilView();
		//===================================

		return true;
	}

	const Directus::RHI_Viewport& D3D11_Device::GetViewport()
	{
		if (!m_deviceContext)
			return RHI_Viewport();

		UINT viewportCount = 1;
		D3D11_VIEWPORT viewport;
		m_deviceContext->RSGetViewports(&viewportCount, &viewport);

		return RHI_Viewport(
			viewport.TopLeftX,
			viewport.TopLeftY,
			viewport.Width,
			viewport.Height,
			viewport.MinDepth,
			viewport.MaxDepth
		);
	}

	void D3D11_Device::SetViewport(const RHI_Viewport& viewport)
	{
		if (!m_deviceContext)
			return;

		m_deviceContext->RSSetViewports(1, (D3D11_VIEWPORT*)&viewport);
	}

	bool D3D11_Device::EnableDepth(bool enable)
	{
		if (!IRHI_Device::EnableDepth(enable))
			return false;

		if (!m_deviceContext)
		{
			LOG_WARNING("D3D11_Device::EnableDepth: Device context is uninitialized.");
			return false;
		}

		// Set depth stencil state
		m_deviceContext->OMSetDepthStencilState(m_depthEnabled ? m_depthStencilStateEnabled : m_depthStencilStateDisabled, 1);
		return true;
	}

	bool D3D11_Device::EnableAlphaBlending(bool enable)
	{
		if (!IRHI_Device::EnableAlphaBlending(enable))
			return false;

		if (!m_deviceContext)
		{
			LOG_WARNING("D3D11_Device::EnableAlphaBlending: Device context is uninitialized.");
			return false;
		}

		// Set blend state
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_deviceContext->OMSetBlendState(enable ? m_blendStateAlphaEnabled : m_blendStateAlphaDisabled, blendFactor, 0xffffffff);

		return true;
	}

	void D3D11_Device::EventBegin(const std::string& name)
	{
		m_eventReporter->BeginEvent(FileSystem::StringToWString(name).c_str());
	}

	void D3D11_Device::EventEnd()
	{
		m_eventReporter->EndEvent();
	}

	void D3D11_Device::QueryBegin()
	{
		// Begin disjoint query, and timestamp the beginning of the frame
		//m_deviceContext->Begin(pQueryDisjoint);
		//m_deviceContext->End(pQueryBeginFrame);
	}

	void D3D11_Device::QueryEnd()
	{
		//m_deviceContext->End(pQueryEndFrame);
		//m_deviceContext->End(pQueryDisjoint);
	}

	bool D3D11_Device::Set_PrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
	{
		if (!m_deviceContext)
		{
			LOG_ERROR("D3D11_Device::Set_InputLayout: Invalid device context");
			return false;
		}

		// Set primitive topology
		m_deviceContext->IASetPrimitiveTopology(d3d11_primitive_topology[primitiveTopology]);
		return true;
	}

	bool D3D11_Device::Set_FillMode(Fill_Mode fillMode)
	{
		return true;
	}

	bool D3D11_Device::Set_InputLayout(void* inputLayout)
	{
		if (!m_deviceContext)
		{
			LOG_ERROR("D3D11_Device::Set_InputLayout: Invalid device context");
			return false;
		}

		m_deviceContext->IASetInputLayout((ID3D11InputLayout*)inputLayout);
		return true;
	}

	bool D3D11_Device::Set_CullMode(Cull_Mode cullMode)
	{
		if (!m_deviceContext)
		{
			LOG_WARNING("D3D11_Device::SetCullMode: Device context is uninitialized.");
			return false;
		}

		auto mode = d3d11_cull_mode[cullMode];

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

		return true;
	}

	//= HELPER FUNCTIONS ================================================================================
	bool D3D11_Device::CreateDepthStencilState(void* depthStencilState, bool depthEnabled, bool writeEnabled)
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
		desc.StencilEnable = depthEnabled;
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
		auto depthStencilStateTyped = (ID3D11DepthStencilState*)depthStencilState;
		auto result = m_device->CreateDepthStencilState(&desc, &depthStencilStateTyped);

		return !FAILED(result);
	}

	bool D3D11_Device::CreateDepthStencilBuffer()
	{
		if (!m_device)
			return false;

		D3D11_TEXTURE2D_DESC depthBufferDesc;
		ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));
		depthBufferDesc.Width = Settings::Get().GetResolutionWidth();
		depthBufferDesc.Height = Settings::Get().GetResolutionHeight();
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
		auto result = m_device->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);

		return !FAILED(result);
	}

	bool D3D11_Device::CreateDepthStencilView()
	{
		if (!m_device)
			return false;

		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
		ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
		depthStencilViewDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		// Create the depth stencil view.
		auto result = m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);

		return !FAILED(result);
	}

	bool D3D11_Device::CreateRasterizerState(Cull_Mode cullMode, Fill_Mode fillMode, ID3D11RasterizerState** rasterizer)
	{
		if (!m_device)
		{
			LOG_ERROR("D3D11_Device::CreateRasterizerState: Aborting rasterizer state creation, device is not present");
			return false;
		}

		D3D11_RASTERIZER_DESC desc = {};
		desc.FillMode = d3d11_fill_Mode[fillMode];
		desc.CullMode = d3d11_cull_mode[cullMode];
		desc.FrontCounterClockwise	= FALSE;
		desc.DepthBias				= 0;
		desc.DepthBiasClamp			= 0.0f;
		desc.SlopeScaledDepthBias	= 0.0f;
		desc.DepthClipEnable		= TRUE;
		desc.ScissorEnable			= FALSE;
		desc.MultisampleEnable		= FALSE;
		desc.AntialiasedLineEnable	= FALSE;

		HRESULT result = m_device->CreateRasterizerState(&desc, rasterizer);

		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::CreateRasterizerState: Failed to rasterizer state.");
			return false;
		}

		return true;
	}

	vector<IDXGIAdapter*> D3D11_Device::GetAvailableAdapters(IDXGIFactory* factory)
	{
		unsigned int i = 0;
		IDXGIAdapter* pAdapter;
		vector <IDXGIAdapter*> adapters;
		while (factory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
		{
			adapters.push_back(pAdapter);
			++i;
		}

		return adapters;
	}

	IDXGIAdapter* D3D11_Device::GetAdapterWithTheHighestVRAM(IDXGIFactory* factory)
	{
		IDXGIAdapter* maxAdapter = nullptr;
		unsigned int maxVRAM = 0;

		auto adapters = GetAvailableAdapters(factory);
		DXGI_ADAPTER_DESC adapterDesc;
		for (const auto& adapter : adapters)
		{
			adapter->GetDesc(&adapterDesc);

			if (adapterDesc.DedicatedVideoMemory > maxVRAM)
			{
				maxVRAM = (unsigned int)adapterDesc.DedicatedVideoMemory;
				maxAdapter = adapter;
			}
		}

		return maxAdapter;
	}

	IDXGIAdapter* D3D11_Device::GetAdapterByVendorID(IDXGIFactory* factory, unsigned int vendorID)
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

	string D3D11_Device::GetAdapterDescription(IDXGIAdapter* adapter)
	{
		if (!adapter)
			return "error";

		DXGI_ADAPTER_DESC adapterDesc;
		HRESULT result = adapter->GetDesc(&adapterDesc);
		if (FAILED(result))
		{
			LOG_ERROR("D3D11_Device::GetAdapterDescription: Failed to get adapter description.");
			return "error";
		}

		int adapterVRAM = (int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024); // MB
		char adapterName[128];
		char DefChar = ' ';
		WideCharToMultiByte(CP_ACP, 0, adapterDesc.Description, -1, adapterName, 128, &DefChar, nullptr);

		return string(adapterName) + " (" + to_string(adapterVRAM) + " MB)";
	}
}
