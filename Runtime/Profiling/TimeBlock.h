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

//= INCLUDES ====
#include <chrono>
#include <memory>
#include <string>
//===============

namespace Spartan
{
	class RHI_Device;

	class TimeBlock
	{
	public:
		TimeBlock() = default;
		~TimeBlock();

		void Start(const std::string& name, bool profile_cpu = false, bool profile_gpu = false, const TimeBlock* parent = nullptr, const std::shared_ptr<RHI_Device>& rhi_device = nullptr);
		void End(const std::shared_ptr<RHI_Device>& rhi_device = nullptr);
		void OnFrameEnd(const std::shared_ptr<RHI_Device>& rhi_device);
		void Clear();

		const bool IsProfilingCpu() const	{ return m_profiling_cpu; }
		const bool IsProfilingGpu() const	{ return m_profiling_gpu; }
		const bool IsComplete() const		{ return m_is_complete; }
		const auto& GetName() const	        { return m_name; }
		const auto GetParent() const	    { return m_parent; }
		auto GetTreeDepth()	const	        { return m_tree_depth; }
		auto GetDurationCpu() const		    { return m_duration_cpu; }
		auto GetDurationGpu() const		    { return m_duration_gpu; }

	private:	
		static uint32_t FindTreeDepth(const TimeBlock* time_block, uint32_t depth = 0);

		std::string m_name;
		RHI_Device* m_rhi_device;
		bool m_has_started	= false;
		bool m_is_complete	= false;

		// Hierarchy
		const TimeBlock* m_parent	= nullptr;
		uint32_t m_tree_depth	    = 0;

		// CPU timing
		bool m_profiling_cpu	= false;
		float m_duration_cpu	= 0.0f;
		std::chrono::steady_clock::time_point start;
		std::chrono::steady_clock::time_point end;
	
		// GPU timing
		bool m_profiling_gpu	= false;
		float m_duration_gpu	= 0.0f;
		void* m_query			= nullptr;
		void* m_query_start		= nullptr;
		void* m_query_end		= nullptr;
	};
}
