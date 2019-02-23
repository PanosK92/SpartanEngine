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
#include <map>
#include <chrono>
#include "../Core/EngineDefs.h"
#include "../Core/ISubsystem.h"
//=============================

// Multi (CPU + GPU)
#define TIME_BLOCK_START_MULTI(profiler)	profiler->TimeBlockStartMulti(__FUNCTION__);
#define TIME_BLOCK_END_MULTI(profiler)		profiler->TimeBlockEndMulti(__FUNCTION__);
// CPU
#define TIME_BLOCK_START_CPU(profiler)		profiler->TimeBlockStartCpu(__FUNCTION__);
#define TIME_BLOCK_END_CPU(profiler)		profiler->TimeBlockEndCpu(__FUNCTION__);
// GPU
#define TIME_BLOCK_START_GPU(profiler)		profiler->TimeBlockStartGpu(__FUNCTION__);
#define TIME_BLOCK_END_GPU(profiler)		profiler->TimeBlockEndGpu(__FUNCTION__);

namespace Directus
{
	class Context;
	class World;
	class Timer;
	class ResourceCache;
	class Renderer;
	class Variant;

	struct TimeBlockCpu
	{
		std::chrono::steady_clock::time_point start;
		std::chrono::steady_clock::time_point end;
		float duration = 0.0f;
	};

	struct TimeBlockGpu
	{
		void* query			= nullptr;
		void* time_start	= nullptr;
		void* time_end		= nullptr;
		float duration		= 0.0f;
		bool initialized	= false;
		bool started		= false;
	};

	class ENGINE_CLASS Profiler : public ISubsystem
	{
	public:
		Profiler(Context* context);

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		// Multi-timing
		bool TimeBlockStartMulti(const std::string& func_name);
		bool TimeBlockEndMulti(const std::string& func_name);
		// CPU timing
		bool TimeBlockStartCpu(const std::string& func_name);
		bool TimeBlockEndCpu(const std::string& func_name);
		// GPU timing
		bool TimeBlockStartGpu(const std::string& func_name);
		bool TimeBlockEndGpu(const std::string& func_name);

		// Events
		void OnFrameStart();
		void OnFrameEnd();

		void SetProfilingEnabledCpu(const bool enabled)	{ m_cpu_profiling = enabled; }
		void SetProfilingEnabledGpu(const bool enabled)	{ m_gpu_profiling = enabled; }
		const std::string& GetMetrics() const			{ return m_metrics; }
		float GetTimeBlockMsCpu(const char* func_name)	{ return m_time_blocks_cpu[func_name].duration; }
		float GetTimeBlockMsGpu(const char* func_name)	{ return m_time_blocks_gpu[func_name].duration; }
		const auto& GetTimeBlocksCpu() const			{ return m_time_blocks_cpu; }
		const auto& GetTimeBlocksGpu() const			{ return m_time_blocks_gpu; }
		float GetRenderTimeCpu() const					{ return m_cpu_time; }
		float GetRenderTimeGpu() const					{ return m_gpu_time; }
		float GetFps() const							{ return m_fps; }
		float GetFrameTimeSec() const					{ return m_frame_time_sec; }

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
		unsigned int m_rhi_draw_calls;
		unsigned int m_rhi_bindings_buffer_index;
		unsigned int m_rhi_bindings_buffer_vertex;
		unsigned int m_rhi_bindings_buffer_constant;
		unsigned int m_rhi_bindings_sampler;
		unsigned int m_rhi_bindings_texture;
		unsigned int m_rhi_bindings_vertex_shader;
		unsigned int m_rhi_bindings_pixel_shader;
		unsigned int m_rhi_bindings_render_target;

		// Metrics - Renderer
		unsigned int m_renderer_meshes_rendered;

		// Metrics - Time
		float m_frame_time_ms;
		float m_frame_time_sec;
		float m_cpu_time;
		float m_gpu_time;

	private:
		void UpdateMetrics(float fps);
		void ComputeFps(float delta_time);
		static std::string ToStringPrecision(float value, unsigned int decimals);

		// Profiling options
		bool m_gpu_profiling;
		bool m_cpu_profiling;
		float m_profiling_frequency_sec;
		float m_profiling_last_update_time;

		// Time blocks
		std::map<std::string, TimeBlockCpu> m_time_blocks_cpu;
		std::map<std::string, TimeBlockGpu> m_time_blocks_gpu;

		// Misc
		std::string m_metrics;
		bool m_should_update;
	
		//= FPS ===================
		float m_fps;
		float m_time_passed;
		unsigned int m_frame_count;
		//=========================

		// Dependencies
		World* m_scene;
		Timer* m_timer;
		ResourceCache* m_resource_manager;
		Renderer* m_renderer;
	};
}
