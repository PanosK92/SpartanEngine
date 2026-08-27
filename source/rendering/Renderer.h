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
#include "../rhi/RHI_Definitions.h"
#include "../math/Vector2.h"
#include "../math/Vector3.h"
#include "../math/Vector4.h"
#include "../math/Plane.h"
#include "../math/Matrix.h"
#include "../commands/console/ConsoleCommands.h"
#include "../world/TerrainLayer.h"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <vector>
//==============================================

namespace spartan
{
    class Material;
    class Mesh;
    class Entity;
    class Camera;
    class Light;
    class Render;
    class Water;
    class Font;
    class RHI_Texture;
    class RHI_Shader;
    class RHI_Buffer;
    class RHI_Sampler;
    class RHI_CommandList;
    class RHI_Viewport;
    class RHI_RasterizerState;
    class RHI_DepthStencilState;
    class RHI_BlendState;
    class RHI_SyncPrimitive;
    class RHI_AccelerationStructure;
    enum class MeshType;
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
    extern TConsoleVar<float> cvar_transform_snap_translate;
    extern TConsoleVar<float> cvar_transform_snap_rotate;
    extern TConsoleVar<float> cvar_transform_snap_scale;
    extern TConsoleVar<float> cvar_selection_outline;
    extern TConsoleVar<float> cvar_entity_icons;
    extern TConsoleVar<float> cvar_performance_metrics;
    extern TConsoleVar<float> cvar_physics;
    extern TConsoleVar<float> cvar_ragdoll;
    extern TConsoleVar<float> cvar_wireframe;
    extern TConsoleVar<float> cvar_bloom;
    extern TConsoleVar<float> cvar_light_flares;
    extern TConsoleVar<float> cvar_light_flares_near_distance;
    extern TConsoleVar<float> cvar_light_flares_fade_length;
    extern TConsoleVar<float> cvar_light_flares_max_distance;
    extern TConsoleVar<float> cvar_light_flares_size_scale;
    extern TConsoleVar<float> cvar_light_flares_intensity_scale;
    extern TConsoleVar<float> cvar_light_flares_max_size_px;
    extern TConsoleVar<float> cvar_light_flares_occlusion;
    extern TConsoleVar<float> cvar_fog;
    extern TConsoleVar<float> cvar_ssao;
    extern TConsoleVar<float> cvar_ray_traced_reflections;
    extern TConsoleVar<float> cvar_ray_traced_shadows;
    extern TConsoleVar<float> cvar_restir_pt;
    extern TConsoleVar<float> cvar_restir_pt_scale;
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
    extern TConsoleVar<float> cvar_mesh_shaders;
    extern TConsoleVar<float> cvar_resolution_scale;
    extern TConsoleVar<float> cvar_dynamic_resolution;
    extern TConsoleVar<float> cvar_hiz_occlusion;
    extern TConsoleVar<float> cvar_meshlet_cull_skinned;
    extern TConsoleVar<float> cvar_meshlet_visualize;
    extern TConsoleVar<float> cvar_cluster_visualize;
    extern TConsoleVar<float> cvar_cluster_visualize_cap;
    extern TConsoleVar<float> cvar_auto_exposure_adaptation_speed;
    extern TConsoleVar<float> cvar_auto_exposure_compensation;

    enum class Renderer_SecondaryViewMode
    {
        Solid,
        Wireframe,
        Vertices
    };

    // what fills the pixels the previewed asset does not cover, the sky keeps lighting the asset
    // either way, this only replaces what is visible so a wireframe has something to read against
    enum class Renderer_SecondaryViewBackdrop
    {
        Sky,
        Charcoal,
        Slate,
        Paper,
        Max
    };

