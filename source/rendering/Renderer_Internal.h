/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
#include "../world/TerrainDeformation.h"
#include "../world/OceanWake.h"
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
            std::shared_ptr<RHI_Buffer> lines_vertex_buffer;
            uint32_t lines_vertex_count = 0;
            bool lines_uploaded = false;
            std::shared_ptr<RHI_Buffer> debug_triangles_vertex_buffer;
            uint32_t debug_triangles_vertex_count = 0;
            bool debug_triangles_uploaded = false;
            std::shared_ptr<RHI_Buffer> indirect_draw_args;
            std::shared_ptr<RHI_Buffer> cpu_indirect_draw_args;
            std::shared_ptr<RHI_Buffer> indirect_draw_data;
            std::shared_ptr<RHI_Buffer> meshlet_instances;
            std::shared_ptr<RHI_Buffer> visible_triangles;
            std::shared_ptr<RHI_Buffer> triangle_dispatch_args;
            std::shared_ptr<RHI_Buffer> cull_tasks;
            std::shared_ptr<RHI_Buffer> surviving_instances;
            std::shared_ptr<RHI_Buffer> tree_wind_cache;
            std::shared_ptr<RHI_Buffer> instance_dispatch_args;
            RHI_SyncPrimitive* completion_timeline = nullptr;
            uint64_t completion_value = 0;
        };

        struct RestirState
        {
            bool cleared_restir          = false;
            bool reservoirs_initialized = false;
            bool accumulation_valid = false;
            uint32_t reference_mode = 0;
            bool history_invalid = true;
            // frames left with a short temporal confidence cap, set by lighting changes that keep the reservoirs
            uint32_t history_fade_frames = 0;
            uint64_t scene_signature = 0;
            uint64_t motion_signature = 0;
            // the gbuffer copy set written this frame, the other one holds last frame's opaque gbuffer
            uint32_t gbuffer_copy_index = 0;
            // consecutive frames that wrote a gbuffer copy, async needs last frame's copy as history
            uint32_t gbuffer_copy_frames = 0;
            // the gi ran on its own compute list this frame, image based lighting reads last frame's gi
            bool async_this_frame = false;

            void Reset() { *this = RestirState(); }
        };

        struct SkyState
        {
            bool     first_frame           = true;
            bool     had_directional_light = false;
            uint32_t frames_remaining      = 0;
            bool     warmup_this_frame     = false;
            bool     state_changed_this_frame = true;
            float    warmup_blend          = 1.0f;

            void Reset() { *this = SkyState(); }
        };

        struct CloudEnvironmentState
        {
            bool     environment_dirty   = true;
            uint32_t environment_strip   = 0;
            bool     environment_baking  = false;
            Light*   light               = nullptr;
            math::Quaternion light_rotation = math::Quaternion::Identity;
            math::Vector3 sky_moon = math::Vector3::Zero;
            math::Vector3 sky_equatorial_x = math::Vector3::Zero;
            math::Vector3 wind            = math::Vector3::Zero;
            math::Vector2 seed_offset     = math::Vector2::Zero;
            float    light_intensity      = -1.0f;
            float    coverage             = -1.0f;
            double   time                 = 0.0;

            void Reset() { *this = CloudEnvironmentState(); }
        };

        struct ExposureState
        {
            Camera*      camera          = nullptr;
            RHI_Texture* history_texture = nullptr;
            bool         history_reset   = false;
            bool         was_automatic   = false;

            void Reset() { *this = ExposureState(); }
        };

        struct SurfaceWaterState
        {
            uint64_t entity_id = 0;
            uint32_t version = 0;
            bool uploaded = false;
            uint32_t dirt_version = 0;
            uint32_t dirt_width = 0;
            uint32_t dirt_height = 0;
            bool dirt_uploaded = false;
            uint32_t dirt_texels_staged = 0; // patches written to this frame's buffer slice, the dispatch must not read past them
        };

        struct PassState
        {
            bool brdf_lut_produced       = false;
            uint64_t brdf_lut_shader_hash = 0;
            struct PathTracingState
            {
                std::shared_ptr<RHI_Texture> accumulation[2];
                math::Matrix camera;
                uint64_t scene_revision = 0;
                uint64_t accumulated_revision = 0;
                uint64_t shader_hash = 0;
                uint32_t samples = 0;
                uint32_t bounces = 0;
                uint32_t current = 0;
            } path_tracing;

            bool atmosphere_lut_produced = false;
            bool cloud_noise_produced    = false;

            bool cleared_reflections     = false;
            bool cleared_rt_reflections  = false;
            bool cleared_rt_shadows      = false;
            bool skip_rt_trace           = false;
            bool depth_history_cleared = false;
            // gbuffer_depth_hiz holds a complete depth pyramid from the previous frame the early cull can test against
            bool hiz_history_valid = false;
            // the late cull ran this frame, so the late tessellated args and the late survivor ranges are live
            bool cull_late_active = false;
            RestirState restir;

            SkyState sky;

            TemporalPingPong cloud_history;
            TemporalPingPong ssao_history;
            TemporalPingPong fog_history;
            Renderer_RenderTarget fog_source = Renderer_RenderTarget::fog_scatter;
            Renderer_RenderTarget fog_water_source = Renderer_RenderTarget::fog_water_source;
            TemporalPingPong particle_volume_history;
            CloudEnvironmentState cloud_environment;

            SurfaceWaterState surface_water;

            RHI_Texture* vrs_last_cleared_texture = nullptr;

            bool                  bindless_materials_dirty = true;

            // one gpu scatter slot, slot 0 is grass and the rest are micro detail, they all share the
            // instance pool and the populate pass, only the mesh, the material and the rules differ
            struct GpuScatterSlot
            {
                bool             enabled    = false;
                Mesh*            mesh       = nullptr;
                Material*        material   = nullptr;
                RHI_Texture*     heightmap  = nullptr;
                RHI_Texture*     prop_mask  = nullptr;
                GpuScatterParams params;
                std::array<Sb_IndirectDrawArgs, renderer_max_gpu_scatter_lods> indirect_args_static{};
                bool             args_baked = false;
            };
            std::array<GpuScatterSlot, renderer_max_gpu_scatter_slots> gpu_scatter{};

            // the grass slot as the terrain sees it past the last ring, written by the populate pass,
            // which is the one place that knows how far the rings actually reach, see FrameBufferData
            struct GrassFar
            {
                RHI_Texture*  prop_mask = nullptr;
                math::Vector4 tint      = math::Vector4::Zero;
                math::Vector4 fade      = math::Vector4::Zero;
                math::Vector4 gate      = math::Vector4::Zero;
                math::Vector4 patch     = math::Vector4::Zero;
            };
            GrassFar grass_far;

            // World-space tire history survives scatter repopulation and camera movement.
            struct GrassInteraction
            {
                std::shared_ptr<RHI_Texture> fields[2];
                std::shared_ptr<RHI_Buffer> contacts;
                std::shared_ptr<RHI_Buffer> bodies;
                // Current/previous rendered chassis pose and fitted convex planes.
                std::array<SurfaceBody, 2> body_data{};
                math::Vector2 origins[2] = {};
                uint32_t current = 0;
                bool valid = false;
                bool previous_valid = false;
                uint64_t vehicle_id = 0;
                std::array<math::Vector3, surface_contact_max> wheel_positions{};
                std::array<bool, surface_contact_max> wheel_valid{};
            } grass_interaction;

            // transient deflection of grass, shrubs and low branches by any interactor, a spring per cell
            struct VegetationInteraction
            {
                std::shared_ptr<RHI_Texture> fields[2];
                std::shared_ptr<RHI_Buffer> interactors;
                math::Vector2 origins[2] = {};
                math::Vector4 mapping          = math::Vector4::Zero;
                math::Vector4 mapping_previous = math::Vector4::Zero;
                uint32_t current = 0;
                bool valid = false;
                bool dispatch = false;
                bool writing = false; // the field is a uav, the global srv slots must not see it
            } vegetation_interaction;

            // tire ruts, the cpu field in TerrainDeformation is the source, this is its gpu mirror
            struct TerrainDeformationState
            {
                std::shared_ptr<RHI_Texture> texture;
                std::vector<TerrainDeformationUpload> uploads;
                math::Vector4 mapping = math::Vector4::Zero;
                math::Vector4 bounds  = math::Vector4::Zero;
                bool was_simulating   = false;
            } terrain_deformation;

            // interactive water on the ocean, the cpu field in OceanWake is the source, two textures so the
            // previous frame's surface stays readable for motion vectors
            struct OceanWakeState
            {
                std::shared_ptr<RHI_Texture> textures[2];
                std::vector<uint16_t> texels;
                math::Vector4 mapping          = math::Vector4::Zero;
                math::Vector4 mapping_previous = math::Vector4::Zero;
                uint32_t current      = 0;
                bool upload_pending   = false;
                bool was_simulating   = false;
            } ocean_wake;

            bool          terrain_enabled = false;
            TerrainParams terrain;

            Water*        ocean                              = nullptr;
            bool          ocean_spectrum_dirty               = true;
            bool          ocean_displacement_produced        = false;
            TemporalPingPong ocean_history;
            math::Vector3 ocean_wind                         = math::Vector3::Zero;

            void ResetCameraHistory()
            {
                ssao_history.Reset();
                fog_history.Reset();
                restir.history_invalid = true;
            }

            void ResetResolutionHistory()
            {
                cloud_history.Reset();
                cloud_environment.environment_dirty = true;
                ssao_history.Reset();
                fog_history.Reset();
                particle_volume_history.Reset();
            }

            void ResetWorld()
            {
                const bool brdf_ready = brdf_lut_produced;
                const uint64_t brdf_hash = brdf_lut_shader_hash;
                const bool atmosphere_ready = atmosphere_lut_produced;
                const bool cloud_noise_ready = cloud_noise_produced;
                *this = PassState();
                brdf_lut_produced = brdf_ready;
                brdf_lut_shader_hash = brdf_hash;
                atmosphere_lut_produced = atmosphere_ready;
                cloud_noise_produced = cloud_noise_ready;
            }

            void Reset()
            {
                *this = PassState();
            }
        };

        struct CrossQueueSync
        {
            RHI_SyncPrimitive* pending_compute_timeline       = nullptr;
            uint64_t           pending_compute_timeline_value = 0;
            // the async gi of the previous frame, it may still run while the next frame starts on graphics
            RHI_SyncPrimitive* gi_timeline       = nullptr;
            uint64_t           gi_timeline_value = 0;
            // graphics wrote a resource the in flight gi reads, the next graphics submission waits for it
            bool               gi_upload_hazard  = false;
            // async fog runs after compute b, graphics only waits for it right before its first reader
            RHI_SyncPrimitive* fog_timeline       = nullptr;
            uint64_t           fog_timeline_value = 0;
        };

        // pass history that follows the camera of one view, it lives in PassState while that view renders
        struct ViewPassHistory
        {
            bool cleared_reflections    = false;
            bool cleared_rt_reflections = false;
            bool cleared_rt_shadows     = false;
            bool depth_history_cleared  = false;
            bool hiz_history_valid      = false;
            RestirState restir;
            TemporalPingPong cloud_history;
            TemporalPingPong ssao_history;
            TemporalPingPong fog_history;
            Renderer_RenderTarget fog_source       = Renderer_RenderTarget::fog_scatter;
            Renderer_RenderTarget fog_water_source = Renderer_RenderTarget::fog_water_source;
            TemporalPingPong particle_volume_history;
            RHI_Texture* vrs_last_cleared_texture = nullptr;
        };

        // Device resources are shared; camera history, exposure and screen sized targets belong to a view.
        // While a view is inactive its renderer globals are parked here, see swap_view_state.
        struct ViewState
        {
            Cb_Frame frame = {};
            math::Matrix view_projection_previous_unjittered_left;
            math::Vector2 jitter_offset;
            math::Matrix view_projection_previous_right;
            Camera* camera = nullptr;
            ExposureState exposure;
            uint32_t temporal_frame = 0;
            float near_plane = 0.0f, far_plane = 1.0f;
            bool dirty_orthographic_projection = true;

            ViewPassHistory history;
            math::Vector2 resolution_render = math::Vector2::Zero;
            math::Vector2 resolution_output = math::Vector2::Zero;
            bool taau_reset_history         = true;
            bool hiz_suppressed             = true;
            std::vector<ShadowSlice> shadow_slices;
            uint32_t light_count            = 0;
            uint32_t volumetric_light_count = 0;
            uint32_t frame_resource_index   = 0;
        };
        ViewState& view();
        Camera* GetViewCamera();
        bool IsViewWireframe();

        // selects which set of per view render targets GetRenderTarget resolves to
        void SetViewRenderTargets(bool secondary);
        // (re)creates the secondary set at the active resolution when it is missing or stale, true when it did
        bool EnsureSecondaryRenderTargets();
        void ReleaseSecondaryRenderTargets();
        bool HasSecondaryRenderTargets();
        // points the per frame buffer slots at a frame resource
        void BindFrameResource(uint32_t index);
        // renders the secondary view with its own targets, history and frame slots, ahead of the primary in the same frame
        void RenderSecondaryView(Entity* camera_entity, Entity* root, uint32_t width, uint32_t height);
        // the lights the active view shades with
        const std::vector<Entity*>& GetViewLightEntities();

        struct State
        {
            State()
            {
                m_cull_tasks.reserve(renderer_max_cull_tasks);
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
            std::vector<Sb_DrawLod> m_draw_lods;
            bool m_draw_lods_uploaded = false;
            std::vector<uint32_t> m_tessellated_draws; // indices into m_draw_calls, args slot is the position in this list
            bool m_tessellated_gpu_culled = false;
            std::array<FrameResource, renderer_frame_resource_count> m_frame_resources;
            uint32_t m_frame_resource_index;
            uint32_t m_cpu_indirect_draw_arg_count;
            std::array<Sb_DrawData, renderer_max_draw_calls> m_draw_data_cpu;
            std::vector<DecalParameters> m_decal_data;
            std::unordered_map<const Render*, std::pair<uint32_t, uint32_t>> m_decal_ranges;
            uint32_t m_draw_data_count;
            bool m_draw_data_gpu_synced;
            std::array<RHI_Texture*, rhi_max_array_size> m_bindless_textures;
            std::array<Sb_Light, rhi_max_array_size> m_bindless_lights;
            std::array<Sb_Aabb, renderer_max_aabbs> m_bindless_aabbs;
            bool m_bindless_samplers_dirty = true;
            PassState m_pass_state;
            Pcb_Pass m_pcb_pass_cpu;

            std::vector<RHI_Vertex_PosCol> m_lines_vertices;
            std::vector<RHI_Vertex_PosCol> m_debug_triangles_vertices;
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
        inline auto& m_draw_lods = state().m_draw_lods;
        inline auto& m_draw_lods_uploaded = state().m_draw_lods_uploaded;
        inline auto& m_tessellated_draws = state().m_tessellated_draws;
        inline auto& m_tessellated_gpu_culled = state().m_tessellated_gpu_culled;
        inline auto& m_frame_resources = state().m_frame_resources;
        inline auto& m_frame_resource_index = state().m_frame_resource_index;
        inline auto& m_cpu_indirect_draw_arg_count = state().m_cpu_indirect_draw_arg_count;
        inline auto& m_draw_data_cpu = state().m_draw_data_cpu;
        inline auto& m_decal_data = state().m_decal_data;
        inline auto& m_decal_ranges = state().m_decal_ranges;
        inline auto& m_draw_data_count = state().m_draw_data_count;
        inline auto& m_draw_data_gpu_synced = state().m_draw_data_gpu_synced;
        inline auto& m_bindless_textures = state().m_bindless_textures;
        inline auto& m_bindless_lights = state().m_bindless_lights;
        inline auto& m_bindless_aabbs = state().m_bindless_aabbs;
        inline auto& m_bindless_samplers_dirty = state().m_bindless_samplers_dirty;
        inline auto& m_pass_state = state().m_pass_state;
        inline auto& m_pcb_pass_cpu = state().m_pcb_pass_cpu;

        inline auto& m_lines_vertices = state().m_lines_vertices;
        inline auto& m_debug_triangles_vertices = state().m_debug_triangles_vertices;
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
