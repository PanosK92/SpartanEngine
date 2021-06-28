/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "RHI/RHI_Semaphore.h"
//====================================

namespace ImGui::RHI
{
    using namespace Spartan;
    using namespace Math;
    using namespace std;

    // Forward Declarations
    void InitializePlatformInterface();

    // Engine subsystems
    Context*    g_context   = nullptr;
    Renderer*   g_renderer  = nullptr;

    // RHI resources
    static shared_ptr<RHI_Device>                                           g_rhi_device;
    static unique_ptr<RHI_Texture>                                          g_texture;
    static unordered_map<uint32_t, vector<unique_ptr<RHI_VertexBuffer>>>    g_vertex_buffers;
    static unordered_map<uint32_t, vector<unique_ptr<RHI_IndexBuffer>>>     g_index_buffers;
    static unique_ptr<RHI_DepthStencilState>                                g_depth_stencil_state;
    static unique_ptr<RHI_RasterizerState>                                  g_rasterizer_state;
    static unique_ptr<RHI_BlendState>                                       g_blend_state;
    static unique_ptr<RHI_Shader>                                           g_shader_vertex;
    static unique_ptr<RHI_Shader>                                           g_shader_pixel;

    inline bool Initialize(Context* context)
    {
        g_context       = context;
        g_renderer      = context->GetSubsystem<Renderer>();
        g_rhi_device    = g_renderer->GetRhiDevice();
        
        if (!g_context || !g_rhi_device || !g_rhi_device->IsInitialised())
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        // Create required RHI objects
        {
            g_depth_stencil_state = make_unique<RHI_DepthStencilState>(g_rhi_device, false, g_renderer->GetComparisonFunction());

            g_rasterizer_state = make_unique<RHI_RasterizerState>
            (
                g_rhi_device,
                RHI_Cull_None,
                RHI_Fill_Solid,
                true,    // depth clip
                true,    // scissor
                false,   // multi-sample
                false    // anti-aliased lines
            );

            g_blend_state = make_unique<RHI_BlendState>
            (
                g_rhi_device,
                true,
                RHI_Blend_Src_Alpha,        // source blend
                RHI_Blend_Inv_Src_Alpha,    // destination blend
                RHI_Blend_Operation_Add,    // blend op
                RHI_Blend_Inv_Src_Alpha,    // source blend alpha
                RHI_Blend_Zero,             // destination blend alpha
                RHI_Blend_Operation_Add     // destination op alpha
            );

            // Compile shaders
            const std::string shader_path = g_context->GetSubsystem<ResourceCache>()->GetResourceDirectory(ResourceDirectory::Shaders) + "\\ImGui.hlsl";
            g_shader_vertex = make_unique<RHI_Shader>(g_context, RHI_Vertex_Type::Pos2dTexCol8);
            bool async = false;
            g_shader_vertex->Compile(RHI_Shader_Vertex, shader_path, async);
            g_shader_pixel = make_unique<RHI_Shader>(g_context);
            g_shader_pixel->Compile(RHI_Shader_Pixel, shader_path, async);
        }

        // Font atlas
        {
            unsigned char* pixels;
            int atlas_width, atlas_height, bpp;
            auto& io = GetIO();
            io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height, &bpp);

            // Copy pixel data
            const uint32_t size = atlas_width * atlas_height * bpp;
            vector<RHI_Texture_Slice> texture_data;
            vector<std::byte>& mip = texture_data.emplace_back().mips.emplace_back().bytes;
            mip.resize(size);
            mip.reserve(size);
            memcpy(&mip[0], reinterpret_cast<std::byte*>(pixels), size);

            // Upload texture to graphics system
            g_texture = make_unique<RHI_Texture2D>(g_context, atlas_width, atlas_height, RHI_Format_R8G8B8A8_Unorm, texture_data);
            io.Fonts->TexID = static_cast<ImTextureID>(g_texture.get());
        }

