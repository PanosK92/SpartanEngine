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
#include "../RHI_BlendState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../../Logging/Log.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Profiling/Profiler.h"
#include "../../Core/Settings.h"
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
		const static bool multithreadProtection		= false;		
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
		ID3DUserDefinedAnnotation* eventReporter;	

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
		m_backBufferFormat		= Format_R8G8B8A8_UNORM;
		m_initialized			= false;

		if (!IsWindow((HWND)drawHandle))
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Create DirectX graphics interface factory
		IDXGIFactory* factory;
		HRESULT result = CreateDXGIFactory(IID_PPV_ARGS(&factory));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create a DirectX graphics interface factory, %s.", D3D11_Common::DxgiErrorToString(result));
			return;
		}

		// Get all available adapters
		vector<IDXGIAdapter*> adapters = _D3D11_Device::GetAvailableAdapters(factory);	
		SafeRelease(factory);
		if (adapters.empty())
		{
			LOG_ERROR("Couldn't find any adapters.");
			return;
		}

		// Save all available adapters
		DXGI_ADAPTER_DESC adapterDesc;
		for (IDXGIAdapter* displayAdapter : adapters)
		{
			if (FAILED(displayAdapter->GetDesc(&adapterDesc)))
			{
				LOG_ERROR("Failed to get adapter description");
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
				result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_backBufferFormat], DXGI_ENUM_MODES_INTERLACED, &displayModeCount, nullptr);
				if (SUCCEEDED(result))
				{
					// Get display modes
					vector<DXGI_MODE_DESC> displayModes;			
					displayModes.resize(displayModeCount);
					result = adapterOutput->GetDisplayModeList(d3d11_dxgi_format[m_backBufferFormat], DXGI_ENUM_MODES_INTERLACED, &displayModeCount, &displayModes[0]);
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
				LOGF_ERROR("Failed to get display modes (%s)", D3D11_Common::DxgiErrorToString(result));
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
			swapChainDesc.BufferDesc.Width				= Settings::Get().GetWindowWidth();
			swapChainDesc.BufferDesc.Height				= Settings::Get().GetWindowHeight();
			swapChainDesc.BufferDesc.Format				= d3d11_dxgi_format[m_backBufferFormat];
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
				LOGF_ERROR("Failed to create device and swapchain, %s.", D3D11_Common::DxgiErrorToString(result));
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
				LOGF_ERROR("Failed to enable multi-threaded protection");
			}
		}

		// RENDER TARGET VIEW
		{
			// Get the pointer to the back buffer.
			ID3D11Texture2D* backBufferPtr;
			result = _D3D11_Device::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBufferPtr));
			if (FAILED(result))
			{
				LOGF_ERROR("buffer, %s.", D3D11_Common::DxgiErrorToString(result));
				return;
			}

			// Create the render target view with the back buffer pointer.
			result = _D3D11_Device::device->CreateRenderTargetView(backBufferPtr, nullptr, &_D3D11_Device::renderTargetView);
			SafeRelease(backBufferPtr);
			if (FAILED(result))
			{
				LOGF_ERROR("Failed to create swapchain render target, %s.", D3D11_Common::DxgiErrorToString(result));
				return;
			}
		}

		// EVENT REPORTER
		_D3D11_Device::eventReporter = nullptr;
		result = _D3D11_Device::deviceContext->QueryInterface(IID_PPV_ARGS(&_D3D11_Device::eventReporter));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create ID3DUserDefinedAnnotation for event reporting, %s.", D3D11_Common::DxgiErrorToString(result));
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

		SafeRelease(_D3D11_Device::renderTargetView);	
		SafeRelease(_D3D11_Device::swapChain);
		SafeRelease(_D3D11_Device::deviceContext);
		SafeRelease(_D3D11_Device::device);
	}

	bool RHI_Device::Draw(unsigned int vertexCount)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (vertexCount == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		_D3D11_Device::deviceContext->Draw(vertexCount, 0);
		return true;
	}

	bool RHI_Device::DrawIndexed(unsigned int indexCount, unsigned int indexOffset, unsigned int vertexOffset)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (indexCount == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		_D3D11_Device::deviceContext->DrawIndexed(indexCount, indexOffset, vertexOffset);
		return true;
	}

	bool RHI_Device::Present()
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::swapChain->Present(Settings::Get().VSync_Get(), 0);
		return true;
	}

	bool RHI_Device::ClearBackBuffer(const Vector4& color)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->ClearRenderTargetView(_D3D11_Device::renderTargetView, color.Data());
		return true;
	}

	bool RHI_Device::ClearRenderTarget(void* renderTarget, const Math::Vector4& color)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->ClearRenderTargetView((ID3D11RenderTargetView*)renderTarget, color.Data());
		return true;
	}

	bool RHI_Device::ClearDepthStencil(void* depthStencil, unsigned int flags, float depth, unsigned int stencil)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		unsigned int clearFlags = 0;
		clearFlags |= flags & Clear_Depth	? D3D11_CLEAR_DEPTH		: 0;
		clearFlags |= flags & Clear_Stencil ? D3D11_CLEAR_STENCIL	: 0;
		_D3D11_Device::deviceContext->ClearDepthStencilView((ID3D11DepthStencilView*)depthStencil, clearFlags, depth, stencil);
		return true;
	}

	bool RHI_Device::SetBackBufferAsRenderTarget()
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		ID3D11DepthStencilView* depthStencil = nullptr;
		_D3D11_Device::deviceContext->OMSetRenderTargets(1, &_D3D11_Device::renderTargetView, depthStencil);
		return true;
	}

	bool RHI_Device::SetVertexBuffer(const std::shared_ptr<RHI_VertexBuffer>& buffer)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!buffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto ptr			= (ID3D11Buffer*)buffer->GetBuffer();
		unsigned int stride = buffer->GetStride();
		unsigned int offset = 0;
		_D3D11_Device::deviceContext->IASetVertexBuffers(0, 1, &ptr, &stride, &offset);
		return true;
	}

	bool RHI_Device::SetIndexBuffer(const std::shared_ptr<RHI_IndexBuffer>& buffer)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!buffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto ptr	= (ID3D11Buffer*)buffer->GetBuffer();
		auto format = d3d11_dxgi_format[buffer->GetFormat()];
		_D3D11_Device::deviceContext->IASetIndexBuffer(ptr, format, 0);

		return true;
	}

	bool RHI_Device::SetVertexShader(const std::shared_ptr<RHI_Shader>& shader)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!shader)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto ptr = (ID3D11VertexShader*)shader->GetVertexShaderBuffer();
		_D3D11_Device::deviceContext->VSSetShader(ptr, nullptr, 0);
		return true;
	}

	bool RHI_Device::SetPixelShader(const std::shared_ptr<RHI_Shader>& shader)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!shader)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto ptr = (ID3D11PixelShader*)shader->GetPixelShaderBuffer();
		_D3D11_Device::deviceContext->PSSetShader(ptr, nullptr, 0);
		return true;
	}

	bool RHI_Device::SetConstantBuffers(unsigned int startSlot, unsigned int bufferCount, void* buffer, RHI_Buffer_Scope scope)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto d3d11buffer = (ID3D11Buffer*const*)buffer;
		if (scope == Buffer_VertexShader || scope == Buffer_Global)
		{
			_D3D11_Device::deviceContext->VSSetConstantBuffers(startSlot, bufferCount, d3d11buffer);
		}

		if (scope == Buffer_PixelShader || scope == Buffer_Global)
		{
			_D3D11_Device::deviceContext->PSSetConstantBuffers(startSlot, bufferCount, d3d11buffer);
		}

		return true;
	}

	bool RHI_Device::SetSamplers(unsigned int startSlot, unsigned int samplerCount, void* samplers)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->PSSetSamplers(startSlot, samplerCount, (ID3D11SamplerState* const*)samplers);
		return true;
	}

	bool RHI_Device::SetRenderTargets(unsigned int renderTargetCount, void* renderTargets, void* depthStencil)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->OMSetRenderTargets(renderTargetCount, (ID3D11RenderTargetView* const*)renderTargets, (ID3D11DepthStencilView*)depthStencil);
		return true;
	}

	bool RHI_Device::SetTextures(unsigned int startSlot, unsigned int resourceCount, void* shaderResources)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->PSSetShaderResources(startSlot, resourceCount, (ID3D11ShaderResourceView* const*)shaderResources);
		return true;
	}

	bool RHI_Device::SetResolution(unsigned int width, unsigned int height)
	{
		if (width == 0 || height == 0)
		{
			LOGF_ERROR("Resolution %fx%f is invalid.", width, height);
			return false;
		}

		if (!_D3D11_Device::swapChain)
		{
			LOG_ERROR("Invalid swapchain.");
			return false;
		}

		DisplayMode displayMode;
		if (!Settings::Get().DisplayMode_GetFastest(&displayMode))
		{
			LOG_ERROR("Failed to get a display mode.");
			return false;
		}

		// Release resolution based stuff
		SafeRelease(_D3D11_Device::renderTargetView);

		// Resize swapchain target
		DXGI_MODE_DESC dxgiModeDesc;
		ZeroMemory(&dxgiModeDesc, sizeof(dxgiModeDesc));
		dxgiModeDesc.Width				= width;
		dxgiModeDesc.Height				= height;
		dxgiModeDesc.Format				= d3d11_dxgi_format[m_backBufferFormat];
		dxgiModeDesc.RefreshRate		= DXGI_RATIONAL{ displayMode.refreshRateNumerator, displayMode.refreshRateDenominator };
		dxgiModeDesc.Scaling			= DXGI_MODE_SCALING_UNSPECIFIED;
		dxgiModeDesc.ScanlineOrdering	= DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;

		auto result = _D3D11_Device::swapChain->ResizeTarget(&dxgiModeDesc);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to resize swapchain target, %s.", D3D11_Common::DxgiErrorToString(result));
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
			LOGF_ERROR("Failed to resize swapchain buffers, %s.", D3D11_Common::DxgiErrorToString(result));
			return false;
		}

		// Get the swapchain buffer
		ID3D11Texture2D* backBuffer = nullptr;	
		result = _D3D11_Device::swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to get pointer to the swapchain's back buffer, %s.", D3D11_Common::DxgiErrorToString(result));
			return false;
		}
		SafeRelease(backBuffer);

		// Create render target view
		result = _D3D11_Device::device->CreateRenderTargetView(backBuffer, nullptr, &_D3D11_Device::renderTargetView);
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create render target view, %s.", D3D11_Common::DxgiErrorToString(result));
			return false;
		}

		return true;
	}

	bool RHI_Device::SetViewport(const RHI_Viewport& viewport)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		D3D11_VIEWPORT dx_viewport;
		dx_viewport.TopLeftX	= viewport.GetX();
		dx_viewport.TopLeftY	= viewport.GetY();
		dx_viewport.Width		= viewport.GetWidth();
		dx_viewport.Height		= viewport.GetHeight();
		dx_viewport.MinDepth	= viewport.GetMinDepth();
		dx_viewport.MaxDepth	= viewport.GetMaxDepth();
		_D3D11_Device::deviceContext->RSSetViewports(1, &dx_viewport);

		return true;
	}

	bool RHI_Device::SetScissorRectangle(int left, int top, int right, int bottom)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		const D3D11_RECT rectangle = { (LONG)left, (LONG)top, (LONG)right, (LONG)bottom };
		_D3D11_Device::deviceContext->RSSetScissorRects(1, &rectangle);

		return true;
	}

	bool RHI_Device::SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depthStencilState)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto ptr = (ID3D11DepthStencilState*)depthStencilState->GetBuffer();
		_D3D11_Device::deviceContext->OMSetDepthStencilState(ptr, 1);
		return true;
	}

	bool RHI_Device::SetBlendState(const std::shared_ptr<RHI_BlendState>& blendState)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!blendState)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Set blend state
		auto ptr = (ID3D11BlendState*)blendState->GetBuffer();
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		_D3D11_Device::deviceContext->OMSetBlendState(ptr, blendFactor, 0xffffffff);

		return true;
	}

	bool RHI_Device::SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitiveTopology)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Set primitive topology
		_D3D11_Device::deviceContext->IASetPrimitiveTopology(d3d11_primitive_topology[primitiveTopology]);
		return true;
	}

	bool RHI_Device::SetInputLayout(const std::shared_ptr<RHI_InputLayout>& inputLayout)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!inputLayout)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto ptr = (ID3D11InputLayout*)inputLayout->GetBuffer();
		_D3D11_Device::deviceContext->IASetInputLayout(ptr);
		return true;
	}

	bool RHI_Device::SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizerState)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!rasterizerState)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto ptr = (ID3D11RasterizerState*)rasterizerState->GetBuffer();
		_D3D11_Device::deviceContext->RSSetState(ptr);
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

	bool RHI_Device::Profiling_CreateQuery(void** query, RHI_Query_Type type)
	{
		if (!_D3D11_Device::device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		D3D11_QUERY_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Query = (type == Query_Timestamp_Disjoint) ? D3D11_QUERY_TIMESTAMP_DISJOINT : D3D11_QUERY_TIMESTAMP;
		desc.MiscFlags = 0;
		auto result = _D3D11_Device::device->CreateQuery(&desc, (ID3D11Query**)query);
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create ID3D11Query");
			return false;
		}

		return true;
	}

	bool RHI_Device::Profiling_QueryStart(void* queryObject)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->Begin((ID3D11Query*)queryObject);
		return true;
	}

	bool RHI_Device::Profiling_QueryEnd(void* queryObject)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->End((ID3D11Query*)queryObject);
		return true;
	}

	bool RHI_Device::Profiling_GetTimeStamp(void* queryObject)
	{
		if (!_D3D11_Device::deviceContext)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::deviceContext->End((ID3D11Query*)queryObject);
		return true;
	}

	float RHI_Device::Profiling_GetDuration(void* queryDisjoint, void* queryStart, void* queryEnd)
	{
		if (!_D3D11_Device::deviceContext)
			return 0.0f;

		// Wait for data to be available	
		while (_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryDisjoint, NULL, 0, 0) == S_FALSE) {}

		// Check whether timestamps were disjoint during the last frame
		D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjointData;
		_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryDisjoint, &disjointData, sizeof(disjointData), 0);
		if (disjointData.Disjoint)
			return 0.0f;

		// Get the query data		
		UINT64 startTime = 0;
		UINT64 endTime = 0;
		_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryStart, &startTime, sizeof(startTime), 0);
		_D3D11_Device::deviceContext->GetData((ID3D11Query*)queryEnd, &endTime, sizeof(endTime), 0);

		// Compute delta in milliseconds
		UINT64 delta		= endTime - startTime;
		float durationMs	= (delta * 1000.0f) / (float)disjointData.Frequency;

		return durationMs;
	}
}
#endif