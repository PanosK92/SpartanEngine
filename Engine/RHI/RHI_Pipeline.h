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
#include <memory>
#include <vector>
#include "../Core/EngineDefs.h"
#include "../Math/Rectangle.h"
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
//=============================

namespace Directus
{
	class Context;
	class Profiler;
	namespace Math
	{
		class Rectangle;
	}

	struct ConstantBuffer
	{
		ConstantBuffer(void* buffer, const unsigned int slot, const RHI_Buffer_Scope scope)
		{
			this->buffer	= buffer;
			this->slot		= slot;
			this->scope		= scope;
		}

		void* buffer;
		unsigned int slot;
		RHI_Buffer_Scope scope;
	};

	class ENGINE_CLASS RHI_Pipeline
	{
	public:
		RHI_Pipeline(Context* context, const std::shared_ptr<RHI_Device>& rhi_device);
		~RHI_Pipeline(){}

		//= DRAW =========================================================================================
		bool Draw(unsigned int vertex_count);
		bool DrawIndexed(unsigned int index_count, unsigned int index_offset, unsigned int vertex_offset);
		//================================================================================================

		//= SET ==============================================================================================================================
		bool SetShader(const std::shared_ptr<RHI_Shader>& shader);
		bool SetVertexShader(const std::shared_ptr<RHI_Shader>& shader);
		bool SetPixelShader(const std::shared_ptr<RHI_Shader>& shader);
		void SetTexture(const std::shared_ptr<RHI_RenderTexture>& texture);
		void SetTexture(const std::shared_ptr<RHI_Texture>& texture);
		void SetTexture(const RHI_Texture* texture);
		void SetTexture(void* texture);
		bool SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depth_stencil_state);
		bool SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizer_state);
		bool SetBlendState(const std::shared_ptr<RHI_BlendState>& blend_state);
		bool SetInputLayout(const std::shared_ptr<RHI_InputLayout>& input_layout);
		bool SetIndexBuffer(const std::shared_ptr<RHI_IndexBuffer>& index_buffer);
		bool SetVertexBuffer(const std::shared_ptr<RHI_VertexBuffer>& vertex_buffer);
		bool SetSampler(const std::shared_ptr<RHI_Sampler>& sampler);	
		void SetViewport(const RHI_Viewport& viewport);
		void SetScissorRectangle(const Math::Rectangle& rectangle);
		bool SetRenderTarget(const std::shared_ptr<RHI_RenderTexture>& render_target, void* depth_stencil_view = nullptr, bool clear = false);
		bool SetRenderTarget(const std::vector<void*>& render_target_views, void* depth_stencil_view = nullptr, bool clear = false);
		bool SetRenderTarget(void* render_target_view, void* depth_stencil_view = nullptr, bool clear = false);
		bool SetConstantBuffer(const std::shared_ptr<RHI_ConstantBuffer>& constant_buffer, unsigned int slot, RHI_Buffer_Scope scope);
		void SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology);		
		//====================================================================================================================================

		//= STATES ==
		void Clear();
		bool Bind();
		//===========

	private:
		// Pipeline	
		std::shared_ptr<RHI_InputLayout> m_input_layout;
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_state;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_state;
		std::shared_ptr<RHI_BlendState> m_blend_state;
		std::shared_ptr<RHI_IndexBuffer> m_index_buffer;
		std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer;
		std::shared_ptr<RHI_Shader> m_vertex_shader;
		std::shared_ptr<RHI_Shader> m_pixel_shader;
		RHI_Viewport m_viewport;
		Math::Rectangle m_scissor_rectangle;
		RHI_PrimitiveTopology_Mode m_primitive_topology;
		std::vector<ConstantBuffer> m_constant_buffers;
		std::vector<void*> m_samplers;
		std::vector<void*> m_textures;
		std::vector<void*> m_render_target_views;	
		void* m_depth_stencil_view = nullptr;
		bool m_render_targets_clear;

		// Dirty flags
		bool m_primitive_topology_dirty		= false;
		bool m_input_layout_dirty			= false;
		bool m_depth_stencil_state_dirty	= false;
		bool m_raterizer_state_dirty		= false;
		bool m_samplers_dirty				= false;
		bool m_textures_dirty				= false;
		bool m_index_buffer_dirty			= false;
		bool m_vertex_buffer_dirty			= false;
		bool m_constant_buffer_dirty		= false;
		bool m_vertex_shader_dirty			= false;
		bool m_pixel_shader_dirty			= false;
		bool m_viewport_dirty				= false;
		bool m_blend_state_dirty			= false;
		bool m_render_targets_dirty			= false;
		bool m_scissor_rectangle_dirty		= false;

		// Misc
		std::shared_ptr<RHI_Device> m_rhi_device;
		Profiler* m_profiler;
	};
}