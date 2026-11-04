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
#include "profiling/Profiler.h"

//= INCLUDES ===========================
#include <array>
#include "../source/imgui.h"
#include "../source/imgui_internal.h"
#include "imgui_impl_sdl3.h"
#include "ui/TexturePreview.h"
#include "resource/ResourceCache.h"
#include "core/Event.h"
#include "rendering/Renderer_Buffers.h"
#include "rendering/Renderer.h"
#include "math/Rectangle.h"
#include "rhi/RHI_Device.h"
#include "rhi/RHI_Implementation.h"
#include "rhi/RHI_Shader.h"
#include "rhi/RHI_Texture.h"
#include "rhi/RHI_SwapChain.h"
#include "rhi/RHI_BlendState.h"
#include "rhi/RHI_Queue.h"
#include "rhi/RHI_CommandList.h"
#include "rhi/RHI_Buffer.h"
#include "rhi/RHI_PipelineState.h"
#include "rhi/RHI_RasterizerState.h"
#include "rhi/RHI_DepthStencilState.h"
#include "rhi/RHI_FrameGeneration.h"
#include <cfloat>
#include <Settings.h>
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

        // frosted glass, every pane shows a blurred copy of whatever was drawn under it
        TConsoleVar<bool> cvar_editor_glass("editor.glass", true, "frosted glass behind editor panels, floating windows, popups, menus and tooltips");
        TConsoleVar<bool> cvar_editor_glass_panels("editor.glass_panels", true, "docked panels are frosted glass over a blurred copy of the scene");
        TConsoleVar<float> cvar_editor_glass_opacity("editor.glass_opacity", 0.6f, "how much of a floating glass window's background color covers the blurred backdrop, 0 to 1");
        TConsoleVar<float> cvar_editor_glass_panel_opacity("editor.glass_panel_opacity", 0.5f, "how much of a docked panel's background color covers the blurred scene behind it, 0 to 1");
        TConsoleVar<float> cvar_editor_glass_blur("editor.glass_blur", 10.0f, "glass blur radius in quarter resolution pixels, 1 to 32");

        // the main window ui is drawn into glass_target when a glass window is up, so what lies under a window can be read back
        shared_ptr<RHI_Texture> g_glass_target;
        array<shared_ptr<RHI_Texture>, 2> g_glass_backdrop;
        ImDrawList* g_glass_draw_list = nullptr;
        Pcb_Pass g_glass_push_constants;
        const uint32_t glass_flag = 1u << 12;

        enum class GlassStep : uint8_t
        {
            Ambient,  // the 3d frame stretched across the editor, what the docked panels are frosted over
            Capture,  // copy and blur everything drawn so far, skipped while nothing under the panes changed
            Plain,    // shadows, veils, sheens and rims
            Backdrop  // the blurred copy laid under a pane
        };

        struct GlassSegment
        {
            GlassStep step        = GlassStep::Plain;
            bool after_list       = false;
            uint32_t list         = 0;
            uint32_t index_offset = 0;
            uint32_t index_count  = 0;
            ImRect bounds;
        };

        enum class GlassThin : uint8_t
        {
            None,
            Leading, // the fills a window opens its draw list with
            Host     // every panel and tab strip fill a dock host lays out for its docked windows
        };

        struct GlassList
        {
            GlassThin thin    = GlassThin::None;
            float opacity     = 1.0f;
            ImU32 background  = 0;
            uint32_t fill_begin = 0;
            uint32_t fill_end   = 0;
        };

        // rectangles a window without a background asked to be frosted, gathered during layout and consumed by render
        struct GlassPane
        {
            const ImGuiWindow* window = nullptr;
            ImRect rect;
            float rounding = 0.0f;
        };
        vector<GlassPane> g_glass_panes;

        bool has_glass_panes(const ImGuiWindow* window)
        {
            return std::any_of(g_glass_panes.begin(), g_glass_panes.end(), [window](const GlassPane& pane) { return pane.window == window; });
        }

        // root windows that float over something, docked panels and the dockspace host sit on nothing but the editor background
        bool is_glass_window(const ImGuiWindow* window, const ImGuiViewport* viewport)
        {
            if (!window->Active || window->Hidden || window->Collapsed || window->Viewport != viewport)
            {
                return false;
            }

            // sub-menus are child windows too, but like popups and tooltips they float over whatever is behind them
            const bool is_floating_child = (window->Flags & (ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip)) != 0;
            if ((window->Flags & ImGuiWindowFlags_ChildWindow) && !is_floating_child)
            {
                return false;
            }

            const ImGuiWindowFlags excluded = ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoBringToFrontOnFocus;
            return (window->Flags & excluded) == 0 && !window->DockIsActive;
        }

        void thin_vertex(ImU32& color, const float opacity)
        {
            const ImU32 alpha      = (color & IM_COL32_A_MASK) >> IM_COL32_A_SHIFT;
            const ImU32 alpha_thin = static_cast<ImU32>(static_cast<float>(alpha) * opacity + 0.5f);
            color = (color & ~IM_COL32_A_MASK) | (alpha_thin << IM_COL32_A_SHIFT);
        }

        bool is_frame_fill(const ImU32 color)
        {
            return color == GetColorU32(ImGuiCol_TitleBg) || color == GetColorU32(ImGuiCol_TitleBgActive) || color == GetColorU32(ImGuiCol_TitleBgCollapsed) || color == GetColorU32(ImGuiCol_MenuBarBg);
        }

        // imgui opens a window's draw list with its background, title bar and menu bar fills, thinning them lets the backdrop through
        // a background of 0 takes the first fill as the window background
        void thin_window_background(ImDrawVert* vertices, const int count, const float opacity, ImU32 background)
        {
            for (int i = 0; i < count; i++)
            {
                ImU32& color = vertices[i].col;
                if ((color & IM_COL32_A_MASK) == 0)
                {
                    continue; // anti-aliased fringe
                }

                if (background == 0)
                {
                    background = color;
                }

                if (color != background && !is_frame_fill(color))
                {
                    break;
                }

                thin_vertex(color, opacity);
            }
        }

        // a dock host draws the backgrounds and tab strips of all its docked windows, interleaved with tabs and buttons
        // empty nodes fill with the editor void, cleared so they show the same dimmed scene as the gutters
        void thin_dock_host(ImDrawVert* vertices, const int count, const float opacity, const ImU32* fills, const uint32_t fill_count)
        {
            const ImU32 void_color = GetColorU32(ImGuiCol_DockingEmptyBg);
            for (int i = 0; i < count; i++)
            {
                ImU32& color = vertices[i].col;
                if ((color & IM_COL32_A_MASK) == 0)
                {
                    continue;
                }

                if (color == void_color)
                {
                    color &= ~IM_COL32_A_MASK;
                    continue;
                }

                bool is_fill = is_frame_fill(color);
                for (uint32_t f = 0; f < fill_count && !is_fill; f++)
                {
                    is_fill = color == fills[f];
                }

                if (is_fill)
                {
                    thin_vertex(color, opacity);
                }
            }
        }

        ImVec4 lerp_color(const ImVec4& a, const ImVec4& b, const float t)
        {
            return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
        }

        // recolors the vertices added since vtx_start with a vertical gradient, anti-aliased fringes keep their fade
        void glass_shade_vertical(ImDrawList* draw_list, const int vtx_start, const float y0, const float y1, const ImVec4& top, const ImVec4& bottom)
        {
            const float height = ImMax(y1 - y0, 1.0f);
            for (int i = vtx_start; i < draw_list->VtxBuffer.Size; i++)
            {
                ImDrawVert& vertex = draw_list->VtxBuffer[i];
                ImVec4 value       = lerp_color(top, bottom, ImSaturate((vertex.pos.y - y0) / height));
                value.w           *= static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
                vertex.col         = ColorConvertFloat4ToU32(value);
            }
        }

        // a soft penumbra under a floating pane, rings fading outward and dropped a little so the light reads as coming from above
        void glass_add_shadow(ImDrawList* draw_list, const ImRect& rect, const float rounding, const float scale)
        {
            constexpr int rings  = 8;
            const float spread   = 18.0f * scale;
            const float drop     = 5.0f * scale;
            const float step     = spread / static_cast<float>(rings);
            for (int i = 0; i < rings; i++)
            {
                const float t      = (static_cast<float>(i) + 0.5f) / static_cast<float>(rings);
                const float expand = step * (static_cast<float>(i) + 0.5f);
                const float alpha  = 0.16f * (1.0f - t) * (1.0f - t);
                draw_list->AddRect(ImVec2(rect.Min.x - expand, rect.Min.y - expand + drop), ImVec2(rect.Max.x + expand, rect.Max.y + expand + drop), ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, alpha)), rounding + expand, step + 0.5f);
            }
        }

        // light skimming the top of the pane, gone before it reaches the middle
        void glass_add_sheen(ImDrawList* draw_list, const ImRect& rect, const float rounding, const float strength)
        {
            const int vtx_start = draw_list->VtxBuffer.Size;
            draw_list->AddRectFilled(rect.Min, rect.Max, IM_COL32_WHITE, rounding);
            glass_shade_vertical(draw_list, vtx_start, rect.Min.y, rect.Min.y + rect.GetHeight() * 0.42f, ImVec4(1.0f, 1.0f, 1.0f, strength), ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
        }

        // the cut edge of the glass: lit along the top, dim along the bottom, with a dark hairline outside to hold it against bright backdrops
        void glass_add_rim(ImDrawList* draw_list, const ImRect& rect, const float rounding, const float scale, const float top, const float bottom, const bool outline)
        {
            const float thickness = ImMax(1.0f, IM_ROUND(scale));
            if (outline)
            {
                draw_list->AddRect(ImVec2(rect.Min.x - 0.5f, rect.Min.y - 0.5f), ImVec2(rect.Max.x + 0.5f, rect.Max.y + 0.5f), IM_COL32(0, 0, 0, 90), rounding + 0.5f, thickness);
            }

            const int vtx_start = draw_list->VtxBuffer.Size;
            draw_list->AddRect(ImVec2(rect.Min.x + 0.5f, rect.Min.y + 0.5f), ImVec2(rect.Max.x - 0.5f, rect.Max.y - 0.5f), IM_COL32_WHITE, rounding, thickness);
            glass_shade_vertical(draw_list, vtx_start, rect.Min.y, rect.Max.y, ImVec4(1.0f, 1.0f, 1.0f, top), ImVec4(1.0f, 1.0f, 1.0f, bottom));
        }

        // closes the run of geometry added since index_start into a segment, merging it into the previous one when they draw the same way
        void glass_push_segment(vector<GlassSegment>& segments, const uint32_t list, const bool after_list, const GlassStep step, const uint32_t index_start, const ImRect& bounds)
        {
            const uint32_t index_end = static_cast<uint32_t>(g_glass_draw_list->IdxBuffer.Size);
            if (step != GlassStep::Capture && index_end == index_start)
            {
                return;
            }

            if (!segments.empty() && step != GlassStep::Capture)
            {
                GlassSegment& last = segments.back();
                if (last.list == list && last.after_list == after_list && last.step == step && last.index_offset + last.index_count == index_start)
                {
                    last.index_count = index_end - last.index_offset;
                    last.bounds.Add(bounds);
                    return;
                }
            }

            GlassSegment segment;
            segment.step         = step;
            segment.after_list   = after_list;
            segment.list         = list;
            segment.index_offset = index_start;
            segment.index_count  = index_end - index_start;
            segment.bounds       = bounds;
            segments.push_back(segment);
        }

        ImRect node_rect(const ImGuiDockNode* node)
        {
            return ImRect(node->Pos.x, node->Pos.y, node->Pos.x + node->Size.x, node->Pos.y + node->Size.y);
        }

        // where the viewport shows the 3d frame, and which part of the frame it shows
        bool glass_find_frame(const ImDrawData* draw_data, const uint64_t frame_id, ImRect& rect, ImRect& uv)
        {
            for (int i = 0; i < draw_data->CmdListsCount; i++)
            {
                const ImDrawList* cmd_list = draw_data->CmdLists[i];
                for (const ImDrawCmd& cmd : cmd_list->CmdBuffer)
                {
                    RHI_Texture* texture = cmd.UserCallback == nullptr ? reinterpret_cast<RHI_Texture*>(cmd.GetTexID()) : nullptr;
                    if (!texture || texture->GetObjectId() != frame_id || cmd.ElemCount == 0)
                    {
                        continue;
                    }

                    rect = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
                    for (uint32_t k = 0; k < cmd.ElemCount; k++)
                    {
                        const ImDrawVert& vertex = cmd_list->VtxBuffer[cmd_list->IdxBuffer[cmd.IdxOffset + k] + cmd.VtxOffset];
                        if (vertex.pos.x < rect.Min.x)
                        {
                            rect.Min.x = vertex.pos.x;
                            uv.Min.x   = vertex.uv.x;
                        }
                        if (vertex.pos.y < rect.Min.y)
                        {
                            rect.Min.y = vertex.pos.y;
                            uv.Min.y   = vertex.uv.y;
                        }
                        if (vertex.pos.x > rect.Max.x)
                        {
                            rect.Max.x = vertex.pos.x;
                            uv.Max.x   = vertex.uv.x;
                        }
                        if (vertex.pos.y > rect.Max.y)
                        {
                            rect.Max.y = vertex.pos.y;
                            uv.Max.y   = vertex.uv.y;
                        }
                    }

                    return rect.GetWidth() > 1.0f && rect.GetHeight() > 1.0f;
                }
            }

            return false;
        }

        RHI_Texture* glass_get_target(RHI_SwapChain* swapchain)
        {
            const uint32_t width  = swapchain->GetWidth();
            const uint32_t height = swapchain->GetHeight();
            const RHI_Format format = swapchain->GetFormat();
            if (width == 0 || height == 0)
            {
                return nullptr;
            }

            if (!g_glass_target || g_glass_target->GetWidth() != width || g_glass_target->GetHeight() != height || g_glass_target->GetFormat() != format)
            {
                g_glass_target = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, width, height, 1, 1, format, RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "imgui_glass_target");
            }

            // a quarter resolution backdrop, the blur hides the downsample and stays cheap at any window size
            const uint32_t backdrop_width  = std::max(width / 4, 1u);
            const uint32_t backdrop_height = std::max(height / 4, 1u);
            for (shared_ptr<RHI_Texture>& backdrop : g_glass_backdrop)
            {
                if (!backdrop || backdrop->GetWidth() != backdrop_width || backdrop->GetHeight() != backdrop_height)
                {
                    backdrop = make_shared<RHI_Texture>(RHI_Texture_Type::Type2D, backdrop_width, backdrop_height, 1, 1, RHI_Format::R16G16B16A16_Float, RHI_Texture_Uav | RHI_Texture_Srv | RHI_Texture_Rtv | RHI_Texture_ClearBlit, "imgui_glass_backdrop");
                }
            }

            const bool ready = g_glass_target->GetRhiResource() && g_glass_backdrop[0]->GetRhiResource() && g_glass_backdrop[1]->GetRhiResource();
            return ready ? g_glass_target.get() : nullptr;
        }

        // copies what has been drawn into the target so far and blurs it, the result lands in g_glass_backdrop[0]
        void glass_capture_backdrop(RHI_Texture* target)
        {
            RHI_Texture* backdrop = g_glass_backdrop[0].get();
            RHI_Texture* scratch  = g_glass_backdrop[1].get();
            RHI_CommandList::Blit(target, backdrop, false);

            RHI_Shader* shader = Renderer::GetShader(Renderer_Shader::blur_gaussian_c);
            if (!shader || !shader->IsCompiled())
            {
                return;
            }

            const float radius           = std::clamp(cvar_editor_glass_blur.GetValue(), 1.0f, 32.0f);
            const uint32_t group_count_x = (backdrop->GetWidth() + 7) / 8;
            const uint32_t group_count_y = (backdrop->GetHeight() + 7) / 8;

            RHI_CommandList::BeginPass("imgui_glass_blur", false);
            RHI_CommandList::SetShader(shader);

            // two separable passes, a single wide gaussian at quarter resolution still shows the downsample blocks
            for (uint32_t i = 0; i < 2; i++)
            {
                g_glass_push_constants.set(pass_blur::radius, radius);
                g_glass_push_constants.set(pass_blur::vertical, false);
                RHI_CommandList::PushConstants(0, sizeof(Pcb_Pass), &g_glass_push_constants);
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, backdrop);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, scratch);
                RHI_CommandList::Dispatch(group_count_x, group_count_y);

                g_glass_push_constants.set(pass_blur::vertical, true);
                RHI_CommandList::PushConstants(0, sizeof(Pcb_Pass), &g_glass_push_constants);
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, scratch);
                RHI_CommandList::SetTexture(Renderer_BindingsUav::tex, backdrop);
                RHI_CommandList::Dispatch(group_count_x, group_count_y);
            }

            RHI_CommandList::EndPass();
        }

        void reset_render_state(const ImDrawList*, const ImDrawCmd*)
        {
        }

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
            case spartan::RHI_Api_Type::Metal:  return ImGui_ImplSDL3_InitForMetal(window);
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

    void add_glass_pane(const ImVec2& min, const ImVec2& max, const float rounding)
    {
        ImGuiWindow* window = GetCurrentWindowRead();
        if (window && max.x > min.x && max.y > min.y)
        {
            g_glass_panes.push_back({ window, ImRect(min, max), rounding });
        }
    }

    void clear_glass_panes()
    {
        g_glass_panes.clear();
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
        g_glass_target        = nullptr;
        g_glass_backdrop.fill(nullptr);

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
        spartan::ScopedTimeBlock block("ui_texture_update");
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
            // Existing glyphs keep their texture and UVs. Upload the bounding
            // rectangle of new glyphs before this frame's UI draw, without a
            // texture allocation or a synchronous graphics-queue round trip.
            auto* tex_holder = static_cast<shared_ptr<RHI_Texture>*>(tex->BackendUserData);
            if (!tex_holder || !*tex_holder) return;
            const ImTextureRect& rect = tex->UpdateRect;
            if (rect.w > 0 && rect.h > 0)
            {
                const size_t row_bytes = static_cast<size_t>(rect.w) * tex->BytesPerPixel;
                vector<std::byte> pixels(row_bytes * rect.h);
                for (uint32_t row = 0; row < rect.h; ++row)
                    memcpy(pixels.data() + row * row_bytes, tex->GetPixelsAt(rect.x, rect.y + row), row_bytes);
                if (!(*tex_holder)->UpdateRegion(rect.x, rect.y, rect.w, rect.h, pixels.data(), true))
                    return;
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
            g_viewport_data       = ViewportRhiResources("imgui", RHI_Device::GetSwapChain());
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
        GetPlatformIO().DrawCallback_ResetRenderState = reset_render_state;
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
        GetPlatformIO().DrawCallback_ResetRenderState = nullptr;
        destroy_imgui_textures();
        DestroyPlatformWindows();

        if (g_glass_draw_list)
        {
            IM_DELETE(g_glass_draw_list);
            g_glass_draw_list = nullptr;
        }
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
        RHI_SwapChain* swapchain            = is_main_window ? RHI_Device::GetSwapChain() : window_data->swapchain.get();
        uint32_t buffer_index               = rhi_resources->buffer_index;
        rhi_resources->buffer_index         = (rhi_resources->buffer_index + 1) % buffer_count;
        RHI_Buffer* vertex_buffer           = rhi_resources->vertex_buffers[buffer_index].get();
        RHI_Buffer* index_buffer            = rhi_resources->index_buffers[buffer_index].get();
        RHI_CommandList* cmd_list           = nullptr;

        // if that's a child window, update it's swapchain and give it a command list
        if (!is_main_window)
        {
            swapchain->AcquireNextImage();

            window_data->cmd_list = RHI_Device::GetQueue(RHI_Queue_Type::Graphics)->NextCommandList();
            cmd_list              = window_data->cmd_list;

            window_data->cmd_list->Begin();
            RHI_Device::Bind(cmd_list);
        }

        // the main window draws straight into the back buffer, so bail out when there is no acquired image
        // recording against a stale index would submit a write to a buffer that is not the one being presented
        if (is_main_window && !swapchain->IsImageAcquired())
        {
            return;
        }

        // pin graphics, world load uploads otherwise steal the bound list
        if (is_main_window)
        {
            RHI_Device::Bind(RHI_Frame_List::Graphics);
        }

        // when the engine splash screen is shown, the command list is not valid as the renderer is initializing
        if (!RHI_Device::IsRecording())
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

        // find the glass panes: floating windows, and the panels each dock host lays out for its docked windows
        static vector<GlassList> glass_lists;
        static vector<ImGuiWindow*> list_windows;
        static vector<ImGuiDockNode*> glass_nodes;
        static vector<GlassPane> window_panes;
        static vector<ImU32> glass_fills;
        static vector<GlassSegment> glass_segments;
        glass_lists.assign(draw_data->CmdListsCount, GlassList());
        list_windows.assign(draw_data->CmdListsCount, nullptr);
        glass_fills.clear();
        glass_segments.clear();
        RHI_Texture* glass_target = nullptr;
        if (is_main_window && cvar_editor_glass.GetValue())
        {
            for (int i = 0; i < draw_data->CmdListsCount; i++)
            {
                for (ImGuiWindow* window : GImGui->Windows)
                {
                    if (window->DrawList == draw_data->CmdLists[i])
                    {
                        list_windows[i] = window;
                        break;
                    }
                }
            }

            bool any_glass = false;
            for (int i = 0; i < draw_data->CmdListsCount && !any_glass; i++)
            {
                const ImGuiWindow* window = list_windows[i];
                any_glass = window && (is_glass_window(window, draw_data->OwnerViewport) || (cvar_editor_glass_panels.GetValue() && window->DockNodeAsHost) || has_glass_panes(window));
            }

            glass_target = any_glass ? glass_get_target(swapchain) : nullptr;
        }

        RHI_Texture* frame_output = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output);
        if (glass_target)
        {
            if (!g_glass_draw_list)
            {
                g_glass_draw_list = IM_NEW(ImDrawList)(GetDrawListSharedData());
            }
            g_glass_draw_list->_ResetForNewFrame();
            g_glass_draw_list->PushClipRectFullScreen();
            ImDrawList* draw_list = g_glass_draw_list;

            const ImTextureRef backdrop_ref(reinterpret_cast<ImTextureID>(g_glass_backdrop[0].get()));
            const ImRect display(draw_data->DisplayPos.x, draw_data->DisplayPos.y, draw_data->DisplayPos.x + draw_data->DisplaySize.x, draw_data->DisplayPos.y + draw_data->DisplaySize.y);
            const float scale         = GetStyle()._MainScale > 0.0f ? GetStyle()._MainScale : 1.0f;
            const float panel_opacity = std::clamp(cvar_editor_glass_panel_opacity.GetValue(), 0.0f, 1.0f);
            const float float_opacity = std::clamp(cvar_editor_glass_opacity.GetValue(), 0.0f, 1.0f);

            ImRect frame_rect;
            ImRect frame_uv;
            const bool frame_found = frame_output && frame_output->GetRhiResource() && glass_find_frame(draw_data, frame_output->GetObjectId(), frame_rect, frame_uv);

            auto backdrop_uv = [&](const ImVec2& position)
            {
                return ImVec2((position.x - display.Min.x) / display.GetWidth(), (position.y - display.Min.y) / display.GetHeight());
            };

            for (int i = 0; i < draw_data->CmdListsCount; i++)
            {
                ImGuiWindow* window = list_windows[i];
                if (!window)
                {
                    continue;
                }

                const uint32_t list = static_cast<uint32_t>(i);

                // a docked window keeps its content in its own list, only a menu bar fill may lead it
                if (window->DockIsActive && window->DockNode && glass_lists[i].thin == GlassThin::None && cvar_editor_glass_panels.GetValue())
                {
                    glass_lists[i].thin       = GlassThin::Leading;
                    glass_lists[i].opacity    = panel_opacity;
                    glass_lists[i].background = window->DockNode->LastBgColor;
                }

                // the visible panels this window hosts
                glass_nodes.clear();
                if (cvar_editor_glass_panels.GetValue() && window->DockNodeAsHost)
                {
                    for (ImGuiWindow* docked : GImGui->Windows)
                    {
                        ImGuiDockNode* node = docked->DockNode;
                        if (!docked->DockIsActive || !node || node->HostWindow != window || !node->IsVisible || node->Size.x <= 0.0f || node->Size.y <= 0.0f)
                        {
                            continue;
                        }

                        if (std::find(glass_nodes.begin(), glass_nodes.end(), node) == glass_nodes.end())
                        {
                            glass_nodes.push_back(node);
                        }
                    }
                }

                // a floating dock group is a root window hosting its panels, the editor dockspace is a child window of the editor root
                const bool is_floating_host = (window->Flags & ImGuiWindowFlags_DockNodeHost) && !(window->Flags & ImGuiWindowFlags_ChildWindow);
                const bool is_floating      = is_glass_window(window, draw_data->OwnerViewport) || (is_floating_host && !glass_nodes.empty());
                if (!is_floating && glass_nodes.empty())
                {
                    continue;
                }

                // the editor dockspace: its panes are frosted over the scene, stretched out of the viewport to fill the editor
                const bool is_dock_root = !is_floating;

                ImRect panes_bounds;
                panes_bounds.Min = ImVec2(FLT_MAX, FLT_MAX);
                panes_bounds.Max = ImVec2(-FLT_MAX, -FLT_MAX);
                if (glass_nodes.empty())
                {
                    panes_bounds = window->Rect();
                }
                for (const ImGuiDockNode* node : glass_nodes)
                {
                    panes_bounds.Add(node_rect(node));
                }

                uint32_t index_start = static_cast<uint32_t>(draw_list->IdxBuffer.Size);
                if (is_dock_root && frame_found)
                {
                    // a 3x3 grid around the viewport, each side cell shows the strip of the viewport along that edge, same size and the right way round
                    const ImVec2 uv_per_pixel = ImVec2(frame_uv.GetWidth() / frame_rect.GetWidth(), frame_uv.GetHeight() / frame_rect.GetHeight());
                    const float xs[4]   = { display.Min.x, ImClamp(frame_rect.Min.x, display.Min.x, display.Max.x), ImClamp(frame_rect.Max.x, display.Min.x, display.Max.x), display.Max.x };
                    const float ys[4]   = { display.Min.y, ImClamp(frame_rect.Min.y, display.Min.y, display.Max.y), ImClamp(frame_rect.Max.y, display.Min.y, display.Max.y), display.Max.y };
                    const float us[3][2] =
                    {
                        { frame_uv.Min.x, frame_uv.Min.x + (xs[1] - xs[0]) * uv_per_pixel.x },
                        { frame_uv.Min.x, frame_uv.Max.x },
                        { frame_uv.Max.x - (xs[3] - xs[2]) * uv_per_pixel.x, frame_uv.Max.x }
                    };
                    const float vs[3][2] =
                    {
                        { frame_uv.Min.y, frame_uv.Min.y + (ys[1] - ys[0]) * uv_per_pixel.y },
                        { frame_uv.Min.y, frame_uv.Max.y },
                        { frame_uv.Max.y - (ys[3] - ys[2]) * uv_per_pixel.y, frame_uv.Max.y }
                    };
                    for (int row = 0; row < 3; row++)
                    {
                        for (int column = 0; column < 3; column++)
                        {
                            if (xs[column + 1] - xs[column] < 1.0f || ys[row + 1] - ys[row] < 1.0f)
                            {
                                continue;
                            }

                            draw_list->PrimReserve(6, 4);
                            draw_list->PrimRectUV(ImVec2(xs[column], ys[row]), ImVec2(xs[column + 1], ys[row + 1]), ImVec2(us[column][0], vs[row][0]), ImVec2(us[column][1], vs[row][1]), IM_COL32_WHITE);
                        }
                    }
                    glass_push_segment(glass_segments, list, false, GlassStep::Ambient, index_start, display);
                }

                index_start = static_cast<uint32_t>(draw_list->IdxBuffer.Size);
                glass_push_segment(glass_segments, list, false, GlassStep::Capture, index_start, panes_bounds);

                // the gutters between panels show the scene dimmed down to near black
                index_start = static_cast<uint32_t>(draw_list->IdxBuffer.Size);
                if (is_dock_root)
                {
                    ImVec4 veil = GetStyleColorVec4(ImGuiCol_DockingEmptyBg);
                    veil.w      = 0.86f;
                    draw_list->AddRectFilled(display.Min, display.Max, ColorConvertFloat4ToU32(veil));
                }
                if (is_floating)
                {
                    glass_add_shadow(draw_list, window->Rect(), window->WindowRounding, scale);
                }
                ImRect shadow_bounds = window->Rect();
                shadow_bounds.Expand(32.0f * scale);
                glass_push_segment(glass_segments, list, false, GlassStep::Plain, index_start, is_dock_root ? display : shadow_bounds);

                index_start = static_cast<uint32_t>(draw_list->IdxBuffer.Size);
                if (glass_nodes.empty())
                {
                    draw_list->AddImageRounded(backdrop_ref, window->Rect().Min, window->Rect().Max, backdrop_uv(window->Rect().Min), backdrop_uv(window->Rect().Max), IM_COL32_WHITE, window->WindowRounding, ImDrawFlags_RoundCornersAll);
                }
                for (const ImGuiDockNode* node : glass_nodes)
                {
                    const ImRect rect = node_rect(node);
                    draw_list->AddImage(backdrop_ref, rect.Min, rect.Max, backdrop_uv(rect.Min), backdrop_uv(rect.Max));
                }
                glass_push_segment(glass_segments, list, false, GlassStep::Backdrop, index_start, panes_bounds);

                index_start = static_cast<uint32_t>(draw_list->IdxBuffer.Size);
                if (glass_nodes.empty())
                {
                    glass_add_sheen(draw_list, window->Rect(), window->WindowRounding, 0.07f);
                }
                for (const ImGuiDockNode* node : glass_nodes)
                {
                    glass_add_sheen(draw_list, node_rect(node), 0.0f, 0.045f);
                }
                glass_push_segment(glass_segments, list, false, GlassStep::Plain, index_start, panes_bounds);

                // rims go over the list so the window fills cannot dim them
                index_start = static_cast<uint32_t>(draw_list->IdxBuffer.Size);
                if (glass_nodes.empty())
                {
                    glass_add_rim(draw_list, window->Rect(), window->WindowRounding, scale, 0.26f, 0.06f, true);
                }
                for (const ImGuiDockNode* node : glass_nodes)
                {
                    glass_add_rim(draw_list, node_rect(node), 0.0f, scale, 0.11f, 0.025f, false);
                }
                glass_push_segment(glass_segments, list, true, GlassStep::Plain, index_start, panes_bounds);

                GlassList& glass_list = glass_lists[i];
                if (glass_nodes.empty())
                {
                    glass_list.thin       = GlassThin::Leading;
                    glass_list.opacity    = float_opacity;
                    glass_list.background = 0;
                }
                else
                {
                    glass_list.thin       = GlassThin::Host;
                    glass_list.opacity    = panel_opacity;
                    glass_list.fill_begin = static_cast<uint32_t>(glass_fills.size());
                    for (const ImGuiDockNode* node : glass_nodes)
                    {
                        glass_fills.push_back(node->LastBgColor);
                    }
                    glass_fills.push_back(GetColorU32(ImGuiCol_TabSelected));
                    glass_fills.push_back(GetColorU32(ImGuiCol_TabDimmedSelected));
                    glass_list.fill_end = static_cast<uint32_t>(glass_fills.size());
                }
            }

            if (glass_segments.empty())
            {
                glass_target = nullptr;
            }
        }
        const uint32_t glass_vertex_count = glass_target ? static_cast<uint32_t>(g_glass_draw_list->VtxBuffer.Size) : 0;
        const uint32_t glass_index_count  = glass_target ? static_cast<uint32_t>(g_glass_draw_list->IdxBuffer.Size) : 0;
        const uint32_t vertex_count       = static_cast<uint32_t>(draw_data->TotalVtxCount) + glass_vertex_count;
        const uint32_t index_count        = static_cast<uint32_t>(draw_data->TotalIdxCount) + glass_index_count;

        // update vertex and index buffers
        {
            // grow vertex buffer as needed
            if (vertex_buffer->GetElementCount() < vertex_count)
            {
                const uint32_t count                        = vertex_buffer->GetElementCount();
                const uint32_t count_new                    = vertex_count + 15000;
                rhi_resources->vertex_buffers[buffer_index] = make_unique<RHI_Buffer>(RHI_Buffer_Type::Vertex, sizeof(ImDrawVert), count_new, nullptr, true, vertex_buffer->GetObjectName().c_str());
                vertex_buffer                               = rhi_resources->vertex_buffers[buffer_index].get();

                if (count != 0)
                {
                    SP_LOG_INFO("Vertex buffer has been re-allocated to fit %d vertices", count_new);
                }
            }

            // grow index buffer as needed
            if (index_buffer->GetElementCount() < index_count)
            {
                const uint32_t count                       = index_buffer->GetElementCount();
                const uint32_t count_new                   = index_count + 30000;
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

                    if (glass_target)
                    {
                        const GlassList& glass_list = glass_lists[i];
                        if (glass_list.thin == GlassThin::Leading)
                        {
                            thin_window_background(vtx_dst, imgui_cmd_list->VtxBuffer.Size, glass_list.opacity, glass_list.background);
                        }
                        else if (glass_list.thin == GlassThin::Host)
                        {
                            thin_dock_host(vtx_dst, imgui_cmd_list->VtxBuffer.Size, glass_list.opacity, glass_fills.data() + glass_list.fill_begin, glass_list.fill_end - glass_list.fill_begin);
                        }
                    }

                    vtx_dst += imgui_cmd_list->VtxBuffer.Size;
                    idx_dst += imgui_cmd_list->IdxBuffer.Size;
                }

                // the glass quads go after every imgui list
                if (glass_target)
                {
                    memcpy(vtx_dst, g_glass_draw_list->VtxBuffer.Data, glass_vertex_count * sizeof(ImDrawVert));
                    memcpy(idx_dst, g_glass_draw_list->IdxBuffer.Data, glass_index_count * sizeof(ImDrawIdx));
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
        pso.render_target_color_textures.fill(nullptr);
        pso.clear_color[0]                   = clear ? Color::standard_black : rhi_color_dont_care;
        pso.use_standard_resources           = false;

        auto setup_render_state = [&]()
        {
            if (is_main_window)
            {
                RHI_Device::Bind(RHI_Frame_List::Graphics);
            }
            RHI_CommandList::SetPipelineState(pso);
            RHI_CommandList::SetConstantBuffer(Renderer_BindingsCb::frame, Renderer::GetBuffer(Renderer_Buffer::ConstantFrame));
            RHI_CommandList::SetBufferVertex(vertex_buffer);
            RHI_CommandList::SetBufferIndex(index_buffer);
            RHI_CommandList::SetCullMode(RHI_CullMode::None);
        };

        // where the 3d view lands in the backbuffer, frame generation interpolates only that area
        const uint64_t frame_output_id = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output)->GetObjectId();
        bool frame_rect_found          = false;
        ImVec2 frame_rect_min          = ImVec2(FLT_MAX, FLT_MAX);
        ImVec2 frame_rect_max          = ImVec2(-FLT_MAX, -FLT_MAX);

        // a replay into the ui screenshot target must not run user callbacks, they record external passes
        // frame_only replays just the 3d view draws, the frame generation hudless image
        // glass is the texture the pass draws into, null when glass windows are drawn without a backdrop
        auto draw_pass = [&](const char* name, const bool gpu_timing, const bool run_callbacks, const bool sdr_capture, const bool frame_only, RHI_Texture* glass)
        {
            RHI_CommandList::BeginPass(name, gpu_timing);
            setup_render_state();

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
            const uint32_t draw_index = Renderer::WriteDrawData(projection, Matrix::Identity, 0, 0, nullptr, true);
            rhi_resources->push_constant_buffer_pass.draw_index = draw_index;
            RHI_CommandList::PushConstants(0, sizeof(Pcb_Pass), &rhi_resources->push_constant_buffer_pass);

            uint32_t global_vtx_offset = 0;
            uint32_t global_idx_offset = 0;
            RHI_Texture* texture_last  = nullptr;
            uint32_t flags_last        = 0;
            float mip_level_last       = 0.0f;
            float array_level_last     = 0.0f;
            bool state_valid           = false;

            // what has been drawn since the backdrop was last blurred, panes that do not overlap it can share that blur
            bool glass_captured = false;
            ImRect glass_dirty(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
            const float glass_reach = std::clamp(cvar_editor_glass_blur.GetValue(), 1.0f, 32.0f) * 8.0f;
            size_t glass_cursor     = 0;

            auto draw_glass = [&](const GlassSegment& segment)
            {
                if (segment.step == GlassStep::Capture)
                {
                    ImRect reach = segment.bounds;
                    reach.Expand(glass_reach);
                    const bool is_dirty = glass_dirty.Min.x <= glass_dirty.Max.x && glass_dirty.Overlaps(reach);
                    if (glass_captured && !is_dirty)
                    {
                        return;
                    }

                    RHI_CommandList::EndPass();
                    glass_capture_backdrop(glass);
                    RHI_CommandList::BeginPass(name, gpu_timing);
                    pso.clear_color[0] = rhi_color_load;
                    RHI_CommandList::RestoreAfterExternalPass();
                    setup_render_state();
                    glass_captured = true;
                    glass_dirty    = ImRect(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
                    state_valid    = false;
                    return;
                }

                // the backdrop and the frame already hold display encoded values, the screenshot replay wants sdr
                RHI_Texture* texture = reinterpret_cast<RHI_Texture*>(GetIO().Fonts->TexRef.GetTexID());
                uint32_t flags       = sdr_capture ? (1u << 11) : 0u;
                if (segment.step == GlassStep::Backdrop)
                {
                    texture = g_glass_backdrop[0].get();
                    flags   = glass_flag | (sdr_capture ? (1u << 11) : (1u << 10));
                }
                else if (segment.step == GlassStep::Ambient)
                {
                    texture = frame_output;
                    flags   = (1u << 10) | (sdr_capture ? (1u << 11) : 0u);
                }
                if (!texture)
                {
                    texture = g_font_atlas.get();
                }

                math::Rectangle rectangle;
                rectangle.width  = draw_data->DisplaySize.x;
                rectangle.height = draw_data->DisplaySize.y;
                RHI_CommandList::SetScissorRectangle(rectangle);
                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, texture);
                rhi_resources->push_constant_buffer_pass.set(pass_imgui::flags, flags);
                rhi_resources->push_constant_buffer_pass.set(pass_imgui::mip_level, 0.0f);
                rhi_resources->push_constant_buffer_pass.set(pass_imgui::array_level, 0.0f);
                RHI_CommandList::PushConstants(0, sizeof(Pcb_Pass), &rhi_resources->push_constant_buffer_pass);
                RHI_CommandList::DrawIndexed(segment.index_count, draw_data->TotalIdxCount + segment.index_offset, draw_data->TotalVtxCount);
                glass_dirty.Add(segment.bounds);
                state_valid = false;
            };

            for (uint32_t i = 0; i < static_cast<uint32_t>(draw_data->CmdListsCount); i++)
            {
                ImDrawList* cmd_list_imgui = draw_data->CmdLists[i];

                // everything drawn so far is what the pane floats over, blur it and lay it down before the pane draws
                const bool draw_glass_steps = glass && draw_index != numeric_limits<uint32_t>::max();
                while (glass_cursor < glass_segments.size() && glass_segments[glass_cursor].list == i && !glass_segments[glass_cursor].after_list)
                {
                    if (draw_glass_steps)
                    {
                        draw_glass(glass_segments[glass_cursor]);
                    }
                    glass_cursor++;
                }

                for (uint32_t cmd_i = 0; cmd_i < static_cast<uint32_t>(cmd_list_imgui->CmdBuffer.Size); cmd_i++)
                {
                    const ImDrawCmd* pcmd = &cmd_list_imgui->CmdBuffer[cmd_i];
                    if (glass && pcmd->UserCallback == nullptr)
                    {
                        glass_dirty.Add(ImRect(pcmd->ClipRect.x, pcmd->ClipRect.y, pcmd->ClipRect.z, pcmd->ClipRect.w));
                    }

                    if (pcmd->UserCallback != nullptr)
                    {
                        if (!run_callbacks)
                        {
                            continue;
                        }

                        if (pcmd->UserCallback != GetPlatformIO().DrawCallback_ResetRenderState)
                        {
                            pcmd->UserCallback(cmd_list_imgui, pcmd);
                        }
                        pso.clear_color[0] = rhi_color_load;
                        RHI_CommandList::RestoreAfterExternalPass();
                        setup_render_state();
                        state_valid = false;
                    }
                    else
                    {
                        if (draw_index == numeric_limits<uint32_t>::max())
                        {
                            continue;
                        }

                        spartan::RHI_Texture* texture_cmd = reinterpret_cast<spartan::RHI_Texture*>(pcmd->GetTexID());
                        const bool is_frame_cmd           = texture_cmd && texture_cmd->GetRhiResource() && texture_cmd->GetObjectId() == frame_output_id;
                        if (frame_only && (!is_frame_cmd || !RHI_FrameGeneration::GetScene()))
                        {
                            continue;
                        }

                        if (is_frame_cmd && is_main_window && !frame_only && !sdr_capture)
                        {
                            for (uint32_t k = 0; k < pcmd->ElemCount; k++)
                            {
                                const ImDrawVert& vertex = cmd_list_imgui->VtxBuffer[cmd_list_imgui->IdxBuffer[pcmd->IdxOffset + k] + pcmd->VtxOffset];
                                frame_rect_min.x         = min(frame_rect_min.x, max(vertex.pos.x, pcmd->ClipRect.x));
                                frame_rect_min.y         = min(frame_rect_min.y, max(vertex.pos.y, pcmd->ClipRect.y));
                                frame_rect_max.x         = max(frame_rect_max.x, min(vertex.pos.x, pcmd->ClipRect.z));
                                frame_rect_max.y         = max(frame_rect_max.y, min(vertex.pos.y, pcmd->ClipRect.w));
                            }
                            frame_rect_found = true;
                        }

                        // set scissor rectangle
                        {
                            math::Rectangle rectangle;
                            rectangle.x      = pcmd->ClipRect.x - draw_data->DisplayPos.x;
                            rectangle.y      = pcmd->ClipRect.y - draw_data->DisplayPos.y;
                            rectangle.width  = (pcmd->ClipRect.z - draw_data->DisplayPos.x) - rectangle.x;
                            rectangle.height = (pcmd->ClipRect.w - draw_data->DisplayPos.y) - rectangle.y;

                            RHI_CommandList::SetScissorRectangle(rectangle);
                        }

                        // set texture and update texture viewer parameters
                        {
                            float mip_level            = 0.0f;
                            float array_level          = 0.0f;
                            bool is_texture_visualised = false;
                            bool is_frame_texture      = false;
                            RHI_Texture* texture_bound = nullptr;

                            if (spartan::RHI_Texture* texture = reinterpret_cast<spartan::RHI_Texture*>(pcmd->GetTexID()))
                            {
                                if (texture->GetRhiResource())
                                {
                                    is_frame_texture = Renderer::GetRenderTarget(Renderer_RenderTarget::frame_output)->GetObjectId() == texture->GetObjectId();

                                    // during engine startup, some textures might be loading in different threads
                                    if (texture->GetResourceState() == ResourceState::PreparedForGpu)
                                    {
                                        // update texture viewer parameters
                                        is_texture_visualised = spartan::gui::GetTexturePreview().id == texture->GetObjectId();
                                        if (is_texture_visualised)
                                        {
                                            mip_level   = static_cast<float>(spartan::gui::GetTexturePreview().mip);
                                            array_level = static_cast<float>(spartan::gui::GetTexturePreview().layer);
                                        }

                                        // keep the same viewport transform and display encoding, but exclude engine text and overlays
                                        texture_bound = frame_only ? RHI_FrameGeneration::GetScene() : texture;
                                    }
                                }
                            }

                            // always bind a texture to avoid uninitialized descriptor errors
                            if (!texture_bound)
                            {
                                texture_bound = g_font_atlas.get();
                            }
                            if (!state_valid || texture_bound != texture_last)
                            {
                                RHI_CommandList::SetTexture(Renderer_BindingsSrv::tex, texture_bound);
                                texture_last = texture_bound;
                            }

                            // pack booleans into uint bitfield
                            uint32_t flags = 0;
                            if (is_texture_visualised)
                            {
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Channel_R)    ? (1u << 0) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Channel_G)    ? (1u << 1) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Channel_B)    ? (1u << 2) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Channel_A)    ? (1u << 3) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_GammaCorrect) ? (1u << 4) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Pack)         ? (1u << 5) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Boost)        ? (1u << 6) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Abs)          ? (1u << 7) : 0;
                                flags |= (spartan::gui::GetTexturePreview().flags & Visualise_Sample_Point) ? (1u << 8) : 0;
                            }
                            flags |= is_texture_visualised ? (1u << 9) : 0;
                            flags |= is_frame_texture      ? (1u << 10) : 0;
                            flags |= sdr_capture           ? (1u << 11) : 0;

                            if (!state_valid || flags != flags_last || mip_level != mip_level_last || array_level != array_level_last)
                            {
                                rhi_resources->push_constant_buffer_pass.set(pass_imgui::flags, flags);
                                rhi_resources->push_constant_buffer_pass.set(pass_imgui::mip_level, mip_level);
                                rhi_resources->push_constant_buffer_pass.set(pass_imgui::array_level, array_level);
                                RHI_CommandList::PushConstants(0, sizeof(Pcb_Pass), &rhi_resources->push_constant_buffer_pass);
                                flags_last       = flags;
                                mip_level_last   = mip_level;
                                array_level_last = array_level;
                            }
                            state_valid = true;
                        }

                        RHI_CommandList::DrawIndexed(pcmd->ElemCount, pcmd->IdxOffset + global_idx_offset, pcmd->VtxOffset + global_vtx_offset);
                    }
                }

                while (glass_cursor < glass_segments.size() && glass_segments[glass_cursor].list == i)
                {
                    if (draw_glass_steps)
                    {
                        draw_glass(glass_segments[glass_cursor]);
                    }
                    glass_cursor++;
                }

                global_idx_offset += static_cast<uint32_t>(cmd_list_imgui->IdxBuffer.Size);
                global_vtx_offset += static_cast<uint32_t>(cmd_list_imgui->VtxBuffer.Size);
            }

            RHI_CommandList::EndPass();
        };

        // with a glass window up the ui goes through a texture so the backdrop can be read back, then lands in the swapchain
        if (glass_target)
        {
            RHI_CommandList::ClearTexture(glass_target, Color::standard_black);
            pso.render_target_swapchain         = nullptr;
            pso.render_target_color_textures[0] = glass_target;
            pso.clear_color[0]                  = rhi_color_load;
        }

        draw_pass(is_main_window ? "imgui_window_main" : "imgui_window_child", is_main_window, true, false, false, glass_target);

        if (glass_target)
        {
            RHI_CommandList::Blit(glass_target, swapchain);
            pso.render_target_color_textures[0] = nullptr;
            pso.render_target_swapchain         = swapchain;
        }

        if (is_main_window)
        {
            math::Rectangle frame_rect;
            if (frame_rect_found && frame_rect_max.x > frame_rect_min.x && frame_rect_max.y > frame_rect_min.y)
            {
                frame_rect.x      = (frame_rect_min.x - draw_data->DisplayPos.x) * draw_data->FramebufferScale.x;
                frame_rect.y      = (frame_rect_min.y - draw_data->DisplayPos.y) * draw_data->FramebufferScale.y;
                frame_rect.width  = (frame_rect_max.x - frame_rect_min.x) * draw_data->FramebufferScale.x;
                frame_rect.height = (frame_rect_max.y - frame_rect_min.y) * draw_data->FramebufferScale.y;
            }
            RHI_FrameGeneration::SetGenerationRect(frame_rect);

            // the 3d view over black, ffx diffs it against the backbuffer to find the ui pixels
            if (RHI_Texture* hudless = frame_rect_found && RHI_FrameGeneration::GetScene() ? RHI_FrameGeneration::GetHudlessTarget(swapchain->GetWidth(), swapchain->GetHeight(), swapchain->GetFormat()) : nullptr)
            {
                RHI_CommandList::ClearTexture(hudless, Color::standard_black);
                pso.render_target_swapchain         = nullptr;
                pso.render_target_color_textures[0] = hudless;
                pso.clear_color[0]                  = rhi_color_load;
                draw_pass("imgui_frame_generation_hudless", false, false, false, true, nullptr);
                pso.render_target_color_textures[0] = nullptr;
                pso.render_target_swapchain         = swapchain;
                RHI_FrameGeneration::SetHudlessRecorded();
            }

            if (RHI_Texture* capture = Renderer::GetUiScreenshotTarget(swapchain->GetWidth(), swapchain->GetHeight()))
            {
                // cleared once up front, a render pass restart (texture transitions) must load, not clear again
                RHI_CommandList::ClearTexture(capture, Color::standard_black);
                pso.render_target_swapchain         = nullptr;
                pso.render_target_color_textures[0] = capture;
                pso.clear_color[0]                  = rhi_color_load;
                draw_pass("imgui_screenshot", false, false, true, false, glass_target ? capture : nullptr);
                pso.render_target_color_textures[0] = nullptr;
                Renderer::SetUiScreenshotRecorded();
            }
        }

        // for child windows, submit and prepare for presentation
        if (!is_main_window)
        {
            if (swapchain->IsImageAcquired())
            {
                RHI_CommandList::PrepareForPresent(swapchain);
                cmd_list->Submit(swapchain->GetImageAcquiredSemaphore(), false, swapchain->GetRenderingCompleteSemaphore());
            }
            else
            {
                cmd_list->Submit(nullptr, true);
            }
            RHI_Device::Bind(static_cast<RHI_CommandList*>(nullptr));
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
