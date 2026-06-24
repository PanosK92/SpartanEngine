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
#include "Renderer_Definitions.h"
#include "../RHI/RHI_Texture.h"
#include "../Math/Vector3.h"
#include "../Math/Plane.h"
#include "../Geometry/Mesh.h"
#include "Renderer_Buffers.h"
#include "../Font/Font.h"
#include "../Commands/Console/ConsoleCommands.h"
#include <unordered_map>
#include <atomic>
#include <string>
#include "../Math/Rectangle.h"
//==============================================

namespace spartan
{
    class Material;
    class Entity;
    class Camera;
    class Light;
    class Render;
    namespace math
    {
        class BoundingBox;
        class Frustum;
    }

    // console variables
    extern TConsoleVar<float> cvar_aabb;
    extern TConsoleVar<float> cvar_picking_ray;
    extern TConsoleVar<float> cvar_grid;
    extern TConsoleVar<float> cvar_transform_handle;
    extern TConsoleVar<float> cvar_transform_snap;
    extern TConsoleVar<float> cvar_selection_outline;
    extern TConsoleVar<float> cvar_lights;
    extern TConsoleVar<float> cvar_audio_sources;
    extern TConsoleVar<float> cvar_performance_metrics;
    extern TConsoleVar<float> cvar_physics;
    extern TConsoleVar<float> cvar_wireframe;
    extern TConsoleVar<float> cvar_bloom;
    extern TConsoleVar<float> cvar_fog;
    extern TConsoleVar<float> cvar_ssao;
    extern TConsoleVar<float> cvar_ray_traced_reflections;
    extern TConsoleVar<float> cvar_ray_traced_reflections_denoise;
    extern TConsoleVar<float> cvar_ray_traced_shadows;
    extern TConsoleVar<float> cvar_restir_pt;
    extern TConsoleVar<float> cvar_restir_pt_scale;
    extern TConsoleVar<float> cvar_restir_pt_w_clamp;
    extern TConsoleVar<float> cvar_restir_pt_debug;
    extern TConsoleVar<float> cvar_motion_blur;
    extern TConsoleVar<float> cvar_depth_of_field;
    extern TConsoleVar<float> cvar_film_grain;
    extern TConsoleVar<float> cvar_vhs;
    extern TConsoleVar<float> cvar_chromatic_aberration;
    extern TConsoleVar<float> cvar_dithering;
    extern TConsoleVar<float> cvar_sharpness;
    extern TConsoleVar<float> cvar_anisotropy;
    extern TConsoleVar<float> cvar_tonemapping;
    extern TConsoleVar<float> cvar_antialiasing_upsampling;
    extern TConsoleVar<float> cvar_hdr;
    extern TConsoleVar<float> cvar_gamma;
    extern TConsoleVar<float> cvar_vsync;
    extern TConsoleVar<float> cvar_variable_rate_shading;
    extern TConsoleVar<float> cvar_resolution_scale;
    extern TConsoleVar<float> cvar_dynamic_resolution;
    extern TConsoleVar<float> cvar_hiz_occlusion;
    extern TConsoleVar<float> cvar_meshlet_cull_skinned;
    extern TConsoleVar<float> cvar_meshlet_visualize;
    extern TConsoleVar<float> cvar_cluster_visualize;
    extern TConsoleVar<float> cvar_cluster_visualize_cap;
    extern TConsoleVar<float> cvar_auto_exposure_adaptation_speed;

    struct ShadowSlice
    {
        Light* light;
        uint32_t slice_index;
        uint32_t res;
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

