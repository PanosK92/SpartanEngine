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
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "RHI_Shader.h"
#include <memory>
#include <map>
#include "../Math/Rectangle.h"
//============================

namespace Spartan
{
	class RHI_Pipeline
	{
	public:
		RHI_Pipeline(const std::shared_ptr<RHI_Device>& rhi_device);
		~RHI_Pipeline();
	
		bool Create();
		void UpdateDescriptorSets(RHI_Texture* texture = nullptr);
		void OnCommandListConsumed();

		auto GetPipeline() const			{ return m_graphics_pipeline; }
		auto GetPipelineLayout() const		{ return m_pipeline_layout; }
		auto GetRenderPass() const			{ return m_render_pass; }
		auto GetDescriptorSet(uint32_t id) 	{ return m_descriptor_set_cache.count(id) ? m_descriptor_set_cache[id] : nullptr; }
		auto GetDescriptorSet()				{ return m_descriptor_set_cache.size() ? m_descriptor_set_cache.begin()->second : nullptr; }
	
		std::shared_ptr<RHI_Shader> m_shader_vertex;
		std::shared_ptr<RHI_Shader> m_shader_pixel;
		std::shared_ptr<RHI_InputLayout> m_input_layout;	
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_state;
		std::shared_ptr<RHI_BlendState> m_blend_state;
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_state;
		RHI_Viewport m_viewport;
		Math::Rectangle m_scissor;
		RHI_PrimitiveTopology_Mode m_primitive_topology = PrimitiveTopology_NotAssigned;
		RHI_Sampler* m_sampler							= nullptr;
		RHI_ConstantBuffer* m_constant_buffer			= nullptr;
		RHI_VertexBuffer* m_vertex_buffer				= nullptr;

	private:
		bool CreateDescriptorPool();
		bool CreateDescriptorSetLayout();
		void ReflectShaders();

		void* m_graphics_pipeline		= nullptr;
		void* m_pipeline_layout			= nullptr;
		void* m_render_pass				= nullptr;
		void* m_descriptor_pool			= nullptr;
		void* m_descriptor_set_layout	= nullptr;
		uint32_t m_desctiptor_set_capacity = 0;
		std::map<uint32_t, void*> m_descriptor_set_cache;
		std::map<std::string, Shader_Resource> m_shader_resources;

		std::shared_ptr<RHI_Device> m_rhi_device;
	};
}
