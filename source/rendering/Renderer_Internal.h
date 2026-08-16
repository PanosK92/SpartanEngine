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

//= INCLUDES ===================================
#include "Renderer.h"
#include "Renderer_Buffers.h"
#include "../rhi/RHI_CommandList.h"
#include "../rhi/RHI_Texture.h"
#include "../rhi/RHI_Vertex.h"
#include "../rhi/RHI_Viewport.h"
#include "../rhi/RHI_Shader.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../font/Font.h"
#include "../math/Quaternion.h"
#include "../math/Rectangle.h"
#include <initializer_list>
#include <type_traits>
#include <tuple>
//==============================================

namespace spartan
{
    struct ShadowSlice
    {
        Light* light;
        uint32_t slice_index;
        uint32_t resolution;
        math::Rectangle rect;
    };

    struct PersistentLine
    {
        math::Vector3 from;
        math::Vector3 to;
        Color color_from;
        Color color_to;
        double expire_time;
    };

    namespace Renderer
    {
        template<typename F = std::nullptr_t>
        void Pass_Compute(const char* name, Renderer_Shader shader_enum, RHI_Texture* tex_in, RHI_Texture* tex_out, F setup = nullptr);
        template<typename F = std::nullptr_t>
        void Pass_Graphics(
            const char* name,
            Renderer_Shader shader_vs_or_mesh,
            Renderer_Shader shader_pixel,
            std::initializer_list<RHI_Texture*> color_targets,
            RHI_Texture* depth = nullptr,
            RHI_BlendState* blend = nullptr,
            RHI_DepthStencilState* depth_stencil = nullptr,
            RHI_RasterizerState* rasterizer = nullptr,
            F setup = nullptr
        );

        struct FrameResource
        {
            std::shared_ptr<RHI_Buffer> indirect_draw_args;
            std::shared_ptr<RHI_Buffer> cpu_indirect_draw_args;
            std::shared_ptr<RHI_Buffer> indirect_draw_data;
            std::shared_ptr<RHI_Buffer> meshlet_instances;
            std::shared_ptr<RHI_Buffer> visible_triangles;
            std::shared_ptr<RHI_Buffer> triangle_dispatch_args;
            std::shared_ptr<RHI_Buffer> cull_tasks;
            std::shared_ptr<RHI_Buffer> surviving_instances;
            std::shared_ptr<RHI_Buffer> instance_dispatch_args;
            RHI_SyncPrimitive* completion_timeline = nullptr;
            uint64_t completion_value = 0;
        };

        struct PassState
        {
            bool brdf_lut_produced       = false;
            uint64_t brdf_lut_shader_hash = 0;
            bool atmosphere_lut_produced = false;
            bool cloud_noise_produced    = false;

            bool cleared_reflections     = false;
            bool cleared_rt_reflections  = false;
            bool cleared_rt_shadows      = false;
            bool cleared_restir          = false;
            bool restir_reservoirs_initialized = false;

            bool     sky_first_frame           = true;
            bool     sky_had_directional_light = false;
            uint32_t sky_frames_remaining      = 0;
            bool     sky_warmup_this_frame     = false;
            bool     sky_state_changed_this_frame = true;
            float    sky_warmup_blend          = 1.0f;

            TemporalPingPong cloud_history;
            TemporalPingPong ssao_history;
            TemporalPingPong fog_history;
            bool     cloud_environment_dirty   = true;
            uint32_t cloud_environment_strip   = 0;
            bool     cloud_environment_baking  = false;
            Light*   cloud_light               = nullptr;
            math::Quaternion cloud_light_rotation = math::Quaternion::Identity;
            math::Vector3 cloud_wind            = math::Vector3::Zero;
            float    cloud_light_intensity      = -1.0f;
            float    cloud_coverage             = -1.0f;
            double   cloud_time                 = 0.0;

            Camera*      exposure_camera          = nullptr;
            RHI_Texture* exposure_history_texture = nullptr;
            bool         exposure_history_reset   = false;
            bool         exposure_was_automatic   = false;

            RHI_Texture* vrs_last_cleared_texture = nullptr;

            bool                  bindless_materials_dirty = true;

            bool                  grass_enabled    = false;
            Mesh*                 grass_mesh       = nullptr;
            Material*             grass_material   = nullptr;
            RHI_Texture*          grass_heightmap  = nullptr;
            RHI_Texture*          grass_prop_mask  = nullptr;
            ProceduralGrassParams grass_params;
            std::array<Sb_IndirectDrawArgs, renderer_max_grass_lod_count> grass_indirect_args_static{};
            bool                  grass_args_baked = false;

