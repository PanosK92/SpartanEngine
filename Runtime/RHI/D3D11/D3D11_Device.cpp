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
#ifdef API_GRAPHICS_D3D11
//================================

//= INCLUDES ===========================
#include "D3D11_Helper.h"
#include "../RHI_Device.h"
#include "../RHI_BlendState.h"
#include "../RHI_RasterizerState.h"
#include "../RHI_DepthStencilState.h"
#include "../RHI_Shader.h"
#include "../RHI_InputLayout.h"
#include "../RHI_VertexBuffer.h"
#include "../RHI_IndexBuffer.h"
#include "../RHI_Viewport.h"
#include "../../Logging/Log.h"
#include "../../Profiling/Profiler.h"
#include "../../Core/Settings.h"
#include "../../Math/Rectangle.h"
#include "../../FileSystem/FileSystem.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	RHI_Device::RHI_Device()
	{
		m_rhi_context = new RHI_Context();
		const static auto multithread_protection = false;

		// Detect adapters
		D3D11_Helper::DetectAdapters(this);

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
				D3D_FEATURE_LEVEL_9_2,
				D3D_FEATURE_LEVEL_9_1
			};

			auto adapter		= static_cast<IDXGIAdapter*>(m_primaryAdapter->data);
			auto driver_type	= adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

			// Create Direct3D device and Direct3D device context.
			const auto result = D3D11CreateDevice(
				adapter,									// pAdapter: If nullptr, the default adapter will be used
				driver_type,								// DriverType
				nullptr,									// HMODULE: nullptr because DriverType = D3D_DRIVER_TYPE_HARDWARE
				device_flags,								// Flags
				feature_levels.data(),						// pFeatureLevels
				static_cast<UINT>(feature_levels.size()),	// FeatureLevels
				D3D11_SDK_VERSION,							// SDKVersion
				&m_rhi_context->device,						// ppDevice
				nullptr,									// pFeatureLevel
				&m_rhi_context->device_context				// ppImmediateContext
			);

			if (FAILED(result))
			{
				LOGF_ERROR("Failed to create device, %s.", D3D11_Helper::dxgi_error_to_string(result));
				return;
			}
		}

		// Log feature level
		{
			const auto feature_level = m_rhi_context->device->GetFeatureLevel();
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
			Settings::Get().m_versionGraphicsAPI = "DirectX " + feature_level_str;
			LOG_INFO(Settings::Get().m_versionGraphicsAPI);
		}

		// Multi-thread protection
		if (multithread_protection)
		{
			ID3D11Multithread* multithread = nullptr;
			if (SUCCEEDED(m_rhi_context->device_context->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&multithread))))
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
		const auto result = m_rhi_context->device_context->QueryInterface(IID_PPV_ARGS(&m_rhi_context->annotation));
		if (FAILED(result))
		{
			LOGF_ERROR("Failed to create ID3DUserDefinedAnnotation for event reporting, %s.", D3D11_Helper::dxgi_error_to_string(result));
			return;
		}

		m_initialized = true;
	}

	RHI_Device::~RHI_Device()
	{
		safe_release(m_rhi_context->device_context);
		safe_release(m_rhi_context->device);
		safe_release(m_rhi_context->annotation);
		safe_delete(m_rhi_context);
	}

	bool RHI_Device::Draw(const unsigned int vertex_count) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (vertex_count == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_rhi_context->device_context->Draw(vertex_count, 0);
		return true;
	}

	bool RHI_Device::DrawIndexed(const unsigned int index_count, const unsigned int index_offset, const unsigned int vertex_offset) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		if (index_count == 0)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_rhi_context->device_context->DrawIndexed(index_count, index_offset, vertex_offset);
		return true;
	}

	bool RHI_Device::ClearRenderTarget(void* render_target, const Vector4& color) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_rhi_context->device_context->ClearRenderTargetView(static_cast<ID3D11RenderTargetView*>(render_target), color.Data());
		return true;
	}

	bool RHI_Device::ClearDepthStencil(void* depth_stencil, const unsigned int flags, const float depth, const unsigned int stencil) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		unsigned int clear_flags = 0;
		clear_flags |= flags & Clear_Depth	? D3D11_CLEAR_DEPTH		: 0;
		clear_flags |= flags & Clear_Stencil ? D3D11_CLEAR_STENCIL	: 0;
		m_rhi_context->device_context->ClearDepthStencilView(static_cast<ID3D11DepthStencilView*>(depth_stencil), clear_flags, depth, stencil);
		return true;
	}

	bool RHI_Device::SetVertexBuffer(const RHI_VertexBuffer* buffer) const
	{
		if (!m_rhi_context->device_context)
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
		m_rhi_context->device_context->IASetVertexBuffers(0, 1, &ptr, &stride, &offset);
		return true;
	}

	bool RHI_Device::SetIndexBuffer(const RHI_IndexBuffer* buffer) const
	{
		if (!m_rhi_context->device_context)
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
		const auto format	= d3d11_format[buffer->GetFormat()];
		m_rhi_context->device_context->IASetIndexBuffer(ptr, format, 0);

		return true;
	}

	bool RHI_Device::SetVertexShader(const RHI_Shader* shader) const
	{
		if (!m_rhi_context->device_context)
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
		m_rhi_context->device_context->VSSetShader(ptr, nullptr, 0);
		return true;
	}

	bool RHI_Device::SetPixelShader(const RHI_Shader* shader) const
	{
		if (!m_rhi_context->device_context)
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
		m_rhi_context->device_context->PSSetShader(ptr, nullptr, 0);
		return true;
	}

	bool RHI_Device::SetConstantBuffers(const unsigned int start_slot, const unsigned int buffer_count, const void* buffer, const RHI_Buffer_Scope scope) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		const auto d3_d11_buffer = static_cast<ID3D11Buffer*const*>(buffer);
		if (scope == Buffer_VertexShader || scope == Buffer_Global)
		{
			m_rhi_context->device_context->VSSetConstantBuffers(start_slot, buffer_count, d3_d11_buffer);
		}

		if (scope == Buffer_PixelShader || scope == Buffer_Global)
		{
			m_rhi_context->device_context->PSSetConstantBuffers(start_slot, buffer_count, d3_d11_buffer);
		}

		return true;
	}

	bool RHI_Device::SetSamplers(const unsigned int start_slot, const unsigned int sampler_count, const void* samplers) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_rhi_context->device_context->PSSetSamplers(start_slot, sampler_count, static_cast<ID3D11SamplerState* const*>(samplers));
		return true;
	}

	bool RHI_Device::SetRenderTargets(const unsigned int render_target_count, const void* render_targets, void* depth_stencil) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_rhi_context->device_context->OMSetRenderTargets(render_target_count, static_cast<ID3D11RenderTargetView* const*>(render_targets), static_cast<ID3D11DepthStencilView*>(depth_stencil));
		return true;
	}

	bool RHI_Device::SetTextures(const unsigned int start_slot, const unsigned int resource_count, const void* textures) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_rhi_context->device_context->PSSetShaderResources(start_slot, resource_count, static_cast<ID3D11ShaderResourceView* const*>(textures));
		return true;
	}

	bool RHI_Device::SetViewport(const RHI_Viewport& viewport) const
	{
		if (!m_rhi_context->device_context)
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
		m_rhi_context->device_context->RSSetViewports(1, &dx_viewport);

		return true;
	}

	bool RHI_Device::SetScissorRectangle(const Math::Rectangle& rectangle) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		const auto left						= rectangle.x;
		const auto top						= rectangle.y;
		const auto right					= rectangle.x + rectangle.width;
		const auto bottom					= rectangle.y + rectangle.height;
		const D3D11_RECT d3d11_rectangle	= { static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(right), static_cast<LONG>(bottom) };

		m_rhi_context->device_context->RSSetScissorRects(1, &d3d11_rectangle);

		return true;
	}

	bool RHI_Device::SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		auto ptr = static_cast<ID3D11DepthStencilState*>(depth_stencil_state->GetBuffer());
		m_rhi_context->device_context->OMSetDepthStencilState(ptr, 1);
		return true;
	}

	bool RHI_Device::SetBlendState(const RHI_BlendState* blend_state) const
	{
		if (!m_rhi_context->device_context)
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
		m_rhi_context->device_context->OMSetBlendState(ptr, blend_fentity, 0xffffffff);

		return true;
	}

	bool RHI_Device::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Set primitive topology
		m_rhi_context->device_context->IASetPrimitiveTopology(d3d11_primitive_topology[primitive_topology]);
		return true;
	}

	bool RHI_Device::SetInputLayout(const RHI_InputLayout* input_layout) const
	{
		if (!m_rhi_context->device_context)
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
		m_rhi_context->device_context->IASetInputLayout(ptr);
		return true;
	}

	bool RHI_Device::SetRasterizerState(const RHI_RasterizerState* rasterizer_state) const
	{
		if (!m_rhi_context->device_context)
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
		m_rhi_context->device_context->RSSetState(ptr);
		return true;
	}

	void RHI_Device::BeginMarker(const std::string& name)
	{
	#ifdef DEBUG
		m_rhi_context->annotation->BeginEvent(FileSystem::StringToWstring(name).c_str());
	#endif
	}

	void RHI_Device::EndMarker()
	{
	#ifdef DEBUG
		m_rhi_context->annotation->EndEvent();
	#endif
	}

	bool RHI_Device::ProfilingCreateQuery(void** query, const RHI_Query_Type type) const
	{
		if (!m_rhi_context->device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		D3D11_QUERY_DESC desc;
		ZeroMemory(&desc, sizeof(desc));
		desc.Query			= (type == Query_Timestamp_Disjoint) ? D3D11_QUERY_TIMESTAMP_DISJOINT : D3D11_QUERY_TIMESTAMP;
		desc.MiscFlags		= 0;
		const auto result	= m_rhi_context->device->CreateQuery(&desc, reinterpret_cast<ID3D11Query**>(query));
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

		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_rhi_context->device_context->Begin(static_cast<ID3D11Query*>(query_object));
		return true;
	}

	bool RHI_Device::ProfilingGetTimeStamp(void* query_object) const
	{
		if (!query_object)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_rhi_context->device_context->End(static_cast<ID3D11Query*>(query_object));
		return true;
	}

	float RHI_Device::ProfilingGetDuration(void* query_disjoint, void* query_start, void* query_end) const
	{
		if (!m_rhi_context->device_context)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return 0.0f;
		}

		// Wait for data to be available	
		while (m_rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_disjoint), nullptr, 0, 0) == S_FALSE) {}

		// Check whether timestamps were disjoint during the last frame
		D3D10_QUERY_DATA_TIMESTAMP_DISJOINT disjoint_data;
		m_rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_disjoint), &disjoint_data, sizeof(disjoint_data), 0);
		if (disjoint_data.Disjoint)
			return 0.0f;

		// Get the query data		
		UINT64 start_time	= 0;
		UINT64 end_time		= 0;
		m_rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_start), &start_time, sizeof(start_time), 0);
		m_rhi_context->device_context->GetData(static_cast<ID3D11Query*>(query_end), &end_time, sizeof(end_time), 0);

		// Convert to real time
		const auto delta		= end_time - start_time;
		const auto duration_ms	= (delta * 1000.0f) / static_cast<float>(disjoint_data.Frequency);

		return duration_ms;
	}

	void RHI_Device::ProfilingReleaseQuery(void* query_object)
	{
		if (!query_object)
			return;

		auto query = static_cast<ID3D11Query*>(query_object);
		query->Release();
	}

	unsigned int RHI_Device::ProfilingGetGpuMemory()
	{
		if (auto adapter = static_cast<IDXGIAdapter3*>(m_primaryAdapter->data))
		{
			DXGI_ADAPTER_DESC adapter_desc;
			if (FAILED(adapter->GetDesc(&adapter_desc)))
			{
				LOG_ERROR("Failed to get adapter description");
				return 0;
			}
			return static_cast<unsigned int>(adapter_desc.DedicatedVideoMemory / 1024 / 1024); // convert to MBs
		}
		return 0;
	}

	unsigned int RHI_Device::ProfilingGetGpuMemoryUsage()
	{
		if (auto adapter = static_cast<IDXGIAdapter3*>(m_primaryAdapter->data))
		{
			DXGI_QUERY_VIDEO_MEMORY_INFO info = {};
			if (FAILED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info)))
			{
				LOG_ERROR("Failed to get adapter memory info");
				return 0;
			}
			return static_cast<unsigned int>(info.CurrentUsage / 1024 / 1024); // convert to MBs
		}
		return 0;
	}

	void RHI_Device::WaitIdle()
	{
		
	}
}
#endif