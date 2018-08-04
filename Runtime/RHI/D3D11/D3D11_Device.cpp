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
#include "../RHI_Device.h"
#include "../IRHI_Implementation.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	namespace D3D11_Internals
	{
		static ID3D11Device* m_device;
		static ID3D11DeviceContext* m_deviceContext;
		static IDXGISwapChain* m_swapChain;
		static ID3D11RenderTargetView* m_renderTargetView;
		static unsigned int m_displayModeCount;
		static unsigned int m_refreshRateNumerator;
		static unsigned int m_refreshRateDenominator;
		static DXGI_MODE_DESC* m_displayModeList;
		static ID3D11Texture2D* m_depthStencilBuffer;
		static ID3D11DepthStencilState* m_depthStencilStateEnabled;
		static ID3D11DepthStencilState* m_depthStencilStateDisabled;
		static ID3D11DepthStencilView* m_depthStencilView;
		static ID3D11RasterizerState* m_rasterStateCullFront;
		static ID3D11RasterizerState* m_rasterStateCullBack;
		static ID3D11RasterizerState* m_rasterStateCullNone;
		static ID3D11BlendState* m_blendStateAlphaEnabled;
		static ID3D11BlendState* m_blendStateAlphaDisabled;
		static ID3DUserDefinedAnnotation* m_eventReporter;

		const static D3D_DRIVER_TYPE driverType = D3D_DRIVER_TYPE_HARDWARE;
		const static unsigned int sdkVersion	= D3D11_SDK_VERSION;

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

		inline bool CreateDepthStencilState(ID3D11DepthStencilState* depthStencilState, bool depthEnabled, bool writeEnabled)
		{
			if (!m_device)
				return false;

			D3D11_DEPTH_STENCIL_DESC desc;
			ZeroMemory(&desc, sizeof(desc));

			// Depth test parameters
			desc.DepthEnable	= depthEnabled ? TRUE : FALSE;
			desc.DepthWriteMask = writeEnabled ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
			desc.DepthFunc		= D3D11_COMPARISON_LESS;

			// Stencil test parameters
			desc.StencilEnable		= depthEnabled;
			desc.StencilReadMask	= D3D11_DEFAULT_STENCIL_READ_MASK;
			desc.StencilWriteMask	= D3D11_DEFAULT_STENCIL_WRITE_MASK;

			// Stencil operations if pixel is front-facing
			desc.FrontFace.StencilFailOp		= D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilDepthFailOp	= D3D11_STENCIL_OP_INCR;
			desc.FrontFace.StencilPassOp		= D3D11_STENCIL_OP_KEEP;
			desc.FrontFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;

			// Stencil operations if pixel is back-facing
			desc.BackFace.StencilFailOp			= D3D11_STENCIL_OP_KEEP;
			desc.BackFace.StencilDepthFailOp	= D3D11_STENCIL_OP_DECR;
			desc.BackFace.StencilPassOp			= D3D11_STENCIL_OP_KEEP;
			desc.BackFace.StencilFunc			= D3D11_COMPARISON_ALWAYS;

			// Create depth stencil state
			auto result = m_device->CreateDepthStencilState(&desc, &depthStencilState);

			return !FAILED(result);
		}

		inline bool CreateDepthStencilView(UINT width, UINT height)
		{
			if (!m_device)
				return false;

			D3D11_TEXTURE2D_DESC depthBufferDesc;
			ZeroMemory(&depthBufferDesc, sizeof(depthBufferDesc));
			depthBufferDesc.Width				= width;
			depthBufferDesc.Height				= height;
			depthBufferDesc.MipLevels			= 1;
			depthBufferDesc.ArraySize			= 1;
			depthBufferDesc.Format				= DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthBufferDesc.SampleDesc.Count	= 1;
			depthBufferDesc.SampleDesc.Quality	= 0;
			depthBufferDesc.Usage				= D3D11_USAGE_DEFAULT;
			depthBufferDesc.BindFlags			= D3D11_BIND_DEPTH_STENCIL;
			depthBufferDesc.CPUAccessFlags		= 0;
			depthBufferDesc.MiscFlags			= 0;

			// Create the texture for the depth buffer using the filled out description.
			auto result = m_device->CreateTexture2D(&depthBufferDesc, nullptr, &m_depthStencilBuffer);
			if (FAILED(result))
				return false;

			D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
			ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
			depthStencilViewDesc.Format				= DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthStencilViewDesc.ViewDimension		= D3D11_DSV_DIMENSION_TEXTURE2D;
			depthStencilViewDesc.Texture2D.MipSlice = 0;

			// Create the depth stencil view.
			result = m_device->CreateDepthStencilView(m_depthStencilBuffer, &depthStencilViewDesc, &m_depthStencilView);
			if (FAILED(result))
				return false;

			return true;
		}

		inline bool CreateRasterizerState(Cull_Mode cullMode, Fill_Mode fillMode, ID3D11RasterizerState** rasterizer)
		{
			if (!m_device)
			{
				LOG_ERROR("D3D11_Device::CreateRasterizerState: Aborting rasterizer state creation, device is not present");
				return false;
			}

			D3D11_RASTERIZER_DESC desc = {};
			desc.FillMode = d3d11_fill_Mode[fillMode];
			desc.CullMode = d3d11_cull_mode[cullMode];
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
				LOG_ERROR("D3D11_Device::CreateRasterizerState: Failed to rasterizer state.");
				return false;
			}

			return true;
		}

		inline vector<IDXGIAdapter*> GetAvailableAdapters(IDXGIFactory* factory)
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

		inline IDXGIAdapter* GetAdapterWithTheHighestVRAM(IDXGIFactory* factory)
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

		inline IDXGIAdapter* GetAdapterByVendorID(IDXGIFactory* factory, unsigned int vendorID)
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

		inline string GetAdapterDescription(IDXGIAdapter* adapter)
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

	RHI_Device::RHI_Device(void* drawHandle)
	{
		m_format				= Texture_Format_R8G8B8A8_UNORM;
		m_depthEnabled			= true;
		m_alphaBlendingEnabled	= false;
		m_initialized			= false;

		if (!IsWindow((HWND)drawHandle))
		{
			LOG_ERROR("RHI_Device::Initialize: Invalid draw handle.");
			return;
		}

		// Create DirectX graphics interface factory
		IDXGIFactory* factory;
		HRESULT result = CreateDXGIFactory(IID_PPV_ARGS(&factory));
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::Initialize: Failed to create a DirectX graphics interface factory.");
			return;
		}

		// Get adapter
		IDXGIAdapter* adapter = D3D11_Internals::GetAdapterWithTheHighestVRAM(factory);
		SafeRelease(factory);
		if (!adapter)
		{
			LOG_ERROR("RHI_Device::Initialize: Couldn't find any adapters.");
			return;
		}

		// ADAPTER OUTPUT / DISPLAY MODE
		{
			// Enumerate the primary adapter output (monitor).
			IDXGIOutput* adapterOutput;
			result = adapter->EnumOutputs(0, &adapterOutput);
			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to enumerate the primary adapter output.");
				return;
			}

			// Get the number of modes that fit the requested format, for the adapter output (monitor).
			result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_format], DXGI_ENUM_MODES_INTERLACED, &D3D11_Internals::m_displayModeCount, nullptr);
			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to get adapter's display modes.");
				return;
			}

			// Create display mode list
			D3D11_Internals::m_displayModeList = new DXGI_MODE_DESC[D3D11_Internals::m_displayModeCount];
			if (!D3D11_Internals::m_displayModeList)
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to create a display mode list.");
				return;
			}

			// Now fill the display mode list structures.
			result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_format], DXGI_ENUM_MODES_INTERLACED, &D3D11_Internals::m_displayModeCount, D3D11_Internals::m_displayModeList);
			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to fill the display mode list structures.");
				return;
			}
			// Release the adapter output.
			adapterOutput->Release();

			// Go through all the display modes and find the one that matches the screen width and height.
			for (unsigned int i = 0; i < D3D11_Internals::m_displayModeCount; i++)
			{
				if (D3D11_Internals::m_displayModeList[i].Width == (unsigned int)Settings::Get().GetResolutionWidth() && D3D11_Internals::m_displayModeList[i].Height == (unsigned int)Settings::Get().GetResolutionHeight())
				{
					D3D11_Internals::m_refreshRateNumerator		= (unsigned int)D3D11_Internals::m_displayModeList[i].RefreshRate.Numerator;
					D3D11_Internals::m_refreshRateDenominator	= (unsigned int)D3D11_Internals::m_displayModeList[i].RefreshRate.Denominator;
					break;
				}
			}
		}

		// SWAPCHAIN
		{
			DXGI_SWAP_CHAIN_DESC swapChainDesc;
			ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

			swapChainDesc.BufferCount					= 1;
			swapChainDesc.BufferDesc.Width				= Settings::Get().GetResolutionWidth();
			swapChainDesc.BufferDesc.Height				= Settings::Get().GetResolutionHeight();
			swapChainDesc.BufferDesc.Format				= d3d11_dxgi_format[m_format];
			swapChainDesc.BufferUsage					= DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.OutputWindow					= (HWND)drawHandle;
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
				D3D11_Internals::driverType,
				nullptr,									// specify nullptr because D3D_DRIVER_TYPE_HARDWARE indicates that this...
				deviceFlags,								// ...function uses hardware, optionally set debug and Direct2D compatibility flags
				D3D11_Internals::featureLevels,
				ARRAYSIZE(D3D11_Internals::featureLevels),
				D3D11_Internals::sdkVersion,				// always set this to D3D11_SDK_VERSION
				&swapChainDesc,
				&D3D11_Internals::m_swapChain,
				&D3D11_Internals::m_device,
				nullptr,
				&D3D11_Internals::m_deviceContext
			);

			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to create swap chain, device and device context.");
				return;
			}
		}

		// RENDER TARGET VIEW
		{
			ID3D11Texture2D* backBufferPtr;
			// Get the pointer to the back buffer.
			result = D3D11_Internals::m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBufferPtr));
			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to get the pointer to the back buffer.");
				return;
			}

			// Create the render target view with the back buffer pointer.
			result = D3D11_Internals::m_device->CreateRenderTargetView(backBufferPtr, nullptr, &D3D11_Internals::m_renderTargetView);
			SafeRelease(backBufferPtr);
			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to create the render target view.");
				return;
			}
		}

		// VIEWPORT
		auto viewport = RHI_Viewport((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight());
		SetViewport(viewport);

		// DEPTH
		if (!D3D11_Internals::CreateDepthStencilState(D3D11_Internals::m_depthStencilStateEnabled, true, true))
		{
			LOG_ERROR("RHI_Device::Initialize: Failed to create depth stencil enabled state.");
			return;
		}

		if (!D3D11_Internals::CreateDepthStencilState(D3D11_Internals::m_depthStencilStateDisabled, false, false))
		{
			LOG_ERROR("RHI_Device::Initialize: Failed to create depth stencil disabled state.");
			return;
		}

		if (!D3D11_Internals::CreateDepthStencilView((UINT)Settings::Get().GetResolutionWidth(), (UINT)Settings::Get().GetResolutionHeight()))
		{
			LOG_ERROR("RHI_Device::Initialize: Failed to create depth stencil view.");
			return;
		}

		// RASTERIZER STATES
		{
			if (!D3D11_Internals::CreateRasterizerState(Cull_Back, Fill_Solid, &D3D11_Internals::m_rasterStateCullBack))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to create the rasterizer state.");
				return;
			}

			if (!D3D11_Internals::CreateRasterizerState(Cull_Front, Fill_Solid, &D3D11_Internals::m_rasterStateCullFront))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to create the rasterizer state.");
				return;
			}

			if (!D3D11_Internals::CreateRasterizerState(Cull_None, Fill_Solid, &D3D11_Internals::m_rasterStateCullNone))
			{
				LOG_ERROR("RHI_Device::Initialize: Failed to create the rasterizer state.");
				return;
			}

			// Set default rasterizer state
			D3D11_Internals::m_deviceContext->RSSetState(D3D11_Internals::m_rasterStateCullBack);
		}

		// BLEND STATES
		{
			D3D11_BLEND_DESC blendStateDesc;
			ZeroMemory(&blendStateDesc, sizeof(blendStateDesc));
			blendStateDesc.RenderTarget[0].BlendEnable				= (BOOL)true;
			blendStateDesc.RenderTarget[0].RenderTargetWriteMask	= D3D11_COLOR_WRITE_ENABLE_ALL;

			blendStateDesc.RenderTarget[0].SrcBlend			= D3D11_BLEND_SRC_ALPHA;
			blendStateDesc.RenderTarget[0].DestBlend		= D3D11_BLEND_INV_SRC_ALPHA;
			blendStateDesc.RenderTarget[0].BlendOp			= D3D11_BLEND_OP_ADD;

			blendStateDesc.RenderTarget[0].SrcBlendAlpha	= D3D11_BLEND_ZERO;
			blendStateDesc.RenderTarget[0].DestBlendAlpha	= D3D11_BLEND_ONE;
			blendStateDesc.RenderTarget[0].BlendOpAlpha		= D3D11_BLEND_OP_ADD;

			// Create a blending state with alpha blending enabled
			blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)true;
			HRESULT result = D3D11_Internals::m_device->CreateBlendState(&blendStateDesc, &D3D11_Internals::m_blendStateAlphaEnabled);
			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::CreateBlendStates: Failed to create blend state.");
				return;
			}

			// Create a blending state with alpha blending disabled
			blendStateDesc.RenderTarget[0].BlendEnable = (BOOL)false;
			result = D3D11_Internals::m_device->CreateBlendState(&blendStateDesc, &D3D11_Internals::m_blendStateAlphaDisabled);
			if (FAILED(result))
			{
				LOG_ERROR("RHI_Device::CreateBlendStates: Failed to create blend state.");
				return;
			}
		}

		// EVENT REPORTER
		D3D11_Internals::m_eventReporter = nullptr;
		result = D3D11_Internals::m_deviceContext->QueryInterface(IID_PPV_ARGS(&D3D11_Internals::m_eventReporter));
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::Initialize: Failed to create ID3DUserDefinedAnnotation for event reporting");
			return;
		}

		// Log feature level and adapter info
		D3D_FEATURE_LEVEL featureLevel = D3D11_Internals::m_device->GetFeatureLevel();
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

		LOGF_INFO("RHI_Device: Feature level %s - %s", featureLevelStr.data(), D3D11_Internals::GetAdapterDescription(adapter).data());
		m_device		= (void*)D3D11_Internals::m_device;
		m_deviceContext = (void*)D3D11_Internals::m_deviceContext;
		m_initialized	= true;
	}

	RHI_Device::~RHI_Device()
	{
		// Before shutting down set to windowed mode or 
		// upon releasing the swap chain it will throw an exception.
		if (D3D11_Internals::m_swapChain)
		{
			D3D11_Internals::m_swapChain->SetFullscreenState(false, nullptr);
		}

		SafeRelease(D3D11_Internals::m_blendStateAlphaEnabled);
		SafeRelease(D3D11_Internals::m_blendStateAlphaDisabled);
		SafeRelease(D3D11_Internals::m_rasterStateCullFront);
		SafeRelease(D3D11_Internals::m_rasterStateCullBack);
		SafeRelease(D3D11_Internals::m_rasterStateCullNone);
		SafeRelease(D3D11_Internals::m_depthStencilView);
		SafeRelease(D3D11_Internals::m_depthStencilStateEnabled);
		SafeRelease(D3D11_Internals::m_depthStencilStateDisabled);
		SafeRelease(D3D11_Internals::m_depthStencilBuffer);
		SafeRelease(D3D11_Internals::m_renderTargetView);
		SafeRelease(D3D11_Internals::m_deviceContext);
		SafeRelease(D3D11_Internals::m_device);
		SafeRelease(D3D11_Internals::m_swapChain);

		if (D3D11_Internals::m_displayModeList)
		{
			delete[] D3D11_Internals::m_displayModeList;
			D3D11_Internals::m_displayModeList = nullptr;
		}
	}

	void RHI_Device::Draw(unsigned int vertexCount)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->Draw(vertexCount, 0);
		Profiler::Get().m_drawCalls++;
	}

	void RHI_Device::DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->DrawIndexed(indexCount, indexOffset, vertexOffset);
		Profiler::Get().m_drawCalls++;
	}

	void RHI_Device::Clear(const Vector4& color)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->ClearRenderTargetView(D3D11_Internals::m_renderTargetView, color.Data()); // back buffer
		if (m_depthEnabled)
		{
			D3D11_Internals::m_deviceContext->ClearDepthStencilView(D3D11_Internals::m_depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, m_viewport.GetMaxDepth(), 0); // depth buffer
		}
	}

	void RHI_Device::Present()
	{
		if (!D3D11_Internals::m_swapChain)
			return;

		D3D11_Internals::m_swapChain->Present(Settings::Get().GetVSync(), 0);
	}

	void RHI_Device::Bind_BackBufferAsRenderTarget()
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->OMSetRenderTargets(1, &D3D11_Internals::m_renderTargetView, m_depthEnabled ? D3D11_Internals::m_depthStencilView : nullptr);
	}

	void RHI_Device::Bind_VertexShader(void* buffer)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->VSSetShader((ID3D11VertexShader*)buffer, nullptr, 0);
	}

	void RHI_Device::Bind_PixelShader(void* buffer)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->PSSetShader((ID3D11PixelShader*)buffer, nullptr, 0);
	}

	void RHI_Device::Bind_ConstantBuffers(unsigned int startSlot, unsigned int bufferCount, Buffer_Scope scope, void* const* buffer)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		auto d3d11buffer = (ID3D11Buffer*const*)buffer;
		if (scope == Buffer_VertexShader || scope == Buffer_Global)
		{
			D3D11_Internals::m_deviceContext->VSSetConstantBuffers(startSlot, bufferCount, d3d11buffer);
		}

		if (scope == Buffer_PixelShader || scope == Buffer_Global)
		{
			D3D11_Internals::m_deviceContext->PSSetConstantBuffers(startSlot, bufferCount, d3d11buffer);
		}
	}

	void RHI_Device::Bind_Samplers(unsigned int startSlot, unsigned int samplerCount, void* const* samplers)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->PSSetSamplers(startSlot, samplerCount, (ID3D11SamplerState* const*)samplers);
	}

	void RHI_Device::Bind_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->OMSetRenderTargets(renderTargetCount, (ID3D11RenderTargetView**)&renderTargets[0], (ID3D11DepthStencilView*)depthStencil);
	}

	void RHI_Device::Bind_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		D3D11_Internals::m_deviceContext->PSSetShaderResources(startSlot, resourceCount, (ID3D11ShaderResourceView**)shaderResources);
	}

	bool RHI_Device::SetResolution(int width, int height)
	{
		if (!D3D11_Internals::m_swapChain)
			return false;

		// Release resolution based stuff
		SafeRelease(D3D11_Internals::m_renderTargetView);
		SafeRelease(D3D11_Internals::m_depthStencilBuffer);
		SafeRelease(D3D11_Internals::m_depthStencilView);

		// Resize swapchain target
		DXGI_MODE_DESC dxgiModeDesc;
		ZeroMemory(&dxgiModeDesc, sizeof(dxgiModeDesc));
		dxgiModeDesc.Width				= (unsigned int)width;
		dxgiModeDesc.Height				= (unsigned int)height;
		dxgiModeDesc.Format				= d3d11_dxgi_format[m_format];
		dxgiModeDesc.RefreshRate		= DXGI_RATIONAL{ D3D11_Internals::m_refreshRateNumerator, D3D11_Internals::m_refreshRateDenominator };
		dxgiModeDesc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;
		dxgiModeDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

		HRESULT result = D3D11_Internals::m_swapChain->ResizeTarget(&dxgiModeDesc);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::SetResolution: Failed to resize swapchain target.");
			return false;
		}

		// Resize swapchain buffers
		result = D3D11_Internals::m_swapChain->ResizeBuffers(1, (unsigned int)width, (unsigned int)height, dxgiModeDesc.Format, 0);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::SetResolution: Failed to resize swapchain buffers.");
			return false;
		}

		// Create render target view
		ID3D11Texture2D* backBuffer = nullptr;
		result = D3D11_Internals::m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::SetResolution: Failed to get pointer to the swapchain's back buffer.");
			return false;
		}

		result = D3D11_Internals::m_device->CreateRenderTargetView(backBuffer, nullptr, &D3D11_Internals::m_renderTargetView);
		SafeRelease(backBuffer);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::SetResolution: Failed to create render target view.");
			return false;
		}

		// Re-create depth stencil view
		D3D11_Internals::CreateDepthStencilView(width, height);

		return true;
	}

	void RHI_Device::SetViewport(const RHI_Viewport& viewport)
	{
		if (!D3D11_Internals::m_deviceContext)
			return;

		m_viewport = viewport;
		D3D11_Internals::m_deviceContext->RSSetViewports(1, (D3D11_VIEWPORT*)&m_viewport);
	}

	bool RHI_Device::EnableDepth(bool enable)
	{
		if (!D3D11_Internals::m_deviceContext)
		{
			LOG_WARNING("D3D11_Device::EnableDepth: Device context is uninitialized.");
			return false;
		}

		// Set depth stencil state
		D3D11_Internals::m_deviceContext->OMSetDepthStencilState(m_depthEnabled ? D3D11_Internals::m_depthStencilStateEnabled : D3D11_Internals::m_depthStencilStateDisabled, 1);
		return true;
	}

	bool RHI_Device::EnableAlphaBlending(bool enable)
	{
		if (!D3D11_Internals::m_deviceContext)
		{
			LOG_WARNING("D3D11_Device::EnableAlphaBlending: Device context is uninitialized.");
			return false;
		}

		// Set blend state
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		D3D11_Internals::m_deviceContext->OMSetBlendState(enable ? D3D11_Internals::m_blendStateAlphaEnabled : D3D11_Internals::m_blendStateAlphaDisabled, blendFactor, 0xffffffff);

		return true;
	}

	void RHI_Device::EventBegin(const std::string& name)
	{
		D3D11_Internals::m_eventReporter->BeginEvent(FileSystem::StringToWString(name).c_str());
	}

	void RHI_Device::EventEnd()
	{
		D3D11_Internals::m_eventReporter->EndEvent();
	}

	void RHI_Device::QueryBegin()
	{
		// Begin disjoint query, and timestamp the beginning of the frame
		//m_deviceContext->Begin(pQueryDisjoint);
		//m_deviceContext->End(pQueryBeginFrame);
	}

	void RHI_Device::QueryEnd()
	{
		//m_deviceContext->End(pQueryEndFrame);
		//m_deviceContext->End(pQueryDisjoint);
	}

	bool RHI_Device::Set_PrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
	{
		if (!D3D11_Internals::m_deviceContext)
		{
			LOG_ERROR("D3D11_Device::Set_InputLayout: Invalid device context");
			return false;
		}

		// Set primitive topology
		D3D11_Internals::m_deviceContext->IASetPrimitiveTopology(d3d11_primitive_topology[primitiveTopology]);
		return true;
	}

	bool RHI_Device::Set_FillMode(Fill_Mode fillMode)
	{
		return true;
	}

	bool RHI_Device::Set_InputLayout(void* inputLayout)
	{
		if (!D3D11_Internals::m_deviceContext)
		{
			LOG_ERROR("D3D11_Device::Set_InputLayout: Invalid device context");
			return false;
		}

		D3D11_Internals::m_deviceContext->IASetInputLayout((ID3D11InputLayout*)inputLayout);
		return true;
	}

	bool RHI_Device::Set_CullMode(Cull_Mode cullMode)
	{
		if (!D3D11_Internals::m_deviceContext)
		{
			LOG_WARNING("D3D11_Device::SetCullMode: Device context is uninitialized.");
			return false;
		}

		auto mode = d3d11_cull_mode[cullMode];

		if (mode == D3D11_CULL_NONE)
		{
			D3D11_Internals::m_deviceContext->RSSetState(D3D11_Internals::m_rasterStateCullNone);
		}
		else if (mode == D3D11_CULL_FRONT)
		{
			D3D11_Internals::m_deviceContext->RSSetState(D3D11_Internals::m_rasterStateCullFront);
		}
		else if (mode == D3D11_CULL_BACK)
		{
			D3D11_Internals::m_deviceContext->RSSetState(D3D11_Internals::m_rasterStateCullBack);
		}

		return true;
	}
}
