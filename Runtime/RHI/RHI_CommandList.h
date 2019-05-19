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
#include <vector>
#include "RHI_Definition.h"
#include "RHI_Viewport.h"
#include "../Math/Rectangle.h"
#include "../Math/Vector4.h"
//============================

namespace Spartan
{
	class Profiler;

	enum RHI_Cmd_Type
	{
		RHI_Cmd_Begin,
		RHI_Cmd_End,
		RHI_Cmd_Draw,
		RHI_Cmd_DrawIndexed,
		RHI_Cmd_SetViewport,
		RHI_Cmd_SetScissorRectangle,
		RHI_Cmd_SetPrimitiveTopology,
		RHI_Cmd_SetInputLayout,
		RHI_Cmd_SetDepthStencilState,
		RHI_Cmd_SetRasterizerState,
		RHI_Cmd_SetBlendState,
		RHI_Cmd_SetVertexBuffer,
		RHI_Cmd_SetIndexBuffer,	
		RHI_Cmd_SetVertexShader,
		RHI_Cmd_SetPixelShader,
		RHI_Cmd_SetConstantBuffers,
		RHI_Cmd_SetSamplers,
		RHI_Cmd_SetTextures,
		RHI_Cmd_SetRenderTargets,
		RHI_Cmd_ClearRenderTarget,
		RHI_Cmd_ClearDepthStencil
	};

	struct RHI_Command
	{
		RHI_Command()
		{
			const uint32_t max_count = 10;
			render_targets.reserve(max_count);
			render_targets.resize(max_count);
			textures.reserve(max_count);
			textures.resize(max_count);
			samplers.reserve(max_count);
			samplers.resize(max_count);
			constant_buffers.reserve(max_count);
			constant_buffers.resize(max_count);
			Clear();
		}

		void Clear()
		{
			render_target_count			= 0;
			textures_start_slot			= 0;
			texture_count				= 0;
			samplers_start_slot			= 0;
			sampler_count				= 0;
			constant_buffers_start_slot	= 0;
			constant_buffer_count		= 0;
			constant_buffers_scope		= Buffer_NotAssigned;
			depth_stencil_state			= nullptr;
			depth_stencil				= nullptr;
			depth_clear					= 0;
			depth_clear_stencil			= 0;
			depth_clear_flags			= 
			vertex_count				= 0;
			vertex_offset				= 0;
			index_count					= 0;
			index_offset				= 0;
			input_layout				= nullptr;
			rasterizer_state			= nullptr;
			blend_state					= nullptr;
			buffer_index				= nullptr;
			buffer_vertex				= nullptr;
			shader_vertex				= nullptr;
			shader_pixel				= nullptr;
			pass_name					= "N/A";
			primitive_topology			= PrimitiveTopology_NotAssigned;
		}

		RHI_Cmd_Type type;

		// Render targets
		uint32_t render_target_count = 0;
		std::vector<void*> render_targets;
		void* render_target_clear;
		Math::Vector4 render_target_clear_color;

		// Texture
		uint32_t textures_start_slot = 0;
		uint32_t texture_count = 0;
		std::vector<void*> textures;

		// Samplers
		uint32_t samplers_start_slot = 0;
		uint32_t sampler_count = 0;
		std::vector<void*> samplers;

		// Constant buffers
		uint32_t constant_buffers_start_slot = 0;
		uint32_t constant_buffer_count = 0;
		RHI_Buffer_Scope constant_buffers_scope;
		std::vector<void*> constant_buffers;	

		// Depth
		const RHI_DepthStencilState* depth_stencil_state	= nullptr;
		void* depth_stencil									= nullptr;
		float depth_clear									= 0;
		uint32_t depth_clear_stencil						= 0;
		uint32_t depth_clear_flags							= 0;

		// Misc	
		std::string pass_name							= "N/A";
		RHI_PrimitiveTopology_Mode primitive_topology	= PrimitiveTopology_NotAssigned;
		uint32_t vertex_count							= 0;
		uint32_t vertex_offset							= 0;
		uint32_t index_count							= 0;
		uint32_t index_offset							= 0;		
		const RHI_InputLayout* input_layout				= nullptr;	
		const RHI_RasterizerState* rasterizer_state		= nullptr;
		const RHI_BlendState* blend_state				= nullptr;
		const RHI_IndexBuffer* buffer_index				= nullptr;
		const RHI_VertexBuffer* buffer_vertex			= nullptr;
		const RHI_Shader* shader_vertex					= nullptr;
		const RHI_Shader* shader_pixel					= nullptr;
		RHI_Viewport viewport;
		Math::Rectangle scissor_rectangle;
	};

	class SPARTAN_CLASS RHI_CommandList
	{
	public:
		RHI_CommandList(const std::shared_ptr<RHI_Device>& rhi_device, Profiler* profiler);
		~RHI_CommandList();

