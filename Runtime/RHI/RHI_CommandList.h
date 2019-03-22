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

namespace Directus
{
	class Profiler;

	enum RHI_Cmd_Type
	{
		RHI_Cmd_Begin,
		RHI_Cmd_End,
		RHI_Cmd_Draw,
		RHI_Cmd_DrawIndexed,
		RHI_Cmd_ClearRenderTarget,
		RHI_Cmd_ClearDepthStencil,
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
		RHI_Cmd_SetRenderTargets
	};

	struct RHI_Command
	{
		RHI_Cmd_Type type;

		// Render targets
		std::vector<void*> render_targets;
		std::vector<void*> render_targets_clear;
		std::vector<Math::Vector4> render_target_clear_color;

		// Texture
		std::vector<void*> textures;
		unsigned textures_start_slot = 0;

		// Samplers
		std::vector<void*> samplers;
		unsigned samplers_start_slot = 0;

		// Constant buffers
		std::vector<void*> constant_buffers;
		unsigned constant_buffers_start_slot	= 0;
		RHI_Buffer_Scope constant_buffers_scope = Buffer_NotAssigned;

		// Depth
		const RHI_DepthStencilState* depth_stencil_state	= nullptr;
		void* depth_stencil									= nullptr;
		float depth_clear									= 0;
		unsigned int depth_clear_stencil					= 0;
		unsigned int depth_clear_flags						= 0;

		// Misc	
		std::string pass_name								= "N/A";
		RHI_PrimitiveTopology_Mode primitive_topology		= PrimitiveTopology_NotAssigned;
		unsigned int vertex_count							= 0;
		unsigned int vertex_offset							= 0;
		unsigned int index_count							= 0;
		unsigned int index_offset							= 0;	
		RHI_Viewport viewport;
		Math::Rectangle scissor_rectangle;
		const RHI_InputLayout* input_layout					= nullptr;	
		const RHI_RasterizerState* rasterizer_state			= nullptr;
		const RHI_BlendState* blend_state					= nullptr;
		const RHI_IndexBuffer* buffer_index					= nullptr;
		const RHI_VertexBuffer* buffer_vertex				= nullptr;
		const RHI_Shader* shader_vertex						= nullptr;
		const RHI_Shader* shader_pixel						= nullptr;
	};

	class ENGINE_CLASS RHI_CommandList
	{
	public:
		RHI_CommandList(RHI_Device* rhi_device, Profiler* profiler);
		~RHI_CommandList() = default;
	
		void Begin(const std::string& pass_name);
		void End();
		void Draw(unsigned int vertex_count);
		void DrawIndexed(unsigned int index_count, unsigned int index_offset, unsigned int vertex_offset);
		void ClearRenderTarget(void* render_target, const Math::Vector4& color);
		void ClearDepthStencil(void* depth_stencil, unsigned int flags, float depth, unsigned int stencil = 0);
		void SetViewport(const RHI_Viewport& viewport);
		void SetScissorRectangle(const Math::Rectangle& scissor_rectangle);
		void SetPrimitiveTopology(RHI_PrimitiveTopology_Mode primitive_topology);

		void SetInputLayout(const RHI_InputLayout* input_layout);
		void SetInputLayout(const std::shared_ptr<RHI_InputLayout>& input_layout);

		void SetDepthStencilState(const RHI_DepthStencilState* depth_stencil_state);
		void SetDepthStencilState(const std::shared_ptr<RHI_DepthStencilState>& depth_stencil_state);

		void SetRasterizerState(const RHI_RasterizerState* rasterizer_state);
		void SetRasterizerState(const std::shared_ptr<RHI_RasterizerState>& rasterizer_state);

		void SetBlendState(const RHI_BlendState* blend_state);
		void SetBlendState(const std::shared_ptr<RHI_BlendState>& blend_state);

		void SetBufferVertex(const RHI_VertexBuffer* buffer);
		void SetBufferVertex(const std::shared_ptr<RHI_VertexBuffer>& buffer);

		void SetBufferIndex(const RHI_IndexBuffer* buffer);
		void SetBufferIndex(const std::shared_ptr<RHI_IndexBuffer>& buffer);

		void SetShaderVertex(const RHI_Shader* shader);
		void SetShaderVertex(const std::shared_ptr<RHI_Shader>& shader);

		void SetShaderPixel(const RHI_Shader* shader);
		void SetShaderPixel(const std::shared_ptr<RHI_Shader>& shader);

		void SetConstantBuffers(unsigned int start_slot, const std::vector<void*>& constant_buffers, RHI_Buffer_Scope scope);
		void SetSamplers(unsigned int start_slot, const std::vector<void*>& samplers);
		void SetTextures(unsigned int start_slot, const std::vector<void*>& textures);
		void SetRenderTargets(const std::vector<void*>& render_targets, void* depth_stencil = nullptr);

		void Clear();
		void Submit();

	private:
		std::vector<RHI_Command> m_commands;
		RHI_Device* m_rhi_device	= nullptr;
		Profiler* m_profiler		= nullptr;
	};
}