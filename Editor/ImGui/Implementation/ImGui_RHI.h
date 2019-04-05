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
#include "RHI/RHI_Texture.h"
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
//====================================

namespace ImGui::RHI
{
	using namespace Directus;
	using namespace Math;
	using namespace std;

	// Forward Declarations
	void InitializePlatformInterface();

	// Engine subsystems
	Context*	g_context		= nullptr;
	Renderer*	g_renderer		= nullptr;
	RHI_CommandList* g_cmd_list	= nullptr;

	// RHI Data	
	shared_ptr<RHI_Device>				g_device;
	shared_ptr<RHI_Texture>				g_fontTexture;
	shared_ptr<RHI_Sampler>				g_fontSampler;
	shared_ptr<RHI_ConstantBuffer>		g_constantBuffer;
	shared_ptr<RHI_VertexBuffer>		g_vertexBuffer;
	shared_ptr<RHI_IndexBuffer>			g_indexBuffer;
	shared_ptr<RHI_BlendState>			g_blendState;
	shared_ptr<RHI_RasterizerState>		g_rasterizerState;
	shared_ptr<RHI_DepthStencilState>	g_depthStencilState;
	shared_ptr<RHI_Shader>				g_shader;

	inline bool Initialize(Context* context)
	{
		g_context	= context;
		g_renderer	= context->GetSubsystem<Renderer>().get();
		g_cmd_list	= g_renderer->GetCmdList().get();
		g_device	= g_renderer->GetRhiDevice();
		
		if (!g_context || !g_device || !g_device->IsInitialized())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Font atlas texture
		g_fontTexture = make_shared<RHI_Texture>(g_context);

		// Font atlas sampler
		g_fontSampler = make_shared<RHI_Sampler>(g_device, Texture_Filter_Bilinear, Sampler_Address_Wrap, Comparison_Always);

		// Constant buffer
		g_constantBuffer = make_shared<RHI_ConstantBuffer>(g_device, static_cast<unsigned int>(sizeof(Matrix)));

		// Vertex buffer
		g_vertexBuffer = make_shared<RHI_VertexBuffer>(g_device);

		// Index buffer
		g_indexBuffer = make_shared<RHI_IndexBuffer>(g_device, sizeof(ImDrawIdx) == 2 ? Format_R16_UINT : Format_R32_UINT);

		// Create depth-stencil State
		g_depthStencilState = make_shared<RHI_DepthStencilState>(g_device, false);

		// Rasterizer state
		g_rasterizerState = make_shared<RHI_RasterizerState>
		(
			g_device,
			Cull_None,
			Fill_Solid,
			true,	// depth clip
			true,	// scissor
			false,	// multi-sample
			false	// anti-aliased lines
		);

		// Blend state
		g_blendState = make_shared<RHI_BlendState>
		(
			g_device,
			true,
			Blend_Src_Alpha,		// source blend
			Blend_Inv_Src_Alpha,	// destination blend
			Blend_Operation_Add,	// blend op
			Blend_Inv_Src_Alpha,	// source blend alpha
			Blend_Zero,				// destination blend alpha
			Blend_Operation_Add		// destination op alpha
		);

		// Shader
		static string shader =
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
		g_shader = make_shared<RHI_Shader>(g_device);
		g_shader->Compile(Shader_VertexPixel, shader, Input_Position2DTextureColor8);

		// Setup back-end capabilities flags
		auto& io = GetIO();	
		io.BackendFlags			|= ImGuiBackendFlags_RendererHasViewports;
		io.BackendRendererName	= "RHI";
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			InitializePlatformInterface();
		}

		// Font atlas
		{
			unsigned char* pixels;
			int width, height, bpp;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height, &bpp);

			// Copy pixel data
			const unsigned int size = width * height * bpp;
			vector<std::byte> data(size);
			data.reserve(size);
			memcpy(&data[0], reinterpret_cast<std::byte*>(pixels), size);