    namespace Renderer
    {
        // one ring_radii_m and cell_size_m entry per lod ring, the renderer assumes three rings ordered near to far
        struct GpuScatterParams
        {
            float ring_radii_m[3]    = { 30.0f, 120.0f, 500.0f };
            float cell_size_m[3]     = { 0.25f, 0.6f, 1.2f };
            float height_min         = 0.0f;
            float height_max         = 400.0f;
            float max_slope_deg      = 45.0f;
            float biome_min_weight   = 0.2f;
            uint32_t mask_channel    = 0;    // which prop mask channel gates the slot, 0 grass, 1 trees, 2 rocks
            float density            = 1.0f;
            float size_min           = 0.8f; // world scale on the mesh, one roll per instance inside the range
            float size_max           = 1.2f;
            // fraction of the material each instance samples, every instance takes a different patch of
            // it. 0 leaves the mesh uv alone, which is what grass needs, its uv is the blade gradient
            float uv_patch           = 0.0f;
            // degrees of random lean away from the surface normal, a solid instance that sits perfectly
            // flat reads as a sticker. grass wants 0, it takes its bend from the wind instead
            float tilt_deg           = 0.0f;
            float height_bake_min    = 0.0f; // remap 0-1 height preview to world y
            float height_bake_max    = 1.0f;
            // metres added to the seating height, negative pushes the instance into the ground
            float surface_offset     = 0.0f;
            // patch clustering, real ground cover grows in pockets rather than spread evenly, and
            // emptying most of the ring is what lets the same budget run thick inside what is left.
            // a size of 0 turns the whole thing off and scatters evenly
            float patch_size_m       = 0.0f;  // pocket scale in metres
            float patch_coverage     = 0.45f; // fraction of the eligible ground the pockets take
            float patch_edge         = 0.35f; // 0 is a hard boundary, 1 is a wide fringe
            float patch_scar         = 0.25f; // bare ground punched through pocket interiors
            // take the complement of the field, for a slot that belongs on the ground the pockets
            // left bare. it only lines up if both slots use the same patch_size_m
            bool patch_invert        = false;
            math::Vector2 terrain_extent_m = math::Vector2(6144.0f, 6144.0f);
            math::Vector4 terrain_world_mapping = math::Vector4::Zero; // xy min xz, zw 1/size
        };

        // everything the terrain surface evaluator needs, pushed by the Terrain component
        // the layer materials are registered as a contiguous block in the bindless table so the
        // shader can walk them from a single base index
        struct TerrainParams
        {
            Material*    surface    = nullptr;
            RHI_Texture* map_a      = nullptr;
            RHI_Texture* map_b      = nullptr;
            RHI_Texture* height_map = nullptr;

            std::array<Material*, terrain_layer_max>        layer_materials{};
            std::array<TerrainLayerRule, terrain_layer_max> layer_rules{};

            math::Vector4 world_mapping = math::Vector4::Zero; // xy = world min xz, zw = 1 / world size xz
            float sea_level             = 0.0f;
            float snow_level            = 400.0f;
            float snow_amount           = 1.0f;
            float wetness               = 0.0f;
            float blend_height          = 0.35f; // metres of ground that creep over an intersecting surface
            uint32_t quality            = 3; // top n layers sampled per pixel, matches Terrain's own default
            uint32_t debug_view         = 0; // TerrainDebugView, 0 is off
        };

        // core
        void Initialize();
        void Shutdown();
        void Tick();

