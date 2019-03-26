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
#include "../Core/EventSystem.h"
#include "../World/World.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	Profiler::Profiler(Context* context) : ISubsystem(context)
	{
		m_metrics = NOT_ASSIGNED;
		m_time_blocks.reserve(m_time_block_capacity);
		m_time_blocks.resize(m_time_block_capacity);

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_Frame_Start, EVENT_HANDLER(OnFrameStart));
		SUBSCRIBE_TO_EVENT(Event_Frame_End, EVENT_HANDLER(OnFrameEnd));
	}

	bool Profiler::Initialize()
	{
		m_timer				= m_context->GetSubsystem<Timer>().get();
		m_resource_manager	= m_context->GetSubsystem<ResourceCache>().get();
		m_renderer			= m_context->GetSubsystem<Renderer>().get();
		return true;
	}

	bool Profiler::TimeBlockStart(const string& func_name, bool profile_cpu /*= true*/, bool profile_gpu /*= false*/)
	{
		if (!m_should_update)
			return false;

		bool can_profile_cpu = profile_cpu && m_profile_cpu_enabled;
		bool can_profile_gpu = profile_gpu && m_profile_gpu_enabled;

		if (!can_profile_cpu && !can_profile_gpu)
			return false;

		if (auto time_block = GetTimeBlockEmpty())
		{
			auto time_block_parent = nullptr; //GetTimeBlockIncomplete(); // wrong, fix tonight
			time_block->Start(func_name, can_profile_cpu, can_profile_gpu, time_block_parent, m_renderer->GetRhiDevice());

			// Track the Renderer time block (so we can derive GPU time later)
			if (!m_time_block_render)
			{
				if (func_name == "Render")
				{
					m_time_block_render = time_block;
				}
			}
		}

		return true;
	}

	bool Profiler::TimeBlockEnd()
	{
		if (!m_should_update || m_time_block_count == 0)
			return false;

		if (auto time_block = GetTimeBlockIncomplete())
		{
			time_block->End(m_renderer->GetRhiDevice());
		}

		return true;
	}

	void Profiler::OnFrameStart()
	{
		// Get delta time
		m_time_frame_ms		= m_timer->GetDeltaTimeMs();
		m_time_frame_sec	= m_timer->GetDeltaTimeSec();

		// Get CPU & GPU render time
		if (m_time_block_render)
		{
			m_time_gpu_ms = m_time_block_render->GetDurationGpu();
			m_time_cpu_ms = m_time_frame_ms - m_time_gpu_ms;
		}

		// Compute FPS
		ComputeFps(m_time_frame_sec);
		
		m_has_new_data = false;

		// Below this point, updating every m_profiling_interval_sec
		m_profiling_last_update_time += m_time_frame_sec;
		if (m_profiling_last_update_time >= m_profiling_interval_sec)
		{
			UpdateStringFormatMetrics(m_fps);

			// Clear old timings
			for (unsigned int i = 0; i < m_time_block_count; i++)
			{
				TimeBlock& time_block = m_time_blocks[i];
				if (!time_block.IsComplete())
				{
					LOGF_WARNING("Ensure that TimeBlockEnd() is called for %s", time_block.GetName().c_str());
				}
				time_block.Clear();
			}

			m_profiling_last_update_time	= 0.0f;
			m_should_update					= true;
			m_time_block_count				= 0;
		}
	}

	void Profiler::OnFrameEnd()
	{
		if (!m_should_update)
			return;

		for (auto& time_block : m_time_blocks)
		{
			if (!time_block.IsProfilingGpu())
				continue;

			time_block.OnFrameEnd(m_renderer->GetRhiDevice());
		}

		m_should_update = false;
		m_has_new_data	= true;
	}

	TimeBlock* Profiler::GetTimeBlockEmpty()
	{
		// Grow capacity if needed
		if (m_time_block_count >= static_cast<unsigned int>(m_time_blocks.size()))
		{
			unsigned int new_size = m_time_block_count + 100;
			m_time_blocks.reserve(new_size);
			m_time_blocks.resize(new_size);
			LOGF_WARNING("Time block list has grown to fit %d commands. Consider making the capacity larger to avoid re-allocations.", m_time_block_count + 1);
		}

		// Return a time block
		m_time_block_count++;
		return &m_time_blocks[m_time_block_count - 1];
	}

	TimeBlock* Profiler::GetTimeBlockIncomplete()
	{
		for (unsigned int i = m_time_block_count - 1; i >= 0; i--)
		{
			TimeBlock& time_block = m_time_blocks[i];
			if (!time_block.IsComplete())
				return &time_block;
		}

		return nullptr;
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

	void Profiler::UpdateStringFormatMetrics(const float fps)
	{
		const auto textures		= m_resource_manager->GetResourceCountByType(Resource_Texture);
		const auto materials	= m_resource_manager->GetResourceCountByType(Resource_Material);
		const auto shaders		= m_resource_manager->GetResourceCountByType(Resource_Shader);

		m_metrics =
			// Performance
			"FPS:\t\t\t\t\t\t\t"	+ ToStringPrecision(fps, 2) + "\n"
			"Frame time:\t\t\t\t\t" + ToStringPrecision(m_time_frame_ms, 2) + " ms\n"
			"CPU time:\t\t\t\t\t\t" + ToStringPrecision(m_time_cpu_ms, 2) + " ms\n"
			"GPU time:\t\t\t\t\t\t" + ToStringPrecision(m_time_gpu_ms, 2) + " ms\n"
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
}
