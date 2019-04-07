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

//= INCLUDES ==================
#include <string>
#include <vector>
#include "TimeBlock.h"
#include "../Core/EngineDefs.h"
#include "../Core/ISubsystem.h"
//=============================

#define TIME_BLOCK_START_MULTI(profiler)	profiler->TimeBlockStart(__FUNCTION__, true, true);
#define TIME_BLOCK_START_CPU(profiler)		profiler->TimeBlockStart(__FUNCTION__, true, false);
#define TIME_BLOCK_START_GPU(profiler)		profiler->TimeBlockStart(__FUNCTION__, false, true);
#define TIME_BLOCK_END(profiler)			profiler->TimeBlockEnd();

namespace Directus
{
	class Context;
	class Timer;
	class ResourceCache;
	class Renderer;

	class ENGINE_CLASS Profiler : public ISubsystem
	{
	public:
		Profiler(Context* context);

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		// Time block
		bool TimeBlockStart(const std::string& func_name, bool profile_cpu = true, bool profile_gpu = false);
		bool TimeBlockEnd();

		// Events
		void OnFrameStart();
		void OnFrameEnd();

		void SetProfilingEnabledCpu(const bool enabled)	{ m_profile_cpu_enabled = enabled; }
		void SetProfilingEnabledGpu(const bool enabled)	{ m_profile_gpu_enabled = enabled; }
		const std::string& GetMetrics() const			{ return m_metrics; }
		const auto& GetTimeBlocks() const				{ return m_time_blocks; }
		float GetTimeCpu() const						{ return m_time_cpu_ms; }
		float GetTimeGpu() const						{ return m_time_gpu_ms; }
		float GetTimeFrame() const						{ return m_time_frame_ms; }
		float GetFps() const							{ return m_fps; }
		float GetUpdateInterval()						{ return m_profiling_interval_sec; }
		void SetUpdateInterval(float internval)			{ m_profiling_interval_sec = internval; }
		bool HasNewData()								{ return m_has_new_data; }
		const std::string& GpuGetName()					{ return m_gpu_name; }
		unsigned int GpuGetMemoryAvailable()			{ return m_gpu_memory_available; }
		unsigned int GpuGetMemoryUsed()					{ return m_gpu_memory_used; }
		
		void Reset()
		{
			m_rhi_draw_calls				= 0;
			m_renderer_meshes_rendered		= 0;
			m_rhi_bindings_buffer_index		= 0;
			m_rhi_bindings_buffer_vertex	= 0;
			m_rhi_bindings_buffer_constant	= 0;
			m_rhi_bindings_sampler			= 0;
			m_rhi_bindings_texture			= 0;
			m_rhi_bindings_vertex_shader	= 0;
			m_rhi_bindings_pixel_shader		= 0;
			m_rhi_bindings_render_target	= 0;
		}

		// Metrics - RHI
		unsigned int m_rhi_draw_calls				= 0;
		unsigned int m_rhi_bindings_buffer_index	= 0;
		unsigned int m_rhi_bindings_buffer_vertex	= 0;
		unsigned int m_rhi_bindings_buffer_constant = 0;
		unsigned int m_rhi_bindings_sampler			= 0;
		unsigned int m_rhi_bindings_texture			= 0;
		unsigned int m_rhi_bindings_vertex_shader	= 0;
		unsigned int m_rhi_bindings_pixel_shader	= 0;
		unsigned int m_rhi_bindings_render_target	= 0;

		// Metrics - Renderer
		unsigned int m_renderer_meshes_rendered = 0;

		// Metrics - Time
		float m_time_frame_ms	= 0.0f;
		float m_time_cpu_ms		= 0.0f;
		float m_time_gpu_ms		= 0.0f;

	private:
		TimeBlock* GetNextTimeBlock();
		TimeBlock* GetLastIncompleteTimeBlock();
		TimeBlock* GetSecondLastIncompleteTimeBlock();
		void ComputeFps(float delta_time);
		void UpdateStringFormatMetrics(float fps);

		// Profiling options
		bool m_profile_cpu_enabled			= true; // cheap
		bool m_profile_gpu_enabled			= true; // expensive
		float m_profiling_interval_sec		= 0.3f;
		float m_profiling_last_update_time	= m_profiling_interval_sec;

		// Time blocks
		unsigned int m_time_block_capacity	= 200;
		unsigned int m_time_block_count		= 0;
		std::vector<TimeBlock> m_time_blocks;

		// FPS
		float m_fps					= 0.0f;
		float m_time_passed			= 0.0f;
		unsigned int m_frame_count	= 0;

		// Hardware - GPU
		std::string m_gpu_name				= "Unknown";
		unsigned int m_gpu_memory_available	= 0;
		unsigned int m_gpu_memory_used		= 0;

		// Misc
		std::string m_metrics;
		bool m_should_update	= true;
		bool m_has_new_data		= false;
	
		// Dependencies
		Timer* m_timer						= nullptr;
		ResourceCache* m_resource_manager	= nullptr;
		Renderer* m_renderer				= nullptr;
	};
}
