/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ===========================
#include <array>
#include "../Source/imgui.h"
#include "imgui_impl_sdl3.h"
#include "../../Widgets/TextureViewer.h"
#include "Core/Event.h"
#include "Rendering/Renderer_Buffers.h"
#include "Rendering/Renderer.h"
#include "Math/Rectangle.h"
#include "RHI/RHI_Device.h"
#include "RHI/RHI_Implementation.h"
#include "RHI/RHI_Shader.h"
#include "RHI/RHI_Texture.h"
#include "RHI/RHI_SwapChain.h"
#include "RHI/RHI_BlendState.h"
#include "RHI/RHI_Queue.h"
#include "RHI/RHI_CommandList.h"
#include "RHI/RHI_Buffer.h"
#include "RHI/RHI_PipelineState.h"
#include "RHI/RHI_RasterizerState.h"
#include "RHI/RHI_DepthStencilState.h"
#include <Debugging.h>
SP_WARNINGS_OFF
#include <SDL3/SDL_video.h>
SP_WARNINGS_ON
//======================================

namespace ImGui::RHI
{
    //= NAMESPACES =========
    using namespace spartan;
    using namespace math;
    using namespace std;
    //======================

    namespace
    {
        const uint32_t buffer_count = 8;

        struct ViewportRhiResources
        {
            array<unique_ptr<RHI_Buffer>, buffer_count> index_buffers;
            array<unique_ptr<RHI_Buffer>, buffer_count> vertex_buffers;
            Pcb_Pass push_constant_buffer_pass;
            uint32_t buffer_index = 0;

            ViewportRhiResources() = default;
            ViewportRhiResources(const char* name, RHI_SwapChain* swapchain)
            {
                // allocate buffers
                for (uint32_t i = 0; i < buffer_count; i++)
                {
                    vertex_buffers[i] = make_unique<RHI_Buffer>(RHI_Buffer_Type::Vertex, sizeof(ImDrawVert), 50000, nullptr, true, name);
                    index_buffers[i]  = make_unique<RHI_Buffer>(RHI_Buffer_Type::Index, sizeof(ImDrawIdx), 100000, nullptr, true, name);
                }
            }
        };

        struct WindowData
        {
            shared_ptr<ViewportRhiResources> viewport_rhi_resources;
            shared_ptr<RHI_SwapChain>        swapchain;
            RHI_CommandList* cmd_list = nullptr;
            bool pending_show        = false;
        };

        // main window rhi resources
        ViewportRhiResources g_viewport_data;

        // shared rhi resources (between all windows)
        shared_ptr<RHI_Texture>           g_font_atlas;
        shared_ptr<RHI_DepthStencilState> g_depth_stencil_state;
        shared_ptr<RHI_RasterizerState>   g_rasterizer_state;
        shared_ptr<RHI_BlendState>        g_blend_state;
        shared_ptr<RHI_Shader>            g_shader_vertex;
        shared_ptr<RHI_Shader>            g_shader_pixel;

        // deferred so the os window is revealed only after the first successful present
        void (*g_platform_show_window)(ImGuiViewport*) = nullptr;

        void platform_show_window(ImGuiViewport* viewport)
        {
            if (WindowData* window = static_cast<WindowData*>(viewport->RendererUserData))
            {
                window->pending_show = true;
                return;
            }

            if (g_platform_show_window)
            {
                g_platform_show_window(viewport);
            }
        }
    }

    // forward declarations
    void initialize_platform_interface();

    // initialise the imgui sdl platform glue, picks the matching backend variant for the active rhi
    // imgui ships separate sdl backends per graphics api, this is the one place that fans out
    bool InitializePlatformBackend(void* sdl_window)
    {
        SDL_Window* window = static_cast<SDL_Window*>(sdl_window);

        switch (spartan::RHI_Context::api_type)
        {
            case spartan::RHI_Api_Type::D3d12:  return ImGui_ImplSDL3_InitForD3D(window);
            case spartan::RHI_Api_Type::Vulkan: return ImGui_ImplSDL3_InitForVulkan(window);
        }

        return false;
    }

    void on_hdr_toggled()
    {
        bool hdr = cvar_hdr.GetValueAs<bool>();

        ImGuiPlatformIO& platform_io = GetPlatformIO();
        for (int i = 1; i < platform_io.Viewports.Size; i++)
        {
            if (WindowData* window = static_cast<WindowData*>(platform_io.Viewports[i]->RendererUserData))
            {
                window->swapchain->SetHdr(hdr);
            }
        }
    }

