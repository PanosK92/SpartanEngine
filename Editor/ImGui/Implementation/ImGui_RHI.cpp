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

//= INCLUDES =========================
#include "ImGui_RHI.h"
#include "../Source/imgui.h"
#include <vector>
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
//====================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

// Forward Declarations
static void ImGui_RHI_InitPlatformInterface();
static void ImGui_RHI_ShutdownPlatformInterface();

// RHI Data
Context*							g_context;
shared_ptr<RHI_Device>				g_device;
shared_ptr<RHI_Texture>				g_fontTexture;
shared_ptr<RHI_Sampler>				g_fontSampler;
shared_ptr<RHI_ConstantBuffer>		g_constantBuffer;
shared_ptr<RHI_VertexBuffer>		g_vertexBuffer;
shared_ptr<RHI_IndexBuffer>			g_indexBuffer;
shared_ptr<RHI_BlendState>			g_blendState;
shared_ptr<RHI_RasterizerState>		g_rasterizerState;
shared_ptr<RHI_DepthStencilState>	g_depthStencilState;;

struct VertexConstantBuffer { Matrix mvp; };
static int	g_vertexBufferSize	= 0;
static int	g_indexBufferSize	= 0;

bool ImGui_RHI_Init(Context* context)
{
	g_device = context->GetSubsystem<Renderer>()->GetRHIDevice();

	// Setup back-end capabilities flags
	ImGuiIO& io = ImGui::GetIO();
	io.BackendFlags			|= ImGuiBackendFlags_RendererHasViewports;
	io.BackendRendererName	= "RHI";

	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		ImGui_RHI_InitPlatformInterface();
	}

	// Constant buffer
	g_constantBuffer = make_shared<RHI_ConstantBuffer>(g_device);

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
		Cull_Back,
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
		Blend_Inv_Src_Alpha,	// dest blend
		Blend_Operation_Add,	// blend op
		Blend_Inv_Src_Alpha,	// source blend alpha
		Blend_Zero,				// dest blend alpha
		Blend_Operation_Add		// dest op alpha
	);

	// Font atlas
	{
		ImGuiIO& io = ImGui::GetIO();
		vector<byte> pixels;
		unsigned int width, height;
		io.Fonts->GetTexDataAsRGBA32((unsigned char**)&pixels[0], (int*)&width, (int*)&height);

		// Upload texture to graphics system
		g_fontTexture = make_shared<RHI_Texture>(g_context);
		g_fontTexture->ShaderResource_Create2D(width, height, 4, Format_R8G8B8A8_UNORM, pixels, false);

		// Store our identifier
		io.Fonts->TexID = (ImTextureID)g_fontTexture->GetShaderResource();

		// Create texture sampler
		g_fontSampler = make_shared<RHI_Sampler>(g_device, Texture_Filter_Bilinear, Texture_Address_Wrap, Comparison_Always);
	}

	//// Create the vertex shader
	//{
	//	static const char* vertexShader =
	//		"cbuffer vertexBuffer : register(b0) \
	 //           {\
	 //           float4x4 ProjectionMatrix; \
	 //           };\
	 //           struct VS_INPUT\
	 //           {\
	 //           float2 pos : POSITION;\
	 //           float4 col : COLOR0;\
	 //           float2 uv  : TEXCOORD0;\
	 //           };\
	 //           \
	 //           struct PS_INPUT\
	 //           {\
	 //           float4 pos : SV_POSITION;\
	 //           float4 col : COLOR0;\
	 //           float2 uv  : TEXCOORD0;\
	 //           };\
	 //           \
	 //           PS_INPUT main(VS_INPUT input)\
	 //           {\
	 //           PS_INPUT output;\
	 //           output.pos = mul( ProjectionMatrix, float4(input.pos.xy, 0.f, 1.f));\
	 //           output.col = input.col;\
	 //           output.uv  = input.uv;\
	 //           return output;\
	 //           }";
	
	//	D3DCompile(vertexShader, strlen(vertexShader), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, &g_pVertexShaderBlob, NULL);
	//	if (g_pVertexShaderBlob == NULL) // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
	//		return false;
	//	if (g_pd3dDevice->CreateVertexShader((DWORD*)g_pVertexShaderBlob->GetBufferPointer(), g_pVertexShaderBlob->GetBufferSize(), NULL, &g_pVertexShader) != S_OK)
	//		return false;

	//	// Create the input layout
	//	D3D11_INPUT_ELEMENT_DESC local_layout[] =
	//	{
	//		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (size_t)(&((ImDrawVert*)0)->pos), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,   0, (size_t)(&((ImDrawVert*)0)->uv),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//		{ "COLOR",    0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, (size_t)(&((ImDrawVert*)0)->col), D3D11_INPUT_PER_VERTEX_DATA, 0 },
	//	};
	//	if (g_pd3dDevice->CreateInputLayout(local_layout, 3, g_pVertexShaderBlob->GetBufferPointer(), g_pVertexShaderBlob->GetBufferSize(), &g_pInputLayout) != S_OK)
	//		return false;

	//// Create the pixel shader
	//{
	//	static const char* pixelShader =
	//		"struct PS_INPUT\
	//           {\
	//           float4 pos : SV_POSITION;\
	//           float4 col : COLOR0;\
	//           float2 uv  : TEXCOORD0;\
	//           };\
	//           sampler sampler0;\
	//           Texture2D texture0;\
	//           \
	//           float4 main(PS_INPUT input) : SV_Target\
	//           {\
	//           float4 out_col = input.col * texture0.Sample(sampler0, input.uv); \
	//           return out_col; \
	//           }";

	//	D3DCompile(pixelShader, strlen(pixelShader), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, &g_pPixelShaderBlob, NULL);
	//	if (g_pPixelShaderBlob == NULL)  // NB: Pass ID3D10Blob* pErrorBlob to D3DCompile() to get error showing in (const char*)pErrorBlob->GetBufferPointer(). Make sure to Release() the blob!
	//		return false;
	//	if (g_pd3dDevice->CreatePixelShader((DWORD*)g_pPixelShaderBlob->GetBufferPointer(), g_pPixelShaderBlob->GetBufferSize(), NULL, &g_pPixelShader) != S_OK)
	//		return false;
	//}

	return true;
}

