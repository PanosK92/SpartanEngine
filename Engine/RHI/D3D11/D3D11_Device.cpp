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
#include "../RHI_Viewport.h"
#ifdef API_D3D11
//================================

//= INCLUDES ========================
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
#include "../../Profiling/Profiler.h"
#include "../../Core/Settings.h"
#include "../../Math/Rectangle.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	namespace _D3D11_Device
	{
		const static bool multithread_protection = false;
		// All the pointers that we need
		ID3D11Device* device;
		ID3D11DeviceContext* device_context;
		ID3DUserDefinedAnnotation* annotation;
	}

	RHI_Device::RHI_Device(void* draw_handle)
	{
		if (!IsWindow(static_cast<HWND>(draw_handle)))
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		// Create device
		{
			// Flags
			UINT device_flags = 0;
			#ifdef DEBUG // Enable debug layer
			device_flags |= D3D11_CREATE_DEVICE_DEBUG;
			#endif

			// The order of the feature levels that we'll try to create a device with
			vector<D3D_FEATURE_LEVEL> feature_levels =
			{
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_11_0,
				D3D_FEATURE_LEVEL_10_1,
				D3D_FEATURE_LEVEL_10_0,
				D3D_FEATURE_LEVEL_9_3,
				D3D_FEATURE_LEVEL_9_1
			};

			// Create the swap chain, Direct3D device, and Direct3D device context.
			const auto result = D3D11CreateDevice(
				nullptr,									// pAdapter: nullptr to use the default adapter
				D3D_DRIVER_TYPE_HARDWARE,					// DriverType
				nullptr,									// HMODULE: nullptr because DriverType = D3D_DRIVER_TYPE_HARDWARE
				device_flags,								// Flags
				feature_levels.data(),						// pFeatureLevels
				static_cast<UINT>(feature_levels.size()),	// FeatureLevels
				D3D11_SDK_VERSION,							// SDKVersion
				&_D3D11_Device::device,						// ppDevice
				nullptr,									// pFeatureLevel
				&_D3D11_Device::device_context				// ppImmediateContext
			);

			if (FAILED(result))
			{
				LOGF_ERROR("Failed to create device, %s.", D3D11_Common::DxgiErrorToString(result));
				return;
			}
		}

		// Log feature level
		{
			const auto feature_level = _D3D11_Device::device->GetFeatureLevel();
			string feature_level_str;
			switch (feature_level)
			{
			case D3D_FEATURE_LEVEL_9_1:
				feature_level_str = "9.1";
				break;

			case D3D_FEATURE_LEVEL_9_2:
				feature_level_str = "9.2";
				break;

			case D3D_FEATURE_LEVEL_9_3:
				feature_level_str = "9.3";
				break;

			case D3D_FEATURE_LEVEL_10_0:
				feature_level_str = "10.0";
				break;

			case D3D_FEATURE_LEVEL_10_1:
				feature_level_str = "10.1";
				break;

			case D3D_FEATURE_LEVEL_11_0:
				feature_level_str = "11.0";
				break;

			case D3D_FEATURE_LEVEL_11_1:
				feature_level_str = "11.1";
				break;
			case D3D_FEATURE_LEVEL_12_0: break;
			case D3D_FEATURE_LEVEL_12_1: break;
			default: ;
			}
			Settings::Get().m_versionGraphicsAPI = "DirectX %s" + feature_level_str;
			LOG_INFO(Settings::Get().m_versionGraphicsAPI);
		}

		// Multi-thread protection
		if (_D3D11_Device::multithread_protection)
		{
			ID3D11Multithread* multithread = nullptr;
			if (SUCCEEDED(_D3D11_Device::device_context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&multithread))))
			{		
				multithread->SetMultithreadProtected(TRUE);
				multithread->Release();
			}
			else 
			{
				LOGF_ERROR("Failed to enable multi-threaded protection");
			}
		}

		// Annotations
		_D3D11_Device::annotation	= nullptr;
		const auto result			= _D3D11_Device::device_context->QueryInterface(IID_PPV_ARGS(&_D3D11_Device::annotation));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create ID3DUserDefinedAnnotation for event reporting, %s.", D3D11_Common::DxgiErrorToString(result));
			return;
		}

		m_device		= static_cast<void*>(_D3D11_Device::device);
		m_device_context	= static_cast<void*>(_D3D11_Device::device_context);
		m_initialized	= true;
	}

	RHI_Device::~RHI_Device()
	{
		safe_release(_D3D11_Device::device_context);
		safe_release(_D3D11_Device::device);
		safe_release(_D3D11_Device::annotation);
	}

	bool RHI_Device::Draw(unsigned int vertex_count) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (vertex_count == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		_D3D11_Device::device_context->Draw(vertex_count, 0);
		return true;
	}

	bool RHI_Device::DrawIndexed(const unsigned int index_count, const unsigned int index_offset, const unsigned int vertex_offset) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (index_count == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		_D3D11_Device::device_context->DrawIndexed(index_count, index_offset, vertex_offset);
		return true;
	}

	bool RHI_Device::ClearRenderTarget(void* render_target, const Vector4& color) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::device_context->ClearRenderTargetView(static_cast<ID3D11RenderTargetView*>(render_target), color.Data());
		return true;
	}

	bool RHI_Device::ClearDepthStencil(void* depth_stencil, const unsigned int flags, const float depth, const unsigned int stencil) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		unsigned int clear_flags = 0;
		clear_flags |= flags & Clear_Depth	? D3D11_CLEAR_DEPTH		: 0;
		clear_flags |= flags & Clear_Stencil ? D3D11_CLEAR_STENCIL	: 0;
		_D3D11_Device::device_context->ClearDepthStencilView(static_cast<ID3D11DepthStencilView*>(depth_stencil), clear_flags, depth, stencil);
		return true;
	}

	bool RHI_Device::SetVertexBuffer(const std::shared_ptr<RHI_VertexBuffer>& buffer) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!buffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto ptr			= static_cast<ID3D11Buffer*>(buffer->GetBuffer());
		auto stride			= buffer->GetStride();
		unsigned int offset = 0;
		_D3D11_Device::device_context->IASetVertexBuffers(0, 1, &ptr, &stride, &offset);
		return true;
	}

	bool RHI_Device::SetIndexBuffer(const std::shared_ptr<RHI_IndexBuffer>& buffer) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!buffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		const auto ptr		= static_cast<ID3D11Buffer*>(buffer->GetBuffer());
		const auto format	= d3d11_dxgi_format[buffer->GetFormat()];
		_D3D11_Device::device_context->IASetIndexBuffer(ptr, format, 0);

		return true;
	}

	bool RHI_Device::SetVertexShader(const std::shared_ptr<RHI_Shader>& shader) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!shader)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		const auto ptr = static_cast<ID3D11VertexShader*>(shader->GetVertexShaderBuffer());
		_D3D11_Device::device_context->VSSetShader(ptr, nullptr, 0);
		return true;
	}

	bool RHI_Device::SetPixelShader(const std::shared_ptr<RHI_Shader>& shader) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!shader)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		const auto ptr = static_cast<ID3D11PixelShader*>(shader->GetPixelShaderBuffer());
		_D3D11_Device::device_context->PSSetShader(ptr, nullptr, 0);
		return true;
	}

	bool RHI_Device::SetConstantBuffers(const unsigned int start_slot, const unsigned int buffer_count, void* buffer, const RHI_Buffer_Scope scope) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		const auto d3_d11_buffer = static_cast<ID3D11Buffer*const*>(buffer);
		if (scope == Buffer_VertexShader || scope == Buffer_Global)
		{
			_D3D11_Device::device_context->VSSetConstantBuffers(start_slot, buffer_count, d3_d11_buffer);
		}

		if (scope == Buffer_PixelShader || scope == Buffer_Global)
		{
			_D3D11_Device::device_context->PSSetConstantBuffers(start_slot, buffer_count, d3_d11_buffer);
		}

		return true;
	}

	bool RHI_Device::SetSamplers(const unsigned int start_slot, const unsigned int sampler_count, void* samplers) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::device_context->PSSetSamplers(start_slot, sampler_count, static_cast<ID3D11SamplerState* const*>(samplers));
		return true;
	}

	bool RHI_Device::SetRenderTargets(const unsigned int render_target_count, void* render_targets, void* depth_stencil) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::device_context->OMSetRenderTargets(render_target_count, static_cast<ID3D11RenderTargetView* const*>(render_targets), static_cast<ID3D11DepthStencilView*>(depth_stencil));
		return true;
	}

	bool RHI_Device::SetTextures(const unsigned int start_slot, const unsigned int resource_count, void* shader_resources) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::device_context->PSSetShaderResources(start_slot, resource_count, static_cast<ID3D11ShaderResourceView* const*>(shader_resources));
		return true;
	}

	bool RHI_Device::SetViewport(const RHI_Viewport& viewport) const
	{
		if (!_D3D11_Device::device_context)
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
		_D3D11_Device::device_context->RSSetViewports(1, &dx_viewport);

		return true;
	}

	bool RHI_Device::SetScissorRectangle(const Math::Rectangle& rectangle) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		const auto left						= rectangle.x;
		const auto top						= rectangle.y;
		const auto right					= rectangle.x + rectangle.width;
		const auto bottom					= rectangle.y + rectangle.height;
		const D3D11_RECT d3d11_rectangle	= { static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom) };

		_D3D11_Device::device_context->RSSetScissorRects(1, &d3d11_rectangle);

		return true;
	}

	bool RHI_Device::SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depth_stencil_state) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto ptr = static_cast<ID3D11DepthStencilState*>(depth_stencil_state->GetBuffer());
		_D3D11_Device::device_context->OMSetDepthStencilState(ptr, 1);
		return true;
	}

	bool RHI_Device::SetBlendState(const std::shared_ptr<RHI_BlendState>& blend_state) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!blend_state)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Set blend state
		const auto ptr			= static_cast<ID3D11BlendState*>(blend_state->GetBuffer());
		float blend_fentity[4]	= { 0.0f, 0.0f, 0.0f, 0.0f };
		_D3D11_Device::device_context->OMSetBlendState(ptr, blend_fentity, 0xffffffff);

		return true;
	}

	bool RHI_Device::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Set primitive topology
		_D3D11_Device::device_context->IASetPrimitiveTopology(d3d11_primitive_topology[primitive_topology]);
		return true;
	}

	bool RHI_Device::SetInputLayout(const std::shared_ptr<RHI_InputLayout>& input_layout) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!input_layout)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		const auto ptr = static_cast<ID3D11InputLayout*>(input_layout->GetBuffer());
		_D3D11_Device::device_context->IASetInputLayout(ptr);
		return true;
	}

	bool RHI_Device::SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizer_state) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (!rasterizer_state)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		const auto ptr = static_cast<ID3D11RasterizerState*>(rasterizer_state->GetBuffer());
		_D3D11_Device::device_context->RSSetState(ptr);
		return true;
	}

	void RHI_Device::EventBegin(const std::string& name)
	{
	#ifdef DEBUG
		const auto s2ws = [](const std::string& s)
		{
			const auto slength = static_cast<int>(s.length()) + 1;
			const auto len = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, nullptr, 0);
			const auto buf = new wchar_t[len];
			MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
			std::wstring r(buf);
			delete[] buf;
			return r;
		};

		_D3D11_Device::annotation->BeginEvent(s2ws(name).c_str());
	#endif
	}

	void RHI_Device::EventEnd()
	{
	#ifdef DEBUG
		_D3D11_Device::annotation->EndEvent();
	#endif
	}

	bool RHI_Device::ProfilingCreateQuery(void** query, const RHI_Query_Type type) const
	{
		if (!_D3D11_Device::device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		D3D11_QUERY_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Query			= (type == Query_Timestamp_Disjoint) ? D3D11_QUERY_TIMESTAMP_DISJOINT : D3D11_QUERY_TIMESTAMP;
		desc.MiscFlags		= 0;
		const auto result	= _D3D11_Device::device->CreateQuery(&desc, reinterpret_cast<ID3D11Query**>(query));
		if (FAILED(result))
		{
			LOG_ERROR("Failed to create ID3D11Query");
			return false;
		}

		return true;
	}

	bool RHI_Device::ProfilingQueryStart(void* query_object) const
	{
		if (!query_object)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::device_context->Begin(static_cast<ID3D11Query*>(query_object));
		return true;
	}

	bool RHI_Device::ProfilingQueryEnd(void* query_object) const
	{
		if (!query_object)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::device_context->End(static_cast<ID3D11Query*>(query_object));
		return true;
	}

	bool RHI_Device::ProfilingGetTimeStamp(void* query_disjoint) const
	{
		if (!query_disjoint)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		_D3D11_Device::device_context->End(static_cast<ID3D11Query*>(query_disjoint));
		return true;
	}

	float RHI_Device::ProfilingGetDuration(void* query_disjoint, void* query_start, void* query_end) const
	{
		if (!_D3D11_Device::device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return 0.0f;
		}

		// Wait for data to be available	
		while (_D3D11_Device::device_context->GetData(static_cast<ID3D11Query*>(query_disjoint), nullptr, 0, 0) == S_FALSE) {}

		// Check whether timestamps were disjoint during the last frame
		D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data;
		_D3D11_Device::device_context->GetData(static_cast<ID3D11Query*>(query_disjoint), &disjoint_data, sizeof(disjoint_data), 0);
		if (disjoint_data.Disjoint)
			return 0.0f;

		// Get the query data		
		UINT64 start_time = 0;
		UINT64 end_time = 0;
		_D3D11_Device::device_context->GetData(static_cast<ID3D11Query*>(query_start), &start_time, sizeof(start_time), 0);
		_D3D11_Device::device_context->GetData(static_cast<ID3D11Query*>(query_end), &end_time, sizeof(end_time), 0);

		// Compute delta in milliseconds
		const auto delta		= end_time - start_time;
		const auto duration_ms	= (delta * 1000.0f) / static_cast<float>(disjoint_data.Frequency);

		return duration_ms;
	}

	void RHI_Device::DetectPrimaryAdapter(RHI_Format format) const
	{
		// Create DirectX graphics interface factory
		IDXGIFactory* factory;
		const auto result = CreateDXGIFactory(IID_PPV_ARGS(&factory));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create a DirectX graphics interface factory, %s.", D3D11_Common::DxgiErrorToString(result));
			return;
		}

		const auto get_available_adapters = [](IDXGIFactory* factory)
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
		};

		// Get all available adapters
		auto adapters = get_available_adapters(factory);
		safe_release(factory);
		if (adapters.empty())
		{
			LOG_ERROR("Couldn't find any adapters");
			return;
		}

		// Save all available adapters
		DXGI_ADAPTER_DESC adapter_desc;
		for (auto display_adapter : adapters)
		{
			if (FAILED(display_adapter->GetDesc(&adapter_desc)))
			{
				LOG_ERROR("Failed to get adapter description");
				continue;
			}

			const auto memory_mb = static_cast<unsigned int>(adapter_desc.DedicatedVideoMemory / 1024 / 1024);
			char name[128];
			auto def_char = ' ';
			WideCharToMultiByte(CP_ACP, 0, adapter_desc.Description, -1, name, 128, &def_char, nullptr);

			Settings::Get().DisplayAdapter_Add(name, memory_mb, adapter_desc.VendorId, static_cast<void*>(display_adapter));
		}

		// DISPLAY MODES
		const auto get_display_modes = [format](IDXGIAdapter* adapter)
		{
			// Enumerate the primary adapter output (monitor).
			IDXGIOutput* adapter_output;
			auto result = adapter->EnumOutputs(0, &adapter_output);
			if (SUCCEEDED(result))
			{
				// Get supported display mode count
				UINT display_mode_count;
				result = adapter_output->GetDisplayModeList(d3d11_dxgi_format[format], DXGI_ENUM_MODES_INTERLACED, &display_mode_count, nullptr);
				if (SUCCEEDED(result))
				{
					// Get display modes
					vector<DXGI_MODE_DESC> display_modes;
					display_modes.resize(display_mode_count);
					result = adapter_output->GetDisplayModeList(d3d11_dxgi_format[format], DXGI_ENUM_MODES_INTERLACED, &display_mode_count, &display_modes[0]);
					if (SUCCEEDED(result))
					{
						// Save all the display modes
						for (const auto& mode : display_modes)
						{
							Settings::Get().DisplayMode_Add(mode.Width, mode.Height, mode.RefreshRate.Numerator, mode.RefreshRate.Denominator);
						}
					}
				}
				adapter_output->Release();
			}

			if (FAILED(result))
			{
				LOGF_ERROR("Failed to get display modes (%s)", D3D11_Common::DxgiErrorToString(result));
				return false;
			}

			return true;
		};

		// Get display modes and set primary adapter
		for (const auto& display_adapter : Settings::Get().DisplayAdapters_Get())
		{
			const auto adapter = static_cast<IDXGIAdapter*>(display_adapter.data);
			// Adapters are ordered by memory (descending), so stop on the first success
			if (get_display_modes(adapter))
			{
				Settings::Get().DisplayAdapter_SetPrimary(&display_adapter);
				break;
			}
		}
	}
}
#endif