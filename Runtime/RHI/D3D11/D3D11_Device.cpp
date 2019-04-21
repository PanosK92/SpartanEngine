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
		D3D11_Common::DetectAdapters(this);

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
				LOGF_ERROR("Failed to create device, %s.", D3D11_Common::dxgi_error_to_string(result));
				return;
			}
		}

		// Log feature level
		{
			auto log_feature_level = [this](const std::string& level)
			{
				Settings::Get().m_versionGraphicsAPI = "DirectX " + level;
				LOG_INFO(Settings::Get().m_versionGraphicsAPI);
			};

			switch (m_rhi_context->device->GetFeatureLevel())
			{
				case D3D_FEATURE_LEVEL_9_1:
					log_feature_level("9.1");
					break;

				case D3D_FEATURE_LEVEL_9_2:
					log_feature_level("9.2");
					break;

				case D3D_FEATURE_LEVEL_9_3:
					log_feature_level("9.3");
					break;

				case D3D_FEATURE_LEVEL_10_0:
					log_feature_level("10.0");
					break;

				case D3D_FEATURE_LEVEL_10_1:
					log_feature_level("10.1");
					break;

				case D3D_FEATURE_LEVEL_11_0:
					log_feature_level("11.0");
					break;

				case D3D_FEATURE_LEVEL_11_1:
					log_feature_level("11.1");
					break;
				case D3D_FEATURE_LEVEL_12_0: break;
				case D3D_FEATURE_LEVEL_12_1: break;
				default: ;
			}
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
			LOGF_ERROR("Failed to create ID3DUserDefinedAnnotation for event reporting, %s.", D3D11_Common::dxgi_error_to_string(result));
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
}
#endif