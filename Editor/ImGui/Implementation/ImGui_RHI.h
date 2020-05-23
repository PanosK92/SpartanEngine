/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "RHI/RHI_Device.h"
#include "RHI/RHI_Shader.h"
#include "RHI/RHI_Texture2D.h"
#include "RHI/RHI_SwapChain.h"
#include "RHI/RHI_BlendState.h"
#include "RHI/RHI_CommandList.h"
#include "RHI/RHI_IndexBuffer.h"
#include "RHI/RHI_VertexBuffer.h"
#include "RHI/RHI_PipelineState.h"
#include "RHI/RHI_RasterizerState.h"
#include "RHI/RHI_DepthStencilState.h"
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

	// RHI Data	
	static shared_ptr<RHI_Device>				g_rhi_device;
	static shared_ptr<RHI_Texture>				g_texture;
	static shared_ptr<RHI_VertexBuffer>			g_vertex_buffer;
	static shared_ptr<RHI_IndexBuffer>			g_index_buffer;
	static shared_ptr<RHI_DepthStencilState>	g_depth_stencil_state;
	static shared_ptr<RHI_RasterizerState>		g_rasterizer_state;
	static shared_ptr<RHI_BlendState>			g_blend_state;
	static shared_ptr<RHI_Shader>				g_shader_vertex;
    static shared_ptr<RHI_Shader>				g_shader_pixel;
	static RHI_Viewport							g_viewport;

	inline bool Initialize(Context* context, const float width, const float height)
	{
		g_context	    = context;
		g_renderer	    = context->GetSubsystem<Renderer>();
		g_rhi_device    = g_renderer->GetRhiDevice();
		
		if (!g_context || !g_rhi_device || !g_rhi_device->IsInitialized())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Create required RHI objects
		{
			g_vertex_buffer			= make_shared<RHI_VertexBuffer>(g_rhi_device, static_cast<uint32_t>(sizeof(ImDrawVert)));
			g_index_buffer			= make_shared<RHI_IndexBuffer>(g_rhi_device);
			g_depth_stencil_state	= make_shared<RHI_DepthStencilState>(g_rhi_device, false, g_renderer->GetComparisonFunction());

			g_rasterizer_state = make_shared<RHI_RasterizerState>
			(
			    g_rhi_device,
			    RHI_Cull_None,
			    RHI_Fill_Solid,
			    true,	// depth clip
			    true,	// scissor
			    false,	// multi-sample
			    false	// anti-aliased lines
			);

			g_blend_state = make_shared<RHI_BlendState>
			(
			    g_rhi_device,
			    true,
			    RHI_Blend_Src_Alpha,		// source blend
			    RHI_Blend_Inv_Src_Alpha,	// destination blend
			    RHI_Blend_Operation_Add,	// blend op
			    RHI_Blend_Inv_Src_Alpha,	// source blend alpha
			    RHI_Blend_Zero,				// destination blend alpha
			    RHI_Blend_Operation_Add		// destination op alpha
			);

            // Compile shaders
            const std::string shader_path = g_context->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Shaders) + "/ImGui.hlsl";
            g_shader_vertex = make_shared<RHI_Shader>(g_context);
            g_shader_vertex->Compile<RHI_Vertex_Pos2dTexCol8>(RHI_Shader_Vertex, shader_path);
            g_shader_pixel = make_shared<RHI_Shader>(g_context);
            g_shader_pixel->Compile(RHI_Shader_Pixel, shader_path);
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
			g_texture = make_shared<RHI_Texture2D>(g_context, atlas_width, atlas_height, RHI_Format_R8G8B8A8_Unorm, data);
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

        // Grab the swap chain we will be working wit
        bool is_child_window        = swap_chain_other != nullptr;
        RHI_SwapChain* swap_chain   = is_child_window ? swap_chain_other : g_renderer->GetSwapChain();
        RHI_CommandList* cmd_list   = swap_chain->GetCmdList();

        if (!cmd_list->IsRecording())
        {
            LOG_ERROR("Command list is not recording, can't render anything");
            return;
        }

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
			const auto wvp = Matrix
			(
				2.0f / (R - L),	0.0f,			0.0f,	(R + L) / (L - R),
				0.0f,			2.0f / (T - B), 0.0f,	(T + B) / (B - T),
				0.0f,			0.0f,			0.5f,	0.5f,
				0.0f,			0.0f,			0.0f,	1.0f
			);

            g_renderer->SetGlobalShaderObjectTransform(wvp);
		}

        // Compute viewport
        g_viewport.width    = draw_data->DisplaySize.x;
        g_viewport.height   = draw_data->DisplaySize.y;

		// Set render state
        static RHI_PipelineState pipeline_state = {};
        pipeline_state.shader_vertex            = g_shader_vertex.get();
        pipeline_state.shader_pixel             = g_shader_pixel.get();
        pipeline_state.rasterizer_state         = g_rasterizer_state.get();
        pipeline_state.blend_state              = g_blend_state.get();
        pipeline_state.depth_stencil_state      = g_depth_stencil_state.get();
        pipeline_state.vertex_buffer_stride     = g_vertex_buffer->GetStride();
        pipeline_state.render_target_swapchain  = swap_chain;
        pipeline_state.clear_color[0]           = clear ? Vector4(0.0f, 0.0f, 0.0f, 1.0f) : state_color_load;
        pipeline_state.viewport                 = g_viewport;
        pipeline_state.dynamic_scissor          = true;
        pipeline_state.primitive_topology       = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                = is_child_window ? "pass_imgui_window_child" : "pass_imgui_window_main";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Transition layouts
            for (auto i = 0; i < draw_data->CmdListsCount; i++)
            {
                auto cmd_list_imgui = draw_data->CmdLists[i];
                for (int cmd_i = 0; cmd_i < cmd_list_imgui->CmdBuffer.Size; cmd_i++)
                {
                    const auto pcmd = &cmd_list_imgui->CmdBuffer[cmd_i];
                    if (RHI_Texture* texture = static_cast<RHI_Texture*>(pcmd->TextureId))
                    {
                        texture->SetLayout(RHI_Image_Shader_Read_Only_Optimal, cmd_list);
                    }
                }
            }

            cmd_list->SetBufferVertex(g_vertex_buffer);
            cmd_list->SetBufferIndex(g_index_buffer);

            // Render command lists
            int global_vtx_offset = 0;
            int global_idx_offset = 0;
            const auto& clip_off = draw_data->DisplayPos;
            Math::Rectangle scissor_rect;
            for (auto i = 0; i < draw_data->CmdListsCount; i++)
            {
                auto cmd_list_imgui = draw_data->CmdLists[i];
                for (auto cmd_i = 0; cmd_i < cmd_list_imgui->CmdBuffer.Size; cmd_i++)
                {
                    const auto pcmd = &cmd_list_imgui->CmdBuffer[cmd_i];
                    if (pcmd->UserCallback != nullptr)
                    {
                        pcmd->UserCallback(cmd_list_imgui, pcmd);
                    }
                    else
                    {
                        // Compute scissor rectangle
                        scissor_rect.left      = pcmd->ClipRect.x - clip_off.x;
                        scissor_rect.top       = pcmd->ClipRect.y - clip_off.y;
                        scissor_rect.right     = pcmd->ClipRect.z - clip_off.x;
                        scissor_rect.bottom    = pcmd->ClipRect.w - clip_off.y;

                        // Apply scissor rectangle, bind texture and draw
                        cmd_list->SetScissorRectangle(scissor_rect);
                        cmd_list->SetTexture(28, static_cast<RHI_Texture*>(pcmd->TextureId));
                        cmd_list->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
                    }

                }
                global_idx_offset += cmd_list_imgui->IdxBuffer.Size;
                global_vtx_offset += cmd_list_imgui->VtxBuffer.Size;
            }

            cmd_list->EndRenderPass();
        }
	}

	inline void OnResize(const float width, const float height)
	{
		if (!g_renderer || !g_renderer->GetSwapChain())
			return;

        // Validate resolution
        if (!g_rhi_device->ValidateResolution(static_cast<uint32_t>(width), static_cast<uint32_t>(height)))
        {
            LOG_WARNING("%dx%d is an invalid resolution", width, height);
            return;
        }

        g_renderer->GetSwapChain()->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
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
			RHI_Format_R8G8B8A8_Unorm,
			2,
            RHI_Present_Immediate | RHI_Swap_Flip_Discard
		);
	}

	static void _DestroyWindow(ImGuiViewport* viewport)
	{
		if (!viewport)
		{
            LOG_ERROR("Invalid viewport");
			return;
		}

        RHI_SwapChain* swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		safe_delete(swap_chain);
		viewport->RendererUserData = nullptr;
	}

	static void _SetWindowSize(ImGuiViewport* viewport, const ImVec2 size)
	{
		if (!viewport)
		{
            LOG_ERROR("Invalid viewport");
			return;
		}

		auto swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		if (!swap_chain)
		{
            LOG_ERROR("Invalid swapchain");
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
            LOG_ERROR("Invalid viewport");
			return;
		}

        RHI_SwapChain* swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		if (!swap_chain)
		{
            LOG_ERROR("Invalid swapchain");
			return;
		}

        if (!swap_chain->GetCmdList()->Begin())
        {
            LOG_ERROR("Failed to begin command list");
        }

		const auto clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
		RenderDrawData(viewport->DrawData, swap_chain, clear);

        if (!swap_chain->GetCmdList()->Submit())
        {
            LOG_ERROR("Failed to submit command list");
        }
	}

	static void _Present(ImGuiViewport* viewport, void*)
	{
		if (!viewport)
		{
            LOG_ERROR("Invalid viewport");
			return;
		}

		const auto swap_chain = static_cast<RHI_SwapChain*>(viewport->RendererUserData);
		if (!swap_chain)
		{
            LOG_ERROR("Invalid swapchain");
			return;
		}

        if (!swap_chain->Present())
        {
            LOG_ERROR("Failed to present");
        }
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
