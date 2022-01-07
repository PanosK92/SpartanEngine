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
#include "Renderer_ConstantBuffers.h"
#include "Material.h"
#include "../Core/ISubsystem.h"
#include "../Math/Rectangle.h"
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Viewport.h"
#include "../RHI/RHI_Vertex.h"
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

    class SPARTAN_CLASS Renderer : public ISubsystem
    {
        // Enums
    public:
        enum class Bindings_Cb
        {
            frame    = 0,
            uber     = 1,
            light    = 2,
            material = 3
        };

        // SRV bindings
        enum class Bindings_Srv
        {
            // Material
            material_albedo    = 0,
            material_roughness = 1,
            material_metallic  = 2,
            material_normal    = 3,
            material_height    = 4,
            material_occlusion = 5,
            material_emission  = 6,
            material_mask      = 7,

            // G-buffer
            gbuffer_albedo   = 8,
            gbuffer_normal   = 9,
            gbuffer_material = 10,
            gbuffer_velocity = 11,
            gbuffer_depth    = 12,

            // Lighting
            light_diffuse              = 13,
            light_diffuse_transparent  = 14,
            light_specular             = 15,
            light_specular_transparent = 16,
            light_volumetric           = 17,

            // Light depth/color maps
            light_directional_depth = 18,
            light_directional_color = 19,
            light_point_depth       = 20,
            light_point_color       = 21,
            light_spot_depth        = 22,
            light_spot_color        = 23,

            // Noise
            noise_normal = 24,
            noise_blue   = 25,

            // Misc
            lutIbl            = 26,
            environment       = 27,
            ssao              = 28,
            ssao_gi           = 29,
            ssr               = 30,
            frame             = 31,
            tex               = 32,
            tex2              = 33,
            font_atlas        = 34,
            reflection_probe  = 35
        };

        // UAV Bindings
        enum class Bindings_Uav
        {
            r         = 0,
            rg        = 1,
            rgb       = 2,
            rgb2      = 3,
            rgb3      = 4,
            rgba      = 5,
            rgba2     = 6,
            rgba_mips = 7
        };

        // Structured buffer bindings
        enum class Bindings_Sb
        {
            counter = 19
        };

        // Shaders
        enum class Shader : uint8_t
        {
            Gbuffer_V,
            Gbuffer_P,
            Depth_Prepass_V,
            Depth_Prepass_P,
            Depth_Light_V,
            Depth_Light_P,
            Quad_V,
            Copy_Point_C,
            Copy_Bilinear_C,
            Copy_Point_P,
            Copy_Bilinear_P,
            Fxaa_C,
            FilmGrain_C,
            Taa_C,
            MotionBlur_C,
            Dof_DownsampleCoc_C,
            Dof_Bokeh_C,
            Dof_Tent_C,
            Dof_UpscaleBlend_C,
            ChromaticAberration_C,
            BloomLuminance_C,
            BloomDownsample_C,
            BloomBlendFrame_C,
            BloomUpsampleBlendMip_C,
            ToneMapping_C,
            Debanding_C,
            Debug_Texture_C,
            Debug_ReflectionProbe_V,
            Debug_ReflectionProbe_P,
            BrdfSpecularLut_C,
            Light_C,
            Light_Composition_C,
            Light_ImageBased_P,
            Color_V,
            Color_P,
            Font_V,
            Font_P,
            Ssao_C,
            Ssr_C,
            Entity_V,
            Entity_Transform_P,
            BlurGaussian_C,
            BlurGaussianBilateral_C,
            Entity_Outline_P,
            Reflection_Probe_V,
            Reflection_Probe_P,
            AMD_FidelityFX_CAS_C,
            AMD_FidelityFX_SPD_C,
            AMD_FidelityFX_SPD_LuminanceAntiflicker_C,
            AMD_FidelityFX_FSR_Upsample_C,
            AMD_FidelityFX_FSR_Sharpen_C
        };

        // Render targets
        enum class RenderTarget : uint8_t
        {
            Undefined,
            Gbuffer_Albedo,
            Gbuffer_Normal,
            Gbuffer_Material,
            Gbuffer_Velocity,
            Gbuffer_Depth,
            Brdf_Specular_Lut,
            Light_Diffuse,
            Light_Diffuse_Transparent,
            Light_Specular,
            Light_Specular_Transparent,
            Light_Volumetric,
            Frame_Render,
            Frame_Render_2,
            Frame_Output,
            Frame_Output_2,
            Dof_Half,
            Dof_Half_2,
            Ssao,
            Ssao_Gi,
            Ssr,
            Taa_History,
            Bloom,
            Blur
        };

        enum Option : uint64_t
        {
            Debug_Aabb                                           = 1 << 0,
            Debug_PickingRay                                     = 1 << 1,
            Debug_Grid                                           = 1 << 2,
            Debug_ReflectionProbes                               = 1 << 3,
            Transform_Handle                                     = 1 << 4,
            Debug_SelectionOutline                               = 1 << 5,
            Debug_Lights                                         = 1 << 6,
            Debug_PerformanceMetrics                             = 1 << 7,
            Debug_Physics                                        = 1 << 8,
            Debug_Wireframe                                      = 1 << 9,
            Bloom                                                = 1 << 10,
            VolumetricFog                                        = 1 << 11,
            AntiAliasing_Taa                                     = 1 << 12,
            AntiAliasing_Fxaa                                    = 1 << 13,
            Ssao                                                 = 1 << 14,
            ScreenSpaceShadows                                   = 1 << 15,
            ScreenSpaceReflections                               = 1 << 16,
            MotionBlur                                           = 1 << 17,
            DepthOfField                                         = 1 << 18,
            FilmGrain                                            = 1 << 19,
            Sharpening_AMD_FidelityFX_ContrastAdaptiveSharpening = 1 << 20,
            ChromaticAberration                                  = 1 << 21,
            Debanding                                            = 1 << 22,
            ReverseZ                                             = 1 << 23,
            DepthPrepass                                         = 1 << 24,
            Upsample_TAA                                         = 1 << 25,
            Upsample_AMD_FidelityFX_SuperResolution              = 1 << 26
        };

        // Renderer/graphics options values
        enum class OptionValue
        {
            Anisotropy,
            ShadowResolution,
            Tonemapping,
            Gamma,
            Bloom_Intensity,
            Sharpen_Strength,
            Fog,
            Ssao_Gi
        };

        // Tonemapping
        enum class Tonemapping
        {
            Renderer_ToneMapping_Off,
            Renderer_ToneMapping_ACES,
            Renderer_ToneMapping_Reinhard,
            Renderer_ToneMapping_Uncharted2
        };

        // Renderable object types
        enum class ObjectType
        {
            GeometryOpaque,
            GeometryTransparent,
            Light,
            Camera,
            ReflectionProbe,
            Environment
        };

        // Defines
        #define DEBUG_COLOR Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)

        Renderer(Context* context);
        ~Renderer();

        //= ISubsystem =========================
        bool OnInitialise() override;
        void OnTick(double delta_time) override;
        //======================================

        // Primitive rendering
        void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DEBUG_COLOR, const Math::Vector4& color_to = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawTriangle(const Math::Vector3& v0, const Math::Vector3& v1, const Math::Vector3& v2, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawRectangle(const Math::Rectangle& rectangle, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);
        void DrawCircle(const Math::Vector3& center, const Math::Vector3& axis, const float radius, const uint32_t segment_count, const Math::Vector4& color = DEBUG_COLOR, const float duration = 0.0f, const bool depth = true);

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
        std::shared_ptr<RHI_Texture> GetRenderTarget(const RenderTarget rt_enum) { return m_render_targets[static_cast<uint8_t>(rt_enum)]; }
        const auto& GetRenderTargets()                                         { return m_render_targets; }
        void SetRenderTargetDebug(const RenderTarget render_target_debug)        { m_render_target_debug = render_target_debug; }
        RenderTarget GetRenderTargetDebug() const                                { return m_render_target_debug; }

        // Depth
        float GetClearDepth() { return GetOption(Renderer::Option::ReverseZ) ? m_viewport.depth_min : m_viewport.depth_max; }

        // Environment
        const std::shared_ptr<RHI_Texture> GetEnvironmentTexture();

        // Options
        uint64_t GetOptions()                        const { return m_options; }
        void SetOptions(const uint64_t options)            { m_options = options; }
        bool GetOption(const Renderer::Option option) const { return m_options & option; }
        void SetOption(Renderer::Option option, bool enable);
        
        // Options values
        template<typename T>
        T GetOptionValue(const Renderer::OptionValue option) { return static_cast<T>(m_option_values[option]); }
        void SetOptionValue(Renderer::OptionValue option, float value);

        // Swapchain
        RHI_SwapChain* GetSwapChain() const { return m_swap_chain.get(); }
        bool Present(RHI_CommandList* cmd_list);

        // Sync
        void Flush();

        // Default textures
        RHI_Texture* GetDefaultTextureWhite()       const { return m_tex_default_white.get(); }
        RHI_Texture* GetDefaultTextureTransparent() const { return m_tex_default_transparent.get(); }
        RHI_Texture* GetDefaultTextureEmpty()       const { return m_tex_default_empty.get(); }

        // Global uber constant buffer calls
        void SetCbUberTransform(RHI_CommandList* cmd_list, const Math::Matrix& transform);
        void SetCbUberColor(RHI_CommandList* cmd_list, const Math::Vector4& color);

        // Rendering
        bool IsRenderingAllowed() const { return m_is_rendering_allowed; }

        // Misc
        void SetGlobalShaderResources(RHI_CommandList* cmd_list) const;
        void RequestTextureMipGeneration(RHI_Texture* texture);
        const std::shared_ptr<RHI_Device>& GetRhiDevice()           const { return m_rhi_device; }
        RHI_PipelineCache* GetPipelineCache()                       const { return m_pipeline_cache.get(); }
        RHI_DescriptorSetLayoutCache* GetDescriptorLayoutSetCache() const { return m_descriptor_set_layout_cache.get(); }
        RHI_Texture* GetFrameTexture()                                    { return GetRenderTarget(Renderer::RenderTarget::Frame_Output).get(); }
        auto GetFrameNum()                                          const { return m_frame_num; }
        std::shared_ptr<Camera> GetCamera()                         const { return m_camera; }
        auto IsInitialised()                                        const { return m_initialised; }
        auto GetShaders()                                           const { return m_shaders; }
        RHI_CommandList* GetCmdList()                               const { return m_cmd_current; }
        uint32_t GetCmdIndex()                                      const { return m_cmd_index;  }

        // Passes
        void Pass_CopyToBackbuffer(RHI_CommandList* cmd_list);

    private:
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
        void Pass_Light(RHI_CommandList* cmd_list, const bool is_transparent_pass);
        void Pass_Light_Composition(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        void Pass_Light_ImageBased(RHI_CommandList* cmd_list, RHI_Texture* tex_out, const bool is_transparent_pass);
        void Pass_PostProcess(RHI_CommandList* cmd_list);
        void Pass_PostProcess_TAA(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_ToneMapping(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_Fxaa(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_FilmGrain(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out); 
        void Pass_PostProcess_ChromaticAberration(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_MotionBlur(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_DepthOfField(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_Debanding(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_PostProcess_Bloom(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Blur_Gaussian(RHI_CommandList* cmd_list, RHI_Texture* tex_in, const bool depth_aware, const float sigma, const float pixel_stride, const int mip = -1);
        void Pass_AMD_FidelityFX_ContrastAdaptiveSharpening(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_AMD_FidelityFX_SinglePassDowsnampler(RHI_CommandList* cmd_list, RHI_Texture* tex, const bool luminance_antiflicker);
        void Pass_AMD_FidelityFX_SuperResolution(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, RHI_Texture* tex_out_scratch);
        bool Pass_DebugBuffer(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Lines(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_DebugMeshes(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Outline(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_TransformHandle(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_PeformanceMetrics(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_BrdfSpecularLut(RHI_CommandList* cmd_list);
        void Pass_Copy(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const bool bilinear);
        void Pass_Generate_Mips();

        // Constant buffers
        bool Update_Cb_Frame(RHI_CommandList* cmd_list);
        bool Update_Cb_Uber(RHI_CommandList* cmd_list);
        bool Update_Cb_Light(RHI_CommandList* cmd_list, const Light* light, const RHI_Shader_Type scope);
        bool Update_Cb_Material(RHI_CommandList* cmd_list);

        // Event handlers
        void OnRenderablesAcquire(const Variant& renderables);
        void OnClear();
        void OnWorldLoaded();
        void OnFullScreenToggled();

        // Misc
        void SortRenderables(std::vector<Entity*>* renderables);

        // Lines
        void Lines_PreMain();
        void Lines_PostMain(const double delta_time);

        // Render targets
        std::array<std::shared_ptr<RHI_Texture>, 24> m_render_targets;

        // Shaders
        std::unordered_map<Renderer::Shader, std::shared_ptr<RHI_Shader>> m_shaders;

        // Standard textures
        std::shared_ptr<RHI_Texture> m_tex_default_noise_normal;
        std::shared_ptr<RHI_Texture> m_tex_default_noise_blue;
        std::shared_ptr<RHI_Texture> m_tex_default_white;
        std::shared_ptr<RHI_Texture> m_tex_default_transparent;
        std::shared_ptr<RHI_Texture> m_tex_default_empty;
        std::shared_ptr<RHI_Texture> m_tex_default_empty_cubemap;
        std::shared_ptr<RHI_Texture> m_tex_default_empty_array;
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
        Cb_Frame m_cb_frame_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_frame_gpu;
        uint32_t m_cb_frame_offset_index = 0;

        Cb_Uber m_cb_uber_cpu;
        Cb_Uber m_cb_uber_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_uber_gpu;
        uint32_t m_cb_uber_offset_index = 0;

        Cb_Light m_cb_light_cpu;
        Cb_Light m_cb_light_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_light_gpu;
        uint32_t m_cb_light_offset_index = 0;

        Cb_Material m_cb_material_cpu;
        Cb_Material m_cb_material_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_cb_material_gpu;
        uint32_t m_cb_material_offset_index = 0;
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
        Math::Rectangle m_gizmo_light_rect;
        std::shared_ptr<RHI_VertexBuffer> m_sphere_vertex_buffer;
        std::shared_ptr<RHI_IndexBuffer> m_sphere_index_buffer;

        // Resolution & Viewport
        Math::Vector2 m_resolution_render          = Math::Vector2::Zero;
        Math::Vector2 m_resolution_output          = Math::Vector2::Zero;
        RHI_Viewport m_viewport                    = RHI_Viewport(0, 0, 0, 0);
        Math::Rectangle m_viewport_quad            = Math::Rectangle(0, 0, 0, 0);
        Math::Vector2 m_resolution_output_previous = Math::Vector2::Zero;
        RHI_Viewport m_viewport_previous           = RHI_Viewport(0, 0, 0, 0);
        Math::Vector2 m_viewport_size_pending      = Math::Vector2::Zero;

        // Options
        uint64_t m_options = 0;
        std::unordered_map<Renderer::OptionValue, float> m_option_values;

        // Dirty states
        bool m_dirty_viewport                = false;
        bool m_dirty_orthographic_projection = true;

        // Misc
        std::unique_ptr<Font> m_font;
        Math::Vector2 m_taa_jitter         = Math::Vector2::Zero;
        Math::Vector2 m_taa_jitter_previous= Math::Vector2::Zero;
        RenderTarget m_render_target_debug = RenderTarget::Undefined;
        bool m_initialised                 = false;
        float m_near_plane                 = 0.0f;
        float m_far_plane                  = 0.0f;
        uint64_t m_frame_num               = 0;
        bool m_is_odd_frame                = false;
        bool m_brdf_specular_lut_rendered  = false;
        uint32_t m_cmd_index               = std::numeric_limits<uint32_t>::max();

        // Threading
        std::thread::id m_render_thread_id;
        std::atomic<bool> m_is_rendering_allowed = true;
        std::atomic<bool> m_flush_requested      = false;

        // Constants
        const uint32_t m_resolution_shadow_min = 128;
        const float m_gizmo_size_max           = 2.0f;
        const float m_gizmo_size_min           = 0.1f;
        const float m_thread_group_count       = 8.0f;
        const float m_depth_bias               = 0.004f; // bias that's applied directly into the depth buffer
        const float m_depth_bias_clamp         = 0.0f;
        const float m_depth_bias_slope_scaled  = 2.0f;

        // Requests for mip generation
        std::vector<RHI_Texture*> m_textures_mip_generation;
        std::atomic<bool> m_is_generating_mips = false;

        // RHI Core
        std::shared_ptr<RHI_Device> m_rhi_device;
        std::shared_ptr<RHI_PipelineCache> m_pipeline_cache;
        std::shared_ptr<RHI_DescriptorSetLayoutCache> m_descriptor_set_layout_cache;
        std::vector<std::shared_ptr<RHI_CommandList>> m_cmd_lists;
        RHI_CommandList* m_cmd_current = nullptr;

        // Swapchain
        static const uint8_t m_swap_chain_buffer_count = 3;
        std::shared_ptr<RHI_SwapChain> m_swap_chain;

        // Entity references
        std::unordered_map<ObjectType, std::vector<Entity*>> m_entities;
        std::array<Material*, m_max_material_instances> m_material_instances;
        std::shared_ptr<Camera> m_camera;

        // Dependencies
        Profiler* m_profiler            = nullptr;
        ResourceCache* m_resource_cache = nullptr;
    };
}