    // releases backend-owned imgui texture holders, must run while the imgui context is still alive
    void destroy_imgui_textures()
    {
        if (GImGui == nullptr)
        {
            return;
        }

        for (ImTextureData* tex : GetPlatformIO().Textures)
        {
            if (shared_ptr<RHI_Texture>* tex_holder = static_cast<shared_ptr<RHI_Texture>*>(tex->BackendUserData))
            {
                delete tex_holder;
                tex->BackendUserData = nullptr;
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
            }
        }

        g_font_atlas = nullptr;
    }

    void destroy_rhi_resources()
    {
        // imgui textures are released earlier in shutdown(), guard in case event fires standalone
        destroy_imgui_textures();

        g_font_atlas          = nullptr;
        g_depth_stencil_state = nullptr;
        g_rasterizer_state    = nullptr;
        g_blend_state         = nullptr;
        g_shader_vertex       = nullptr;
        g_shader_pixel        = nullptr;

        for (auto& ptr : g_viewport_data.index_buffers)
        {
            ptr = nullptr;
        }

        for (auto& ptr : g_viewport_data.vertex_buffers)
        {
            ptr = nullptr;
        }
    }

    // honor an imgui-managed texture (font atlas etc) according to the new imtexturedata protocol
    static void update_texture(ImTextureData* tex)
    {
        if (tex->Status == ImTextureStatus_WantCreate)
        {
            IM_ASSERT(tex->Format == ImTextureFormat_RGBA32);

            // copy pixel data into a texture slice
            vector<RHI_Texture_Slice> texture_data;
            vector<std::byte>& mip = texture_data.emplace_back().mips.emplace_back().bytes;
            const uint32_t size    = static_cast<uint32_t>(tex->Width * tex->Height * tex->BytesPerPixel);
            mip.resize(size);
            memcpy(&mip[0], tex->Pixels, size);

            // create a heap allocated shared_ptr so the texture stays alive while imgui references it
            shared_ptr<RHI_Texture>* tex_holder = new shared_ptr<RHI_Texture>(
                make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D, tex->Width, tex->Height, 1, 1,
                    RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv, "imgui_atlas", texture_data
                )
            );

            tex->BackendUserData = tex_holder;
            tex->SetTexID(reinterpret_cast<ImTextureID>(tex_holder->get()));
            tex->SetStatus(ImTextureStatus_OK);

            // first imgui-managed texture is the font atlas, track it as the default bind
            if (g_font_atlas == nullptr)
            {
                g_font_atlas = *tex_holder;
            }
        }
        else if (tex->Status == ImTextureStatus_WantUpdates)
        {
            // imgui added glyphs to the atlas, re-upload the latest pixel data
            shared_ptr<RHI_Texture>* tex_holder = static_cast<shared_ptr<RHI_Texture>*>(tex->BackendUserData);
            if (tex_holder)
            {
                bool is_font_atlas = (g_font_atlas == *tex_holder);

                vector<RHI_Texture_Slice> texture_data;
                vector<std::byte>& mip = texture_data.emplace_back().mips.emplace_back().bytes;
                const uint32_t size    = static_cast<uint32_t>(tex->Width * tex->Height * tex->BytesPerPixel);
                mip.resize(size);
                memcpy(&mip[0], tex->Pixels, size);

                *tex_holder = make_shared<RHI_Texture>(
                    RHI_Texture_Type::Type2D, tex->Width, tex->Height, 1, 1,
                    RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv, "imgui_atlas", texture_data
                );

                tex->SetTexID(reinterpret_cast<ImTextureID>(tex_holder->get()));

                if (is_font_atlas)
                {
                    g_font_atlas = *tex_holder;
                }
            }
            tex->SetStatus(ImTextureStatus_OK);
        }
        else if (tex->Status == ImTextureStatus_WantDestroy && tex->UnusedFrames > 0)
        {
            shared_ptr<RHI_Texture>* tex_holder = static_cast<shared_ptr<RHI_Texture>*>(tex->BackendUserData);
            if (tex_holder)
            {
                if (g_font_atlas.get() == tex_holder->get())
                {
                    g_font_atlas = nullptr;
                }
                delete tex_holder;
            }
            tex->BackendUserData = nullptr;
            tex->SetTexID(ImTextureID_Invalid);
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }

