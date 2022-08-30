/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Core/Events.h"
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
//====================================

namespace ImGui::RHI
{
    //= NAMESPACES =========
    using namespace Spartan;
    using namespace Math;
    using namespace std;
    //======================

    struct ViewportResources
    {
        ViewportResources() = default;
        ViewportResources(const char* name, RHI_Device* rhi_device, RHI_SwapChain* swapchain)
        {
            // Allocate command pool
            cmd_pool = rhi_device->AllocateCommandPool("imgui", swapchain->GetObjectId());
            cmd_pool->AllocateCommandLists(RHI_Queue_Type::Graphics, 2, 2);

            // Allocate constant buffer
            cb_gpu = make_shared<RHI_ConstantBuffer>(rhi_device, name);
            cb_gpu->Create<Cb_ImGui>(256);
        }

        // Index and vertex buffers
        vector<unique_ptr<RHI_IndexBuffer>>  index_buffers;
        vector<unique_ptr<RHI_VertexBuffer>> vertex_buffers;

        // Constant buffer
        shared_ptr<RHI_ConstantBuffer> cb_gpu;
        Cb_ImGui cb_cpu;

        // Command pool
        RHI_CommandPool* cmd_pool = nullptr;
    };

    struct WindowData
    {
        shared_ptr<ViewportResources> viewport_data;
        shared_ptr<RHI_SwapChain>     swapchain;
    };

    // Forward Declarations
    void InitialisePlatformInterface();

    // Engine subsystems
    Context*  g_context  = nullptr;
    Renderer* g_renderer = nullptr;

    // Resources
    static RHI_Device*                       g_rhi_device;
    static shared_ptr<RHI_Texture>           g_font_atlas;
    static shared_ptr<RHI_DepthStencilState> g_depth_stencil_state;
    static shared_ptr<RHI_RasterizerState>   g_rasterizer_state;
    static shared_ptr<RHI_BlendState>        g_blend_state;
    static shared_ptr<RHI_Shader>            g_shader_vertex;
    static shared_ptr<RHI_Shader>            g_shader_pixel;
    ViewportResources                        g_viewport_data; // per swapchain resources

    static void destroy_rhi_resources()
    {
        g_font_atlas          = nullptr;
        g_depth_stencil_state = nullptr;
        g_rasterizer_state    = nullptr;
        g_blend_state         = nullptr;
        g_shader_vertex       = nullptr;
        g_shader_pixel        = nullptr;
    }

