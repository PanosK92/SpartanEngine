/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= IMPLEMENTATION ===============
#include "../RHI_Implementation.h"
#ifdef API_D3D11
//================================

//= INCLUDES ===========================
#include "D3D11_Common.h"
#include "../RHI_Device.h"
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Profiling/Profiler.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	namespace _D3D11_Device
	{
		// Options
		const static D3D_DRIVER_TYPE driverType		= D3D_DRIVER_TYPE_HARDWARE;
		const static unsigned int sdkVersion		= D3D11_SDK_VERSION;
		const static UINT swapchainBufferCount		= 2;
		const static auto swapEffect				= DXGI_SWAP_EFFECT_FLIP_DISCARD;
		const static auto swapchainFlags			= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		const static bool multithreadProtection		= true;		
#ifdef DEBUG
		const static UINT deviceFlags = D3D11_CREATE_DEVICE_DEBUG; // Debug layer
#else
		const static UINT deviceFlags = 0;
#endif

		// The order of the feature levels that we'll try to create a device from
		const static D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
			D3D_FEATURE_LEVEL_9_3,
			D3D_FEATURE_LEVEL_9_1
		};

		// All the pointers that we need
		ID3D11Device* device;
		ID3D11DeviceContext* deviceContext;
		IDXGISwapChain* swapChain;
		ID3D11RenderTargetView* renderTargetView;
		ID3D11Texture2D* depthStencilBuffer;
		ID3D11DepthStencilState* depthStencilStateEnabled;
		ID3D11DepthStencilState* depthStencilStateDisabled;
		ID3D11DepthStencilView* depthStencilView;
		ID3D11RasterizerState* rasterStateCullFront;
		ID3D11RasterizerState* rasterStateCullBack;
		ID3D11RasterizerState* rasterStateCullNone;
		ID3D11BlendState* blendStateAlphaEnabled;
		ID3D11BlendState* blendStateAlphaDisabled;
		ID3DUserDefinedAnnotation* eventReporter;	
		D3D11_VIEWPORT viewport;

		inline const char* DxgiErrorToString(HRESULT errorCode)
		{
			switch (errorCode)
			{
			case DXGI_ERROR_DEVICE_HUNG:                    return "DXGI_ERROR_DEVICE_HUNG";                  // The application's device failed due to badly formed commands sent by the application. This is an design-time issue that should be investigated and fixed.
			case DXGI_ERROR_DEVICE_REMOVED:                 return "DXGI_ERROR_DEVICE_REMOVED";               // The video card has been physically removed from the system, or a driver upgrade for the video card has occurred. The application should destroy and recreate the device. For help debugging the problem, call ID3D10Device::GetDeviceRemovedReason.
			case DXGI_ERROR_DEVICE_RESET:                   return "DXGI_ERROR_DEVICE_RESET";                 // The device failed due to a badly formed command. This is a run-time issue; The application should destroy and recreate the device.
			case DXGI_ERROR_DRIVER_INTERNAL_ERROR:          return "DXGI_ERROR_DRIVER_INTERNAL_ERROR";        // The driver encountered a problem and was put into the device removed state.
			case DXGI_ERROR_FRAME_STATISTICS_DISJOINT:      return "DXGI_ERROR_FRAME_STATISTICS_DISJOINT";    // An event (for example, a power cycle) interrupted the gathering of presentation statistics.
			case DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE:   return "DXGI_ERROR_GRAPHICS_VIDPN_SOURCE_IN_USE"; // The application attempted to acquire exclusive ownership of an output, but failed because some other application (or device within the application) already acquired ownership.
			case DXGI_ERROR_INVALID_CALL:                   return "DXGI_ERROR_INVALID_CALL";                 // The application provided invalid parameter data; this must be debugged and fixed before the application is released.
			case DXGI_ERROR_MORE_DATA:                      return "DXGI_ERROR_MORE_DATA";                    // The buffer supplied by the application is not big enough to hold the requested data.
			case DXGI_ERROR_NONEXCLUSIVE:                   return "DXGI_ERROR_NONEXCLUSIVE";                 // A global counter resource is in use, and the Direct3D device can't currently use the counter resource.
			case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:        return "DXGI_ERROR_NOT_CURRENTLY_AVAILABLE";      // The resource or request is not currently available, but it might become available later.
			case DXGI_ERROR_NOT_FOUND:                      return "DXGI_ERROR_NOT_FOUND";                    // When calling IDXGIObject::GetPrivateData, the GUID passed in is not recognized as one previously passed to IDXGIObject::SetPrivateData or IDXGIObject::SetPrivateDataInterface. When calling IDXGIFactory::EnumAdapters or IDXGIAdapter::EnumOutputs, the enumerated ordinal is out of range.
			case DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED:     return "DXGI_ERROR_REMOTE_CLIENT_DISCONNECTED";   // Reserved
			case DXGI_ERROR_REMOTE_OUTOFMEMORY:             return "DXGI_ERROR_REMOTE_OUTOFMEMORY";           // Reserved
			case DXGI_ERROR_WAS_STILL_DRAWING:              return "DXGI_ERROR_WAS_STILL_DRAWING";            // The GPU was busy at the moment when a call was made to perform an operation, and did not execute or schedule the operation.
			case DXGI_ERROR_UNSUPPORTED:                    return "DXGI_ERROR_UNSUPPORTED";                  // The requested functionality is not supported by the device or the driver.
			case DXGI_ERROR_ACCESS_LOST:                    return "DXGI_ERROR_ACCESS_LOST";                  // The desktop duplication interface is invalid. The desktop duplication interface typically becomes invalid when a different type of image is displayed on the desktop.
			case DXGI_ERROR_WAIT_TIMEOUT:                   return "DXGI_ERROR_WAIT_TIMEOUT";                 // The time-out interval elapsed before the next desktop frame was available.
			case DXGI_ERROR_SESSION_DISCONNECTED:           return "DXGI_ERROR_SESSION_DISCONNECTED";         // The Remote Desktop Services session is currently disconnected.
			case DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE:       return "DXGI_ERROR_RESTRICT_TO_OUTPUT_STALE";     // The DXGI output (monitor) to which the swap chain content was restricted is now disconnected or changed.
			case DXGI_ERROR_CANNOT_PROTECT_CONTENT:         return "DXGI_ERROR_CANNOT_PROTECT_CONTENT";       // DXGI can't provide content protection on the swap chain. This error is typically caused by an older driver, or when you use a swap chain that is incompatible with content protection.
			case DXGI_ERROR_ACCESS_DENIED:                  return "DXGI_ERROR_ACCESS_DENIED";                // You tried to use a resource to which you did not have the required access privileges. This error is most typically caused when you write to a shared resource with read-only access.
			case DXGI_ERROR_NAME_ALREADY_EXISTS:            return "DXGI_ERROR_NAME_ALREADY_EXISTS";          // The supplied name of a resource in a call to IDXGIResource1::CreateSharedHandle is already associated with some other resource.
			case DXGI_ERROR_SDK_COMPONENT_MISSING:          return "DXGI_ERROR_SDK_COMPONENT_MISSING";        // The operation depends on an SDK component that is missing or mismatched.
			}

			return "Unknown error code";
		}

		inline bool CreateDepthStencilView(UINT width, UINT height)
		{
			if (!device)
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
			auto result = device->CreateTexture2D(&depthBufferDesc, nullptr, &depthStencilBuffer);
			if (FAILED(result))
			{
				LOGF_ERROR("RHI_Device::CreateDepthStencilView: Failed to create depth stencil buffer, %s.", DxgiErrorToString(result));
				return false;
			}

			D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
			ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
			depthStencilViewDesc.Format				= DXGI_FORMAT_D24_UNORM_S8_UINT;
			depthStencilViewDesc.ViewDimension		= D3D11_DSV_DIMENSION_TEXTURE2D;
			depthStencilViewDesc.Texture2D.MipSlice = 0;

			// Create the depth stencil view.
			result = device->CreateDepthStencilView(depthStencilBuffer, &depthStencilViewDesc, &depthStencilView);
			if (FAILED(result))
			{
				LOGF_ERROR("RHI_Device::CreateDepthStencilView: Failed to create depth stencil view, %s.", DxgiErrorToString(result));
				return false;
			}

			return true;
		}

		inline vector<IDXGIAdapter*> GetAvailableAdapters(IDXGIFactory* factory)
		{
			unsigned int i = 0;
			IDXGIAdapter* adapter;
			vector<IDXGIAdapter*> adapters;
			while (factory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND)
			{
				adapters.emplace_back(adapter);
				++i;
			}

			return adapters;
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

		// Get all available adapters
		vector<IDXGIAdapter*> adapters = _D3D11_Device::GetAvailableAdapters(factory);	
		SafeRelease(factory);
		if (adapters.empty())
		{
			LOG_ERROR("RHI_Device::RHI_Device: Couldn't find any adapters.");
			return;
		}

		// Save all available adapters
		DXGI_ADAPTER_DESC adapterDesc;
		for (IDXGIAdapter* displayAdapter : adapters)
		{
			if (FAILED(displayAdapter->GetDesc(&adapterDesc)))
			{
				LOG_ERROR("RHI_Device::RHI_Device: Failed to get adapter description");
				continue;
			}

			auto memoryMB = (unsigned int)(adapterDesc.DedicatedVideoMemory / 1024 / 1024);
			char name[128];
			char defChar = ' ';
			WideCharToMultiByte(CP_ACP, 0, adapterDesc.Description, -1, name, 128, &defChar, nullptr);

			Settings::Get().DisplayAdapter_Add(name, memoryMB, adapterDesc.VendorId, (void*)displayAdapter);
		}
	
		// DISPLAY MODES
		auto GetDisplayModes = [this](IDXGIAdapter* adapter)
		{
			// Enumerate the primary adapter output (monitor).
			IDXGIOutput* adapterOutput;
			auto result = adapter->EnumOutputs(0, &adapterOutput);
			if (SUCCEEDED(result))
			{
				// Get supported display mode count
				UINT displayModeCount;
				result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_format], DXGI_ENUM_MODES_INTERLACED, &displayModeCount, nullptr);
				if (SUCCEEDED(result))
				{
					// Get display modes
					vector<DXGI_MODE_DESC> displayModes;			
					displayModes.resize(displayModeCount);
					result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_format], DXGI_ENUM_MODES_INTERLACED, &displayModeCount, &displayModes[0]);
					if (SUCCEEDED(result))
					{
						// Save all the display modes
						for (const DXGI_MODE_DESC& mode : displayModes)
						{
							Settings::Get().DisplayMode_Add(mode.Width, mode.Height, mode.RefreshRate.Numerator, mode.RefreshRate.Denominator);
						}
					}
				}
				adapterOutput->Release();
			}

			if (FAILED(result))
			{
				LOGF_ERROR("RHI_Device::RHI_Device: Failed to get display modes (%s)", _D3D11_Device::DxgiErrorToString(result));
				return false;
			}

			return true;
		};

		// Get display modes and set primary adapter
		for (const auto& displayAdapter : Settings::Get().DisplayAdapters_Get())
		{
			auto adapter = (IDXGIAdapter*)displayAdapter.data;
			// Adapters are ordered by memory (descending), so stop on the first success
			if (GetDisplayModes(adapter))
			{
				Settings::Get().DisplayAdapter_SetPrimary(&displayAdapter);
				break;
			}
		}

		// SWAPCHAIN
		{
			DXGI_SWAP_CHAIN_DESC swapChainDesc;
			ZeroMemory(&swapChainDesc, sizeof(swapChainDesc));

			swapChainDesc.BufferCount					= _D3D11_Device::swapchainBufferCount;
			swapChainDesc.BufferDesc.Width				= Settings::Get().Resolution_GetWidth();
			swapChainDesc.BufferDesc.Height				= Settings::Get().Resolution_GetHeight();
			swapChainDesc.BufferDesc.Format				= d3d11_dxgi_format[m_format];
			swapChainDesc.BufferUsage					= DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swapChainDesc.OutputWindow					= (HWND)drawHandle;
			swapChainDesc.SampleDesc.Count				= 1;
			swapChainDesc.SampleDesc.Quality			= 0;
			swapChainDesc.Windowed						= (BOOL)!Settings::Get().FullScreen_Get();
			swapChainDesc.BufferDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			swapChainDesc.BufferDesc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;
			swapChainDesc.SwapEffect					= _D3D11_Device::swapEffect;
			swapChainDesc.Flags							= _D3D11_Device::swapchainFlags;

			// Create the swap chain, Direct3D device, and Direct3D device context.
			auto result = D3D11CreateDeviceAndSwapChain(
				nullptr,								// specify nullptr to use the default adapter
				_D3D11_Device::driverType,
				nullptr,								// specify nullptr because D3D_DRIVER_TYPE_HARDWARE indicates that this...
				_D3D11_Device::deviceFlags,				// ...function uses hardware, optionally set debug and Direct2D compatibility flags
				_D3D11_Device::featureLevels,
				ARRAYSIZE(_D3D11_Device::featureLevels),
				_D3D11_Device::sdkVersion,				// always set this to D3D11_SDK_VERSION
				&swapChainDesc,
				&_D3D11_Device::swapChain,
				&_D3D11_Device::device,
				nullptr,
				&_D3D11_Device::deviceContext
			);

			if (FAILED(result))
			{
				LOGF_ERROR("RHI_Device::RHI_Device: Failed to create device and swapchain, %s.", _D3D11_Device::DxgiErrorToString(result));
				return;
			}
		}

		// Enable multi-thread protection
		if (_D3D11_Device::multithreadProtection)
		{
			ID3D11Multithread* multithread = nullptr;
			if (SUCCEEDED(_D3D11_Device::deviceContext->QueryInterface(__uuidof(ID3D11Multithread), (void**)&multithread)))
			{		
				multithread->SetMultithreadProtected(TRUE);
				multithread->Release();
			}
			else 
			{
				LOGF_ERROR("RHI_Device::RHI_Device: Failed to enable multithreaded protection");
			}
		}

		// RENDER TARGET VIEW
		{
			// Get the pointer to the back buffer.
			ID3D11Texture2D* backBufferPtr;
			result = _D3D11_Device::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBufferPtr));
			if (FAILED(result))
			{
				LOGF_ERROR("RHI_Device::RHI_Device: Failed to get swapchain buffer, %s.", _D3D11_Device::DxgiErrorToString(result));
				return;
			}

			// Create the render target view with the back buffer pointer.
			result = _D3D11_Device::device->CreateRenderTargetView(backBufferPtr, nullptr, &_D3D11_Device::renderTargetView);
			SafeRelease(backBufferPtr);
			if (FAILED(result))
			{
				LOGF_ERROR("RHI_Device::RHI_Device: Failed to create swapchain render target, %s.", _D3D11_Device::DxgiErrorToString(result));
				return;
			}
		}

		// VIEWPORT
		m_viewport = make_shared<RHI_Viewport>();
		ZeroMemory(&_D3D11_Device::viewport, sizeof(D3D11_VIEWPORT));
		Set_Viewport(m_viewport);

		// DEPTH STATES
		#if REVERSE_Z == 1
		auto desc = Desc_DepthReverseEnabled();
		#else
		auto desc = Desc_DepthEnabled();
		#endif
		if (FAILED(_D3D11_Device::device->CreateDepthStencilState(&desc, &_D3D11_Device::depthStencilStateEnabled)))
		{
			LOG_ERROR("RHI_Device::RHI_Device: Failed to create depth stencil enabled state.");
			return;
		}

		desc = Desc_DepthDisabled();
		if (FAILED(_D3D11_Device::device->CreateDepthStencilState(&desc, &_D3D11_Device::depthStencilStateDisabled)))
		{
			LOG_ERROR("RHI_Device::RHI_Device: Failed to create depth stencil disabled state.");
			return;
		}

		// DEPTH STENCIL VIEW
		if (!_D3D11_Device::CreateDepthStencilView((UINT)Settings::Get().Resolution_GetWidth(), (UINT)Settings::Get().Resolution_GetHeight()))
		{
			LOG_ERROR("RHI_Device::RHI_Device: Failed to create depth stencil view.");
			return;
		}

		// RASTERIZER STATES
		{
			auto desc = Desc_RasterizerCullBack();
			if (FAILED(_D3D11_Device::device->CreateRasterizerState(&desc, &_D3D11_Device::rasterStateCullBack)))
			{
				LOG_ERROR("RHI_Device::RHI_Device: Failed to create the rasterizer cull back state.");
				return;
			}

			desc = Desc_RasterizerCullFront();
			if (FAILED(_D3D11_Device::device->CreateRasterizerState(&desc, &_D3D11_Device::rasterStateCullFront)))
			{
				LOG_ERROR("RHI_Device::RHI_Device: Failed to create the rasterizer cull front state.");
				return;
			}

			desc = Desc_RasterizerCullNone();
			if (FAILED(_D3D11_Device::device->CreateRasterizerState(&desc, &_D3D11_Device::rasterStateCullNone)))
			{
				LOG_ERROR("RHI_Device::RHI_Device: Failed to create the rasterizer cull non state.");
				return;
			}

			// Set default rasterizer state
			_D3D11_Device::deviceContext->RSSetState(_D3D11_Device::rasterStateCullBack);
		}

		// BLEND STATES
		{
			// Create a blending state with alpha blending enabled
			auto desc = Desc_BlendAlpha();
			if (FAILED(_D3D11_Device::device->CreateBlendState(&desc, &_D3D11_Device::blendStateAlphaEnabled)))
			{
				LOG_ERROR("RHI_Device::RHI_Device: Failed to create blend alpha state.");
				return;
			}

			// Create a blending state with alpha blending disabled
			desc = Desc_BlendDisabled();
			if (FAILED(_D3D11_Device::device->CreateBlendState(&desc, &_D3D11_Device::blendStateAlphaDisabled)))
			{
				LOG_ERROR("RHI_Device::RHI_Device: Failed to create blend disabled state.");
				return;
			}
		}

		// EVENT REPORTER
		_D3D11_Device::eventReporter = nullptr;
		result = _D3D11_Device::deviceContext->QueryInterface(IID_PPV_ARGS(&_D3D11_Device::eventReporter));
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::RHI_Device: Failed to create ID3DUserDefinedAnnotation for event reporting");
			return;
		}

		// Log feature level and adapter info
		D3D_FEATURE_LEVEL featureLevel = _D3D11_Device::device->GetFeatureLevel();
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

		m_device		= (void*)_D3D11_Device::device;
		m_deviceContext	= (void*)_D3D11_Device::deviceContext;
		m_initialized	= true;
	}

	RHI_Device::~RHI_Device()
	{
		// Before shutting down set to windowed mode or 
		// upon releasing the swap chain it will throw an exception.
		if (_D3D11_Device::swapChain)
		{
			_D3D11_Device::swapChain->SetFullscreenState(false, nullptr);
		}

		SafeRelease(_D3D11_Device::blendStateAlphaEnabled);
		SafeRelease(_D3D11_Device::blendStateAlphaDisabled);
		SafeRelease(_D3D11_Device::rasterStateCullFront);
		SafeRelease(_D3D11_Device::rasterStateCullBack);
		SafeRelease(_D3D11_Device::rasterStateCullNone);
		SafeRelease(_D3D11_Device::depthStencilView);
		SafeRelease(_D3D11_Device::depthStencilStateEnabled);
		SafeRelease(_D3D11_Device::depthStencilStateDisabled);
		SafeRelease(_D3D11_Device::depthStencilBuffer);
		SafeRelease(_D3D11_Device::renderTargetView);
		SafeRelease(_D3D11_Device::deviceContext);
		SafeRelease(_D3D11_Device::device);
		SafeRelease(_D3D11_Device::swapChain);
	}

	void RHI_Device::Draw(unsigned int vertexCount)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->Draw(vertexCount, 0);
		Profiler::Get().m_rhiDrawCalls++;
	}

	void RHI_Device::DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->DrawIndexed(indexCount, indexOffset, vertexOffset);
		Profiler::Get().m_rhiDrawCalls++;
	}

	void RHI_Device::ClearBackBuffer(const Vector4& color)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->ClearRenderTargetView(_D3D11_Device::renderTargetView, color.Data()); // back buffer
		if (m_depthEnabled)
		{
			float depth = _D3D11_Device::viewport.MaxDepth;
			#if REVERSE_Z == 1
			depth = 1.0f - depth;
			#endif
			_D3D11_Device::deviceContext->ClearDepthStencilView(_D3D11_Device::depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, 0); // depth buffer
		}
	}

	void RHI_Device::ClearRenderTarget(void* renderTarget, const Math::Vector4& color)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->ClearRenderTargetView((ID3D11RenderTargetView*)renderTarget, color.Data());
	}

	void RHI_Device::ClearDepthStencil(void* depthStencil, unsigned int flags, float depth, uint8_t stencil)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		#if REVERSE_Z == 1
		depth = 1.0f - depth;
		#endif

		unsigned int clearFlags = 0;
		clearFlags |= flags & Clear_Depth ? D3D11_CLEAR_DEPTH : 0;
		clearFlags |= flags & Clear_Stencil ? D3D11_CLEAR_STENCIL : 0;
		_D3D11_Device::deviceContext->ClearDepthStencilView((ID3D11DepthStencilView*)depthStencil, clearFlags, depth, stencil);
	}

	void RHI_Device::Present()
	{
		if (!_D3D11_Device::swapChain)
			return;

		_D3D11_Device::swapChain->Present(Settings::Get().VSync_Get(), 0);
	}

	void RHI_Device::Set_BackBufferAsRenderTarget()
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->OMSetRenderTargets(1, &_D3D11_Device::renderTargetView, m_depthEnabled ? _D3D11_Device::depthStencilView : nullptr);
	}

	void RHI_Device::Set_VertexShader(void* buffer)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->VSSetShader((ID3D11VertexShader*)buffer, nullptr, 0);
	}

	void RHI_Device::Set_PixelShader(void* buffer)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->PSSetShader((ID3D11PixelShader*)buffer, nullptr, 0);
	}

	void RHI_Device::Set_ConstantBuffers(unsigned int startSlot, unsigned int bufferCount, Buffer_Scope scope, void* const* buffer)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		auto d3d11buffer = (ID3D11Buffer*const*)buffer;
		if (scope == Buffer_VertexShader || scope == Buffer_Global)
		{
			_D3D11_Device::deviceContext->VSSetConstantBuffers(startSlot, bufferCount, d3d11buffer);
		}

		if (scope == Buffer_PixelShader || scope == Buffer_Global)
		{
			_D3D11_Device::deviceContext->PSSetConstantBuffers(startSlot, bufferCount, d3d11buffer);
		}
	}

	void RHI_Device::Set_Samplers(unsigned int startSlot, unsigned int samplerCount, void* const* samplers)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->PSSetSamplers(startSlot, samplerCount, (ID3D11SamplerState* const*)samplers);
	}

	void RHI_Device::Set_RenderTargets(unsigned int renderTargetCount, void* const* renderTargets, void* depthStencil)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->OMSetRenderTargets(renderTargetCount, (ID3D11RenderTargetView* const*)renderTargets, (ID3D11DepthStencilView*)depthStencil);
	}

	void RHI_Device::Set_Textures(unsigned int startSlot, unsigned int resourceCount, void* const* shaderResources)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->PSSetShaderResources(startSlot, resourceCount, (ID3D11ShaderResourceView* const*)shaderResources);
	}

	bool RHI_Device::Set_Resolution(unsigned int width, unsigned int height)
	{
		if (width == 0 || height == 0)
		{
			LOGF_ERROR("RHI_Device::SetResolution: Resolution %fx%f is invalid", width, height);
			return false;
		}

		if (!_D3D11_Device::swapChain)
		{
			LOG_ERROR("RHI_Device::SetResolution: Invalid swapchain");
			return false;
		}

		const DisplayMode& displayMode = Settings::Get().DisplayMode_GetFastest();

		// Release resolution based stuff
		SafeRelease(_D3D11_Device::renderTargetView);
		SafeRelease(_D3D11_Device::depthStencilBuffer);
		SafeRelease(_D3D11_Device::depthStencilView);

		// Resize swapchain target
		DXGI_MODE_DESC dxgiModeDesc;
		ZeroMemory(&dxgiModeDesc, sizeof(dxgiModeDesc));
		dxgiModeDesc.Width				= width;
		dxgiModeDesc.Height				= height;
		dxgiModeDesc.Format				= d3d11_dxgi_format[m_format];
		dxgiModeDesc.RefreshRate		= DXGI_RATIONAL{ displayMode.refreshRateNumerator, displayMode.refreshRateDenominator };
		dxgiModeDesc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;
		dxgiModeDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

		auto result = _D3D11_Device::swapChain->ResizeTarget(&dxgiModeDesc);
		if (FAILED(result))
		{
			LOGF_ERROR("RHI_Device::SetResolution: Failed to resize swapchain target, %s.", _D3D11_Device::DxgiErrorToString(result));
			return false;
		}

		// Resize swapchain buffers
		result = _D3D11_Device::swapChain->ResizeBuffers(
			_D3D11_Device::swapchainBufferCount,
			width,
			height,
			dxgiModeDesc.Format,
			_D3D11_Device::swapchainFlags
		);
		if (FAILED(result))
		{
			LOGF_ERROR("RHI_Device::SetResolution: Failed to resize swapchain buffers, %s.", _D3D11_Device::DxgiErrorToString(result));
			return false;
		}

		// Get the swapchain buffer
		ID3D11Texture2D* backBuffer = nullptr;	
		result = _D3D11_Device::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (FAILED(result))
		{
			LOGF_ERROR("RHI_Device::SetResolution: Failed to get pointer to the swapchain's back buffer, %s.", _D3D11_Device::DxgiErrorToString(result));
			return false;
		}
		SafeRelease(backBuffer);

		// Create render target view
		result = _D3D11_Device::device->CreateRenderTargetView(backBuffer, nullptr, &_D3D11_Device::renderTargetView);
		if (FAILED(result))
		{
			LOGF_ERROR("RHI_Device::SetResolution:  Failed to create render target view, %s.", _D3D11_Device::DxgiErrorToString(result));
			return false;
		}

		// Re-create depth stencil view
		_D3D11_Device::CreateDepthStencilView(width, height);

		return true;
	}

	void RHI_Device::Set_Viewport(const shared_ptr<RHI_Viewport>& viewport)
	{
		if (!viewport)
		{
			LOG_WARNING("RHI_Device::Set_Viewport: Invalid parameter");
			return;
		}

		if (!_D3D11_Device::deviceContext)
			return;

		m_viewport = viewport;
		_D3D11_Device::viewport.TopLeftX	= viewport->GetTopLeftX();
		_D3D11_Device::viewport.TopLeftY	= viewport->GetTopLeftY();
		_D3D11_Device::viewport.Width		= viewport->GetWidth();
		_D3D11_Device::viewport.Height		= viewport->GetHeight();
		_D3D11_Device::viewport.MinDepth	= viewport->GetMinDepth();
		_D3D11_Device::viewport.MaxDepth	= viewport->GetMaxDepth();
		_D3D11_Device::deviceContext->RSSetViewports(1, &_D3D11_Device::viewport);
	}

	bool RHI_Device::Set_DepthEnabled(bool enable)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_WARNING("RHI_Device::EnableDepth: Device context is uninitialized.");
			return false;
		}

		if (m_depthEnabled == enable)
			return true;

		_D3D11_Device::deviceContext->OMSetDepthStencilState(enable ? _D3D11_Device::depthStencilStateEnabled : _D3D11_Device::depthStencilStateDisabled, 1);
		m_depthEnabled = enable;

		return true;
	}

	bool RHI_Device::Set_AlphaBlendingEnabled(bool enable)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_WARNING("RHI_Device::EnableAlphaBlending: Device context is uninitialized.");
			return false;
		}

		// Set blend state
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		_D3D11_Device::deviceContext->OMSetBlendState(enable ? _D3D11_Device::blendStateAlphaEnabled : _D3D11_Device::blendStateAlphaDisabled, blendFactor, 0xffffffff);

		return true;
	}

	void RHI_Device::EventBegin(const std::string& name)
	{
		#ifdef DEBUG
		auto s2ws = [](const std::string& s)
		{
			int len;
			int slength = (int)s.length() + 1;
			len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
			auto buf = new wchar_t[len];
			MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
			std::wstring r(buf);
			delete[] buf;
			return r;
		};

		_D3D11_Device::eventReporter->BeginEvent(s2ws(name).c_str());
		#endif
	}

	void RHI_Device::EventEnd()
	{
		#ifdef DEBUG
		_D3D11_Device::eventReporter->EndEvent();
		#endif
	}

	bool RHI_Device::Profiling_CreateQuery(void** query, Query_Type type)
	{
		if (!_D3D11_Device::device)
			return false;

		D3D11_QUERY_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Query		= (type == Query_Timestamp_Disjoint) ? D3D11_QUERY_TIMESTAMP_DISJOINT : D3D11_QUERY_TIMESTAMP;
		desc.MiscFlags	= 0;
		auto result		= _D3D11_Device::device->CreateQuery(&desc, (ID3D11Query**)query);
		if (FAILED(result))
		{
			LOG_ERROR("RHI_Device::Profiling_CreateQuery: Failed to create ID3D11Query");
			return false;
		}

		return true;
	}

	void RHI_Device::Profiling_QueryStart(void* queryObject)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->Begin((ID3D11Query*)queryObject);
	}

	void RHI_Device::Profiling_QueryEnd(void* queryObject)
	{ 
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->End((ID3D11Query*)queryObject);
	}

	void RHI_Device::Profiling_GetTimeStamp(void* queryObject)
	{
		if (!_D3D11_Device::deviceContext)
			return;

		_D3D11_Device::deviceContext->End((ID3D11Query*)queryObject);
	}

	float RHI_Device::Profiling_GetDuration(void* queryDisjoint, void* queryStart, void* queryEnd)
	{
		if (!_D3D11_Device::deviceContext)
			return 0.0f;

		// Wait for data to be available	
		while (_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryDisjoint, NULL, 0, 0) == S_FALSE){}

		// Check whether timestamps were disjoint during the last frame
		D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
		_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryDisjoint, &disjointData, sizeof(disjointData), 0);
		if (disjointData.Disjoint)
			return 0.0f;

		// Get the query data		
		UINT64 startTime	= 0;
		UINT64 endTime		= 0;
		_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryStart, &startTime, sizeof(startTime), 0);
		_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryEnd,	&endTime, sizeof(endTime), 0);

		// Compute delta in milliseconds
		UINT64 delta		= endTime - startTime;
		float durationMs	= (delta * 1000.0f) / (float)disjointData.Frequency;

		return durationMs;
	}

	bool RHI_Device::Set_PrimitiveTopology(PrimitiveTopology_Mode primitiveTopology)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR("D3D11_Device::Set_InputLayout: Invalid device context");
			return false;
		}

		// Set primitive topology
		_D3D11_Device::deviceContext->IASetPrimitiveTopology(d3d11_primitive_topology[primitiveTopology]);
		return true;
	}

	bool RHI_Device::Set_FillMode(Fill_Mode fillMode)
	{
		return true;
	}

	bool RHI_Device::Set_InputLayout(void* inputLayout)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR("D3D11_Device::Set_InputLayout: Invalid device context");
			return false;
		}

		_D3D11_Device::deviceContext->IASetInputLayout((ID3D11InputLayout*)inputLayout);
		return true;
	}

	bool RHI_Device::Set_CullMode(Cull_Mode cullMode)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_WARNING("D3D11_Device::SetCullMode: Device context is uninitialized.");
			return false;
		}

		if (cullMode == Cull_Mode::Cull_None)
		{
			_D3D11_Device::deviceContext->RSSetState(_D3D11_Device::rasterStateCullNone);
		}
		else if (cullMode == Cull_Mode::Cull_Front)
		{
			_D3D11_Device::deviceContext->RSSetState(_D3D11_Device::rasterStateCullFront);
		}
		else if (cullMode == Cull_Mode::Cull_Back)
		{
			_D3D11_Device::deviceContext->RSSetState(_D3D11_Device::rasterStateCullBack);
		}

		return true;
	}
}
#endif