		void Begin(const std::string& pass_name, void* render_pass = nullptr, RHI_SwapChain* swap_chain = nullptr);
		void End();

		void Draw(uint32_t vertex_count);
		void DrawIndexed(uint32_t index_count, uint32_t index_offset, uint32_t vertex_offset);

		void SetPipeline(RHI_Pipeline* pipeline);

		void SetViewport(const RHI_Viewport& viewport);
		void SetScissorRectangle(const Math::Rectangle& scissor_rectangle);
		void SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology);

		void SetInputLayout(const RHI_InputLayout* input_layout);
		void SetInputLayout(const std::shared_ptr<RHI_InputLayout>& input_layout) { SetInputLayout(input_layout.get()); }

		void SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state);
		void SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depth_stencil_state) { SetDepthStencilState(depth_stencil_state.get()); }

		void SetRasterizerState(const RHI_RasterizerState* rasterizer_state);
		void SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizer_state) { SetRasterizerState(rasterizer_state.get()); }

		void SetBlendState(const RHI_BlendState* blend_state);
		void SetBlendState(const std::shared_ptr<RHI_BlendState>& blend_state) { SetBlendState(blend_state.get()); }

		void SetBufferVertex(const RHI_VertexBuffer* buffer);
		void SetBufferVertex(const std::shared_ptr<RHI_VertexBuffer>& buffer) { SetBufferVertex(buffer.get()); }

		void SetBufferIndex(const RHI_IndexBuffer* buffer);
		void SetBufferIndex(const std::shared_ptr<RHI_IndexBuffer>& buffer) { SetBufferIndex(buffer.get()); }

		void SetShaderVertex(const RHI_Shader* shader);
		void SetShaderVertex(const std::shared_ptr<RHI_Shader>& shader) { SetShaderVertex(shader.get()); }

		void SetShaderPixel(const RHI_Shader* shader);
		void SetShaderPixel(const std::shared_ptr<RHI_Shader>& shader) { SetShaderPixel(shader.get()); }

		void SetConstantBuffers(uint32_t start_slot, RHI_Buffer_Scope scope, const std::vector<void*>& constant_buffers);
		void SetConstantBuffer(uint32_t slot, RHI_Buffer_Scope scope, const std::shared_ptr<RHI_ConstantBuffer>& constant_buffer);
			
		void SetSamplers(uint32_t start_slot, const std::vector<void*>& samplers);
		void SetSampler(uint32_t slot, const std::shared_ptr<RHI_Sampler>& sampler);
		
		void SetTextures(uint32_t start_slot, const std::vector<void*>& textures);
		void SetTexture(uint32_t slot, RHI_Texture* texture);
		void SetTexture(uint32_t slot, const std::shared_ptr<RHI_Texture>& texture) { SetTexture(slot, texture.get()); }
		void ClearTextures() { SetTextures(0, m_textures_empty); }

		void SetRenderTargets(const std::vector<void*>& render_targets, void* depth_stencil = nullptr);
		void SetRenderTarget(void* render_target, void* depth_stencil = nullptr);
		void SetRenderTarget(const std::shared_ptr<RHI_Texture>&, void* depth_stencil = nullptr);

		void ClearRenderTarget(void* render_target, const Math::Vector4& color);
		void ClearRenderTargets(const std::vector<void*>& render_targets, const Math::Vector4& color)
		{
			for (const auto& render_target : render_targets)
			{
				ClearRenderTarget(render_target, color);
			}
		}
		void ClearDepthStencil(void* depth_stencil, uint32_t flags, float depth, uint32_t stencil = 0);

		bool Submit();
		const auto& GetSemaphoreRenderFinished() { return !m_semaphores_render_finished.empty() ? m_semaphores_render_finished[m_current_frame] : nullptr; }

	private:
		void OnCmdListConsumed();
		void Clear();

		RHI_SwapChain* m_swap_chain = nullptr;

		// D3D11
		RHI_Command& GetCmd();
		std::vector<RHI_Command> m_commands;
		uint32_t m_initial_capacity = 2500;
		uint32_t m_command_count	= 0;

		// Vulkan
		RHI_Command m_empty_cmd; // for GetCmd()
		RHI_Pipeline* m_pipeline		= nullptr;
		void* m_cmd_pool				= nullptr;
		uint32_t m_current_frame	= 0;
		bool m_is_recording				= false;
		bool m_sync_cpu_to_gpu			= false;
		std::vector<void*> m_cmd_buffers;
		std::vector<void*> m_semaphores_render_finished;
		std::vector<void*> m_fences_in_flight;

		// Dependencies
		std::vector<void*> m_textures_empty	= std::vector<void*>(10);
		Profiler* m_profiler = nullptr;
		std::shared_ptr<RHI_Device> m_rhi_device;		
	};
}