    class Renderer
    {
    public:
        // configures the gpu procedural grass system, passed verbatim to Renderer::EnableProceduralGrass
        // ring_radii_m and cell_size_m carry one entry per lod ring, the renderer assumes three rings ordered near to far
        // the populate compute shader walks a square grid of (2 * ring_radius / cell_size)^2 cells per ring,
        // samples the terrain heightmap, hashes the cell for placement jitter, yaw and scale,
        // and atomically appends accepted samples to the per-lod section of grass_instances
        struct ProceduralGrassParams
        {
            float ring_radii_m[3]    = { 30.0f, 120.0f, 500.0f };
            float cell_size_m[3]     = { 0.25f, 0.6f, 1.2f };
            float height_min         = 0.0f;
            float height_max         = 400.0f;
            float max_slope_deg      = 45.0f;
            math::Vector2 terrain_extent_m = math::Vector2(6144.0f, 6144.0f); // terrain xz world-space extent, centered at origin
        };

        // configures the fft ocean, passed verbatim to Renderer::EnableOcean by the Water component
        // cascade_length carries one patch length (meters) per cascade, the spectrum and ifft run per slice
        struct OceanParams
        {
            uint32_t cascade_count    = 3;
            float    cascade_length[4] = { 1503.0f, 389.0f, 97.0f, 41.0f };
            float    amplitude        = 2.0f;
            float    choppiness       = 1.4f;
            float    displacement_scale = 1.0f;
            float    normal_strength  = 1.0f;
            float    foam_coverage    = 0.6f;
            float    sea_level        = 0.0f;
        };

        // core
        static void Initialize();
        static void Shutdown();
        static void Tick();