        // Setup back-end capabilities flags
        auto& io = GetIO();
        io.BackendFlags         |= ImGuiBackendFlags_RendererHasViewports;
        io.BackendFlags         |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName  = "RHI";
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

    inline void Render(ImDrawData* draw_data, RHI_SwapChain* swap_chain_other = nullptr, const bool clear = true)
    {
        // Validate draw data
        SP_ASSERT(draw_data != nullptr);

        // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
        const int fb_width  = static_cast<int>(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
        const int fb_height = static_cast<int>(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
        if (fb_width <= 0 || fb_height <= 0 || draw_data->TotalVtxCount == 0)
            return;

        // Get swap chain
        bool is_child_window        = swap_chain_other != nullptr;
        RHI_SwapChain* swap_chain   = is_child_window ? swap_chain_other : g_renderer->GetSwapChain();
        RHI_CommandList* cmd_list   = swap_chain->GetCmdList();

        // Validate command list
        SP_ASSERT(cmd_list != nullptr);

        // Wait until the Renderer is able to render (it can get flushed and stopped during world loading)
        while (!g_renderer->IsAllowedToRender())
        {
            LOG_INFO("Waiting for the Renderer to be ready...");
            this_thread::sleep_for(chrono::milliseconds(16));
        }

        // Don't render if the the command list is not recording (can happen when the Renderer is not allowed to render)
        if (cmd_list->GetState() != Spartan::RHI_CommandListState::Recording)
        {
            LOG_INFO("Waiting for command list to be ready...");
            return;
        }

        // Update vertex and index buffers
        RHI_VertexBuffer* vertex_buffer = nullptr;
        RHI_IndexBuffer* index_buffer   = nullptr;
        {
            const uint32_t swapchain_id         = swap_chain->GetObjectId();
            const uint32_t swapchain_cmd_index  = swap_chain->GetCmdIndex();

            const uint32_t gap =  Math::Helper::Clamp<uint32_t>((swapchain_cmd_index + 1) - static_cast<uint32_t>(g_vertex_buffers[swapchain_id].size()), 0, 10);
            for (uint32_t i = 0; i < gap; i++)
            {
                g_vertex_buffers[swapchain_id].emplace_back(make_unique<RHI_VertexBuffer>(g_rhi_device, static_cast<uint32_t>(sizeof(ImDrawVert))));
                g_index_buffers[swapchain_id].emplace_back(make_unique<RHI_IndexBuffer>(g_rhi_device));
            }

            vertex_buffer   = g_vertex_buffers[swapchain_id][swapchain_cmd_index].get();
            index_buffer    = g_index_buffers[swapchain_id][swapchain_cmd_index].get();

            // Grow vertex buffer as needed
            if (vertex_buffer->GetVertexCount() < static_cast<unsigned int>(draw_data->TotalVtxCount))
            {
                const unsigned int new_size = draw_data->TotalVtxCount + 5000;
                if (!vertex_buffer->CreateDynamic<ImDrawVert>(new_size))
                    return;
            }
            
            // Grow index buffer as needed
            if (index_buffer->GetIndexCount() < static_cast<unsigned int>(draw_data->TotalIdxCount))
            {
                const unsigned int new_size = draw_data->TotalIdxCount + 10000;
                if (!index_buffer->CreateDynamic<ImDrawIdx>(new_size))
                    return;
            }
            
            // Copy and convert all vertices into a single contiguous buffer
            ImDrawVert* vtx_dst = static_cast<ImDrawVert*>(vertex_buffer->Map());
            ImDrawIdx* idx_dst = static_cast<ImDrawIdx*>(index_buffer->Map());
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
            
                vertex_buffer->Unmap();
                index_buffer->Unmap();
            }
        }

        // Set render state
        static RHI_PipelineState pipeline_state = {};
        pipeline_state.shader_vertex            = g_shader_vertex.get();
        pipeline_state.shader_pixel             = g_shader_pixel.get();
        pipeline_state.rasterizer_state         = g_rasterizer_state.get();
        pipeline_state.blend_state              = g_blend_state.get();
        pipeline_state.depth_stencil_state      = g_depth_stencil_state.get();
        pipeline_state.vertex_buffer_stride     = vertex_buffer->GetStride();
        pipeline_state.render_target_swapchain  = swap_chain;
        pipeline_state.clear_color[0]           = clear ? Vector4(0.0f, 0.0f, 0.0f, 1.0f) : rhi_color_load;
        pipeline_state.viewport.width           = draw_data->DisplaySize.x;
        pipeline_state.viewport.height          = draw_data->DisplaySize.y;
        pipeline_state.dynamic_scissor          = true;
        pipeline_state.primitive_topology       = RHI_PrimitiveTopology_TriangleList;
        pipeline_state.pass_name                = is_child_window ? "pass_imgui_window_child" : "pass_imgui_window_main";

        // Record commands
        if (cmd_list->BeginRenderPass(pipeline_state))
        {
            // Setup orthographic projection matrix into our constant buffer
            // Our visible ImGui space lies from draw_data->DisplayPos (top left) to 
            // draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is (0,0) for single viewport apps.
            {
                const float L       = draw_data->DisplayPos.x;
                const float R       = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
                const float T       = draw_data->DisplayPos.y;
                const float B       = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
                const Matrix wvp    = Matrix
                (
                    2.0f / (R - L), 0.0f, 0.0f, (R + L) / (L - R),
                    0.0f, 2.0f / (T - B), 0.0f, (T + B) / (B - T),
                    0.0f, 0.0f, 0.5f, 0.5f,
                    0.0f, 0.0f, 0.0f, 1.0f
                );

                g_renderer->SetGlobalShaderObjectTransform(cmd_list, wvp);
            }

            // Transition layouts
            for (auto i = 0; i < draw_data->CmdListsCount; i++)
            {
                auto cmd_list_imgui = draw_data->CmdLists[i];
                for (int cmd_i = 0; cmd_i < cmd_list_imgui->CmdBuffer.Size; cmd_i++)
                {
                    const auto pcmd = &cmd_list_imgui->CmdBuffer[cmd_i];
                    if (RHI_Texture* texture = static_cast<RHI_Texture*>(pcmd->TextureId))
                    {
                        texture->SetLayout(RHI_Image_Layout::Shader_Read_Only_Optimal, cmd_list);
                    }
                }
            }

            cmd_list->SetBufferVertex(vertex_buffer);
            cmd_list->SetBufferIndex(index_buffer);

            // Render command lists
            int global_vtx_offset   = 0;
            int global_idx_offset   = 0;
            const ImVec2& clip_off  = draw_data->DisplayPos;
            Math::Rectangle scissor_rect;
            for (int i = 0; i < draw_data->CmdListsCount; i++)
            {
                ImDrawList* cmd_list_imgui = draw_data->CmdLists[i];
                for (int cmd_i = 0; cmd_i < cmd_list_imgui->CmdBuffer.Size; cmd_i++)
                {
                    const ImDrawCmd* pcmd = &cmd_list_imgui->CmdBuffer[cmd_i];
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
                        cmd_list->SetTexture(RendererBindingsSrv::tex, static_cast<RHI_Texture*>(pcmd->TextureId));
                        cmd_list->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
                    }

                }
                global_idx_offset += cmd_list_imgui->IdxBuffer.Size;
                global_vtx_offset += cmd_list_imgui->VtxBuffer.Size;
            }

            cmd_list->EndRenderPass();
        }
    }