    inline bool Initialize(Context* context)
    {
        g_context    = context;
        g_renderer   = context->GetSystem<Renderer>();
        g_rhi_device = g_renderer->GetRhiDevice().get();

        SP_ASSERT(g_context != nullptr);
        SP_ASSERT(g_rhi_device != nullptr);

        // Create required RHI objects
        {
            g_viewport_data = ViewportResources("imgui", g_rhi_device, g_renderer->GetSwapChain());

            g_depth_stencil_state = make_shared<RHI_DepthStencilState>(g_rhi_device, false, false, RHI_Comparison_Function::Always);

            g_rasterizer_state = make_shared<RHI_RasterizerState>
            (
                g_rhi_device,
                RHI_CullMode::None,
                RHI_PolygonMode::Solid,
                true,  // depth clip
                true,  // scissor
                false, // multi-sample
                false  // anti-aliased lines
            );

            g_blend_state = make_shared<RHI_BlendState>
            (
                g_rhi_device,
                true,
                RHI_Blend::Src_Alpha,     // source blend
                RHI_Blend::Inv_Src_Alpha, // destination blend
                RHI_Blend_Operation::Add, // blend op
                RHI_Blend::Inv_Src_Alpha, // source blend alpha
                RHI_Blend::Zero,          // destination blend alpha
                RHI_Blend_Operation::Add  // destination op alpha
            );

            // Compile shaders
            {
                const string shader_path = g_context->GetSystem<ResourceCache>()->GetResourceDirectory(ResourceDirectory::Shaders) + "\\ImGui.hlsl";

                bool async = false;

                g_shader_vertex = make_shared<RHI_Shader>(g_context);
                g_shader_vertex->Compile(RHI_Shader_Vertex, shader_path, async, RHI_Vertex_Type::Pos2dTexCol8);

                g_shader_pixel = make_shared<RHI_Shader>(g_context);
                g_shader_pixel->Compile(RHI_Shader_Pixel, shader_path, async);
            }
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
            g_font_atlas = make_shared<RHI_Texture2D>(g_context, atlas_width, atlas_height, RHI_Format_R8G8B8A8_Unorm, RHI_Texture_Srv, texture_data, "imgui_font_atlas");
            io.Fonts->TexID = static_cast<ImTextureID>(g_font_atlas.get());
        }

        // Setup back-end capabilities flags
        auto& io = GetIO();
        io.BackendFlags        |= ImGuiBackendFlags_RendererHasViewports;
        io.BackendFlags        |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendRendererName = "RHI";
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            InitialisePlatformInterface();
        }

        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnShutdown, SP_EVENT_HANDLER_STATIC(destroy_rhi_resources));

        return true;
    }

    inline void Shutdown()
    {
        DestroyPlatformWindows();
    }

    inline void Render(ImDrawData* draw_data, WindowData* window_data = nullptr, const bool clear = true)
    {
        // Validate draw data
        SP_ASSERT(draw_data != nullptr);

        // Don't render when minimised
        if (draw_data->DisplaySize.x == 0 || draw_data->DisplaySize.y == 0)
            return;

        // Get swap chain and cmd list
        bool is_child_window         = window_data != nullptr;
        RHI_SwapChain* swap_chain    = is_child_window ? window_data->swapchain.get() : g_renderer->GetSwapChain();
        ViewportResources* resources = is_child_window ? window_data->viewport_data.get() : &g_viewport_data;

        // Tick the command pool
        if (resources->cmd_pool->Step())
        {
            resources->cb_gpu->ResetOffset();
        }

        // Get current command list
        RHI_CommandList* cmd_list = resources->cmd_pool->GetCurrentCommandList();
        cmd_list->Begin();

        // Begin timeblock
        const char* name = is_child_window ? "imgui_window_child" : "imgui_window_main";
        // don't profile child windows, the profiler also requires more work in order to not
        // crash when the are brought back into the main viewport and their command pool is destroyed
        bool gpu_timing = !is_child_window;
        cmd_list->BeginTimeblock(name, true, gpu_timing);

        // Update vertex and index buffers
        RHI_VertexBuffer* vertex_buffer = nullptr;
        RHI_IndexBuffer* index_buffer   = nullptr;
        {
            const uint32_t cmd_index = resources->cmd_pool->GetCommandListIndex();

            while (resources->vertex_buffers.size() <= cmd_index)
            {
                bool is_mappable = true;
                resources->vertex_buffers.emplace_back(make_unique<RHI_VertexBuffer>(g_rhi_device, is_mappable, "imgui"));
                resources->index_buffers.emplace_back(make_unique<RHI_IndexBuffer>(g_rhi_device, is_mappable, "imgui"));
            }

            vertex_buffer = resources->vertex_buffers[cmd_index].get();
            index_buffer  = resources->index_buffers[cmd_index].get();

            // Grow vertex buffer as needed
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

            // Grow index buffer as needed
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

            // Copy and convert all vertices into a single contiguous buffer
            ImDrawVert* vtx_dst = static_cast<ImDrawVert*>(vertex_buffer->Map());
            ImDrawIdx*  idx_dst = static_cast<ImDrawIdx*>(index_buffer->Map());
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
            
                vertex_buffer->Unmap();
                index_buffer->Unmap();
            }
        }

        // Define pipeline state
        static RHI_PipelineState pso = {};
        pso.shader_vertex            = g_shader_vertex.get();
        pso.shader_pixel             = g_shader_pixel.get();
        pso.rasterizer_state         = g_rasterizer_state.get();
        pso.blend_state              = g_blend_state.get();
        pso.depth_stencil_state      = g_depth_stencil_state.get();
        pso.render_target_swapchain  = swap_chain;
        pso.clear_color[0]           = clear ? Color::standard_black : rhi_color_dont_care;
        pso.viewport.width           = draw_data->DisplaySize.x;
        pso.viewport.height          = draw_data->DisplaySize.y;
        pso.dynamic_scissor          = true;
        pso.primitive_topology       = RHI_PrimitiveTopology_Mode::TriangleList;

        // Set pipeline state
        cmd_list->SetPipelineState(pso);

        // Render
        cmd_list->BeginRenderPass();
        {
            // Setup orthographic projection matrix into our constant buffer
            // Our visible ImGui space lies from draw_data->DisplayPos (top left) to 
            // draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayMin is (0,0) for single viewport apps.
            {
                const float L = draw_data->DisplayPos.x;
                const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
                const float T = draw_data->DisplayPos.y;
                const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
                resources->cb_cpu.transform = Matrix
                (
                    2.0f / (R - L), 0.0f, 0.0f, (R + L) / (L - R),
                    0.0f, 2.0f / (T - B), 0.0f, (T + B) / (B - T),
                    0.0f, 0.0f, 0.5f, 0.5f,
                    0.0f, 0.0f, 0.0f, 1.0f
                );
            }

            cmd_list->SetBufferVertex(vertex_buffer);
            cmd_list->SetBufferIndex(index_buffer);

            // Render command lists
            int global_vtx_offset  = 0;
            int global_idx_offset  = 0;
            const ImVec2& clip_off = draw_data->DisplayPos;
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
                        scissor_rect.left   = pcmd->ClipRect.x - clip_off.x;
                        scissor_rect.top    = pcmd->ClipRect.y - clip_off.y;
                        scissor_rect.right  = pcmd->ClipRect.z - clip_off.x;
                        scissor_rect.bottom = pcmd->ClipRect.w - clip_off.y;

                        // Set scissor rectangle
                        cmd_list->SetScissorRectangle(scissor_rect);

                        // Set texture
                        resources->cb_cpu.options_texture_visualisation = 0;
                        if (RHI_Texture* texture = static_cast<RHI_Texture*>(pcmd->TextureId))
                        {
                            // During engine initialization, some editor icons might still be loading on a different thread
                            if (!texture->IsReadyForUse())
                                continue;

                            cmd_list->SetTexture(RendererBindingsSrv::tex, texture);

                            // Make sure single channel texture appear white instead of red.
                            if (texture->GetChannelCount() == 1)
                            {
                                texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise);
                                texture->SetFlag(RHI_Texture_Flags::RHI_Texture_Visualise_Channel_R);
                            }

                            resources->cb_cpu.options_texture_visualisation = texture->GetFlags();
                        }

                        // Update and bind the uber constant buffer (will only happen if the data changes)
                        {
                            resources->cb_gpu->Update(&resources->cb_cpu);

                            // Bind because the offset just changed
                            cmd_list->SetConstantBuffer(RendererBindingsCb::imgui, RHI_Shader_Vertex | RHI_Shader_Pixel, resources->cb_gpu);
                        }

                        // Draw
                        cmd_list->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
                    }

                }
                global_idx_offset += cmd_list_imgui->IdxBuffer.Size;
                global_vtx_offset += cmd_list_imgui->VtxBuffer.Size;
            }

            cmd_list->EndRenderPass();
        }

        cmd_list->EndTimeblock();

        if (!is_child_window)
        {
            swap_chain->SetLayout(RHI_Image_Layout::Present_Src, cmd_list);
        }

        cmd_list->End();
        cmd_list->Submit();
    }

    //--------------------------------------------
    // MULTI-VIEWPORT / PLATFORM INTERFACE SUPPORT
    //--------------------------------------------

    static void RHI_Window_Create(ImGuiViewport* viewport)
    {
        // PlatformHandle is SDL_Window, PlatformHandleRaw is HWND
        void* platform_handle = viewport->PlatformHandleRaw;
        SP_ASSERT_MSG(platform_handle != nullptr, "Platform handle is invalid");

        WindowData* window = new WindowData();
        window->swapchain = make_shared<RHI_SwapChain>
        (
            platform_handle,
            g_rhi_device,
            static_cast<uint32_t>(viewport->Size.x),
            static_cast<uint32_t>(viewport->Size.y),
            RHI_Format_R8G8B8A8_Unorm,
            2,
            RHI_Present_Immediate | RHI_Swap_Flip_Discard,
            (string("swapchain_child_") + string(to_string(viewport->ID))).c_str()
        );

        window->viewport_data = make_shared<ViewportResources>("imgui_child_window", g_rhi_device, window->swapchain.get());
        viewport->RendererUserData = window;
    }

    static void RHI_Window_Destroy(ImGuiViewport* viewport)
    {
        if (WindowData* window = static_cast<WindowData*>(viewport->RendererUserData))
        {
            g_rhi_device->DestroyCommandPool(window->viewport_data->cmd_pool);
            delete window;
        }

        viewport->RendererUserData = nullptr;
    }

    static void RHI_Window_SetSize(ImGuiViewport* viewport, const ImVec2 size)
    {
        SP_ASSERT_MSG(
            static_cast<WindowData*>(viewport->RendererUserData)->swapchain->Resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)),
            "Failed to resize swap chain"
        );
    }

    static void RHI_Window_Render(ImGuiViewport* viewport, void*)
    {
        const bool clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
        Render(viewport->DrawData, static_cast<WindowData*>(viewport->RendererUserData), clear);
    }

    static void RHI_Window_Present(ImGuiViewport* viewport, void*)
    {
        // Get window data
        WindowData* window = static_cast<WindowData*>(viewport->RendererUserData);

        // Validate cmd list state
        SP_ASSERT(window->viewport_data->cmd_pool->GetCurrentCommandList()->GetState() == Spartan::RHI_CommandListState::Submitted);

        // Present
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
