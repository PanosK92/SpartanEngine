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
#include "ImGui_RHI.h"
#include "../Source/imgui.h"
#include <vector>
#include "Profiling/Profiler.h"
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
	RHI_Pipeline* g_pipeline	= nullptr;
	Profiler* g_profiler		= nullptr;

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
	struct VertexConstantBuffer { Matrix mvp; };

	inline bool Initialize(Context* context)
	{
		g_context	= context;
		g_profiler	= context->GetSubsystem<Profiler>().get();
		g_renderer	= context->GetSubsystem<Renderer>().get();
		g_pipeline	= g_renderer->GetRhiPipeline().get();
		g_device	= g_renderer->GetRhiDevice();
		
		if (!g_context || !g_device || !g_device->IsInitialized())
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		// Font atlas texture
		g_fontTexture = make_shared<RHI_Texture>(g_context);

		// Font atlas sampler
		g_fontSampler = make_shared<RHI_Sampler>(g_device, Texture_Filter_Bilinear, Texture_Address_Wrap, Comparison_Always);

		// Constant buffer
		g_constantBuffer = make_shared<RHI_ConstantBuffer>(g_device, static_cast<unsigned int>(sizeof(VertexConstantBuffer)));

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
		g_shader->CompileVertexPixel(shader, Input_Position2DTextureColor8);

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

	inline void RenderDrawData(ImDrawData* draw_data, const bool is_other_window = false)
	{
		TIME_BLOCK_START_MULTI(g_profiler);
		g_device->EventBegin("Pass_ImGui");

		// Grow vertex buffer as needed
		if (g_vertexBuffer->GetVertexCount() < static_cast<unsigned int>(draw_data->TotalVtxCount))
		{
			const unsigned int new_size = draw_data->TotalVtxCount + 5000;
			if (!g_vertexBuffer->CreateDynamic(sizeof(ImDrawVert), new_size))
				return;
		}

		// Grow index buffer as needed
		if (g_indexBuffer->GetIndexCount() < static_cast<unsigned int>(draw_data->TotalIdxCount))
		{
			const unsigned int new_size = draw_data->TotalIdxCount + 10000;
			if (!g_indexBuffer->CreateDynamic(sizeof(ImDrawIdx), new_size))
				return;
		}

		// Copy and convert all vertices into a single contiguous buffer
		auto vtx_dst	= static_cast<ImDrawVert*>(g_vertexBuffer->Map());
		auto* idx_dst	= static_cast<ImDrawIdx*>(g_indexBuffer->Map());
		for (auto i = 0; i < draw_data->CmdListsCount; i++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[i];
			memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
			memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
			vtx_dst += cmd_list->VtxBuffer.Size;
			idx_dst += cmd_list->IdxBuffer.Size;
		}
		g_vertexBuffer->Unmap();
		g_indexBuffer->Unmap();

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

			auto buffer = static_cast<VertexConstantBuffer*>(g_constantBuffer->Map());
			buffer->mvp = mvp;
			g_constantBuffer->Unmap();
		}

		// Setup render state
		if (!is_other_window)
		{
			g_pipeline->Clear();
			g_renderer->SwapChain_SetAsRenderTarget();
			g_renderer->SwapChain_Clear(Vector4(0, 0, 0, 1));
		}
		const auto viewport = RHI_Viewport(0.0f, 0.0f, draw_data->DisplaySize.x, draw_data->DisplaySize.y);
		g_pipeline->SetViewport(viewport);
		g_pipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		g_pipeline->SetBlendState(g_blendState);
		g_pipeline->SetDepthStencilState(g_depthStencilState);
		g_pipeline->SetRasterizerState(g_rasterizerState);	
		g_pipeline->SetVertexShader(g_shader);
		g_pipeline->SetPixelShader(g_shader);
		g_pipeline->SetVertexBuffer(g_vertexBuffer);
		g_pipeline->SetIndexBuffer(g_indexBuffer);
		g_pipeline->SetConstantBuffer(g_constantBuffer, 0, Buffer_VertexShader);
		g_pipeline->SetSampler(g_fontSampler);

		// Render command lists
		auto vtx_offset = 0;
		auto idx_offset = 0;
		const auto pos = draw_data->DisplayPos;
		for (auto i = 0; i < draw_data->CmdListsCount; i++)
		{
			const ImDrawList* cmd_list = draw_data->CmdLists[i];
			for (auto cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
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
					g_pipeline->SetScissorRectangle(scissor_rect);

					// Bind texture, Draw
					const auto texture_srv = static_cast<void*>(pcmd->TextureId);
					g_pipeline->SetTexture(texture_srv);
					g_pipeline->DrawIndexed(pcmd->ElemCount, idx_offset, vtx_offset);
					g_pipeline->Bind();
				}
				idx_offset += pcmd->ElemCount;
			}
			vtx_offset += cmd_list->VtxBuffer.Size;
		}

		if (!is_other_window)
		{
			g_renderer->SwapChain_Present();
		}

		g_device->EventEnd();
		TIME_BLOCK_END_MULTI(g_profiler);
	}

	inline void OnResize(const unsigned int width, const unsigned int height)
	{
		if (!g_renderer)
			return;

		g_renderer->SwapChain_Resize(width, height);
	}

	//--------------------------------------------
	// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
	//--------------------------------------------
	struct RHI_Window
	{
		std::shared_ptr<RHI_SwapChain> swap_chain;
	};

	inline void _CreateWindow(ImGuiViewport* viewport)
	{
		auto data = IM_NEW(RHI_Window)();
		viewport->RendererUserData = data;

		data->swap_chain = make_shared<RHI_SwapChain>
		(
			viewport->PlatformHandle,
			g_device,
			static_cast<unsigned int>(viewport->Size.x),
			static_cast<unsigned int>(viewport->Size.y),
			Format_R8G8B8A8_UNORM,
			Swap_Discard,
			0
		);
	}

	inline void _DestroyWindow(ImGuiViewport* viewport)
	{
		const auto window = static_cast<RHI_Window*>(viewport->RendererUserData);
		if (window) { IM_DELETE(window); }
		viewport->RendererUserData = nullptr;
	}

	inline void _SetWindowSize(ImGuiViewport* viewport, const ImVec2 size)
	{
		auto window = static_cast<RHI_Window*>(viewport->RendererUserData);
		if (!window || !window->swap_chain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}
		
		if (!window->swap_chain->Resize(static_cast<unsigned int>(size.x), static_cast<unsigned int>(size.y)))
		{
			LOG_ERROR("Failed to resize swap chain");
		}
	}

	inline void _RenderWindow(ImGuiViewport* viewport, void*)
	{
		const auto window = static_cast<RHI_Window*>(viewport->RendererUserData);
		if (!window || !window->swap_chain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}

		const auto clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
		window->swap_chain->SetAsRenderTarget();
		if (clear) window->swap_chain->Clear(Vector4(0, 0, 0, 1));

		RenderDrawData(viewport->DrawData, true);
	}

	inline void _SwapBuffers(ImGuiViewport* viewport, void*)
	{
		const auto window = static_cast<RHI_Window*>(viewport->RendererUserData);
		if (!window || !window->swap_chain)
		{
			LOG_ERROR_INVALID_INTERNALS();
			return;
		}

		window->swap_chain->Present(Present_Off);
	}

	inline void InitializePlatformInterface()
	{
		auto& platform_io					= GetPlatformIO();
		platform_io.Renderer_CreateWindow	= _CreateWindow;
		platform_io.Renderer_DestroyWindow	= _DestroyWindow;
		platform_io.Renderer_SetWindowSize	= _SetWindowSize;
		platform_io.Renderer_RenderWindow	= _RenderWindow;
		platform_io.Renderer_SwapBuffers	= _SwapBuffers;
	}
}