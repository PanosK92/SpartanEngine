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

//= INCLUDES =========================
#include <vector>
#include "ImGui_RHI.h"
#include "../Source/imgui.h"
#include "Rendering/Renderer.h"
#include "RHI/RHI_Sampler.h"
#include "RHI/RHI_Texture2D.h"
#include "RHI/RHI_Device.h"
#include "RHI/RHI_VertexBuffer.h"
#include "RHI/RHI_IndexBuffer.h"
#include "RHI/RHI_ConstantBuffer.h"
#include "RHI/RHI_DepthStencilState.h"
#include "RHI/RHI_RasterizerState.h"
#include "RHI/RHI_BlendState.h"
#include "RHI/RHI_Shader.h"
#include "RHI/RHI_SwapChain.h"
#include "RHI/RHI_CommandList.h"
#include "RHI/RHI_PipelineCache.h"
//====================================

namespace ImGui::RHI
{
	using namespace Spartan;
	using namespace Math;
	using namespace std;

	// Forward Declarations
	void InitializePlatformInterface();

	// Engine subsystems
	Context*	g_context		= nullptr;
	Renderer*	g_renderer		= nullptr;
	RHI_CommandList* g_cmd_list	= nullptr;

	// RHI Data	
	static shared_ptr<RHI_Device>				g_rhi_device;
	static shared_ptr<RHI_Texture>				g_texture;
	static shared_ptr<RHI_Sampler>				g_sampler;
	static shared_ptr<RHI_ConstantBuffer>		g_constant_buffer;
	static shared_ptr<RHI_VertexBuffer>			g_vertex_buffer;
	static shared_ptr<RHI_IndexBuffer>			g_index_buffer;
	static shared_ptr<RHI_DepthStencilState>	g_depth_stencil_state;
	static shared_ptr<RHI_RasterizerState>		g_rasterizer_state;
	static shared_ptr<RHI_BlendState>			g_blend_state;
	static shared_ptr<RHI_Shader>				g_shader;
	static shared_ptr<RHI_PipelineCache>		g_pipeline_cache;
	static RHI_Viewport							g_viewport;

	inline bool Initialize(Context* context, const float width, const float height)
	{
		g_context			= context;
		g_renderer			= context->GetSubsystem<Renderer>().get();
		g_pipeline_cache	= g_renderer->GetPipelineCache();
		g_cmd_list			= g_renderer->GetCmdList().get();
		g_rhi_device		= g_renderer->GetRhiDevice();
		
		if (!g_context || !g_rhi_device || !g_rhi_device->IsInitialized())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Create required RHI objects
		{
			g_sampler				= make_shared<RHI_Sampler>(g_rhi_device, SAMPLER_BILINEAR, Sampler_Address_Wrap, Comparison_Always);
			g_constant_buffer		= make_shared<RHI_ConstantBuffer>(g_rhi_device); g_constant_buffer->Create<Matrix>();
			g_vertex_buffer			= make_shared<RHI_VertexBuffer>(g_rhi_device, sizeof(ImDrawVert));
			g_index_buffer			= make_shared<RHI_IndexBuffer>(g_rhi_device);
			g_depth_stencil_state	= make_shared<RHI_DepthStencilState>(g_rhi_device, false);

			g_rasterizer_state = make_shared<RHI_RasterizerState>
				(
					g_rhi_device,
					Cull_None,
					Fill_Solid,
					true,	// depth clip
					true,	// scissor
					false,	// multi-sample
					false	// anti-aliased lines
					);

			g_blend_state = make_shared<RHI_BlendState>
				(
					g_rhi_device,
					true,
					Blend_Src_Alpha,		// source blend
					Blend_Inv_Src_Alpha,	// destination blend
					Blend_Operation_Add,	// blend op
					Blend_Inv_Src_Alpha,	// source blend alpha
					Blend_Zero,				// destination blend alpha
					Blend_Operation_Add		// destination op alpha
					);

			// Shader
			static string shader_source =
				"SamplerState sampler0;"
				"Texture2D texture0;"
				"cbuffer vertexBuffer : register(b0)"
				"{"
				"	matrix transform;"
				"};"
				"struct VS_INPUT"
				"{"
				"	float2 pos	: POSITION0;"
				"	float2 uv	: TEXCOORD0;"
				"	float4 col	: COLOR0;"
				"};"
				"struct PS_INPUT"
				"{"
				"	float4 pos : SV_POSITION;"
				"	float4 col : COLOR;"
				"	float2 uv  : TEXCOORD;"
				"};"
				"PS_INPUT mainVS(VS_INPUT input)"
				"{"
				"	PS_INPUT output;"
				"	output.pos = mul(transform, float4(input.pos.xy, 0.f, 1.f));"
				"	output.col = input.col;	"
				"	output.uv  = input.uv;"
				"	return output;"
				"}"
				"float4 mainPS(PS_INPUT input) : SV_Target"
				"{"
				"	return input.col * texture0.Sample(sampler0, input.uv);"
				"}";
			g_shader = make_shared<RHI_Shader>(g_rhi_device);
			g_shader->Compile<RHI_Vertex_Pos2dTexCol8>(Shader_VertexPixel, shader_source);
		}

		// Font atlas
		{
			unsigned char* pixels;
			int atlas_width, atlas_height, bpp;
			auto& io = GetIO();
			io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height, &bpp);

			// Copy pixel data
			const unsigned int size = atlas_width * atlas_height * bpp;
			vector<std::byte> data(size);
			data.reserve(size);
			memcpy(&data[0], reinterpret_cast<std::byte*>(pixels), size);

			// Upload texture to graphics system
			g_texture = make_shared<RHI_Texture2D>(g_context, atlas_width, atlas_height, Format_R8G8B8A8_UNORM, data);
			io.Fonts->TexID = static_cast<ImTextureID>(g_texture.get());
		}

