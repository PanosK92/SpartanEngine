/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../Core/EngineDefs.h"
#include <string>
#include <map>
#include <chrono>
//=============================

#define PROFILE_FUNCTION_BEGIN()	Directus::Profiler::Get().BeginBlock(__FUNCTION__)
#define PROFILE_FUNCTION_END()		Directus::Profiler::Get().EndBlock(__FUNCTION__)

namespace Directus
{
	class Context;
	class Scene;
	class Timer;
	class ResourceManager;
	class Renderer;

	struct Block
	{
		std::chrono::steady_clock::time_point start;
		std::chrono::steady_clock::time_point end;
		float duration;
	};

	class ENGINE_CLASS Profiler
	{
	public:
		static Profiler& Get()
		{
			static Profiler instance;
			return instance;
		}

		Profiler();

		void Initialize(Context* context);

		void BeginBlock(const char* funcName);
		void EndBlock(const char* funcName);
		float GetBlockTimeMs(const char* funcName) { return m_timeBlocks[funcName].duration; }
		const auto& GetAllBlocks() { return m_timeBlocks; }
		void UpdateMetrics();
		const std::string& GetMetrics() { return m_metrics; }

		void Reset()
		{
			m_drawCalls					= 0;
			m_meshesRendered			= 0;
			m_bindBufferIndexCount		= 0;
			m_bindBufferVertexCount		= 0;
			m_bindConstantBufferCount	= 0;
			m_bindSamplerCount			= 0;
			m_bindVertexShaderCount		= 0;
			m_bindPixelShaderCount		= 0;
		}

		unsigned int m_drawCalls;
		unsigned int m_meshesRendered;
		unsigned int m_bindBufferIndexCount;
		unsigned int m_bindBufferVertexCount;
		unsigned int m_bindConstantBufferCount;
		unsigned int m_bindSamplerCount;
		unsigned int m_bindVertexShaderCount;
		unsigned int m_bindPixelShaderCount;
	
	private:
		// Converts float to string with specified precision
		std::string to_string_precision(float value, int decimals);

		// Timings
		std::map<const char*, Block> m_timeBlocks;

		// Misc
		float m_updateFrequencyMs;
		float m_timeSinceLastUpdate;
		std::string m_metrics;

		// Dependencies
		Scene* m_scene;
		Timer* m_timer;
		ResourceManager* m_resourceManager;
		Renderer* m_renderer;
	};
}
