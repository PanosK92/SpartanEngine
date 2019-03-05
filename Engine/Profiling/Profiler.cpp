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

//= INCLUDES =========================
#include "Profiler.h"
#include <iomanip>
#include <sstream>
#include "../Core/Timer.h"
#include "../Core/Settings.h"
#include "../Core/EventSystem.h"
#include "../World/World.h"
#include "../Rendering/Renderer.h"
#include "../RHI/RHI_Device.h"
#include "../Core/Variant.h"
#include "../Resource/ResourceCache.h"
//====================================

//= NAMESPACES =============
using namespace std;
using namespace std::chrono;
//==========================

namespace Directus
{
	Profiler::Profiler(Context* context) : ISubsystem(context)
	{
		m_metrics						= NOT_ASSIGNED;
		m_scene							= nullptr;
		m_timer							= nullptr;
		m_resource_manager				= nullptr;
		m_gpu_profiling					= false; // expensive
		m_cpu_profiling					= false; // cheap
		m_profiling_frequency_sec		= 0.0f;
		m_profiling_last_update_time	= 0;
		m_fps							= 0.0f;
		m_time_passed					= 0.0f;
		m_frame_count					= 0;
		m_profiling_frequency_sec		= 0.35f;
		m_profiling_last_update_time	= m_profiling_frequency_sec;

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_Frame_Start, EVENT_HANDLER(OnFrameStart));
		SUBSCRIBE_TO_EVENT(Event_Frame_End, EVENT_HANDLER(OnFrameEnd));
	}

	bool Profiler::Initialize()
	{
		m_scene				= m_context->GetSubsystem<World>().get();
		m_timer				= m_context->GetSubsystem<Timer>().get();
		m_resource_manager	= m_context->GetSubsystem<ResourceCache>().get();
		m_renderer			= m_context->GetSubsystem<Renderer>().get();
		return true;
	}

	bool Profiler::TimeBlockStartCpu(const string& func_name)
	{
		if (!m_cpu_profiling || !m_should_update)
			return false;

		m_time_blocks_cpu[func_name].start = high_resolution_clock::now();
		return true;
	}

	bool Profiler::TimeBlockEndCpu(const string& func_name)
	{
		if (!m_cpu_profiling || !m_should_update)
			return false;

		if (m_time_blocks_cpu.find(func_name) == m_time_blocks_cpu.end() )
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		auto time_block				= &m_time_blocks_cpu[func_name];
		time_block->end				= high_resolution_clock::now();
		duration<double, milli> ms	= time_block->end - time_block->start;
		time_block->duration		= static_cast<float>(ms.count());
		return true;
	}

	bool Profiler::TimeBlockStartGpu(const string& func_name)
	{
		if (!m_gpu_profiling || !m_should_update)
			return false;

		auto time_block = &m_time_blocks_gpu[func_name];

		if (!time_block->initialized)
		{
			m_renderer->GetRhiDevice()->ProfilingCreateQuery(&time_block->query,		Query_Timestamp_Disjoint);
			m_renderer->GetRhiDevice()->ProfilingCreateQuery(&time_block->time_start,	Query_Timestamp);
			m_renderer->GetRhiDevice()->ProfilingCreateQuery(&time_block->time_end,		Query_Timestamp);
			time_block->initialized = true;
		}

		m_renderer->GetRhiDevice()->ProfilingQueryStart(time_block->query);
		m_renderer->GetRhiDevice()->ProfilingGetTimeStamp(time_block->time_start);
		time_block->started = true;
		return true;
	}

	bool Profiler::TimeBlockEndGpu(const string& func_name)
	{
		if (!m_gpu_profiling || !m_should_update)
			return false;

		if (m_time_blocks_gpu.find(func_name) == m_time_blocks_gpu.end() )
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		const auto time_block = &m_time_blocks_gpu[func_name];
		if (!time_block->initialized)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		m_renderer->GetRhiDevice()->ProfilingGetTimeStamp(time_block->time_end);
		m_renderer->GetRhiDevice()->ProfilingQueryEnd(time_block->query);
		return true;
	}

	bool Profiler::TimeBlockStartMulti(const string& func_name)
	{
		const auto cpu = TimeBlockStartCpu(func_name);
		const auto gpu = TimeBlockStartGpu(func_name);
		return cpu && gpu;
	}

	bool Profiler::TimeBlockEndMulti(const string& func_name)
	{
		const auto cpu = TimeBlockEndCpu(func_name);
		const auto gpu = TimeBlockEndGpu(func_name);
		return cpu && gpu;
	}

	void Profiler::OnFrameStart()
	{
		// Get delta time
		m_frame_time_ms	= m_timer->GetDeltaTimeMs();
		m_frame_time_sec	= m_timer->GetDeltaTimeSec();

		// Compute FPS
		ComputeFps(m_frame_time_sec);
		// Get GPU render time
		m_cpu_time = GetTimeBlockMsCpu("Directus::Renderer::Tick");
		// Get CPU render time
		m_gpu_time = GetTimeBlockMsGpu("Directus::Renderer::Tick");

		// Below this point, update every m_profilingFrequencyMs
		m_profiling_last_update_time += m_frame_time_sec;
		if (m_profiling_last_update_time >= m_profiling_frequency_sec)
		{
			UpdateMetrics(m_fps);
			m_should_update					= true;
			m_profiling_last_update_time	= 0.0f;
		}
	}

	void Profiler::OnFrameEnd()
	{
		if (!m_should_update)
			return;

		for (auto& entry : m_time_blocks_gpu)
		{
			auto& time_block = entry.second;

			if (time_block.started)
			{
				time_block.duration = m_renderer->GetRhiDevice()->ProfilingGetDuration(time_block.query, time_block.time_start, time_block.time_end);
			}
			time_block.started = false;
		}

		m_should_update = false;
	}

	void Profiler::UpdateMetrics(const float fps)
	{
		const auto textures		= m_resource_manager->GetResourceCountByType(Resource_Texture);
		const auto materials	= m_resource_manager->GetResourceCountByType(Resource_Material);
		const auto shaders		= m_resource_manager->GetResourceCountByType(Resource_Shader);

		m_metrics =
			// Performance
			"FPS:\t\t\t\t\t\t\t"	+ ToStringPrecision(fps, 2) + "\n"
			"Frame time:\t\t\t\t\t" + ToStringPrecision(m_frame_time_ms, 2) + " ms\n"
			"CPU time:\t\t\t\t\t\t" + ToStringPrecision(m_cpu_time, 2) + " ms\n"
			"GPU time:\t\t\t\t\t\t" + ToStringPrecision(m_gpu_time, 2) + " ms\n"
			"GPU:\t\t\t\t\t\t\t"	+ Settings::Get().GpuGetName() + "\n"
			"VRAM:\t\t\t\t\t\t\t"	+ to_string(Settings::Get().GpuGetMemory()) + " MB\n"

			// Renderer
			"Resolution:\t\t\t\t\t"				+ to_string(static_cast<int>(m_renderer->GetResolution().x)) + "x" + to_string(static_cast<int>(m_renderer->GetResolution().y)) + "\n"
			"Meshes rendered:\t\t\t\t"			+ to_string(m_renderer_meshes_rendered) + "\n"
			"Textures:\t\t\t\t\t\t"				+ to_string(textures) + "\n"
			"Materials:\t\t\t\t\t\t"			+ to_string(materials) + "\n"
			"Shaders:\t\t\t\t\t\t"				+ to_string(shaders) + "\n"

			// RHI
			"RHI Draw calls:\t\t\t\t\t"			+ to_string(m_rhi_draw_calls) + "\n"
			"RHI Index buffer bindings:\t\t"	+ to_string(m_rhi_bindings_buffer_index) + "\n"
			"RHI Vertex buffer bindings:\t"		+ to_string(m_rhi_bindings_buffer_vertex) + "\n"
			"RHI Constant buffer bindings:\t"	+ to_string(m_rhi_bindings_buffer_constant) + "\n"
			"RHI Sampler bindings:\t\t\t"		+ to_string(m_rhi_bindings_sampler) + "\n"
			"RHI Texture bindings:\t\t\t"		+ to_string(m_rhi_bindings_texture) + "\n"
			"RHI Vertex Shader bindings:\t"		+ to_string(m_rhi_bindings_vertex_shader) + "\n"
			"RHI Pixel Shader bindings:\t\t"	+ to_string(m_rhi_bindings_pixel_shader) + "\n"
			"RHI Render Target bindings:\t"		+ to_string(m_rhi_bindings_render_target) + "\n";
	}

	void Profiler::ComputeFps(const float delta_time)
	{
		// update counters
		m_frame_count++;
		m_time_passed += delta_time;

		if (m_time_passed >= 1.0f)
		{
			// compute fps
			m_fps = static_cast<float>(m_frame_count) / (m_time_passed / 1.0f);

			// reset counters
			m_frame_count = 0;
			m_time_passed = 0;
		}
	}

	string Profiler::ToStringPrecision(const float value, const unsigned int decimals)
	{
		ostringstream out;
		out << fixed << setprecision(decimals) << value;
		return out.str();
	}
}