        // debug primitives (duration: 0 = one frame, > 0 = seconds, FLT_MAX = forever)
        void DrawLine(const math::Vector3& from, const math::Vector3& to, const Color& color_from = Color::standard_renderer_lines, const Color& color_to = Color::standard_renderer_lines, float duration_sec = 0.0f);
        void DrawTriangle(const math::Vector3& v0, const math::Vector3& v1, const math::Vector3& v2, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        void DrawBox(const math::BoundingBox& box, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        void DrawCircle(const math::Vector3& center, const math::Vector3& axis, const float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        void DrawSphere(const math::Vector3& center, float radius, uint32_t segment_count, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        void DrawDirectionalArrow(const math::Vector3& start, const math::Vector3& end, float arrow_size, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        void DrawPlane(const math::Plane& plane, const Color& color = Color::standard_renderer_lines, float duration_sec = 0.0f);
        void DrawString(const char* text, const math::Vector2& position_screen_percentage);
        void DrawIcon(RHI_Texture* icon, const math::Vector2& position_screen_percentage);


        void SetPresentInRenderer(bool enabled);
        void BlitToXrSwapchain(RHI_Texture* texture);
        void EndXrFrame();

        // misc
        void SetStandardResources(RHI_CommandList* cmd_list = nullptr);
        uint64_t GetFrameNumber();
        RHI_Api_Type GetRhiApiType();
        bool Screenshot();
        bool Screenshot(const std::string& file_path);
        bool ScreenshotSecondary(
            const std::string& file_path
        );

        // returns the entry index, or uint32_max when the frame budget is full, a null render writes an identity uv transform
        uint32_t WriteDrawData(const math::Matrix& transform, const math::Matrix& transform_previous = math::Matrix::Identity, uint32_t material_index = 0, uint32_t is_transparent = 0, const Render* render = nullptr);

        // wind
        const math::Vector3& GetWind();
        void SetWind(const math::Vector3& wind);

        // gpu scatter, camera relative rings populated on the gpu with no entities behind them, slot 0
        // is grass and the higher slots are micro detail, the caller keeps ownership of the mesh,
        // material and heightmap and must outlive the renderer's use
        void EnableGpuScatter(
            uint32_t slot,
            Mesh* mesh,
            Material* material,
            RHI_Texture* terrain_heightmap,
            const GpuScatterParams& params,
            RHI_Texture* terrain_prop_mask = nullptr
        );
        void DisableGpuScatter(uint32_t slot);
        void DisableGpuScatter();
        bool IsGpuScatterEnabled();

        // terrain surface, the Terrain component keeps ownership of the materials and maps and must outlive their use
        void SetTerrain(const TerrainParams& params);
        void ClearTerrain(Material* surface);

        // fft ocean, the water component stays the owner of the simulation parameters and must outlive its use
        void EnableOcean(
            Water* water,
            bool spectrum_dirty
        );
        void DisableOcean(Water* water);
        bool IsOceanEnabled();
        // world space wave height at (x, z) from the readback of the gpu displacement, false when no ocean is active
        bool GetOceanHeight(const float x, const float z, float& height);

        // viewport
        const RHI_Viewport& GetViewport();
        void SetViewport(float width, float height);
        bool RequestSecondaryView(
            Entity* camera_entity,
            Entity* render_root,
            uint32_t width,
            uint32_t height,
            Renderer_SecondaryViewMode mode =
                Renderer_SecondaryViewMode::Solid,
            Renderer_SecondaryViewBackdrop backdrop =
                Renderer_SecondaryViewBackdrop::Sky
        );
        RHI_Texture* GetSecondaryViewOutput();
        bool IsSecondaryViewReady();
        // true only while the frame currently being recorded belongs to a secondary view
        bool IsSecondaryViewActive();
        void InvalidateSecondaryView();
        // a capture is waiting on a secondary render, issuing a new request would change what it grabs
        bool IsSecondaryScreenshotPending();
        uint64_t GetSecondaryViewGeneration();
        uint64_t GetSecondaryViewRequestGeneration();
        Renderer_SecondaryViewMode GetSecondaryViewMode();

        // resolution render
        const math::Vector2& GetResolutionRender();
        void SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources = true);

        // resolution output
        const math::Vector2& GetResolutionOutput();
        void SetResolutionOutput(uint32_t width, uint32_t height, bool recreate_resources = true);
        float GetResolutionScale();
        uint32_t GetScaledDimension(uint32_t dimension, float scale = -1.0f);

        // force render target recreation (e.g. when xr stereo mode changes)
        void RecreateRenderTargets();
        void ResetTaauHistory();

        // get all
        std::array<std::shared_ptr<RHI_Texture>, static_cast<uint32_t>(Renderer_RenderTarget::max)>& GetRenderTargets();
        std::array<std::shared_ptr<RHI_Shader>, static_cast<uint32_t>(Renderer_Shader::max)>& GetShaders();
        std::array<std::shared_ptr<RHI_Buffer>, static_cast<uint32_t>(Renderer_Buffer::Max)>& GetStructuredBuffers();
        std::array<std::shared_ptr<RHI_Sampler>, static_cast<uint32_t>(Renderer_Sampler::Max)>& GetSamplers();
        std::array<RHI_Texture*, rhi_max_array_size>& GetBindlessMaterialTextures();

        // get individual
        RHI_RasterizerState* GetRasterizerState(const Renderer_RasterizerState type);
        RHI_DepthStencilState* GetDepthStencilState(const Renderer_DepthStencilState type);
        RHI_BlendState* GetBlendState(const Renderer_BlendState type);
        RHI_Texture* GetRenderTarget(const Renderer_RenderTarget type);
        RHI_Shader* GetShader(const Renderer_Shader type);
        RHI_Buffer* GetBuffer(const Renderer_Buffer type);
        RHI_Texture* GetStandardTexture(const Renderer_StandardTexture type);
        RHI_AccelerationStructure* GetTopLevelAccelerationStructure();
        void DestroyAccelerationStructures();

        // cluster shading telemetry, last frame's count of clusters that exceeded CLUSTER_MAX_LIGHTS
        uint32_t GetClusterOverflowCount();
        std::shared_ptr<Mesh>& GetStandardMesh(const MeshType type);
        std::shared_ptr<Font>& GetFont();
        std::shared_ptr<Material>& GetStandardMaterial();
        void ClearMaterialTextureReferences();
        void UpdateFrameConstantBuffer();
        void UpdateFrameCb_CameraAndProjectionHistory();
        void UpdateFrameCb_ProjectionJitter();
        void UpdateFrameCb_ViewProjectionAndCameraFields();
        void UpdateFrameCb_ScalarFields();
        void UpdateFrameCb_ClusterLighting();
        void UpdateFrameCb_FeatureBits();
        void UpdateFrameCb_StereoXr();
        void UpdateFrameCb_RadialBlurHubs();
        bool SetResolution(math::Vector2& current, uint32_t width, uint32_t height, bool recreate_resources, bool create_render, bool create_output, const char* label);

        // resources
        void CreateBuffers();
        void CreateDepthStencilStates();
        void CreateRasterizerStates();
        void CreateBlendStates();
        void CreateShaders();
        void CreateSamplers();
        void CreateRenderTargets(const bool create_render, const bool create_output, const bool create_dynamic);
        void UpdateOptionalRenderTargets();
        void CreateFonts();
        void CreateStandardMeshes();
        void CreateStandardTextures();
        void CreateStandardMaterials();

        // passes - core
        void ProduceFrame();
        bool UpdateSkysphereConvergenceState();
        void Pass_ComputeBatchA(
            bool update_skysphere
        );
        void Pass_GraphicsPhase1_Geometry();
        void Pass_ComputeBatchB();
        void Pass_GraphicsPhase2_ShadowsAndRT();
        void ProduceFrame_PerEye(uint32_t eye, uint32_t eye_layer);
        void Pass_VariableRateShading();
        void Pass_ShadowMaps();
        void Pass_HiZ();
        void Pass_HiZ_BuildFromDepth(RHI_Texture* tex_depth);
        void Pass_IndirectCull();
        void Pass_IndirectCull_Meshlets();
        void Pass_IndirectCull_Refine();
        void Pass_Depth_Prepass();
        void Pass_GBuffer(const bool is_transparent_pass);
        void Pass_GBuffer_Indirect();
        void Pass_GBuffer_TessellatedAndTransparent(const bool is_transparent_pass);
        void Pass_MeshletVisualize();
        void Pass_ScreenSpaceAmbientOcclusion();
        void Pass_Reflections_Trace(uint32_t eye_layer = rhi_all_mips);
        void Pass_Reflections_Shade(uint32_t eye_layer = rhi_all_mips);
        void Pass_Reflections_Denoise(uint32_t eye_layer = rhi_all_mips);
        void Pass_Reflections_Apply(uint32_t eye_layer = rhi_all_mips);
        void Pass_RayTracedShadows();
        void Pass_Denoise_RayTracedShadows();
        void Pass_ReSTIR_PathTracing();
        void Pass_ReSTIR_TraceInitial(RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* tex_skysphere, RHI_Texture* const* reservoirs, uint32_t width, uint32_t height);
        void Pass_ReSTIR_Temporal(RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_prev, uint32_t dispatch_x, uint32_t dispatch_y);
        bool Pass_ReSTIR_SpatialPair(RHI_AccelerationStructure* tlas, RHI_Texture* tex_gi, RHI_Texture* const* reservoirs, RHI_Texture* const* reservoirs_spatial, uint32_t dispatch_x, uint32_t dispatch_y);
        void Pass_ReSTIR_SwapReservoirs();
        void Pass_ReSTIR_SwapGBufferHistory();
        void Pass_ReSTIR_Denoising();
        void Pass_ScreenSpaceShadows();
        void Pass_Skysphere();
        void Pass_Skysphere_SH_Project();
        void Pass_Clouds_Render(uint32_t eye_layer);
        void Pass_Clouds_Temporal(uint32_t eye_layer);
        void Pass_Clouds_Composite(uint32_t eye_layer, RHI_Texture* tex_scene);
        void Pass_Clouds_Environment();
        bool Pass_Clouds_Prepare(uint32_t eye_layer);
        void Pass_Clouds(uint32_t eye_layer, bool last_eye);
        // passes - lighting
        void Pass_LightClusterAssign();
        void Pass_LightClusterVisualize();
        void Pass_LightFlares(uint32_t eye_layer = rhi_all_mips);
        void Pass_Fog(uint32_t eye, uint32_t eye_layer = rhi_all_mips);
        void Pass_Light(const bool is_transparent_pass, uint32_t eye_layer = rhi_all_mips);
        void Pass_Light_Composition(const bool is_transparent_pass, uint32_t eye_layer = rhi_all_mips);
        void Pass_Light_Ibl(uint32_t eye_layer = rhi_all_mips);
        void Pass_Lut_BrdfSpecular();
        void Pass_Lut_AtmosphericScattering();
        void Pass_CloudNoise();
        // passes - particles
        void Pass_Particles();
        // passes - gpu procedural grass
        // runs the placement compute + indirect args build, the draw is folded into the g-buffer pass via Pass_Grass_Draw
        void Pass_Grass_Populate();
        void Pass_Grass_Draw();
        // passes - wind field
        void Pass_WindField();
        // passes - fft ocean
        void Pass_Ocean();
        bool ResolveOceanHeightReadback(
            uint32_t readback_index
        );
        void ResetOceanHeightReadback();
        // passes - debug/editor
        void Pass_Grid(RHI_Texture* tex_out);
        void Pass_Lines(RHI_Texture* tex_out);
        void Pass_Outline(RHI_Texture* tex_out);
        void Pass_Icons(RHI_Texture* tex_out);
        void Pass_Text(RHI_Texture* tex_out);
        // asset preview backdrop and wireframe recolour, secondary views only
        void Pass_PreviewStudio(RHI_Texture* tex_out);
        // passes - post-process
        void Pass_PostProcess(uint32_t eye_layer = rhi_all_mips);
        void Pass_PostProcess_Color(RHI_Texture*& tex_in, RHI_Texture*& tex_out, uint32_t eye_layer);
        void Pass_PostProcess_EditorOverlays(RHI_Texture* tex_out);
        void Pass_PostProcess_DisplayEffects(RHI_Texture*& tex_in, RHI_Texture*& tex_out, bool apply_dithering = true);
        void Pass_Tonemap(RHI_Texture* tex_in, RHI_Texture* tex_out, bool force_sdr = false);
        void Pass_Bloom(RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_AA_Upscale(uint32_t eye_layer = rhi_all_mips);
        void Pass_AutoExposure(RHI_Texture* tex_in);
        // passes - utility
        void Pass_Blit(RHI_Texture* tex_in, RHI_Texture* tex_out, const bool gpu_timing = true);
        void Pass_Downscale(RHI_Texture* tex, const Renderer_DownsampleFilter filter);
        void Pass_Blur(RHI_Texture* tex_in, const bool bilateral, const float radius, const uint32_t mip = rhi_all_mips);
        // restir denoising fallback, history clear plus blit raw to denoised
        void Pass_BlitRestirFallback(RHI_Texture* tex_raw, RHI_Texture* tex_denoised);

        // event handlers
        void OnFullScreenToggled();

        // bindless
        void UpdateMaterials();
        void UpdateLights();
        void UpdateBoundingBoxes();

        // misc
        void AddLinesToBeRendered();
        void UpdatePersistentLines();
        void SetCommonTextures(uint32_t eye_layer = rhi_all_mips, bool bind_ssao = true);
        void BeginPass(const char* name, uint32_t eye_layer, bool bind_ssao = true);
        void SetPass(const char* name, uint32_t eye_layer, bool bind_ssao = true);
        void DestroyResources();
        void UpdateShadowAtlas();

        // tick helpers
        void TickRecreateOptionalRenderTargetsIfNeeded();
        void TickUpdateHiZSuppressionState();
        // must run before UpdateDrawCalls, it assigns the material indices the draw data carries
        void TickUploadMaterials();
        void TickUploadBindlessDependencies();
        void TickAdvanceFrameConstantBufferRing();
        void TickLogClusterOverflowRateLimited();
        void Pass_Screenshot(RHI_Texture* tex_pre_tonemap);
        void Pass_ScreenshotXr();
        void FinalizeScreenshotReadback();
        void UpdateDrawCalls();
        void UpdateDrawCalls_ResetCounts();
        void UpdateDrawCalls_CollectAndSort();
        void UpdateDrawCalls_BuildPrepass();
        void UpdateDrawCalls_BuildIndirectAndCullTasks();
        void UpdateDrawCalls_SelectOccluders();
        void UpdateAccelerationStructures();
        // fills EmissiveTriangles from lod 0 of every emissive render, area weighted with a prefix sum so restir can sample in o(log n)
        void BuildEmissiveTriangleNeePool();
        void RotateFrameBuffers();
    }
}