			// Upload texture to graphics system
			if (g_fontTexture->ShaderResource_Create2D(width, height, 4, Format_R8G8B8A8_UNORM, data, false))
			{
				io.Fonts->TexID = static_cast<ImTextureID>(g_fontTexture->GetShaderResource());
			}
		}

		return true;
	}

	inline void Shutdown()
	{
		DestroyPlatformWindows();
	}

	inline void RenderDrawData(ImDrawData* draw_data, void* render_target = nullptr, bool clear = true)
	{
		bool is_main_viewport	= (render_target == nullptr);
		void* _render_target	= is_main_viewport ? g_renderer->GetSwapChain()->GetRenderTargetView() : render_target;
		g_cmd_list->Begin("Pass_ImGui");
		g_cmd_list->SetRenderTarget(_render_target);
		if (clear) g_cmd_list->ClearRenderTarget(_render_target, Vector4(0, 0, 0, 1));

		// Updated vertex and index buffers
		{
			// Grow vertex buffer as needed
			if (g_vertexBuffer->GetVertexCount() < static_cast<unsigned int>(draw_data->TotalVtxCount))
			{
				const unsigned int new_size = draw_data->TotalVtxCount + 5000;
				if (!g_vertexBuffer->CreateDynamic<ImDrawVert>(new_size))
				{
					g_cmd_list->End();
					g_cmd_list->Flush();
					g_cmd_list->Clear();
					return;
				}
			}

			// Grow index buffer as needed
			if (g_indexBuffer->GetIndexCount() < static_cast<unsigned int>(draw_data->TotalIdxCount))
			{
				const unsigned int new_size = draw_data->TotalIdxCount + 10000;
				if (!g_indexBuffer->CreateDynamic<ImDrawIdx>(new_size))
				{
					g_cmd_list->End();
					g_cmd_list->Flush();
					g_cmd_list->Clear();
					return;
				}
			}

			// Copy and convert all vertices into a single contiguous buffer		
			auto vtx_dst = static_cast<ImDrawVert*>(g_vertexBuffer->Map());
			auto idx_dst = static_cast<ImDrawIdx*>(g_indexBuffer->Map());
			if (vtx_dst && idx_dst)
			{
				for (int i = 0; i < draw_data->CmdListsCount; i++)
				{
					const ImDrawList* cmd_list = draw_data->CmdLists[i];
					memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
					memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
					vtx_dst += cmd_list->VtxBuffer.Size;
					idx_dst += cmd_list->IdxBuffer.Size;
				}

				g_vertexBuffer->Unmap();
				g_indexBuffer->Unmap();
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

			if (auto buffer = static_cast<Matrix*>(g_constantBuffer->Map()))
			{
				*buffer = mvp;
				g_constantBuffer->Unmap();
			}
		}

		const auto viewport = RHI_Viewport(0.0f, 0.0f, draw_data->DisplaySize.x, draw_data->DisplaySize.y);

		g_cmd_list->SetViewport(viewport);
		g_cmd_list->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		g_cmd_list->SetBlendState(g_blendState);
		g_cmd_list->SetDepthStencilState(g_depthStencilState);
		g_cmd_list->SetRasterizerState(g_rasterizerState);	
		g_cmd_list->SetShaderVertex(g_shader);
		g_cmd_list->SetShaderPixel(g_shader);
		g_cmd_list->SetInputLayout(g_shader->GetInputLayout());
		g_cmd_list->SetBufferVertex(g_vertexBuffer);
		g_cmd_list->SetBufferIndex(g_indexBuffer);
		g_cmd_list->SetConstantBuffer(0, Buffer_VertexShader, g_constantBuffer);
		g_cmd_list->SetSampler(0, g_fontSampler);

		// Render command lists
		unsigned int vtx_offset = 0;
		unsigned int idx_offset = 0;
		const auto& pos = draw_data->DisplayPos;
		for (int i = 0; i < draw_data->CmdListsCount; i++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[i];
			for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
			{
				const auto pcmd = &cmd_list->CmdBuffer[cmd_i];
				if (pcmd->UserCallback)
				{
					// User callback (registered via ImDrawList::AddCallback)
					pcmd->UserCallback(cmd_list, pcmd);
				}
				else
				{
					// Apply scissor rectangle
					auto scissor_rect	 = Rectangle(pcmd->ClipRect.x - pos.x, pcmd->ClipRect.y - pos.y, pcmd->ClipRect.z - pos.x, pcmd->ClipRect.w - pos.y);
					scissor_rect.width	-= scissor_rect.x;
					scissor_rect.height	-= scissor_rect.y;
					g_cmd_list->SetScissorRectangle(scissor_rect);

					// Bind texture, Draw
					g_cmd_list->SetTexture(0, pcmd->TextureId);
					g_cmd_list->DrawIndexed(pcmd->ElemCount, idx_offset, vtx_offset);
				}
				idx_offset += pcmd->ElemCount;
			}
			vtx_offset += cmd_list->VtxBuffer.Size;
		}

		g_cmd_list->End();
		g_cmd_list->Flush();
		g_cmd_list->Clear();

		if (is_main_viewport)
		{
			g_renderer->GetSwapChain()->Present(Present_Off);
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
			g_device,
			static_cast<unsigned int>(viewport->Size.x),
			static_cast<unsigned int>(viewport->Size.y),
			Format_R8G8B8A8_UNORM,
			Swap_Flip_Discard,
			SwapChain_Allow_Tearing,
			2
		);
	}

	static void _DestroyWindow(ImGuiViewport* viewport)
	{
		if (!viewport || !viewport->RendererUserData)
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

		safe_delete(swap_chain);
		viewport->RendererUserData = nullptr;
	}

	static void _SetWindowSize(ImGuiViewport* viewport, const ImVec2 size)
	{
		if (!viewport || !viewport->RendererUserData)
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
		if (!viewport || !viewport->RendererUserData)
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
		RenderDrawData(viewport->DrawData, swap_chain->GetRenderTargetView(), clear);
	}

	static void _Present(ImGuiViewport* viewport, void*)
	{
		if (!viewport || !viewport->RendererUserData)
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

		swap_chain->Present(Present_Off);
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