void ImGui_RHI_Shutdown()
{
	ImGui_RHI_ShutdownPlatformInterface();
}

void ImGui_RHI_RenderDrawData(ImDrawData* draw_data)
{
	// Grow vertex buffer as needed
	if (g_vertexBufferSize < draw_data->TotalVtxCount)
	{
		g_vertexBufferSize = draw_data->TotalVtxCount + 5000;
		if (!g_vertexBuffer->CreateDynamic(sizeof(ImDrawVert), g_vertexBufferSize))
			return;
	}

	// Grow index buffer as needed
	if (g_indexBufferSize < draw_data->TotalIdxCount)
	{
		g_indexBufferSize = draw_data->TotalIdxCount + 10000;
		if (!g_indexBuffer->CreateDynamic(sizeof(ImDrawIdx), g_indexBufferSize))
			return;
	}
	
	// Copy and convert all vertices into a single contiguous buffer
	auto vtx_dst	= (ImDrawVert*)g_vertexBuffer->Map();
	auto* idx_dst	= (ImDrawIdx*)g_indexBuffer->Map();
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
	
	// Setup orthographic projection matrix into our constant buffer
	// Our visible ImGui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is (0,0) for single viewport apps.
	{
		float L = draw_data->DisplayPos.x;
		float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
		float T = draw_data->DisplayPos.y;
		float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
		Matrix mvp = Matrix
		(
			2.0f / (R - L),		0.0f,				0.0f,	0.0f,
			0.0f,				2.0f / (T - B),		0.0f,	0.0f,
			0.0f,				0.0f,				0.5f,	0.0f,
			(R + L) / (L - R),	(T + B) / (B - T),	0.5f,	1.0f
		);

		auto buffer = (VertexConstantBuffer*)g_constantBuffer->Map();
		buffer->mvp = mvp;
		g_constantBuffer->Unmap();
	}

	// Setup render state
	//ctx->VSSetShader(g_pVertexShader, NULL, 0);
	//ctx->PSSetShader(g_pPixelShader, NULL, 0);
	//ctx->IASetInputLayout(g_pInputLayout);
	g_device->SetVertexBuffer(g_vertexBuffer);
	g_device->SetIndexBuffer(g_indexBuffer);
	g_device->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
	g_device->SetConstantBuffers(0, 1, g_constantBuffer->GetBuffer(), Buffer_VertexShader);
	g_device->SetSamplers(0, 1, g_fontSampler->GetBuffer());
	g_device->SetBlendState(g_blendState);
	g_device->SetDepthStencilState(g_depthStencilState);
	g_device->SetRasterizerState(g_rasterizerState);
	RHI_Viewport viewport = RHI_Viewport(0.0f, 0.0f, draw_data->DisplaySize.x, draw_data->DisplaySize.y);
	g_device->SetViewport(viewport);

	// Render command lists
	int vtx_offset = 0;
	int idx_offset = 0;
	ImVec2 pos = draw_data->DisplayPos;
	for (int i = 0; i < draw_data->CmdListsCount; i++)
	{
		const ImDrawList* cmd_list = draw_data->CmdLists[i];
		for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
		{
			const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
			if (pcmd->UserCallback)
			{
				// User callback (registered via ImDrawList::AddCallback)
				pcmd->UserCallback(cmd_list, pcmd);
			}
			else
			{
				// Apply scissor/clipping rectangle
				g_device->SetScissorRectangle((int)(pcmd->ClipRect.x - pos.x), (int)(pcmd->ClipRect.y - pos.y), (int)(pcmd->ClipRect.z - pos.x), (int)(pcmd->ClipRect.w - pos.y));

				// Bind texture, Draw
				auto texture_srv = (void*)pcmd->TextureId;
				g_device->SetTextures(0, 1, texture_srv);
				g_device->DrawIndexed(pcmd->ElemCount, idx_offset, vtx_offset);
			}
			idx_offset += pcmd->ElemCount;
		}
		vtx_offset += cmd_list->VtxBuffer.Size;
	}
}