    void Initialize()
    {
        // create required RHI objects
        {
            g_viewport_data       = ViewportRhiResources("imgui", Renderer::GetSwapChain());
            g_depth_stencil_state = make_shared<RHI_DepthStencilState>(false, false, RHI_Comparison_Function::Always);
            g_rasterizer_state    = make_shared<RHI_RasterizerState>(RHI_PolygonMode::Solid, true);

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
                const string shader_path = ResourceCache::GetResourceDirectory(ResourceDirectory::Shaders) + "/imgui.hlsl";

                bool async = false;

                g_shader_vertex = make_shared<RHI_Shader>();
                g_shader_vertex->Compile(RHI_Shader_Type::Vertex, shader_path, async, RHI_Vertex_Type::Pos2dUvCol8);

                g_shader_pixel = make_shared<RHI_Shader>();
                g_shader_pixel->Compile(RHI_Shader_Type::Pixel, shader_path, async);
            }
        }

        // setup back-end capabilities flags, font atlas is created lazily via the imtexturedata protocol
        ImGuiIO& io             = GetIO();
        io.BackendFlags        |= ImGuiBackendFlags_RendererHasViewports;
        io.BackendFlags        |= ImGuiBackendFlags_RendererHasVtxOffset;
        io.BackendFlags        |= ImGuiBackendFlags_RendererHasTextures;
        io.BackendRendererName  = "RHI";
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            initialize_platform_interface();
        }

        SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnShutdown, SP_EVENT_HANDLER_STATIC(destroy_rhi_resources));
        SP_SUBSCRIBE_TO_EVENT(EventType::HdrToggled, SP_EVENT_HANDLER_STATIC(on_hdr_toggled));
    }

    void shutdown()
    {
        // release imgui-owned textures while the context is still alive,
        // the renderer shutdown event fires later and the imtexturedata list is gone by then
        destroy_imgui_textures();
        DestroyPlatformWindows();
    }

    void render(ImDrawData* draw_data, WindowData* window_data = nullptr, const bool clear = true)
    {
        // skip the first two frames to let the renderer fully initialize.
        // frame 0: pipeline layouts and descriptor sets are still being created.
        // frame 1: bindless draw_data buffer descriptor may not have been written yet.
        if (Renderer::GetFrameNumber() < 2)
        {
            return;
        }

        // get resources
        bool is_main_window                 = window_data == nullptr;
        ViewportRhiResources* rhi_resources = is_main_window ? &g_viewport_data : window_data->viewport_rhi_resources.get();
        RHI_SwapChain* swapchain            = is_main_window ? Renderer::GetSwapChain() : window_data->swapchain.get();
        uint32_t buffer_index               = rhi_resources->buffer_index;
        rhi_resources->buffer_index         = (rhi_resources->buffer_index + 1) % buffer_count;
        RHI_Buffer* vertex_buffer           = rhi_resources->vertex_buffers[buffer_index].get();
        RHI_Buffer* index_buffer            = rhi_resources->index_buffers[buffer_index].get();
        RHI_CommandList* cmd_list           = Renderer::GetCommandListPresent();

        // if that's a child window, update it's swapchain and give it a command list
        if (!is_main_window)
        {
            swapchain->AcquireNextImage();

            window_data->cmd_list = RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->NextCommandList();
            cmd_list              = window_data->cmd_list;

            window_data->cmd_list->Begin();
        }

        // when the engine splash screen is shown, the command list is not valid as the renderer is initializing
        if (!cmd_list || cmd_list->GetState() != RHI_CommandListState::Recording)
        {
            return;
        }

        // honor texture create/update/destroy requests from imgui, must run before draw command processing
        if (draw_data->Textures != nullptr)
        {
            for (ImTextureData* tex : *draw_data->Textures)
            {
                if (tex->Status != ImTextureStatus_OK)
                {
                    update_texture(tex);
                }
            }
        }

        // update vertex and index buffers
        {
            // grow vertex buffer as needed
            if (vertex_buffer->GetElementCount() < static_cast<uint32_t>(draw_data->TotalVtxCount))
            {
                const uint32_t count                        = vertex_buffer->GetElementCount();
                const uint32_t count_new                    = draw_data->TotalVtxCount + 15000;
                rhi_resources->vertex_buffers[buffer_index] = make_unique<RHI_Buffer>(RHI_Buffer_Type::Vertex, sizeof(ImDrawVert), count_new, nullptr, true, vertex_buffer->GetObjectName().c_str());
                vertex_buffer                               = rhi_resources->vertex_buffers[buffer_index].get();

                if (count != 0)
                {
                    SP_LOG_INFO("Vertex buffer has been re-allocated to fit %d vertices", count_new);
                }
            }

            // grow index buffer as needed
            if (index_buffer->GetElementCount() < static_cast<uint32_t>(draw_data->TotalIdxCount))
            {
                const uint32_t count                       = index_buffer->GetElementCount();
                const uint32_t count_new                   = draw_data->TotalIdxCount + 30000;
                rhi_resources->index_buffers[buffer_index] = make_unique<RHI_Buffer>(RHI_Buffer_Type::Index, sizeof(ImDrawIdx), count_new, nullptr, true, index_buffer->GetObjectName().c_str());
                index_buffer                               = rhi_resources->index_buffers[buffer_index].get();

                if (count != 0)
                {
                    SP_LOG_INFO("Index buffer has been re-allocated to fit %d indices", count_new);
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

        // set pipeline state
        static RHI_PipelineState pso         = {};
        pso.name                             = "imgui";
        pso.shaders[RHI_Shader_Type::Vertex] = g_shader_vertex.get();
        pso.shaders[RHI_Shader_Type::Pixel]  = g_shader_pixel.get();
        pso.rasterizer_state                 = g_rasterizer_state.get();
        pso.blend_state                      = g_blend_state.get();
        pso.depth_stencil_state              = g_depth_stencil_state.get();
        pso.render_target_swapchain          = swapchain;
        pso.clear_color[0]                   = clear ? Color::standard_black : rhi_color_dont_care;

        // start the pass
        const char* name = is_main_window ? "imgui_window_main" : "imgui_window_child";
        bool gpu_timing  = is_main_window;
        cmd_list->BeginTimeblock(name, true, spartan::Debugging::IsGpuTimingEnabled() && gpu_timing);
        cmd_list->SetPipelineState(pso);
        cmd_list->SetBufferVertex(vertex_buffer);
        cmd_list->SetBufferIndex(index_buffer);
        cmd_list->SetCullMode(RHI_CullMode::None);

        // render
        {
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
                            math::Rectangle rectangle;
                            rectangle.x      = pcmd->ClipRect.x - draw_data->DisplayPos.x;
                            rectangle.y      = pcmd->ClipRect.y - draw_data->DisplayPos.y;
                            rectangle.width  = (pcmd->ClipRect.z - draw_data->DisplayPos.x) - rectangle.x;
                            rectangle.height = (pcmd->ClipRect.w - draw_data->DisplayPos.y) - rectangle.y;

                            cmd_list->SetScissorRectangle(rectangle);
                        }

                        // set texture and update texture viewer parameters
                        {
                            float mip_level            = 0.0f;
                            float array_level          = 0.0f;
                            bool is_texture_visualised = false;
                            bool is_frame_texture      = false;
                            bool texture_bound         = false;

                            if (spartan::RHI_Texture* texture = reinterpret_cast<spartan::RHI_Texture*>(pcmd->GetTexID()))
                            {
                                is_frame_texture = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output)->GetObjectId() == texture->GetObjectId();

                                // during engine startup, some textures might be loading in different threads
                                if (texture->GetResourceState() == ResourceState::PreparedForGpu)
                                {
                                    // update texture viewer parameters
                                    is_texture_visualised = TextureViewer::GetVisualisedTextureId() == texture->GetObjectId();
                                    if (is_texture_visualised)
                                    {
                                        mip_level   = static_cast<float>(TextureViewer::GetMipLevel());
                                        array_level = static_cast<float>(TextureViewer::GetArrayLevel());
                                    }

                                    cmd_list->SetTexture(Renderer_BindingsSrv::tex, texture);
                                    texture_bound = true;
                                }
                            }

                            // always bind a texture to avoid uninitialized descriptor errors
                            if (!texture_bound && g_font_atlas)
                            {
                                cmd_list->SetTexture(Renderer_BindingsSrv::tex, g_font_atlas.get());
                            }

                            // pack booleans into uint bitfield
                            uint32_t flags = 0;
                            if (is_texture_visualised)
                            {
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Channel_R)    ? (1u << 0) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Channel_G)    ? (1u << 1) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Channel_B)    ? (1u << 2) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Channel_A)    ? (1u << 3) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_GammaCorrect) ? (1u << 4) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Pack)         ? (1u << 5) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Boost)        ? (1u << 6) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Abs)          ? (1u << 7) : 0;
                                flags |= (TextureViewer::GetVisualisationFlags() & Visualise_Sample_Point) ? (1u << 8) : 0;
                            }
                            flags |= is_texture_visualised ? (1u << 9) : 0;
                            flags |= is_frame_texture      ? (1u << 10) : 0;

                            // store bitfield in m00 and mip/array levels in m23, m30
                            rhi_resources->push_constant_buffer_pass.set_f3_value(*reinterpret_cast<float*>(&flags), 0.0f, 0.0f);
                            rhi_resources->push_constant_buffer_pass.set_f2_value(mip_level, array_level);
                        }

                        // compute transform matrix and write to the bindless draw data buffer
                        {
                            const float L = draw_data->DisplayPos.x;
                            const float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
                            const float T = draw_data->DisplayPos.y;
                            const float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;

                            Matrix projection
                            (
                                2.0f / (R - L), 0.0f, 0.0f, (R + L) / (L - R),
                                0.0f, 2.0f / (T - B), 0.0f, (T + B) / (B - T),
                                0.0f, 0.0f, 0.5f, 0.5f,
                                0.0f, 0.0f, 0.0f, 1.0f
                            );

                            const uint32_t draw_index = Renderer::WriteDrawData(projection);
                            if (draw_index == numeric_limits<uint32_t>::max())
                            {
                                continue;
                            }
                            rhi_resources->push_constant_buffer_pass.draw_index = draw_index;
                        }

                        cmd_list->PushConstants(0, sizeof(Pcb_Pass), &rhi_resources->push_constant_buffer_pass);

                        cmd_list->DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
                    }
                }

                global_idx_offset += static_cast<uint32_t>(cmd_list_imgui->IdxBuffer.Size);
                global_vtx_offset += static_cast<uint32_t>(cmd_list_imgui->VtxBuffer.Size);
            }
        }

        cmd_list->EndTimeblock();

        // for child windows, submit and prepare for presentation
        if (!is_main_window)
        {
            if (swapchain->IsImageAcquired())
            {
                cmd_list->InsertBarrier(swapchain->GetRhiRt(), swapchain->GetFormat(), 0, 1, 1, RHI_Image_Layout::Present_Source);
                // use per-swapchain-image semaphore to signal rendering complete
                cmd_list->Submit(swapchain->GetImageAcquiredSemaphore(), false, swapchain->GetRenderingCompleteSemaphore());
            }
            else
            {
                // no image acquired (window minimized/transitioning), submit without presentation semaphores
                cmd_list->Submit(nullptr, true);
            }
        }
    }

    void window_create(ImGuiViewport* viewport)
    {
        SP_ASSERT(viewport->PlatformHandle);

        // note: platformHandle is SDL_Window, PlatformHandleRaw is HWND
        SDL_Window* sdl_window = SDL_GetWindowFromID(static_cast<uint32_t>(reinterpret_cast<uintptr_t>(viewport->PlatformHandle)));

        WindowData* window = new WindowData();
        window->swapchain = make_shared<RHI_SwapChain>
        (
            sdl_window, 
            static_cast<uint32_t>(viewport->Size.x),
            static_cast<uint32_t>(viewport->Size.y),
            RHI_Present_Mode::Immediate,
            2,
            spartan::cvar_hdr.GetValueAs<bool>(),
            (string("swapchain_child_") + string(to_string(viewport->ID))).c_str()
        );

        window->viewport_rhi_resources = make_shared<ViewportRhiResources>("imgui_child_window", window->swapchain.get());
        viewport->RendererUserData     = window;
    }

    void window_destroy(ImGuiViewport* viewport)
    {
        if (WindowData* window = static_cast<WindowData*>(viewport->RendererUserData))
        {
            viewport->RendererUserData = nullptr;
            delete window;
        }
    }

    void window_resize(ImGuiViewport* viewport, const ImVec2 size)
    {
        static_cast<WindowData*>(viewport->RendererUserData)->swapchain->Resize(static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y));
    }

    void window_render(ImGuiViewport* viewport, void*)
    {
        const bool clear = !(viewport->Flags & ImGuiViewportFlags_NoRendererClear);
        render(viewport->DrawData, static_cast<WindowData*>(viewport->RendererUserData), clear);
    }

    void window_present(ImGuiViewport* viewport, void*)
    {
        WindowData* window = static_cast<WindowData*>(viewport->RendererUserData);
        if (!window || !window->swapchain)
        {
            return;
        }

        const bool had_image = window->swapchain->IsImageAcquired();
        window->swapchain->Present(window->cmd_list);

        // reveal only after a real present so undocking does not flash an empty os window
        if (had_image && window->pending_show && g_platform_show_window)
        {
            g_platform_show_window(viewport);
            window->pending_show = false;
        }
    }

    void initialize_platform_interface()
    {
        ImGuiPlatformIO& platform_io       = ImGui::GetPlatformIO();
        platform_io.Renderer_CreateWindow  = window_create;
        platform_io.Renderer_DestroyWindow = window_destroy;
        platform_io.Renderer_SetWindowSize = window_resize;
        platform_io.Renderer_RenderWindow  = window_render;
        platform_io.Renderer_SwapBuffers   = window_present;

        g_platform_show_window           = platform_io.Platform_ShowWindow;
        platform_io.Platform_ShowWindow  = platform_show_window;
    }
}