    //--------------------------------------------
    // MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
    //--------------------------------------------

    inline RHI_SwapChain* GetSwapchain(ImGuiViewport* viewport)
    {
        SP_ASSERT(viewport != nullptr);
        return static_cast<RHI_SwapChain*>(viewport->RendererUserData);
    }

    static void RHI_Window_Create(ImGuiViewport* viewport)
    {
        SP_ASSERT(viewport != nullptr);

        RHI_SwapChain* swapchain = new RHI_SwapChain
        (
            viewport->PlatformHandleRaw, // PlatformHandle is SDL_Window, PlatformHandleRaw is HWND
            g_rhi_device,
            static_cast<uint32_t>(viewport->Size.x),
            static_cast<uint32_t>(viewport->Size.y),
            RHI_Format_R8G8B8A8_Unorm,
            g_renderer->GetSwapChain()->GetBufferCount(),
            g_renderer->GetSwapChain()->GetFlags(),
            (string("swapchain_child_") + string(to_string(viewport->ID))).c_str()
        );

        SP_ASSERT(swapchain->IsInitialised());

        viewport->RendererUserData = swapchain;
    }

    static void RHI_Window_Destroy(ImGuiViewport* viewport)
    {
        SP_ASSERT(viewport != nullptr);

        if (RHI_SwapChain* swapchain = GetSwapchain(viewport))
        {
            delete swapchain;
        }

        viewport->RendererUserData = nullptr;
    }

