/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ==================================
#include <array>
#include "ImGui_RHI.h"
#include "../Source/imgui.h"
#include "Core/Event.h"
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
#include "RHI/RHI_CommandPool.h"
#include "RHI/RHI_ConstantBuffer.h"
#include "Rendering/Renderer_ConstantBuffers.h"
#include "../../Widgets/TextureViewer.h"
//=============================================

namespace ImGui::RHI
{
    //= NAMESPACES =========
    using namespace Spartan;
    using namespace Math;
    using namespace std;
    //======================

    static const uint32_t buffer_count = 2;

    struct ViewportRhiResources
    {
        uint32_t buffer_index = 0;

        RHI_CommandPool* cmd_pool = nullptr;
        array<unique_ptr<RHI_IndexBuffer>,    buffer_count> index_buffers;
        array<unique_ptr<RHI_VertexBuffer>,   buffer_count> vertex_buffers;
        array<shared_ptr<RHI_ConstantBuffer>, buffer_count> constant_buffer;
        Cb_ImGui constant_buffer_cpu;
        
        ViewportRhiResources() = default;
        ViewportRhiResources(const char* name, RHI_SwapChain* swapchain)
        {
            // allocate command pool
            cmd_pool = RHI_Device::AllocateCommandPool("imgui", swapchain->GetObjectId(), RHI_Queue_Type::Graphics);

            // allocate buffers
            for (uint32_t i = 0; i < buffer_count; i++)
            {
                constant_buffer[i] = make_shared<RHI_ConstantBuffer>(name);
                constant_buffer[i]->Create<Cb_ImGui>(256);

                index_buffers[i]  = make_unique<RHI_IndexBuffer>(true, name);
                vertex_buffers[i] = make_unique<RHI_VertexBuffer>(true, name);
            }
        }
    };

    struct WindowData
    {
        shared_ptr<ViewportRhiResources> viewport_rhi_resources;
        shared_ptr<RHI_SwapChain>        swapchain;
    };

    // forward declarations
    void InitialisePlatformInterface();

    // main window rhi resources
    ViewportRhiResources g_viewport_data;

    // shared rhi resources (between all windows)
    shared_ptr<RHI_Texture>           g_font_atlas;
    shared_ptr<RHI_DepthStencilState> g_depth_stencil_state;
    shared_ptr<RHI_RasterizerState>   g_rasterizer_state;
    shared_ptr<RHI_BlendState>        g_blend_state;
    shared_ptr<RHI_Shader>            g_shader_vertex;
    shared_ptr<RHI_Shader>            g_shader_pixel;

    static void destroy_rhi_resources()
    {
        g_font_atlas                       = nullptr;
        g_depth_stencil_state              = nullptr;
        g_rasterizer_state                 = nullptr;
        g_blend_state                      = nullptr;
        g_shader_vertex                    = nullptr;
        g_shader_pixel                     = nullptr;
        g_viewport_data.constant_buffer[0] = nullptr;
        g_viewport_data.constant_buffer[1] = nullptr;
        g_viewport_data.index_buffers[0]   = nullptr;
        g_viewport_data.index_buffers[1]   = nullptr;
        g_viewport_data.vertex_buffers[0]  = nullptr;
        g_viewport_data.vertex_buffers[1]  = nullptr;
    }

