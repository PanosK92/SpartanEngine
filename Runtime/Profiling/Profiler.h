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
#include <memory>
#include "../Core/EngineDefs.h"
#include "../Core/ISubsystem.h"
//=============================

// Multi (CPU + GPU)
#define TIME_BLOCK_START_MULTI(profiler)	profiler->TimeBlockStart_Multi(__FUNCTION__);
#define TIME_BLOCK_END_MULTI(profiler)		profiler->TimeBlockEnd_Multi(__FUNCTION__);
// CPU
#define TIME_BLOCK_START_CPU(profiler)		profiler->TimeBlockStart_CPU(__FUNCTION__);
#define TIME_BLOCK_END_CPU(profiler)		profiler->TimeBlockEnd_CPU(__FUNCTION__);
// GPU
#define TIME_BLOCK_START_GPU(profiler)		profiler->TimeBlockStart_CPU(__FUNCTION__);
#define TIME_BLOCK_END_GPU(profiler)		profiler->TimeBlockEnd_GPU(__FUNCTION__);

namespace Directus
{
	class Context;
	class World;
	class Timer;
	class ResourceCache;
	class Renderer;
	class Variant;

	struct TimeBlock_CPU
	{
		std::chrono::steady_clock::time_point start;
		std::chrono::steady_clock::time_point end;
		float duration = 0.0f;
	};

	struct TimeBlock_GPU
	{
		void* query;
		void* time_start;
		void* time_end;
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
		void TimeBlockStart_Multi(const char* funcName);
		void TimeBlockEnd_Multi(const char* funcName);
		// CPU timing
		void TimeBlockStart_CPU(const char* funcName);
		void TimeBlockEnd_CPU(const char* funcName);
		// GPU timing
		void TimeBlockStart_GPU(const char* funcName);
		void TimeBlockEnd_GPU(const char* funcName);

		// Events
		void OnFrameStart();
		void OnFrameEnd();

		void SetProfilingEnabled_CPU(bool enabled)		{ m_cpuProfiling = enabled; }
		void SetProfilingEnabled_GPU(bool enabled)		{ m_gpuProfiling = enabled; }
		const std::string& GetMetrics()					{ return m_metrics; }
		float GetTimeBlockMs_CPU(const char* funcName)	{ return m_timeBlocks_cpu[funcName].duration; }
		float GetTimeBlockMs_GPU(const char* funcName)	{ return m_timeBlocks_gpu[funcName].duration; }
		const auto& GetTimeBlocks_CPU()					{ return m_timeBlocks_cpu; }
		const auto& GetTimeBlocks_GPU()					{ return m_timeBlocks_gpu; }
		float GetRenderTime_CPU()						{ return m_cpuTime; }
		float GetRenderTime_GPU()						{ return m_gpuTime; }
		float GetFPS()									{ return m_fps; }
		float GetFrameTimeSec()							{ return m_frameTimeSec; }

		void Reset()
		{
			m_rhiDrawCalls				= 0;
			m_rendererMeshesRendered	= 0;
			m_rhiBindingsBufferIndex	= 0;
			m_rhiBindingsBufferVertex	= 0;
			m_rhiBindingsBufferConstant	= 0;
			m_rhiBindingsSampler		= 0;
			m_rhiBindingsTexture		= 0;
			m_rhiBindingsVertexShader	= 0;
			m_rhiBindingsPixelShader	= 0;
			m_rhiBindingsRenderTarget	= 0;
		}

		// Metrics - RHI
		unsigned int m_rhiDrawCalls;
		unsigned int m_rhiBindingsBufferIndex;
		unsigned int m_rhiBindingsBufferVertex;
		unsigned int m_rhiBindingsBufferConstant;
		unsigned int m_rhiBindingsSampler;
		unsigned int m_rhiBindingsTexture;
		unsigned int m_rhiBindingsVertexShader;
		unsigned int m_rhiBindingsPixelShader;
		unsigned int m_rhiBindingsRenderTarget;

		// Metrics - Renderer
		unsigned int m_rendererMeshesRendered;

		// Metrics - Time
		float m_frameTimeMs;
		float m_frameTimeSec;
		float m_cpuTime;
		float m_gpuTime;

	private:
		void UpdateMetrics(float fps);
		void ComputeFPS(float deltaTime);
		// Converts float to string with specified precision
		std::string to_string_precision(float value, int decimals);

		// Profiling options
		bool m_gpuProfiling;
		bool m_cpuProfiling;
		float m_profilingFrequencySec;
		float m_profilingLastUpdateTime;

		// Time blocks
		std::map<const char*, TimeBlock_CPU> m_timeBlocks_cpu;
		std::map<const char*, TimeBlock_GPU> m_timeBlocks_gpu;

		// Misc
		std::string m_metrics;
		bool m_shouldUpdate;
	
		//= FPS ===========
		float m_fps;
		float m_timePassed;
		int m_frameCount;
		//=================

		// Dependencies
		World* m_scene;
		Timer* m_timer;
		ResourceCache* m_resourceManager;
		Renderer* m_renderer;
	};
}