		// Setup back-end capabilities flags
		auto& io = GetIO();
		io.BackendFlags			|= ImGuiBackendFlags_RendererHasViewports;
		io.BackendFlags			|= ImGuiBackendFlags_RendererHasVtxOffset;
		io.BackendRendererName	= "RHI";
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			InitializePlatformInterface();
		}

		return true;
	}

	inline void Shutdown()
	{
		DestroyPlatformWindows();
	}

	inline void RenderDrawData(ImDrawData* draw_data, RHI_SwapChain* swap_chain_other = nullptr, const bool clear = true)
	{
		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		const auto fb_width		= static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
		const auto fb_height	= static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
		if (fb_width <= 0 || fb_height <= 0 || draw_data->TotalVtxCount == 0)
			return;

		const auto is_main_viewport = (swap_chain_other == nullptr);

		// Updated vertex and index buffers
		{
			// Grow vertex buffer as needed
			if (g_vertex_buffer->GetVertexCount() < static_cast<unsigned int>(draw_data->TotalVtxCount))
			{
				const unsigned int new_size = draw_data->TotalVtxCount + 5000;
				if (!g_vertex_buffer->CreateDynamic<ImDrawVert>(new_size))
					return;
			}

			// Grow index buffer as needed
			if (g_index_buffer->GetIndexCount() < static_cast<unsigned int>(draw_data->TotalIdxCount))
			{
				const unsigned int new_size = draw_data->TotalIdxCount + 10000;
				if (!g_index_buffer->CreateDynamic<ImDrawIdx>(new_size))
					return;
			}

			// Copy and convert all vertices into a single contiguous buffer		
			auto vtx_dst = static_cast<ImDrawVert*>(g_vertex_buffer->Map());
			auto idx_dst = static_cast<ImDrawIdx*>(g_index_buffer->Map());
			if (vtx_dst && idx_dst)
			{
				for (auto i = 0; i < draw_data->CmdListsCount; i++)
				{
					const ImDrawList* cmd_list = draw_data->CmdLists[i];
					memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
					memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
					vtx_dst += cmd_list->VtxBuffer.Size;
					idx_dst += cmd_list->IdxBuffer.Size;
				}

				g_vertex_buffer->Unmap();
				g_index_buffer->Unmap();
			}
		}

		// Setup orthographic projection matrix into our constant buffer
		// Our visible ImGui space lies from draw_data->DisplayPos (top left) to 
		// draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is (0,0) for single viewport apps.
		{
			const auto L = draw_data->DisplayPos.x;
			const auto R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
			const auto T = draw_data->DisplayPos.y;
			const auto B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
			const auto mvp = Matrix
			(
				2.0f / (R - L),	0.0f,			0.0f,	(R + L) / (L - R),
				0.0f,			2.0f / (T - B), 0.0f,	(T + B) / (B - T),
				0.0f,			0.0f,			0.5f,	0.5f,
				0.0f,			0.0f,			0.0f,	1.0f
			);

			if (const auto buffer = static_cast<Matrix*>(g_constant_buffer->Map()))
			{
				*buffer = mvp;
				g_constant_buffer->Unmap();
			}
		}

		// Set render state
		{
			// Compute viewport
			g_viewport.width	= draw_data->DisplaySize.x;
			g_viewport.height	= draw_data->DisplaySize.y;

			// Setup pipeline
			RHI_PipelineState state		= {};
			state.shader_vertex			= g_shader.get();
			state.shader_pixel			= g_shader.get();
			state.input_layout			= g_shader->GetInputLayout().get();
			state.constant_buffer		= g_constant_buffer.get();
			state.rasterizer_state		= g_rasterizer_state.get();
			state.blend_state			= g_blend_state.get();
			state.depth_stencil_state	= g_depth_stencil_state.get();
			state.sampler				= g_sampler.get();
			state.vertex_buffer			= g_vertex_buffer.get();
			state.primitive_topology	= PrimitiveTopology_TriangleList;
			state.swap_chain			= is_main_viewport ? g_renderer->GetSwapChain().get() : swap_chain_other;

			// Start witting command list
			g_cmd_list->Begin("Pass_ImGui", g_pipeline_cache->GetPipeline(state).get());
			g_cmd_list->SetRenderTarget(state.swap_chain->GetRenderTargetView());
			if (clear) g_cmd_list->ClearRenderTarget(state.swap_chain->GetRenderTargetView(), Vector4(0, 0, 0, 1));
			g_cmd_list->SetViewport(g_viewport);
			g_cmd_list->SetBufferVertex(g_vertex_buffer);
			g_cmd_list->SetBufferIndex(g_index_buffer);
			g_cmd_list->SetConstantBuffer(0, Buffer_VertexShader, g_constant_buffer);
			g_cmd_list->SetSampler(0, g_sampler);
		}
		
		// Render command lists
		int global_vtx_offset = 0;
		int global_idx_offset = 0;
		const auto& clip_off = draw_data->DisplayPos;
		for (auto i = 0; i < draw_data->CmdListsCount; i++)
		{
			auto cmd_list = draw_data->CmdLists[i];
			for (auto cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
			{
				auto pcmd = &cmd_list->CmdBuffer[cmd_i];
				if (pcmd->UserCallback != nullptr)
				{
					pcmd->UserCallback(cmd_list, pcmd);
				}
				else
				{
					// Compute scissor rectangle
					auto scissor_rect	 = Math::Rectangle(pcmd->ClipRect.x - clip_off.x, pcmd->ClipRect.y - clip_off.y, pcmd->ClipRect.z - clip_off.x, pcmd->ClipRect.w - clip_off.y);
					scissor_rect.width	-= scissor_rect.x;
					scissor_rect.height -= scissor_rect.y;
					
					// Apply scissor rectangle, bind texture and draw
					g_cmd_list->SetScissorRectangle(scissor_rect);
					g_cmd_list->SetTexture(0, static_cast<RHI_Texture*>(pcmd->TextureId));
					g_cmd_list->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
				}
				
			}
			global_idx_offset += cmd_list->IdxBuffer.Size;
			global_vtx_offset += cmd_list->VtxBuffer.Size;
		}

		g_cmd_list->End();
		if (g_cmd_list->Submit() && is_main_viewport)
		{
            g_renderer->GetSwapChain()->Present();
		}
	}

	inline void OnResize(const unsigned int width, const unsigned int height)
	{
		if (!g_renderer)
			return;

        g_renderer->GetSwapChain()->Resize(width, height);
	}

	//--------------------------------------------
	// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
	//--------------------------------------------
	static void _CreateWindow(ImGuiViewport* viewport)
	{
		if (!viewport)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		viewport->RendererUserData = new RHI_SwapChain
		(
			viewport->PlatformHandle,
			g_rhi_device,
			static_cast<uint32_t>(viewport->Size.x),
			static_cast<uint32_t>(viewport->Size.y),
			Format_R8G8B8A8_UNORM,
			Present_Immediate,
			2
		);
	}

	static void _DestroyWindow(ImGuiViewport* viewport)
	{
		if (!viewport)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		safe_delete(swap_chain);
		viewport->RendererUserData = nullptr;
	}

	static void _SetWindowSize(ImGuiViewport* viewport, const ImVec2 size)
	{
		if (!viewport)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		auto swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		if (!swap_chain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}
		
		if (!swap_chain->Resize(static_cast<unsigned int>(size.x), static_cast<unsigned int>(size.y)))
		{
			LOG_ERROR("Failed to resize swap chain");
		}
	}

	static void _RenderWindow(ImGuiViewport* viewport, void*)
	{
		if (!viewport)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		const auto swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		if (!swap_chain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}

		const auto clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
		RenderDrawData(viewport->DrawData, swap_chain, clear);
	}

	static void _Present(ImGuiViewport* viewport, void*)
	{
		if (!viewport)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}

		const auto swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		if (!swap_chain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}

		swap_chain->Present();
	}

	inline void InitializePlatformInterface()
	{
		auto& platform_io					= GetPlatformIO();
		platform_io.Renderer_CreateWindow	= _CreateWindow;
		platform_io.Renderer_DestroyWindow	= _DestroyWindow;
		platform_io.Renderer_SetWindowSize	= _SetWindowSize;
		platform_io.Renderer_RenderWindow	= _RenderWindow;
		platform_io.Renderer_SwapBuffers	= _Present;
	}
}