    static void RHI_Window_SetSize(ImGuiViewport* viewport, const ImVec2 size)
    {
        SP_ASSERT(viewport != nullptr);
        RHI_SwapChain* swapchain = GetSwapchain(viewport);
        SP_ASSERT(swapchain != nullptr);
        
        if (!swapchain->Resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)))
        {
            LOG_ERROR("Failed to resize swap chain");
        }
    }

    static void RHI_Window_Render(ImGuiViewport* viewport, void*)
    {
        SP_ASSERT(viewport != nullptr);
        RHI_SwapChain* swapchain = GetSwapchain(viewport);
        SP_ASSERT(swapchain != nullptr);

        RHI_CommandList* cmd_list = swapchain->GetCmdList();
        SP_ASSERT(cmd_list != nullptr);

        if (!cmd_list->Begin())
        {
            LOG_ERROR("Failed to begin command list");
            return;
        }

        const bool clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
        Render(viewport->DrawData, swapchain, clear);

        // Get wait semaphore
        RHI_Semaphore* wait_semaphore = cmd_list->GetProcessedSemaphore();

        // When moving a window outside of the main viewport for the first time
        // it skips presenting every other time, hence the semaphore will be signaled
        // because it was never waited for by present. So we do a dummy present here.
        // Not sure why this behavior is occurring yet.
        if (wait_semaphore) // Semaphore is null for D3D11
        {
            if (wait_semaphore->GetState() == RHI_Semaphore_State::Signaled)
            {
                LOG_INFO("Dummy presenting to reset semaphore");
                swapchain->Present(wait_semaphore);
            }
        }

        if (!cmd_list->End())
        {
            LOG_ERROR("Failed to end command list");
            return;
        }

        if (!cmd_list->Submit())
        {
            LOG_ERROR("Failed to submit command list");
        }
    }

    static void RHI_Window_Present(ImGuiViewport* viewport, void*)
    {
        RHI_SwapChain* swapchain = GetSwapchain(viewport);
        SP_ASSERT(swapchain != nullptr);
        SP_ASSERT(swapchain->GetCmdList() != nullptr);

        if (!swapchain->Present(swapchain->GetCmdList()->GetProcessedSemaphore()))
        {
            LOG_ERROR("Failed to present");
        }
    }

    inline void InitializePlatformInterface()
    {
        ImGuiPlatformIO& platform_io        = ImGui::GetPlatformIO();
        platform_io.Renderer_CreateWindow   = RHI_Window_Create;
        platform_io.Renderer_DestroyWindow  = RHI_Window_Destroy;
        platform_io.Renderer_SetWindowSize  = RHI_Window_SetSize;
        platform_io.Renderer_RenderWindow   = RHI_Window_Render;
        platform_io.Renderer_SwapBuffers    = RHI_Window_Present;
    }
}
