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

//= INCLUDES =====================
#include "RHI_Pipeline.h"
#include "RHI_Implementation.h"
#include "RHI_Sampler.h"
#include "RHI_Device.h"
#include "RHI_RenderTexture.h"
#include "RHI_VertexBuffer.h"
#include "RHI_IndexBuffer.h"
#include "RHI_Texture.h"
#include "RHI_Shader.h"
#include "RHI_ConstantBuffer.h"
#include "RHI_InputLayout.h"
#include "RHI_DepthStencilState.h"
#include "RHI_RasterizerState.h"
#include "RHI_BlendState.h"
#include "../Logging/Log.h"
#include "../Profiling/Profiler.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	RHI_Pipeline::RHI_Pipeline(Context* context, const shared_ptr<RHI_Device>& rhi_device)
	{
		m_rhi_device	= rhi_device;
		m_profiler		= context->GetSubsystem<Profiler>().get();
	}

	bool RHI_Pipeline::DrawIndexed(const unsigned int index_count, const unsigned int index_offset, const unsigned int vertex_offset)
	{
		const auto bind = Bind();
		const auto draw = m_rhi_device->DrawIndexed(index_count, index_offset, vertex_offset);
		m_profiler->m_rhi_draw_calls++;
		return bind && draw;
	}

	bool RHI_Pipeline::Draw(const unsigned int vertex_count)
	{
		const auto bind	= Bind();
		const auto draw	= m_rhi_device->Draw(vertex_count);
		m_profiler->m_rhi_draw_calls++;
		return bind && draw;
	}

	bool RHI_Pipeline::SetShader(const shared_ptr<RHI_Shader>& shader)
	{
		return SetVertexShader(shader) && SetPixelShader(shader);
	}

	bool RHI_Pipeline::SetVertexShader(const shared_ptr<RHI_Shader>& shader)
	{
		if (!shader || !shader->HasVertexShader())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_vertex_shader)
		{
			if (m_vertex_shader->RHI_GetID() == shader->RHI_GetID())
				return true;
		}

		SetInputLayout(shader->GetInputLayout()); // TODO: this has to be done outside of this function 
		m_vertex_shader		= shader;
		m_vertex_shader_dirty = true;

		return true;
	}

	bool RHI_Pipeline::SetPixelShader(const shared_ptr<RHI_Shader>& shader)
	{
		if (!shader || !shader->HasPixelShader())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_pixel_shader)
		{
			if (m_pixel_shader->RHI_GetID() == shader->RHI_GetID())
				return true;
		}

		m_pixel_shader		= shader;
		m_pixel_shader_dirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetIndexBuffer(const shared_ptr<RHI_IndexBuffer>& index_buffer)
	{
		if (!index_buffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_index_buffer		= index_buffer;
		m_index_buffer_dirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetVertexBuffer(const shared_ptr<RHI_VertexBuffer>& vertex_buffer)
	{
		if (!vertex_buffer)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		m_vertex_buffer		= vertex_buffer;
		m_vertex_buffer_dirty = true;

		return true;
	}

	bool RHI_Pipeline::SetSampler(const shared_ptr<RHI_Sampler>& sampler)
	{
		m_samplers.emplace_back(sampler ? sampler->GetBuffer() : nullptr);
		m_samplers_dirty = true;

		return true;
	}

	void RHI_Pipeline::SetTexture(const shared_ptr<RHI_RenderTexture>& texture)
	{
		// allow for null texture to be bound so we can maintain slot order
		m_textures.emplace_back(texture ? texture->GetShaderResource() : nullptr);
		m_textures_dirty = true;
	}

	void RHI_Pipeline::SetTexture(const shared_ptr<RHI_Texture>& texture)
	{
		// allow for null texture to be bound so we can maintain slot order
		m_textures.emplace_back(texture ? texture->GetShaderResource() : nullptr);
		m_textures_dirty = true;
	}

	void RHI_Pipeline::SetTexture(const RHI_Texture* texture)
	{
		// allow for null texture to be bound so we can maintain slot order
		m_textures.emplace_back(texture ? texture->GetShaderResource() : nullptr);
		m_textures_dirty = true;
	}

	void RHI_Pipeline::SetTexture(void* texture)
	{
		m_textures.emplace_back(texture);
		m_textures_dirty = true;
	}

	bool RHI_Pipeline::SetRenderTarget(const shared_ptr<RHI_RenderTexture>& render_target, void* depth_stencil_view /*= nullptr*/, const bool clear /*= false*/)
	{
		if (!render_target)
			return false;

		m_render_target_views.clear();
		m_render_target_views.emplace_back(render_target->GetRenderTargetView());
		m_depth_stencil_view		= depth_stencil_view;
		m_render_targets_clear	= clear;
		m_render_targets_dirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetRenderTarget(void* render_target_view, void* depth_stencil_view /*= nullptr*/, const bool clear /*= false*/)
	{
		if (!render_target_view)
			return false;

		m_render_target_views.clear();
		m_render_target_views.emplace_back(render_target_view);

		m_depth_stencil_view			= depth_stencil_view;
		m_render_targets_clear	= clear;
		m_render_targets_dirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetRenderTarget(const vector<void*>& render_target_views, void* depth_stencil_view /*= nullptr*/, const bool clear /*= false*/)
	{
		if (render_target_views.empty())
			return false;

		m_render_target_views.clear();
		for (const auto& render_target : render_target_views)
		{
			if (!render_target)
				continue;

			m_render_target_views.emplace_back(render_target);
		}

		m_depth_stencil_view		= depth_stencil_view;
		m_render_targets_clear	= clear;
		m_render_targets_dirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetConstantBuffer(const shared_ptr<RHI_ConstantBuffer>& constant_buffer, unsigned int slot, RHI_Buffer_Scope scope)
	{
		auto buffer_ptr = constant_buffer ? constant_buffer->GetBuffer() : nullptr;
		m_constant_buffers.emplace_back(buffer_ptr, slot, scope);
		m_constant_buffer_dirty = true;

		return true;
	}

	void RHI_Pipeline::SetPrimitiveTopology(const RHI_PrimitiveTopology_Mode primitive_topology)
	{
		if (m_primitive_topology == primitive_topology)
			return;
	
		m_primitive_topology			= primitive_topology;
		m_primitive_topology_dirty	= true;
	}

	bool RHI_Pipeline::SetInputLayout(const shared_ptr<RHI_InputLayout>& input_layout)
	{
		if (!input_layout)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_input_layout)
		{
			if (m_input_layout->GetInputLayout() == input_layout->GetInputLayout())
				return true;
		}

		m_input_layout		= input_layout;
		m_input_layout_dirty	= true;

		return true;
	}

	bool RHI_Pipeline::SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depth_stencil_state)
	{
		if (!depth_stencil_state)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_depth_stencil_state)
		{
			if (m_depth_stencil_state->GetDepthEnabled() == depth_stencil_state->GetDepthEnabled())
				return true;
		}

		m_depth_stencil_state			= depth_stencil_state;
		m_depth_stencil_state_dirty	= true;
		return true;
	}

	bool RHI_Pipeline::SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizer_state)
	{
		if (!rasterizer_state)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_rasterizer_state)
		{
			const auto equal =
				m_rasterizer_state->GetCullMode()				== rasterizer_state->GetCullMode()			&&
				m_rasterizer_state->GetFillMode()				== rasterizer_state->GetFillMode()			&&
				m_rasterizer_state->GetDepthClipEnabled()		== rasterizer_state->GetDepthClipEnabled()	&&
				m_rasterizer_state->GetScissorEnabled()			== rasterizer_state->GetScissorEnabled()	&&
				m_rasterizer_state->GetMultiSampleEnabled()		== rasterizer_state->GetMultiSampleEnabled() &&
				m_rasterizer_state->GetAntialisedLineEnabled()	== rasterizer_state->GetAntialisedLineEnabled();

			if (equal)
				return true;
		}

		m_rasterizer_state		= rasterizer_state;
		m_raterizer_state_dirty	= true;
		return true;
	}

	bool RHI_Pipeline::SetBlendState(const std::shared_ptr<RHI_BlendState>& blend_state)
	{
		if (!blend_state)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		if (m_blend_state)
		{
			if (m_blend_state->BlendEnabled() == blend_state->BlendEnabled())
				return true;
		}

		m_blend_state		= blend_state;
		m_blend_state_dirty	= true;
		return true;
	}

	void RHI_Pipeline::SetViewport(const RHI_Viewport& viewport)
	{
		if (viewport == m_viewport)
			return;

		m_viewport		= viewport;
		m_viewport_dirty = true;
	}

	void RHI_Pipeline::SetScissorRectangle(const Math::Rectangle& rectangle)
	{
		if (m_scissor_rectangle == rectangle)
			return;

		m_scissor_rectangle		= rectangle;
		m_scissor_rectangle_dirty = true;
	}

	bool RHI_Pipeline::Bind()
	{
		if (!m_rhi_device)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return false;
		}

		// Render Targets
		if (m_render_targets_dirty)
		{
			if (m_render_target_views.empty())
			{
				LOG_ERROR("Invalid render target(s)");
				return false;
			}

			m_rhi_device->SetRenderTargets(static_cast<unsigned int>(m_render_target_views.size()), &m_render_target_views[0], m_depth_stencil_view);
			m_profiler->m_rhi_bindings_render_target++;

			if (m_render_targets_clear)
			{
				for (const auto& render_target_view : m_render_target_views)
				{
					m_rhi_device->ClearRenderTarget(render_target_view, Vector4(0, 0, 0, 0));
				}

				if (m_depth_stencil_view)
				{
					const auto depth = Settings::Get().GetReverseZ() ? 1.0f - m_viewport.GetMaxDepth() : m_viewport.GetMaxDepth();
					m_rhi_device->ClearDepthStencil(m_depth_stencil_view, Clear_Depth, depth, 0);
				}
			}

			m_render_targets_clear = false;
			m_render_targets_dirty = false;
		}

		// Textures
		if (m_textures_dirty)
		{
			const unsigned int start_slot	= 0;
			const auto texture_count		= static_cast<unsigned int>(m_textures.size());
			void* textures					= texture_count != 0 ? &m_textures[0] : nullptr;

			m_rhi_device->SetTextures(start_slot, texture_count, textures);
			m_profiler->m_rhi_bindings_texture++;

			m_textures.clear();
			m_textures_dirty = false;
		}

		// Samplers
		if (m_samplers_dirty)
		{
			const unsigned int start_slot	= 0;
			const auto sampler_count		= static_cast<unsigned int>(m_samplers.size());
			void* samplers					= sampler_count != 0 ? &m_samplers[0] : nullptr;

			m_rhi_device->SetSamplers(start_slot, sampler_count, samplers);
			m_profiler->m_rhi_bindings_sampler++;

			m_samplers.clear();
			m_samplers_dirty = false;
		}

		// Constant buffers
		if (m_constant_buffer_dirty)
		{
			for (const auto& constant_buffer : m_constant_buffers)
			{
				m_rhi_device->SetConstantBuffers(constant_buffer.slot, 1, (void*)&constant_buffer.buffer, constant_buffer.scope);
				m_profiler->m_rhi_bindings_buffer_constant += (constant_buffer.scope == Buffer_Global) ? 2 : 1;
			}

			m_constant_buffers.clear();
			m_constant_buffer_dirty = false;
		}

		// Vertex shader
		auto result_vertex_shader = false;
		if (m_vertex_shader_dirty)
		{
			result_vertex_shader = m_rhi_device->SetVertexShader(m_vertex_shader);
			m_profiler->m_rhi_bindings_vertex_shader++;
			m_vertex_shader_dirty = false;
		}

		// Pixel shader
		auto result_pixel_shader = false;
		if (m_pixel_shader_dirty)
		{
			result_pixel_shader = m_rhi_device->SetPixelShader(m_pixel_shader);
			m_profiler->m_rhi_bindings_pixel_shader++;
			m_pixel_shader_dirty = false;
		}

		// Input layout
		auto result_input_layout = false;
		if (m_input_layout_dirty)
		{
			result_input_layout = m_rhi_device->SetInputLayout(m_input_layout);
			m_input_layout_dirty = false;
		}

		// Viewport
		auto result_viewport = false;
		if (m_viewport_dirty)
		{
			result_viewport = m_rhi_device->SetViewport(m_viewport);
			m_viewport_dirty = false;
		}

		auto result_scissor_rectangle = false;
		if (m_scissor_rectangle_dirty)
		{
			result_scissor_rectangle = m_rhi_device->SetScissorRectangle(m_scissor_rectangle);
			m_scissor_rectangle_dirty = false;
		}

		// Primitive topology
		auto result_primitive_topology = false;
		if (m_primitive_topology_dirty)
		{
			result_primitive_topology = m_rhi_device->SetPrimitiveTopology(m_primitive_topology);
			m_primitive_topology_dirty = false;
		}

		// Depth-Stencil state
		auto result_depth_stencil_state = false;
		if (m_depth_stencil_state_dirty)
		{
			result_depth_stencil_state = m_rhi_device->SetDepthStencilState(m_depth_stencil_state);
			m_depth_stencil_state_dirty = false;
		}

		// Rasterizer state
		auto result_rasterizer_state = false;
		if (m_raterizer_state_dirty)
		{
			result_rasterizer_state = m_rhi_device->SetRasterizerState(m_rasterizer_state);
			m_raterizer_state_dirty = false;
		}

		// Index buffer
		auto result_index_buffer = false;
		if (m_index_buffer_dirty)
		{
			result_index_buffer = m_rhi_device->SetIndexBuffer(m_index_buffer);
			m_profiler->m_rhi_bindings_buffer_index++;
			m_index_buffer_dirty = false;
		}

		// Vertex buffer
		auto result_vertex_buffer = false;
		if (m_vertex_buffer_dirty)
		{
			result_vertex_buffer = m_rhi_device->SetVertexBuffer(m_vertex_buffer);
			m_profiler->m_rhi_bindings_buffer_vertex++;
			m_vertex_buffer_dirty = false;
		}

		// Blend state
		auto result_blend_state = false;
		if (m_blend_state_dirty)
		{
			result_blend_state = m_rhi_device->SetBlendState(m_blend_state);
			m_blend_state_dirty = false;
		}

		return 
			result_vertex_shader && 
			result_pixel_shader &&
			result_input_layout &&
			result_viewport &&
			result_scissor_rectangle &&
			result_primitive_topology &&
			result_depth_stencil_state &&
			result_rasterizer_state &&
			result_index_buffer && 
			result_vertex_buffer && 
			result_blend_state;
	}

	void RHI_Pipeline::Clear()
	{
		vector<void*> empty(30);
		void* empty_ptr	= &empty[0];
		
		// Render targets
		m_rhi_device->SetRenderTargets(8, empty_ptr, nullptr);
		m_render_target_views.clear();
		m_depth_stencil_view		= nullptr;
		m_render_targets_clear	= false;
		m_render_targets_dirty	= false;

		// Textures
		m_rhi_device->SetTextures(0, 20, empty_ptr);
		m_textures.clear();
		m_textures_dirty = false;

		// Samplers
		m_rhi_device->SetSamplers(0, 10, empty_ptr);
		m_samplers.clear();
		m_samplers_dirty = false;

		// Constant buffers
		m_rhi_device->SetConstantBuffers(0, 10, empty_ptr, Buffer_Global);
		m_constant_buffers.clear();
		m_constant_buffer_dirty = false;
	}
}