    static void Initialize()
    {
        // create required RHI objects
        {
            g_viewport_data = ViewportRhiResources("imgui", Renderer::GetSwapChain());

            g_depth_stencil_state = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Always);

            g_rasterizer_state = make_shared<RHI_RasterizerState>
            (
                RHI_CullMode::None,
                RHI_PolygonMode::Solid,
                true,  // depth clip
                true,  // scissor
                false, // multi-sample
                false  // anti-aliased lines
            );

            g_blend_state = make_shared<RHI_BlendState>
            (
                true,
                RHI_Blend::Src_Alpha,     // source blend
                RHI_Blend::Inv_Src_Alpha, // destination blend
                RHI_Blend_Operation::Add, // blend op
                RHI_Blend::Inv_Src_Alpha, // source blend alpha
                RHI_Blend::Zero,          // destination blend alpha
                RHI_Blend_Operation::Add  // destination op alpha
            );

            // compile shaders
            {
                const string shader_path = ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders) + "\\ImGui.hlsl";

                bool async = false;

                g_shader_vertex = make_shared<RHI_Shader>();
                g_shader_vertex->Compile(RHI_Shader_Vertex, shader_path, async, RHI_Vertex_Type::Pos2dUvCol8);

                g_shader_pixel = make_shared<RHI_Shader>();
                g_shader_pixel->Compile(RHI_Shader_Pixel, shader_path, async);
            }
        }

        // font atlas
        {
            unsigned char* pixels = nullptr;
            int atlas_width       = 0;
            int atlas_height      = 0;
            int bpp               = 0;
            ImGuiIO& io           = GetIO();
            io.Fonts->GetTexDataAsRGBA32(&pixels, &atlas_width, &atlas_height, &bpp);

            // copy pixel data
            vector<RHI_Texture_Slice> texture_data;
            vector<std::byte>& mip = texture_data.emplace_back().mips.emplace_back().bytes;
            const uint32_t size = atlas_width * atlas_height * bpp;
            mip.resize(size);
            mip.reserve(size);
            memcpy(&mip[0], reinterpret_cast<std::byte*>(pixels), size);

            // upload texture to graphics system
            g_font_atlas = make_shared<RHI_Texture2D>(atlas_width, atlas_height, RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv, texture_data, "imgui_font_atlas");
            io.Fonts->TexID = static_cast<ImTextureID>(g_font_atlas.get());
        }

        // setup back-end capabilities flags
        ImGuiIO& io             = GetIO();
        io.BackendFlags        |= ImGuiBackendFlags_RendererHasViewports;
        io.BackendFlags        |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName  = "RHI";
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            InitialisePlatformInterface();
        }

        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnShutdown, SP_EVENT_HANDLER_STATIC(destroy_rhi_resources));
    }

    static void Shutdown()
    {
        DestroyPlatformWindows();
    }

    static void Render(ImDrawData* draw_data, WindowData* window_data = nullptr, const bool clear = true)
    {
        SP_ASSERT(draw_data != nullptr);

        // don't render when minimised
        if (draw_data->DisplaySize.x == 0 || draw_data->DisplaySize.y == 0)
            return;

        // get the viewport resources
        bool is_child_window                = window_data != nullptr;
        ViewportRhiResources* rhi_resources = is_child_window ? window_data->viewport_rhi_resources.get() : &g_viewport_data;

        // tick the command pool
        if (rhi_resources->cmd_pool->Tick())
        {
            // switch to the next buffers
            rhi_resources->buffer_index = (rhi_resources->buffer_index + 1) % buffer_count;
            rhi_resources->constant_buffer[rhi_resources->buffer_index]->ResetOffset();
        }

        // get rhi resources for this command buffer
        shared_ptr<RHI_ConstantBuffer> constant_buffer = rhi_resources->constant_buffer[rhi_resources->buffer_index];
        RHI_VertexBuffer* vertex_buffer                = rhi_resources->vertex_buffers[rhi_resources->buffer_index].get();
        RHI_IndexBuffer* index_buffer                  = rhi_resources->index_buffers[rhi_resources->buffer_index].get();
        RHI_CommandList* cmd_list                      = rhi_resources->cmd_pool->GetCurrentCommandList();

        // update vertex and index buffers
        {
            // grow vertex buffer as needed
            if (vertex_buffer->GetVertexCount() < static_cast<uint32_t>(draw_data->TotalVtxCount))
            {
                const uint32_t vertex_count     = vertex_buffer->GetVertexCount();
                const uint32_t vertex_count_new = draw_data->TotalVtxCount + 15000;
                vertex_buffer->CreateDynamic<ImDrawVert>(vertex_count_new);

                if (vertex_count != 0)
                {
                    SP_LOG_INFO("Vertex buffer has been re-allocated to fit %d vertices", vertex_count_new);
                }
            }

            // grow index buffer as needed
            if (index_buffer->GetIndexCount() < static_cast<uint32_t>(draw_data->TotalIdxCount))
            {
                const uint32_t index_count     = index_buffer->GetIndexCount();
                const uint32_t index_count_new = draw_data->TotalIdxCount + 30000;
                index_buffer->CreateDynamic<ImDrawIdx>(index_count_new);

                if (index_count != 0)
                {
                    SP_LOG_INFO("Index buffer has been re-allocated to fit %d indices", index_count_new);
                }
            }

            // copy all imgui vertices into a single buffer
            ImDrawVert* vtx_dst = static_cast<ImDrawVert*>(vertex_buffer->GetMappedData());
            ImDrawIdx*  idx_dst = static_cast<ImDrawIdx*>(index_buffer->GetMappedData());
            if (vtx_dst && idx_dst)
            {
                for (auto i = 0; i < draw_data->CmdListsCount; i++)
                {
                    const ImDrawList* imgui_cmd_list = draw_data->CmdLists[i];

                    memcpy(vtx_dst, imgui_cmd_list->VtxBuffer.Data, imgui_cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
                    memcpy(idx_dst, imgui_cmd_list->IdxBuffer.Data, imgui_cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));

                    vtx_dst += imgui_cmd_list->VtxBuffer.Size;
                    idx_dst += imgui_cmd_list->IdxBuffer.Size;
                }
            }
        }

        // begin timeblock
        const char* name = is_child_window ? "imgui_window_child" : "imgui_window_main";
        bool gpu_timing = !is_child_window; // profiler requires more work when windows are enter the main window and their command pool is destroyed
        cmd_list->Begin();
        cmd_list->BeginTimeblock(name, true, gpu_timing);

        // define pipeline state
        static RHI_PipelineState pso = {};
        pso.shader_vertex            = g_shader_vertex.get();
        pso.shader_pixel             = g_shader_pixel.get();
        pso.rasterizer_state         = g_rasterizer_state.get();
        pso.blend_state              = g_blend_state.get();
        pso.depth_stencil_state      = g_depth_stencil_state.get();
        pso.render_target_swapchain  = is_child_window ? window_data->swapchain.get() : Renderer::GetSwapChain();
        pso.clear_color[0]           = clear ? Color::standard_black : rhi_color_dont_care;
        pso.dynamic_scissor          = true;
        pso.primitive_topology       = RHI_PrimitiveTopology_Mode::TriangleList;

        // start rendering
        cmd_list->SetPipelineState(pso);
        cmd_list->BeginRenderPass();
        {
            // compute transformation matrix
            {
                const float L = draw_data->DisplayPos.x;
                const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
                const float T = draw_data->DisplayPos.y;
                const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

                rhi_resources->constant_buffer_cpu.transform = Matrix
                (
                    2.0f / (R - L), 0.0f, 0.0f, (R + L) / (L - R),
                    0.0f, 2.0f / (T - B), 0.0f, (T + B) / (B - T),
                    0.0f, 0.0f, 0.5f, 0.5f,
                    0.0f, 0.0f, 0.0f, 1.0f
                );
            }

            cmd_list->SetBufferVertex(vertex_buffer);
            cmd_list->SetBufferIndex(index_buffer);

            // render command lists
            uint32_t global_vtx_offset = 0;
            uint32_t global_idx_offset = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(draw_data->CmdListsCount); i++)
            {
                ImDrawList* cmd_list_imgui = draw_data->CmdLists[i];

                for (uint32_t cmd_i = 0; cmd_i < static_cast<uint32_t>(cmd_list_imgui->CmdBuffer.Size); cmd_i++)
                {
                    const ImDrawCmd* pcmd = &cmd_list_imgui->CmdBuffer[cmd_i];

                    if (pcmd->UserCallback != nullptr)
                    {
                        pcmd->UserCallback(cmd_list_imgui, pcmd);
                    }
                    else
                    {
                        // set scissor rectangle
                        {
                            Math::Rectangle scissor_rect;
                            scissor_rect.left   = pcmd->ClipRect.x - draw_data->DisplayPos.x;
                            scissor_rect.top    = pcmd->ClipRect.y - draw_data->DisplayPos.y;
                            scissor_rect.right  = pcmd->ClipRect.z - draw_data->DisplayPos.x;
                            scissor_rect.bottom = pcmd->ClipRect.w - draw_data->DisplayPos.y;

                            cmd_list->SetScissorRectangle(scissor_rect);
                        }

                        // set texture
                        rhi_resources->constant_buffer_cpu.options_texture_visualisation = 0;
                        if (RHI_Texture* texture = static_cast<RHI_Texture*>(pcmd->TextureId))
                        {
                            // during engine startup, some textures might be loading in different threads
                            if (texture->IsReadyForUse())
                            {
                                cmd_list->SetTexture(Renderer_BindingsSrv::tex, texture);

                                // update texture viewer parameters
                                bool is_texture_visualised                                       = TextureViewer::GetVisualisedTextureId() == texture->GetObjectId();
                                rhi_resources->constant_buffer_cpu.options_texture_visualisation = is_texture_visualised ? TextureViewer::GetVisualisationFlags() : 0;
                                rhi_resources->constant_buffer_cpu.mip_level                     = is_texture_visualised ? TextureViewer::GetMipLevel() : 0;
                            }
                        }

                        // update imgui buffer
                        constant_buffer->Update(&rhi_resources->constant_buffer_cpu);
                        cmd_list->SetConstantBuffer(Renderer_BindingsCb::imgui, constant_buffer);

                        // draw
                        cmd_list->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
                    }

                }

                global_idx_offset += static_cast<uint32_t>(cmd_list_imgui->IdxBuffer.Size);
                global_vtx_offset += static_cast<uint32_t>(cmd_list_imgui->VtxBuffer.Size);
            }
        }

        // submit
        cmd_list->EndRenderPass();
        cmd_list->EndTimeblock();
        cmd_list->End();
        cmd_list->Submit();
    }

    //--------------------------------------------
    // MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
    //--------------------------------------------

    static void RHI_Window_Create(ImGuiViewport* viewport)
    {
        // platformHandle is SDL_Window, PlatformHandleRaw is HWND
        SP_ASSERT_MSG(viewport->PlatformHandle != nullptr, "Platform handle is invalid");

        WindowData* window = new WindowData();
        window->swapchain = make_shared<RHI_SwapChain>
        (
            viewport->PlatformHandle,
            static_cast<uint32_t>(viewport->Size.x),
            static_cast<uint32_t>(viewport->Size.y),
            RHI_Present_Mode::Immediate,
            2,
            (string("swapchain_child_") + string(to_string(viewport->ID))).c_str()
        );

        window->viewport_rhi_resources = make_shared<ViewportRhiResources>("imgui_child_window", window->swapchain.get());
        viewport->RendererUserData = window;
    }

    static void RHI_Window_Destroy(ImGuiViewport* viewport)
    {
        if (WindowData* window = static_cast<WindowData*>(viewport->RendererUserData))
        {
            RHI_Device::DestroyCommandPool(window->viewport_rhi_resources->cmd_pool);
            delete window;
        }

        viewport->RendererUserData = nullptr;
    }

    static void RHI_Window_SetSize(ImGuiViewport* viewport, const ImVec2 size)
    {
        static_cast<WindowData*>(viewport->RendererUserData)->swapchain->Resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
    }

    static void RHI_Window_Render(ImGuiViewport* viewport, void*)
    {
        const bool clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
        Render(viewport->DrawData, static_cast<WindowData*>(viewport->RendererUserData), clear);
    }

    static void RHI_Window_Present(ImGuiViewport* viewport, void*)
    {
        WindowData* window = static_cast<WindowData*>(viewport->RendererUserData);
        SP_ASSERT(window->viewport_rhi_resources->cmd_pool->GetCurrentCommandList()->GetState() == Spartan::RHI_CommandListState::Submitted);
        window->swapchain->Present();
    }

    inline void InitialisePlatformInterface()
    {
        ImGuiPlatformIO& platform_io       = ImGui::GetPlatformIO();
        platform_io.Renderer_CreateWindow  = RHI_Window_Create;
        platform_io.Renderer_DestroyWindow = RHI_Window_Destroy;
        platform_io.Renderer_SetWindowSize = RHI_Window_SetSize;
        platform_io.Renderer_RenderWindow  = RHI_Window_Render;
        platform_io.Renderer_SwapBuffers   = RHI_Window_Present;
    }
}
