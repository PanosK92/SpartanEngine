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
#include "RHI_PipelineState.h"
//============================

namespace Spartan
{
	class RHI_Pipeline
	{
	public:
		RHI_Pipeline() = default;
		RHI_Pipeline(const std::shared_ptr<RHI_Device>& rhi_device, const RHI_PipelineState& pipeline_state);
		~RHI_Pipeline();
	
		void UpdateDescriptorSets(RHI_Texture* texture = nullptr);
		void OnCommandListConsumed();

		auto GetPipeline() const					{ return m_pipeline; }
		auto GetPipelineLayout() const				{ return m_pipeline_layout; }
		auto GetRenderPass() const					{ return m_render_pass; }
		auto GetState() const						{ return m_state; }
		auto GetDescriptorSet(const uint32_t id) 	{ return m_descriptor_set_cache.count(id) ? m_descriptor_set_cache[id] : nullptr; }
		auto GetDescriptorSet()						{ return !m_descriptor_set_cache.empty() ? m_descriptor_set_cache.begin()->second : nullptr; }

	private:
		bool CreateDescriptorPool();
		bool CreateDescriptorSetLayout();
		void ReflectShaders();

		// API
		void* m_pipeline					= nullptr;
		void* m_pipeline_layout				= nullptr;
		void* m_render_pass					= nullptr;
		void* m_descriptor_pool				= nullptr;
		void* m_descriptor_set_layout		= nullptr;
		std::map<uint32_t, void*> m_descriptor_set_cache;
		uint32_t m_descriptor_set_capacity	= 20;
		const RHI_PipelineState* m_state	= nullptr;
		std::map<std::string, Shader_Resource> m_shader_resources;
		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}