//--------------------------------------------
// MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
//--------------------------------------------
struct ImGuiViewportDataDx11
{
	/*IDXGISwapChain*             SwapChain;
	ID3D11RenderTargetView*     RTView;

	ImGuiViewportDataDx11() { SwapChain = NULL; RTView = NULL; }
	~ImGuiViewportDataDx11() { IM_ASSERT(SwapChain == NULL && RTView == NULL); }*/
};

static void ImGui_RHI_CreateWindow(ImGuiViewport* viewport)
{
	//ImGuiViewportDataDx11* data = IM_NEW(ImGuiViewportDataDx11)();
	//viewport->RendererUserData = data;

	//HWND hwnd = (HWND)viewport->PlatformHandle;
	//IM_ASSERT(hwnd != 0);

	//// Create swap chain
	//DXGI_SWAP_CHAIN_DESC sd;
	//ZeroMemory(&sd, sizeof(sd));
	//sd.BufferDesc.Width = (UINT)viewport->Size.x;
	//sd.BufferDesc.Height = (UINT)viewport->Size.y;
	//sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	//sd.SampleDesc.Count = 1;
	//sd.SampleDesc.Quality = 0;
	//sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	//sd.BufferCount = 1;
	//sd.OutputWindow = hwnd;
	//sd.Windowed = TRUE;
	//sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	//sd.Flags = 0;

	//IM_ASSERT(data->SwapChain == NULL && data->RTView == NULL);
	//g_pFactory->CreateSwapChain(g_pd3dDevice, &sd, &data->SwapChain);

	//// Create the render target
	//if (data->SwapChain)
	//{
	//	ID3D11Texture2D* pBackBuffer;
	//	data->SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
	//	g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &data->RTView);
	//	pBackBuffer->Release();
	//}
}

static void ImGui_RHI_DestroyWindow(ImGuiViewport* viewport)
{
	//// The main viewport (owned by the application) will always have RendererUserData == NULL since we didn't create the data for it.
	//if (ImGuiViewportDataDx11* data = (ImGuiViewportDataDx11*)viewport->RendererUserData)
	//{
	//	if (data->SwapChain)
	//		data->SwapChain->Release();

	//	data->SwapChain = NULL;

	//	if (data->RTView)
	//		data->RTView->Release();

	//	data->RTView = NULL;

	//	IM_DELETE(data);
	//}
	//viewport->RendererUserData = NULL;
}

static void ImGui_RHI_SetWindowSize(ImGuiViewport* viewport, ImVec2 size)
{
	/*ImGuiViewportDataDx11* data = (ImGuiViewportDataDx11*)viewport->RendererUserData;
	if (data->RTView)
	{
		data->RTView->Release();
		data->RTView = NULL;
	}
	if (data->SwapChain)
	{
		ID3D11Texture2D* pBackBuffer = NULL;
		data->SwapChain->ResizeBuffers(0, (UINT)size.x, (UINT)size.y, DXGI_FORMAT_UNKNOWN, 0);
		data->SwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
		if (pBackBuffer == NULL) { fprintf(stderr, "ImGui_ImplDX11_SetWindowSize() failed creating buffers.\n"); return; }
		g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &data->RTView);
		pBackBuffer->Release();
	}*/
}

static void ImGui_RHI_RenderWindow(ImGuiViewport* viewport, void*)
{
	/*ImGuiViewportDataDx11* data = (ImGuiViewportDataDx11*)viewport->RendererUserData;
	ImVec4 clear_color = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	g_pd3dDeviceContext->OMSetRenderTargets(1, &data->RTView, NULL);
	if (!(viewport->Flags & ImGuiViewportFlags_NoRendererClear))
		g_pd3dDeviceContext->ClearRenderTargetView(data->RTView, (float*)&clear_color);
	ImGui_RHI_RenderDrawData(viewport->DrawData);*/
}

static void ImGui_RHI_SwapBuffers(ImGuiViewport* viewport, void*)
{
	//ImGuiViewportDataDx11* data = (ImGuiViewportDataDx11*)viewport->RendererUserData;
	//data->SwapChain->Present(0, 0); // Present without vsync
}

static void ImGui_RHI_InitPlatformInterface()
{
	ImGuiPlatformIO& platform_io		= ImGui::GetPlatformIO();
	platform_io.Renderer_CreateWindow	= ImGui_RHI_CreateWindow;
	platform_io.Renderer_DestroyWindow	= ImGui_RHI_DestroyWindow;
	platform_io.Renderer_SetWindowSize	= ImGui_RHI_SetWindowSize;
	platform_io.Renderer_RenderWindow	= ImGui_RHI_RenderWindow;
	platform_io.Renderer_SwapBuffers	= ImGui_RHI_SwapBuffers;
}

static void ImGui_RHI_ShutdownPlatformInterface()
{
	ImGui::DestroyPlatformWindows();
}