            bool          terrain_enabled = false;
            TerrainParams terrain;

            Water*        ocean                              = nullptr;
            bool          ocean_spectrum_dirty               = true;
            bool          ocean_displacement_produced        = false;
            TemporalPingPong ocean_history;
            math::Vector3 ocean_wind                         = math::Vector3::Zero;

            void Reset()
            {
                *this = PassState();
            }
        };

        struct CrossQueueSync
        {
            RHI_SyncPrimitive* pending_compute_timeline       = nullptr;
            uint64_t           pending_compute_timeline_value = 0;
        };

        struct State
        {
            State()
            {
                m_cull_tasks.resize(renderer_max_cull_tasks);
            }

            std::array<Renderer_DrawCall, renderer_max_draw_calls> m_draw_calls;
            uint32_t m_draw_call_count;
            std::array<Renderer_DrawCall, renderer_max_draw_calls> m_draw_calls_prepass;
            uint32_t m_draw_calls_prepass_count;
            std::array<Sb_DrawData, renderer_max_indirect_draws> m_indirect_draw_data;
            std::array<Render*, renderer_max_indirect_draws>     m_indirect_renders;
            uint32_t m_indirect_draw_count;
            uint32_t m_indirect_render_count;
            std::vector<Sb_CullTask> m_cull_tasks;
            uint32_t m_cull_task_count;
            std::array<FrameResource, renderer_draw_data_buffer_count> m_frame_resources;
            uint32_t m_frame_resource_index;
            uint32_t m_cpu_indirect_draw_arg_count;
            std::array<Sb_DrawData, renderer_max_draw_calls> m_draw_data_cpu;
            uint32_t m_draw_data_count;
            bool m_draw_data_gpu_synced;
            std::array<RHI_Texture*, rhi_max_array_size> m_bindless_textures;
            std::array<Sb_Light, rhi_max_array_size> m_bindless_lights;
            std::array<Sb_Aabb, rhi_max_array_size> m_bindless_aabbs;
            bool m_bindless_samplers_dirty = true;
            PassState m_pass_state;
            Cb_Frame m_cb_frame_cpu;
            Pcb_Pass m_pcb_pass_cpu;
            math::Matrix m_view_projection_previous_right;
            math::Matrix m_view_projection_previous_unjittered_left;
            std::shared_ptr<RHI_Buffer> m_lines_vertex_buffer;
            std::vector<RHI_Vertex_PosCol> m_lines_vertices;
            std::vector<PersistentLine> m_persistent_lines;
            std::shared_ptr<RHI_Buffer> m_icons_vertex_buffer;
            std::vector<RHI_Vertex_PosTex> m_icons_vertices;
            std::vector<std::tuple<RHI_Texture*, math::Vector3>> m_icons;
            uint32_t m_frame_cb_ring_slot;
            std::atomic<bool> m_initialized_resources;
            bool m_transparents_present;
            bool m_is_hiz_suppressed;
            bool m_taau_reset_history = true;
            CrossQueueSync m_cross_queue_sync;
            std::vector<ShadowSlice> m_shadow_slices;
            uint32_t m_count_active_lights;
            uint32_t m_volumetric_light_count;
            std::unique_ptr<RHI_AccelerationStructure> m_tlas;
            math::Vector2                 m_resolution_render;
            math::Vector2                 m_resolution_output;
            RHI_Viewport                  m_viewport;
            bool m_present_in_renderer = true;
            uint64_t                      m_frame_num;
            math::Vector2                 m_jitter_offset;
            ~State();
        };
        State& state();
        inline auto& m_draw_calls = state().m_draw_calls;
        inline auto& m_draw_call_count = state().m_draw_call_count;
        inline auto& m_draw_calls_prepass = state().m_draw_calls_prepass;
        inline auto& m_draw_calls_prepass_count = state().m_draw_calls_prepass_count;
        inline auto& m_indirect_draw_data = state().m_indirect_draw_data;
        inline auto& m_indirect_renders = state().m_indirect_renders;
        inline auto& m_indirect_draw_count = state().m_indirect_draw_count;
        inline auto& m_indirect_render_count = state().m_indirect_render_count;
        inline auto& m_cull_tasks = state().m_cull_tasks;
        inline auto& m_cull_task_count = state().m_cull_task_count;
        inline auto& m_frame_resources = state().m_frame_resources;
        inline auto& m_frame_resource_index = state().m_frame_resource_index;
        inline auto& m_cpu_indirect_draw_arg_count = state().m_cpu_indirect_draw_arg_count;
        inline auto& m_draw_data_cpu = state().m_draw_data_cpu;
        inline auto& m_draw_data_count = state().m_draw_data_count;
        inline auto& m_draw_data_gpu_synced = state().m_draw_data_gpu_synced;
        inline auto& m_bindless_textures = state().m_bindless_textures;
        inline auto& m_bindless_lights = state().m_bindless_lights;
        inline auto& m_bindless_aabbs = state().m_bindless_aabbs;
        inline auto& m_bindless_samplers_dirty = state().m_bindless_samplers_dirty;
        inline auto& m_pass_state = state().m_pass_state;
        inline auto& m_cb_frame_cpu = state().m_cb_frame_cpu;
        inline auto& m_pcb_pass_cpu = state().m_pcb_pass_cpu;
        inline auto& m_view_projection_previous_right = state().m_view_projection_previous_right;
        inline auto& m_view_projection_previous_unjittered_left = state().m_view_projection_previous_unjittered_left;
        inline auto& m_lines_vertex_buffer = state().m_lines_vertex_buffer;
        inline auto& m_lines_vertices = state().m_lines_vertices;
        inline auto& m_persistent_lines = state().m_persistent_lines;
        inline auto& m_icons_vertex_buffer = state().m_icons_vertex_buffer;
        inline auto& m_icons_vertices = state().m_icons_vertices;
        inline auto& m_icons = state().m_icons;
        inline auto& m_frame_cb_ring_slot = state().m_frame_cb_ring_slot;
        inline auto& m_initialized_resources = state().m_initialized_resources;
        inline auto& m_transparents_present = state().m_transparents_present;
        inline auto& m_is_hiz_suppressed = state().m_is_hiz_suppressed;
        inline auto& m_taau_reset_history = state().m_taau_reset_history;
        inline auto& m_cross_queue_sync = state().m_cross_queue_sync;
        inline auto& m_shadow_slices = state().m_shadow_slices;
        inline auto& m_count_active_lights = state().m_count_active_lights;
        inline auto& m_volumetric_light_count = state().m_volumetric_light_count;
        inline auto& m_tlas = state().m_tlas;
        inline auto& m_resolution_render = state().m_resolution_render;
        inline auto& m_resolution_output = state().m_resolution_output;
        inline auto& m_viewport = state().m_viewport;
        inline auto& m_present_in_renderer = state().m_present_in_renderer;
        inline auto& m_frame_num = state().m_frame_num;
        inline auto& m_jitter_offset = state().m_jitter_offset;
    }

