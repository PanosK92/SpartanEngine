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

#pragma once


//= INCLUDES =================
#include <chrono>
#include "../Logging/Log.h"
#include "../RHI/RHI_Device.h"
//============================

namespace Directus
{
	struct TimeBlock
	{
		bool Start(bool m_time_cpu = false, bool m_time_gpu = false, const std::shared_ptr<RHI_Device>& rhi_device = nullptr)
		{
			if (m_time_cpu)
			{
				start = std::chrono::high_resolution_clock::now();
				m_tracking_cpu = true;
			}
	
			if (m_time_gpu)
			{
				// Create required queries
				if (!query)
				{
					rhi_device->ProfilingCreateQuery(&query,		Query_Timestamp_Disjoint);
					rhi_device->ProfilingCreateQuery(&query_start,	Query_Timestamp);
					rhi_device->ProfilingCreateQuery(&query_end,	Query_Timestamp);
				}

				// Get time stamp
				rhi_device->ProfilingQueryStart(query);
				rhi_device->ProfilingGetTimeStamp(query_start);

				m_tracking_gpu = true;
			}

			return true;
		}
	
		bool End(bool m_time_cpu = false, bool time_gpu = false, const std::shared_ptr<RHI_Device>& rhi_device = nullptr)
		{
			if (m_time_cpu)
			{
				end = std::chrono::high_resolution_clock::now();
				std::chrono::duration<double, std::milli> ms = end - start;
				duration_cpu = static_cast<float>(ms.count());
			}
	
			if (time_gpu)
			{
				if (query)
				{
					// Get time stamp
					rhi_device->ProfilingGetTimeStamp(query_end);
					rhi_device->ProfilingGetTimeStamp(query);
				}
				else
				{
					LOG_ERROR_INVALID_INTERNALS();
					return false;
				}
			}

			return true;
		}

		void OnFrameEnd(const std::shared_ptr<RHI_Device>& rhi_device)
		{
			if (!query)
				return;

			duration_gpu = rhi_device->ProfilingGetDuration(query, query_start, query_end);
		}

		void Clear()
		{
			duration_cpu	= 0.0f;
			duration_gpu	= 0.0f;
			m_tracking_cpu	= false;
			m_tracking_gpu	= false;
		}

		const bool TrackingCpu() const { return m_tracking_cpu; }
		const bool TrackingGpu() const { return m_tracking_gpu; }

		// CPU timing
		bool m_tracking_cpu	= false;
		float duration_cpu	= 0.0f;
		std::chrono::steady_clock::time_point start;
		std::chrono::steady_clock::time_point end;
	
		// GPU timing
		bool m_tracking_gpu	= false;
		float duration_gpu	= 0.0f;
		void* query			= nullptr;
		void* query_start	= nullptr;
		void* query_end		= nullptr;
	};
}