        // debug primitives (duration: 0 = one frame, > 0 = seconds, FLT_MAX = forever)
        static void DrawLine(const math::Vector3& from, const math::Vector3& to, const Color& color_from = Color::standard_renderer_lines, const Color& color_to = Color::standard_renderer_lines, float duration_sec = 0.0f);
        static void DrawTriangle(const math::Vector3& v0, const math::Vector3& v1, const math::Vector3& v2, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        static void DrawBox(const math::BoundingBox& box, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        static void DrawCircle(const math::Vector3& center, const math::Vector3& axis, const float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        static void DrawSphere(const math::Vector3& center, float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        static void DrawDirectionalArrow(const math::Vector3& start, const math::Vector3& end, float arrow_size, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        static void DrawPlane(const math::Plane& plane, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        static void DrawString(const char* text, const math::Vector2& position_screen_percentage);
        static void DrawIcon(RHI_Texture* icon, const math::Vector2& position_screen_percentage);


        // swapchain
        static RHI_SwapChain* GetSwapChain();
        static void BlitToBackBuffer(RHI_CommandList* cmd_list, RHI_Texture* texture);
        static void BlitToXrSwapchain(RHI_CommandList* cmd_list, RHI_Texture* texture);
        static void SubmitAndPresent();

        // misc
        static void SetStandardResources(RHI_CommandList* cmd_list);
        static uint64_t GetFrameNumber();
        static RHI_Api_Type GetRhiApiType();
        static bool Screenshot();
        static bool Screenshot(const std::string& file_path);
        static RHI_CommandList* GetCommandListPresent() { return m_cmd_list_present; }

        // write a draw data entry and return its index
        // when renderable is non-null its uv overrides are resolved against the material defaults,
        // otherwise an identity uv transform (tiling 1, offset 0, rotation 0, no invert) is written
        static uint32_t WriteDrawData(const math::Matrix& transform, const math::Matrix& transform_previous = math::Matrix::Identity, uint32_t material_index = 0, uint32_t is_transparent = 0, const Render* renderable = nullptr);

        // wind
        static const math::Vector3& GetWind();
        static void SetWind(const math::Vector3& wind);

        // gpu procedural grass
        // EnableProceduralGrass sets up the per-lod indirect args (based on the grass mesh's lod offsets in the
        // global geometry buffer) and arms the per-frame populate/draw passes. the mesh, material and heightmap
        // pointers are stored as raw and must outlive the renderer's use, the engine owns the actual resources.
        // DisableProceduralGrass tears it back down and lets the game ship its own grass system if it wants.
        static void EnableProceduralGrass(Mesh* grass_mesh, Material* grass_material, RHI_Texture* terrain_heightmap, const ProceduralGrassParams& params);
        static void DisableProceduralGrass();
        static bool IsProceduralGrassEnabled();

        // fft ocean
        // EnableOcean arms the per-frame spectrum + ifft passes and captures the parameters that the
        // water vertex/pixel shaders read from the frame constant buffer. the mesh and material pointers
        // are stored raw and must outlive the renderer's use, the Water component owns the resources.
        static void EnableOcean(Mesh* ocean_mesh, Material* ocean_material, const OceanParams& params);
        static void DisableOcean();
        static bool IsOceanEnabled();

        // viewport
        static const RHI_Viewport& GetViewport();
        static void SetViewport(float width, float height);

        // resolution render
        static const math::Vector2& GetResolutionRender();
        static void SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources = true);

        // resolution output
        static const math::Vector2& GetResolutionOutput();
        static void SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources = true);
        static float GetResolutionScale();
        static uint32_t GetScaledDimension(uint32_t dimension, float scale = -1.0f);

        // force render target recreation (e.g. when xr stereo mode changes)
        static void RecreateRenderTargets();
        static void ResetTaauHistory();

        // get all
        static std::array<std::shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)>& GetRenderTargets();
        static std::array<std::shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>& GetShaders();
        static std::array<std::shared_ptr<RHI_Buffer>, static_cast<uint32_t>(Renderer_Buffer::Max)>& GetStructuredBuffers();
        static std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>& GetSamplers();
        static std::array<RHI_Texture*, rhi_max_array_size>& GetBindlessMaterialTextures();

        // get individual
        static RHI_RasterizerState* GetRasterizerState(const Renderer_RasterizerState type);
        static RHI_DepthStencilState* GetDepthStencilState(const Renderer_DepthStencilState type);
        static RHI_BlendState* GetBlendState(const Renderer_BlendState type);
        static RHI_Texture* GetRenderTarget(const Renderer_RenderTarget type);
        static RHI_Shader* GetShader(const Renderer_Shader type);
        static RHI_Buffer* GetBuffer(const Renderer_Buffer type);
        static RHI_Texture* GetStandardTexture(const Renderer_StandardTexture type);
        static RHI_AccelerationStructure* GetTopLevelAccelerationStructure();
        static void DestroyAccelerationStructures();

        // cluster shading telemetry, last frame's count of clusters that exceeded CLUSTER_MAX_LIGHTS
        static uint32_t GetClusterOverflowCount();
        static std::shared_ptr<Mesh>& GetStandardMesh(const MeshType type);
        static std::shared_ptr<Font>& GetFont();
        static std::shared_ptr<Material>& GetStandardMaterial();
        static void ClearMaterialTextureReferences();
    private:
        static void UpdateFrameConstantBuffer(RHI_CommandList* cmd_list);
        static void UpdateFrameCb_CameraAndProjectionHistory();
        static void UpdateFrameCb_ProjectionJitter();
        static void UpdateFrameCb_ViewProjectionAndCameraFields();
        static void UpdateFrameCb_ScalarFields();
        static void UpdateFrameCb_ClusterLighting();
        static void UpdateFrameCb_FeatureBits();
        static void UpdateFrameCb_StereoXr();
        static bool SetResolution(math::Vector2& current, uint32_t width, uint32_t height, bool recreate_resources,
                                  bool create_render, bool create_output, const char* label);

        // resources
        static void CreateBuffers();
        static void CreateDepthStencilStates();
        static void CreateRasterizerStates();
        static void CreateBlendStates();
        static void CreateShaders();
        static void CreateSamplers();
        static void CreateRenderTargets(const bool create_render, const bool create_output, const bool create_dynamic);
        static void UpdateOptionalRenderTargets();
        static void CreateFonts();
        static void CreateStandardMeshes();
        static void CreateStandardTextures();
        static void CreateStandardMaterials();

        // passes - core
        static void ProduceFrame(RHI_CommandList* cmd_list_graphics_present, RHI_CommandList* cmd_list_compute);
        static bool UpdateSkysphereConvergenceState();
        static void Pass_ComputeBatchA(RHI_CommandList* cmd_list, bool update_skysphere, Light* directional_light);
        static void Pass_GraphicsPhase1_Geometry(RHI_CommandList* cmd_list);
        static void Pass_ComputeBatchB(RHI_CommandList* cmd_list);
        static void Pass_GraphicsPhase2_ShadowsAndRT(RHI_CommandList* cmd_list);
        static void ProduceFrame_PerEye(RHI_CommandList* cmd_list, uint32_t eye, uint32_t eye_layer);
        static void Pass_VariableRateShading(RHI_CommandList* cmd_list);
        static void Pass_ShadowMaps(RHI_CommandList* cmd_list);
        static void Pass_HiZ(RHI_CommandList* cmd_list);
        static void Pass_IndirectCull(RHI_CommandList* cmd_list);
        static void Pass_Depth_Prepass(RHI_CommandList* cmd_list);
        static void Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_GBuffer_Indirect(RHI_CommandList* cmd_list);
        static void Pass_GBuffer_TessellatedAndTransparent(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        static void Pass_MeshletVisualize(RHI_CommandList* cmd_list);
        static void Pass_ScreenSpaceAmbientOcclusion(RHI_CommandList* cmd_list);
        static void Pass_TransparencyReflectionRefraction(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void Pass_RayTracedReflections(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void Pass_RayTracedShadows(RHI_CommandList* cmd_list);
        static void Pass_ReSTIR_PathTracing(RHI_CommandList* cmd_list);
        static void Pass_ReSTIR_TraceInitial(RHI_CommandList* cmd_list, RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* tex_skysphere, RHI_Texture* const* reservoirs, uint32_t width, uint32_t height);
        static void Pass_ReSTIR_Temporal(RHI_CommandList* cmd_list, RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_prev, uint32_t dispatch_x, uint32_t dispatch_y);
        static bool Pass_ReSTIR_SpatialPair(RHI_CommandList* cmd_list, RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_spatial, uint32_t dispatch_x, uint32_t dispatch_y);
        static void Pass_ReSTIR_SwapReservoirs();
        static void Pass_ReSTIR_SwapGBufferHistory();
        static void Pass_ReSTIR_Denoising(RHI_CommandList* cmd_list);
        static void Pass_Composite_RayTracedReflections(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void Pass_Denoise_RayTracedReflections(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void Pass_ScreenSpaceShadows(RHI_CommandList* cmd_list);
        static void Pass_Skysphere(RHI_CommandList* cmd_list);
        // passes - lighting
        static void Pass_LightClusterAssign(RHI_CommandList* cmd_list);
        static void Pass_LightClusterVisualize(RHI_CommandList* cmd_list);
        static void Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass, uint32_t eye_layer = rhi_all_mips);
        static void Pass_Light_Composition(RHI_CommandList* cmd_list, const bool is_transparent_pass, uint32_t eye_layer = rhi_all_mips);
        static void Pass_Light_Ibl(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void Pass_Lut_BrdfSpecular(RHI_CommandList* cmd_list);
        static void Pass_Lut_AtmosphericScattering(RHI_CommandList* cmd_list);
        static void Pass_CloudNoise(RHI_CommandList* cmd_list);
        // passes - particles
        static void Pass_Particles(RHI_CommandList* cmd_list);
        // passes - gpu procedural grass
        // runs the placement compute + indirect args build, the actual draw calls are folded into
        // Pass_Depth_Prepass and Pass_GBuffer_Indirect via Pass_Grass_Draw to share their pso state
        static void Pass_Grass_Populate(RHI_CommandList* cmd_list);
        static void Pass_Grass_Draw(RHI_CommandList* cmd_list, bool is_depth_prepass);
        // passes - wind field
        static void Pass_WindField(RHI_CommandList* cmd_list);
        // passes - fft ocean
        static void Pass_Ocean(RHI_CommandList* cmd_list);
        // passes - debug/editor
        static void Pass_Grid(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_Text(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        // passes - post-process
        static void Pass_PostProcess(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void Pass_PostProcess_Color(RHI_CommandList* cmd_list, RHI_Texture*& tex_in, RHI_Texture*& tex_out, uint32_t eye_layer);
        static void Pass_PostProcess_EditorOverlays(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        static void Pass_PostProcess_DisplayEffects(RHI_CommandList* cmd_list, RHI_Texture*& tex_in, RHI_Texture*& tex_out, bool apply_dithering = true);
        static void Pass_Tonemap(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, bool force_sdr = false);
        static void Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_AA_Upscale(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void Pass_AutoExposure(RHI_CommandList* cmd_list, RHI_Texture* tex_in);
        template<typename F = std::nullptr_t>
        static void Pass_Compute(RHI_CommandList* cmd_list, const char* name, Renderer_Shader shader_enum,
                                 RHI_Texture* tex_in, RHI_Texture* tex_out, F setup = nullptr);
        // passes - utility
        static void Pass_Blit(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        static void Pass_Downscale(RHI_CommandList* cmd_list, RHI_Texture* tex, const Renderer_DownsampleFilter filter);
        static void Pass_Blur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool bilateral, const float radius, const uint32_t mip = rhi_all_mips);
        // restir denoising fallback, history clear plus blit raw to denoised, used when restir is off, debug mode is on, or shaders missing
        static void Pass_BlitRestirFallback(RHI_CommandList* cmd_list, RHI_Texture* tex_raw, RHI_Texture* tex_denoised, RHI_Texture* tex_history, bool clear_history_to_black);

        // event handlers
        static void OnFullScreenToggled();

        // bindless
        static void UpdateMaterials(RHI_CommandList* cmd_list);
        static void UpdateLights(RHI_CommandList* cmd_list);
        static void UpdateBoundingBoxes(RHI_CommandList* cmd_list);

        // misc
        static void AddLinesToBeRendered();
        static void UpdatePersistentLines();
        static void SetCommonTextures(RHI_CommandList* cmd_list, uint32_t eye_layer = rhi_all_mips);
        static void DestroyResources();
        static void UpdateShadowAtlas();

        // tick helpers
        static void TickRecreateOptionalRenderTargetsIfNeeded();
        static void TickUpdateHiZSuppressionState();
        static void TickUploadBindlessDependencies(RHI_CommandList* cmd_list);
        static void TickAdvanceFrameConstantBufferRing();
        static void TickLogClusterOverflowRateLimited();
        static void Pass_Screenshot(RHI_CommandList* cmd_list, RHI_Texture* tex_pre_tonemap);
        static void FinalizeScreenshotReadback();
        static void UpdateDrawCalls(RHI_CommandList* cmd_list);
        static void UpdateDrawCalls_ResetCounts();
        static void UpdateDrawCalls_CollectAndSort();
        static void UpdateDrawCalls_BuildPrepass();
        static void UpdateDrawCalls_BuildIndirectAndCullTasks();
        static void UpdateDrawCalls_SelectOccluders();
        static void UpdateAccelerationStructures(RHI_CommandList* cmd_list);
        // walk every visible renderable with a non zero emission property and write an entry
        // into the EmissiveTriangles structured buffer for each of its lod 0 triangles, the
        // entries carry world space positions, area, normal and emission radiance plus a
        // per triangle weight = area * lum(emission) and a running prefix sum used by the
        // restir initial trace pass to area sample a triangle in o(log n), the count gets
        // written into buffer_frame.restir_pt_emissive_tri_count, zero disables the strategy
        static void BuildEmissiveTriangleNeePool(RHI_CommandList* cmd_list);
        static void RotateFrameBuffers();

        // draw calls
        static std::array<Renderer_DrawCall, renderer_max_draw_calls> m_draw_calls;
        static uint32_t m_draw_call_count;
        static std::array<Renderer_DrawCall, renderer_max_draw_calls> m_draw_calls_prepass;
        static uint32_t m_draw_calls_prepass_count;

        // gpu-driven indirect drawing
        // m_indirect_draw_data holds per-renderable lod entries, sized for the per-renderable budget
        // m_indirect_renderables is a parallel array so UpdateBoundingBoxes can write each renderable's aabb at the same slot the cull shader reads, no filter divergence allowed
        // m_cull_tasks expands to one entry per (renderable, meshlet) and is what the cull pass dispatches over
        static std::array<Sb_DrawData, renderer_max_indirect_draws> m_indirect_draw_data;
        static std::array<Render*, renderer_max_indirect_draws>     m_indirect_renderables;
        static uint32_t m_indirect_draw_count;
        // count of distinct renderables in the indirect path, used to lay out one aabb slot per renderable
        static uint32_t m_indirect_renderable_count;
        static std::array<Sb_CullTask, renderer_max_cull_tasks> m_cull_tasks;
        static uint32_t m_cull_task_count;

        // per-frame gpu buffers, rotated so in-flight frames never race
        struct FrameResource
        {
            std::shared_ptr<RHI_Buffer> indirect_draw_args;     // single-slot args buffer for the final non-indexed indirect draw
            std::shared_ptr<RHI_Buffer> indirect_draw_data;     // per-renderable lod draw data
            std::shared_ptr<RHI_Buffer> meshlet_instances;      // meshlet-cull survivor list
            std::shared_ptr<RHI_Buffer> visible_triangles;      // triangle-cull survivor list (packed meshlet_instance + triangle index)
            std::shared_ptr<RHI_Buffer> triangle_dispatch_args; // single-slot indirect dispatch args for the triangle cull pass
            std::shared_ptr<RHI_Buffer> cull_tasks;
        };
        static std::array<FrameResource, renderer_draw_data_buffer_count> m_frame_resources;
        static uint32_t m_frame_resource_index;

        // cpu-side draw data staging
        static std::array<Sb_DrawData, renderer_max_draw_calls> m_draw_data_cpu;
        static uint32_t m_draw_data_count;

        // bindless
        static std::array<RHI_Texture*, rhi_max_array_size> m_bindless_textures;
        static std::array<Sb_Light, rhi_max_array_size> m_bindless_lights;
        static std::array<Sb_Aabb, rhi_max_array_size> m_bindless_aabbs;
        static bool m_bindless_samplers_dirty;

        // one-shot and feature-toggle state
        struct PassState
        {
            // one-shot initialization (run once, never again unless reset)
            bool brdf_lut_produced       = false;
            bool atmosphere_lut_produced = false;
            bool cloud_noise_produced    = false;

            // feature-toggle clear flags (set when feature disabled, reset when re-enabled)
            bool cleared_reflections     = false;
            bool cleared_rt_reflections  = false;
            bool cleared_rt_shadows      = false;
            bool cleared_restir          = false;
            // first-frame reservoir clear, prevents uninitialized memory from being read by the
            // temporal pass before the initial trace has populated all 18 reservoir textures,
            // toggled false when restir resources are (re)allocated, then set true after the
            // one-shot clear on the next dispatch, see Pass_ReSTIR_PathTracing
            bool restir_reservoirs_initialized = false;

            // skysphere convergence tracking
            // sky_warmup_this_frame is the warmup flag for this frame's Pass_Skysphere dispatch,
            // captured by UpdateSkysphereConvergenceState before it decrements sky_frames_remaining.
            // during warmup the shader does a full bake with the legacy 0.1 temporal blend, after
            // warmup it switches to a phase-distributed partial dispatch (1/4 of pixels per frame)
            // with a direct write, so animated clouds stay live without re-baking the whole panorama
            bool     sky_first_frame           = true;
            bool     sky_had_directional_light = false;
            uint32_t sky_frames_remaining      = 0;
            bool     sky_warmup_this_frame     = false;

            // vrs
            RHI_Texture* vrs_last_cleared_texture = nullptr;

            // gpu procedural grass
            // enabled is toggled by EnableProceduralGrass / DisableProceduralGrass, the per-frame passes
            // early out when it is false. mesh/material/heightmap and the params are captured at enable time
            // and used to build the static parts of the per-lod indirect args plus the populate dispatches.
            bool                  grass_enabled = false;
            Mesh*                 grass_mesh       = nullptr;
            Material*             grass_material   = nullptr;
            RHI_Texture*          grass_heightmap  = nullptr;
            ProceduralGrassParams grass_params;
            // baked once on enable, mirrors what the cpu would write into grass_indirect_args before the
            // args build shader bakes in the dynamic instance_count from grass_count, three entries per lod
            std::array<Sb_IndirectDrawArgs, renderer_max_grass_lod_count> grass_indirect_args_static{};
            bool                  grass_args_baked = false;

            // fft ocean, toggled by EnableOcean / DisableOcean, the spectrum init runs whenever the params change
            bool        ocean_enabled        = false;
            Mesh*       ocean_mesh           = nullptr;
            Material*   ocean_material       = nullptr;
            OceanParams ocean_params;
            bool        ocean_spectrum_dirty = true;
            math::Vector3 ocean_wind         = math::Vector3::Zero; // last world wind used, re-seeds the spectrum on change

            void Reset()
            {
                *this = PassState();
            }
        };
        static PassState m_pass_state;

        // misc
        static Cb_Frame m_cb_frame_cpu;
        static Pcb_Pass m_pcb_pass_cpu;
        static math::Matrix m_view_projection_previous_right;
        static math::Matrix m_view_projection_previous_unjittered_left;
        static std::shared_ptr<RHI_Buffer> m_lines_vertex_buffer;
        static std::vector<RHI_Vertex_PosCol> m_lines_vertices;
        static std::vector<PersistentLine> m_persistent_lines;
        static std::vector<std::tuple<RHI_Texture*, math::Vector3>> m_icons;
        static uint32_t m_frame_cb_ring_slot;
        static std::atomic<bool> m_initialized_resources;
        static bool m_transparents_present;
        static bool m_is_hiz_suppressed;
        static bool m_taau_reset_history;
        static RHI_CommandList* m_cmd_list_present;
        static RHI_CommandList* m_cmd_list_compute;

        // cross-queue and cross-frame timeline sync, see CrossQueueSync member fields for the contract
        struct CrossQueueSync
        {
            // phase 3 present submit waits on async compute batch b before recording lighting work
            RHI_SyncPrimitive* pending_compute_timeline       = nullptr;
            uint64_t           pending_compute_timeline_value = 0;
            // compute batch a waits on the previous frame's last graphics submit so it does not
            // overwrite resources still in flight (e.g. tlas, skysphere)
            RHI_SyncPrimitive* previous_present_timeline       = nullptr;
            uint64_t           previous_present_timeline_value = 0;
        };
        static CrossQueueSync m_cross_queue_sync;

        static std::vector<ShadowSlice> m_shadow_slices;
        static uint32_t m_count_active_lights;
        static uint32_t m_volumetric_light_count;

        // top-level acceleration structure built once per frame from all bindless mesh instances
        static std::unique_ptr<RHI_AccelerationStructure> m_tlas;

        // session statics (resolution, viewport, swapchain, frame counter, taa jitter)
        static math::Vector2                 m_resolution_render;
        static math::Vector2                 m_resolution_output;
        static RHI_Viewport                  m_viewport;
        static std::shared_ptr<RHI_SwapChain> m_swapchain;
        static uint64_t                      m_frame_num;
        static math::Vector2                 m_jitter_offset;
    };
}