    template<typename F>
    void Renderer::Pass_Graphics(
        const char* name,
        Renderer_Shader shader_vs_or_mesh,
        Renderer_Shader shader_pixel,
        std::initializer_list<RHI_Texture*> color_targets,
        RHI_Texture* depth,
        RHI_BlendState* blend,
        RHI_DepthStencilState* depth_stencil,
        RHI_RasterizerState* rasterizer,
        F setup
    )
    {
        RHI_CommandList::BeginPass(name);
        {
            RHI_Shader* shader_a = GetShader(shader_vs_or_mesh);
            RHI_Shader* shader_p = GetShader(shader_pixel);
            RHI_CommandList::SetShaders(shader_a, shader_p);
            RHI_CommandList::SetBlendState(blend);
            RHI_CommandList::SetDepthStencilState(depth_stencil);
            RHI_CommandList::SetRasterizerState(rasterizer);
            RHI_CommandList::SetDepthTarget(depth);

            uint32_t color_index = 0;
            RHI_Texture* colors[rhi_max_render_target_count] = {};
            for (RHI_Texture* color : color_targets)
            {
                if (color_index >= rhi_max_render_target_count)
                {
                    break;
                }
                colors[color_index] = color;
                color_index++;
            }
            RHI_CommandList::SetColorTargets(
                colors[0], colors[1], colors[2], colors[3],
                colors[4], colors[5], colors[6], colors[7]
            );

            if constexpr (!std::is_null_pointer_v<F>)
            {
                setup();
            }
        }
        RHI_CommandList::EndPass();
    }
}
