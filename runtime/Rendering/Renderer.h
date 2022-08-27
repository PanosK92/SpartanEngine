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

//= INCLUDES ========================
#include <unordered_map>
#include <array>
#include <atomic>
#include <thread>
#include "Renderer_ConstantBuffers.h"
#include "Material.h"
#include "../Core/Subsystem.h"
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Viewport.h"
#include "../RHI/RHI_Vertex.h"
#include "../Math/Rectangle.h"
#include "../Math/Plane.h"
#include "Renderer_Definitions.h"
//===================================

namespace Spartan
{
    //= FWD DECLARATIONS =
    class Entity;
    class Camera;
    class Light;
    class ResourceCache;
    class Font;
    class Variant;
    class Grid;
    class Profiler;
    //====================

    namespace Math
    {
        class BoundingBox;
        class Frustum;
    }

    class SPARTAN_CLASS Renderer : public Subsystem
    {
    public:
        Renderer(Context* context);
        ~Renderer();

        //= ISubsystem =========================
        void OnInitialise() override;
        void OnTick(double delta_time) override;
        //======================================

        // Primitive rendering (excellent for debugging)
        void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DEBUG_COLOR, const Math::Vector4& color_to = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawTriangle(const Math::Vector3& v0, const Math::Vector3& v1, const Math::Vector3& v2, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawRectangle(const Math::Rectangle& rectangle, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawCircle(const Math::Vector3& center, const Math::Vector3& axis, const float radius, uint32_t segment_count, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawSphere(const Math::Vector3& center, float radius, uint32_t segment_count, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawDirectionalArrow(const Math::Vector3& start, const Math::Vector3& end, float arrow_size, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawPlane(const Math::Plane& plane, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);

        // Viewport
        const RHI_Viewport& GetViewport() const { return m_viewport; }
        void SetViewport(float width, float height);

        // Resolution render
        const Math::Vector2& GetResolutionRender() const { return m_resolution_render; }
        void SetResolutionRender(uint32_t width, uint32_t height, bool recreate_resources = true);

        // Resolution output
        const Math::Vector2& GetResolutionOutput() const { return m_resolution_output; }
        void SetResolutionOutput (uint32_t width, uint32_t height, bool recreate_resources = true);

        // Render targets
        std::shared_ptr<RHI_Texture> GetRenderTarget(const RendererTexture rt_enum) { return m_render_targets[static_cast<uint8_t>(rt_enum)]; }
        const auto& GetRenderTargets()                                              { return m_render_targets; }

        // Clear depth value
        float GetClearDepth() { return GetOption<bool>(RendererOption::ReverseZ) ? 0.0f : 1.0f; }

        // Environment texture
        const std::shared_ptr<RHI_Texture> GetEnvironmentTexture();
        void SetEnvironmentTexture(std::shared_ptr<RHI_Texture> texture);

        // Options
        template<typename T>
        T GetOption(const RendererOption option) { return static_cast<T>(m_options[static_cast<uint32_t>(option)]); }
        void SetOption(RendererOption option, float value);
        std::array<float, 32> GetOptions() const { return m_options; }
        void SetOptions(std::array<float, 32> options) { m_options = options; }

        // Swapchain
        RHI_SwapChain* GetSwapChain() const { return m_swap_chain.get(); }
        void Present();

        // Sync
        void Flush();

        // Default colored textures
        RHI_Texture* GetDefaultTextureWhite()       const { return m_tex_default_white.get(); }
        RHI_Texture* GetDefaultTextureBlack()       const { return m_tex_default_black.get(); }
        RHI_Texture* GetDefaultTextureTransparent() const { return m_tex_default_transparent.get(); }

        // Command lists
        RHI_CommandList* GetCmdList() const { return m_cmd_current; }

        // Rhi
        static RHI_Api_Type GetRhiApiType();
        const std::shared_ptr<RHI_Device>& GetRhiDevice()  const { return m_rhi_device; }
        const RHI_Context* GetRhiContext()                 const { return m_rhi_context.get(); }
        bool IsRenderDocEnabled();

        // Misc
        void SetGlobalShaderResources(RHI_CommandList* cmd_list) const;
        void RequestTextureMipGeneration(std::shared_ptr<RHI_Texture> texture);

        RHI_Texture* GetFrameTexture()                                 { return GetRenderTarget(RendererTexture::Frame_Output).get(); }
        auto GetFrameNum()                                       const { return m_frame_num; }
        std::shared_ptr<Camera> GetCamera()                      const { return m_camera; }
        std::array<std::shared_ptr<RHI_Shader>, 48> GetShaders() const { return m_shaders; }

        // Passes
        void Pass_CopyToBackbuffer();

    private:
        // Constant buffers
        void Update_Cb_Frame(RHI_CommandList* cmd_list);
        void Update_Cb_Uber(RHI_CommandList* cmd_list);
        void Update_Cb_Light(RHI_CommandList* cmd_list, const Light* light, const RHI_Shader_Type scope);
        void Update_Cb_Material(RHI_CommandList* cmd_list);

        // Resource creation
        void CreateConstantBuffers();
        void CreateStructuredBuffers();
        void CreateDepthStencilStates();
        void CreateRasterizerStates();
        void CreateBlendStates();
        void CreateFonts();
        void CreateMeshes();
        void CreateTextures();
        void CreateShaders();
        void CreateSamplers(const bool create_only_anisotropic = false);
        void CreateRenderTextures(const bool create_render, const bool create_output, const bool create_fixed, const bool create_dynamic);

        // Passes
        void Pass_Main(RHI_CommandList* cmd_list);
        void Pass_UpdateFrameBuffer(RHI_CommandList* cmd_list);
        void Pass_ShadowMaps(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        void Pass_ReflectionProbes(RHI_CommandList* cmd_list);
        void Pass_Depth_Prepass(RHI_CommandList* cmd_list);
        void Pass_GBuffer(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        void Pass_Ssao(RHI_CommandList* cmd_list);
        void Pass_Ssr(RHI_CommandList* cmd_list, RHI_Texture* tex_in);
        void Pass_PostProcess(RHI_CommandList* cmd_list);
        void Pass_ToneMappingGammaCorrection(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_Fxaa(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_FilmGrain(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_ChromaticAberration(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_MotionBlur(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_DepthOfField(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_Debanding(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_Bloom(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool depth_aware, const float sigma, const float pixel_stride, const uint32_t mip = rhi_all_mips);
        void Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_DebugMeshes(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_TransformHandle(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_PeformanceMetrics(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_BrdfSpecularLut(RHI_CommandList* cmd_list);
        void Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const bool bilinear);
        void Pass_Generate_Mips(RHI_CommandList* cmd_list);
        // Lighting
        void Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        void Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        void Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        // AMD FidelityFX
        void Pass_Ffx_Cas(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);
        void Pass_Ffx_Spd(RHI_CommandList* cmd_list, RHI_Texture* tex, const bool luminance_antiflicker);
        void Pass_Ffx_Fsr2(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out);

        // Event handlers
        void OnAddRenderables(const Variant& renderables);
        void OnClear();
        void OnWorldLoaded();
        void OnFullScreenToggled();

        // Misc
        void SortRenderables(std::vector<Entity*>* renderables);
        bool IsCallingFromOtherThread();
        void OnResourceSafe();

        // Lines
        void Lines_PreMain();
        void Lines_PostMain(const double delta_time);

        // Render targets
        std::array<std::shared_ptr<RHI_Texture>, 25> m_render_targets;

        // Shaders
        std::array<std::shared_ptr<RHI_Shader>, 48> m_shaders;

        // Standard textures
        std::shared_ptr<RHI_Texture> m_tex_default_noise_normal;
        std::shared_ptr<RHI_Texture> m_tex_default_noise_blue;
        std::shared_ptr<RHI_Texture> m_tex_default_white;
        std::shared_ptr<RHI_Texture> m_tex_default_black;
        std::shared_ptr<RHI_Texture> m_tex_default_transparent;
        std::shared_ptr<RHI_Texture> m_tex_gizmo_light_directional;
        std::shared_ptr<RHI_Texture> m_tex_gizmo_light_point;
        std::shared_ptr<RHI_Texture> m_tex_gizmo_light_spot;

        // Depth-stencil states
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_off_off;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_off_r;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_rw_off;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_r_off;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_rw_w;

        // Blend states 
        std::shared_ptr<RHI_BlendState> m_blend_disabled;
        std::shared_ptr<RHI_BlendState> m_blend_alpha;
        std::shared_ptr<RHI_BlendState> m_blend_additive;

        // Rasterizer states
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_solid;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_wireframe;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_none_solid;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_light_point_spot;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_light_directional;

        // Samplers
        std::shared_ptr<RHI_Sampler> m_sampler_compare_depth;
        std::shared_ptr<RHI_Sampler> m_sampler_point_clamp;
        std::shared_ptr<RHI_Sampler> m_sampler_point_wrap;
        std::shared_ptr<RHI_Sampler> m_sampler_bilinear_clamp;
        std::shared_ptr<RHI_Sampler> m_sampler_bilinear_wrap;
        std::shared_ptr<RHI_Sampler> m_sampler_trilinear_clamp;
        std::shared_ptr<RHI_Sampler> m_sampler_anisotropic_wrap;

        //= CONSTANT BUFFERS =================================
        Cb_Frame m_cb_frame_cpu;
        Cb_Frame m_cb_frame_cpu_mapped;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_frame_gpu;

        Cb_Uber m_cb_uber_cpu;
        Cb_Uber m_cb_uber_cpu_mapped;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_uber_gpu;

        Cb_Light m_cb_light_cpu;
        Cb_Light m_cb_light_cpu_mapped;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_light_gpu;

        Cb_Material m_cb_material_cpu;
        Cb_Material m_cb_material_cpu_mapped;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_material_gpu;
        //====================================================

        // Structured buffers
        std::shared_ptr<RHI_StructuredBuffer> m_sb_counter;

        // Line rendering
        std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer_lines;
        std::vector<RHI_Vertex_PosCol> m_line_vertices;
        std::vector<float> m_lines_duration;
        uint32_t m_lines_index_depth_off = 0;
        uint32_t m_lines_index_depth_on  = 0;

        // Gizmos
        std::unique_ptr<Grid> m_gizmo_grid;
        std::shared_ptr<RHI_VertexBuffer> m_quad_vertex_buffer;
        std::shared_ptr<RHI_IndexBuffer> m_quad_index_buffer;
        std::shared_ptr<RHI_VertexBuffer> m_sphere_vertex_buffer;
        std::shared_ptr<RHI_IndexBuffer> m_sphere_index_buffer;

        // Resolution & Viewport
        Math::Vector2 m_resolution_render          = Math::Vector2::Zero;
        Math::Vector2 m_resolution_output          = Math::Vector2::Zero;
        RHI_Viewport m_viewport                    = RHI_Viewport(0, 0, 0, 0);
        Math::Vector2 m_resolution_output_previous = Math::Vector2::Zero;
        RHI_Viewport m_viewport_previous           = RHI_Viewport(0, 0, 0, 0);

        // Environment texture
        std::shared_ptr<RHI_Texture> m_environment_texture;
        std::atomic<std::shared_ptr<RHI_Texture>> m_environment_texture_temp;
        bool m_environment_texture_dirty = false;

        // Options
        std::array<float, 32> m_options;

        // Misc
        std::unique_ptr<Font> m_font;
        Math::Vector2 m_taa_jitter        = Math::Vector2::Zero;
        float m_near_plane                = 0.0f;
        float m_far_plane                 = 0.0f;
        uint64_t m_frame_num              = 0;
        bool m_is_odd_frame               = false;
        bool m_brdf_specular_lut_rendered = false;
        bool m_ffx_fsr2_reset            = false;
        std::thread::id m_render_thread_id;

        // Constants
        const uint32_t m_resolution_shadow_min = 128;
        const float m_thread_group_count       = 8.0f;
        const float m_depth_bias               = 0.004f; // bias that's applied directly into the depth buffer
        const float m_depth_bias_clamp         = 0.0f;
        const float m_depth_bias_slope_scaled  = 2.0f;

        // Requests for mip generation
        std::vector<std::shared_ptr<RHI_Texture>> m_textures_mip_generation;
        std::vector<std::shared_ptr<RHI_Texture>> m_textures_mip_generation_delete_per_mip;

        // States
        std::atomic<bool> m_is_rendering_allowed = true;
        std::atomic<bool> m_flush_requested      = false;
        bool m_dirty_orthographic_projection     = true;
        std::atomic<bool> m_reading_requests     = false;

        // RHI Core
        std::shared_ptr<RHI_Context> m_rhi_context;
        std::shared_ptr<RHI_Device> m_rhi_device;
        RHI_CommandPool* m_cmd_pool    = nullptr;
        RHI_CommandList* m_cmd_current = nullptr;

        // Swapchain
        static const uint8_t m_swap_chain_buffer_count = 2;
        std::shared_ptr<RHI_SwapChain> m_swap_chain;

        // Entity references
        std::vector<Entity*> m_entities_to_add;
        bool m_add_new_entities = false;
        std::unordered_map<RendererEntityType, std::vector<Entity*>> m_entities;
        std::array<Material*, m_max_material_instances> m_material_instances;
        std::shared_ptr<Camera> m_camera;

        // Sync objects
        std::mutex m_mutex_entity_addition;
        std::mutex m_mutex_mip_generation;

        // Dependencies
        Profiler* m_profiler            = nullptr;
        ResourceCache* m_resource_cache = nullptr;
    };
}
