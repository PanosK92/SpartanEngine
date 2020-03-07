/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include "../Core/ISubsystem.h"
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Viewport.h"
#include "../Math/Rectangle.h"
#include "Renderer_ConstantBuffers.h"
//===================================

namespace Spartan
{
    // Forward declarations
	class Entity;
	class Camera;
	class Light;
	class ResourceCache;
	class Font;
	class Variant;
	class Grid;
	class Transform_Gizmo;
	class Profiler;
	namespace Math
	{
		class BoundingBox;
		class Frustum;
	}

	enum Renderer_Option : uint64_t
	{
		Render_Debug_Aabb				    = 1 << 0,
		Render_Debug_PickingRay			    = 1 << 1,
		Render_Debug_Grid				    = 1 << 2,
		Render_Debug_Transform			    = 1 << 3,
		Render_Debug_Lights				    = 1 << 4,
		Render_Debug_PerformanceMetrics	    = 1 << 5,
		Render_Debug_Physics			    = 1 << 6,
        Render_Debug_Wireframe              = 1 << 7,
		Render_Bloom				        = 1 << 8,
        Render_VolumetricLighting           = 1 << 9,
        Render_AntiAliasing_Taa             = 1 << 10,
		Render_AntiAliasing_Fxaa	        = 1 << 11,
		Render_ScreenSpaceAmbientOcclusion  = 1 << 12,
        Render_ScreenSpaceShadows           = 1 << 13,
		Render_ScreenSpaceReflections		= 1 << 14,
		Render_MotionBlur			        = 1 << 15,
		Render_Sharpening_LumaSharpen	    = 1 << 16,
		Render_ChromaticAberration	        = 1 << 17,
		Render_Dithering			        = 1 << 18,
        Render_ReverseZ                     = 1 << 19,
        Render_DepthPrepass                 = 1 << 20
	};

    enum Renderer_Option_Value
    {
        Option_Value_Anisotropy,
        Option_Value_ShadowResolution,
        Option_Value_Tonemapping,
        Option_Value_Exposure,
        Option_Value_Gamma,
        Option_Value_Bloom_Intensity,
        Option_Value_Sharpen_Strength,
        Option_Value_Sharpen_Clamp, // Limits maximum amount of sharpening a pixel receives - Algorithm's default: 0.035f
        Option_Value_Motion_Blur_Intensity
    };

    enum Renderer_ToneMapping_Type
    {
        Renderer_ToneMapping_Off,
        Renderer_ToneMapping_ACES,
        Renderer_ToneMapping_Reinhard,
        Renderer_ToneMapping_Uncharted2
    };

	enum Renderer_Buffer_Type
	{
		Renderer_Buffer_None,
		Renderer_Buffer_Albedo,
		Renderer_Buffer_Normal,
		Renderer_Buffer_Material,
        Renderer_Buffer_Diffuse,
        Renderer_Buffer_Specular,
		Renderer_Buffer_Velocity,
		Renderer_Buffer_Depth,
		Renderer_Buffer_SSAO,
        Renderer_Buffer_SSR,
        Renderer_Buffer_Bloom,
        Renderer_Buffer_VolumetricLighting,
        Renderer_Buffer_Shadows
	};

	enum Renderer_Object_Type
	{
		Renderer_Object_Opaque,
		Renderer_Object_Transparent,
        Renderer_Object_Light,
		Renderer_Object_LightDirectional,
        Renderer_Object_LightPoint,
        Renderer_Object_LightSpot,
		Renderer_Object_Camera
	};

	enum Renderer_Shader_Type
	{
		Shader_Gbuffer_V,
		Shader_Depth_V,
        Shader_Depth_P,
		Shader_Quad_V,
		Shader_Texture_P,
		Shader_Fxaa_P,
		Shader_Luma_P,
		Shader_Taa_P,
		Shader_MotionBlur_P,
		Shader_Sharpen_Luma_P,
		Shader_ChromaticAberration_P,	
		Shader_BloomDownsampleLuminance_P,
        Shader_BloomDownsample_P,
		Shader_BloomBlend_P,
		Shader_ToneMapping_P,
		Shader_GammaCorrection_P,
		Shader_Dithering_P,
		Shader_Upsample_P,
        Shader_Downsample_P,
		Shader_DebugNormal_P,
		Shader_DebugVelocity_P,
		Shader_DebugChannelR_P,
        Shader_DebugChannelA_P,
        Shader_DebugChannelRgbGammaCorrect_P,
        Shader_BrdfSpecularLut,
        Shader_LightDirectional_P,
        Shader_LightPoint_P,
        Shader_LightSpot_P,
		Shader_Composition_P,
		Shader_Color_V,
        Shader_Color_P,
		Shader_Font_V,
        Shader_Font_P,
		Shader_Ssao_P,
        Shader_Ssr_P,
		Shader_GizmoTransform_V,
        Shader_GizmoTransform_P,
		Shader_BlurBox_P,
		Shader_BlurGaussian_P,
		Shader_BlurGaussianBilateral_P,
        Shader_Outline_P
	};

    enum Renderer_RenderTarget_Type
    {
        // G-Buffer
        RenderTarget_Gbuffer_Albedo,
        RenderTarget_Gbuffer_Normal,
        RenderTarget_Gbuffer_Material,
        RenderTarget_Gbuffer_Velocity,
        RenderTarget_Gbuffer_Depth,
        // BRDF
        RenderTarget_Brdf_Prefiltered_Environment,
        RenderTarget_Brdf_Specular_Lut,
        // Lighting
        RenderTarget_Light_Diffuse,
        RenderTarget_Light_Specular,
        // Volumetric light
        RenderTarget_Light_Volumetric,
        // Composition
        RenderTarget_Composition_Hdr,
        RenderTarget_Composition_Hdr_2,
        RenderTarget_Composition_Ldr,
        RenderTarget_Composition_Ldr_2,
        // SSAO
        RenderTarget_Ssao_Noisy,
        RenderTarget_Ssao,
        // SSR
        RenderTarget_Ssr,
        // Frame
        RenderTarget_TaaHistory
    };

	class SPARTAN_CLASS Renderer : public ISubsystem
	{
	public:
		Renderer(Context* context);
		~Renderer();

		//= ISubsystem ======================
		bool Initialize() override;
		void Tick(float delta_time) override;
		//===================================

		#define DebugColor Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)
		void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DebugColor, const Math::Vector4& color_to = DebugColor, bool depth = true);
        void DrawRectangle(const Math::Rectangle& rectangle, const Math::Vector4& color = DebugColor, bool depth = true);
		void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DebugColor, bool depth = true);

		// Viewport
		const auto& GetViewport() const			        { return m_viewport; }
		void SetViewport(const RHI_Viewport& viewport)	{ m_viewport = viewport; }

        // Resolution
        const auto& GetResolution() const { return m_resolution; }
        void SetResolution(uint32_t width, uint32_t height);

		// Editor
		float m_gizmo_transform_size    = 0.015f;
		float m_gizmo_transform_speed   = 12.0f;
        Math::Vector2 viewport_editor_offset;
        std::weak_ptr<Entity> SnapTransformGizmoTo(const std::shared_ptr<Entity>& entity) const;

		// Debug
		void SetDebugBuffer(const Renderer_Buffer_Type buffer)	{ m_debug_buffer = buffer; }
		auto GetDebugBuffer() const				                { return m_debug_buffer; }

        // Depth
        auto GetClearDepth()                { return GetOption(Render_ReverseZ) ? m_viewport.depth_min : m_viewport.depth_max; }
        auto GetComparisonFunction() const  { return GetOption(Render_ReverseZ) ? RHI_Comparison_GreaterEqual : RHI_Comparison_LessEqual; }

        // Environment
        const std::shared_ptr<RHI_Texture>& GetEnvironmentTexture();
        void SetEnvironmentTexture(const std::shared_ptr<RHI_Texture>& texture);

        // Options
        uint64_t GetOptions()                           const { return m_options; }
        void SetOptions(const uint64_t options)               { m_options = options; }
        bool GetOption(const Renderer_Option option)    const { return m_options & option; }
        void SetOption(Renderer_Option option, bool enable);
        
        // Options values
        template<typename T>
        T GetOptionValue(const Renderer_Option_Value option) { return static_cast<T>(m_option_values[option]); }
        void SetOptionValue(Renderer_Option_Value option, float value);

        // Misc
        const auto& GetRhiDevice()	    const { return m_rhi_device; }
        const auto& GetSwapChain()      const { return m_swap_chain; }
        const auto& GetPipelineCache()  const { return m_pipeline_cache; }
        RHI_Texture* GetFrameTexture()        { return m_render_targets[RenderTarget_Composition_Ldr].get(); }
        auto GetFrameNum()              const { return m_frame_num; }
        const auto& GetCamera()         const { return m_camera; }
        auto IsInitialized()            const { return m_initialized; }
        auto& GetShaders()              const { return m_shaders; }
        auto IsRendering()              const { return m_is_rendering; }
        uint32_t GetMaxResolution() const;

        // Globals
        void SetGlobalShaderObjectTransform(const Math::Matrix& transform) { m_buffer_object_cpu.object = transform; UpdateObjectBuffer(nullptr); }
        void SetGlobalSamplersAndConstantBuffers(RHI_CommandList* cmd_list) const;
        RHI_Texture* GetBlackTexture() const { return m_tex_black.get(); }

	private:
        // Resource creation
        void CreateConstantBuffers();
		void CreateDepthStencilStates();
		void CreateRasterizerStates();
		void CreateBlendStates();
		void CreateFonts();
		void CreateTextures();
		void CreateShaders();
		void CreateSamplers();
		void CreateRenderTextures();

		// Passes
		void Pass_Main(RHI_CommandList* cmd_list);
		void Pass_LightShadow(RHI_CommandList* cmd_list, const Renderer_Object_Type object_type);
        void Pass_DepthPrePass(RHI_CommandList* cmd_list);
		void Pass_GBuffer(RHI_CommandList* cmd_list, const Renderer_Object_Type object_type);
		void Pass_Ssao(RHI_CommandList* cmd_list, const bool use_stencil);
        void Pass_Ssr(RHI_CommandList* cmd_list, const bool use_stencil);
        void Pass_Light(RHI_CommandList* cmd_list, const bool use_stencil);
		void Pass_Composition(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_out, const bool use_stencil);
        void Pass_AlphaBlend(RHI_CommandList* cmd_list, RHI_Texture* tex_in, RHI_Texture* tex_out, const bool use_stencil);
		void Pass_PostProcess(RHI_CommandList* cmd_list);
		void Pass_TAA(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		bool Pass_DebugBuffer(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ToneMapping(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_GammaCorrection(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_FXAA(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in,	std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_LumaSharpen(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ChromaticAberration(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_MotionBlur(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Dithering(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Bloom(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Upsample(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Downsample(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out, const Renderer_Shader_Type pixel_shader);
		void Pass_BlurBox(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride, const bool use_stencil);
		void Pass_BlurGaussian(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride = 1.0f);
		void Pass_BlurBilateralGaussian(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out, const float sigma, const float pixel_stride = 1.0f, const bool use_stencil = false);
		void Pass_Lines(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Outline(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Icons(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_TransformHandle(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
		void Pass_PerformanceMetrics(RHI_CommandList* cmd_list, RHI_Texture* tex_out);
        void Pass_BrdfSpecularLut(RHI_CommandList* cmd_list);
        void Pass_Copy(RHI_CommandList* cmd_list, std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);

        // Constant buffers
        bool UpdateFrameBuffer();
        bool UpdateUberBuffer();
        bool UpdateObjectBuffer(RHI_CommandList* cmd_list, const uint32_t offset_index = 0);
        bool UpdateLightBuffer(const Light* light);

        // Misc
        void RenderablesAcquire(const Variant& renderables);
        void RenderablesSort(std::vector<Entity*>* renderables);
        void ClearEntities() { m_entities.clear(); }

        // Render textures
        std::unordered_map<Renderer_RenderTarget_Type, std::shared_ptr<RHI_Texture>> m_render_targets;
        std::vector<std::shared_ptr<RHI_Texture>> m_render_tex_bloom;

        // Standard textures
        std::shared_ptr<RHI_Texture> m_tex_noise_normal;
        std::shared_ptr<RHI_Texture> m_tex_white;
        std::shared_ptr<RHI_Texture> m_tex_black;
        std::shared_ptr<RHI_Texture> m_gizmo_tex_light_directional;
        std::shared_ptr<RHI_Texture> m_gizmo_tex_light_point;
        std::shared_ptr<RHI_Texture> m_gizmo_tex_light_spot;

		// Shaders
		std::unordered_map<Renderer_Shader_Type, std::shared_ptr<RHI_Shader>> m_shaders;

		// Depth-stencil states
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_disabled;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_disabled_enabled_read;
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_enabled_disabled_write;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_enabled_disabled_read;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_enabled_enabled_write;

        // Blend states 
        std::shared_ptr<RHI_BlendState> m_blend_disabled;
        std::shared_ptr<RHI_BlendState> m_blend_alpha;
        std::shared_ptr<RHI_BlendState> m_blend_additive;

        // Rasterizer states
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_solid;
        std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_solid_no_clip;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_front_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_none_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_wireframe;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_front_wireframe;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_none_wireframe;

        // Samplers
		std::shared_ptr<RHI_Sampler> m_sampler_compare_depth;
		std::shared_ptr<RHI_Sampler> m_sampler_point_clamp;
		std::shared_ptr<RHI_Sampler> m_sampler_bilinear_clamp;
		std::shared_ptr<RHI_Sampler> m_sampler_bilinear_wrap;
		std::shared_ptr<RHI_Sampler> m_sampler_trilinear_clamp;
		std::shared_ptr<RHI_Sampler> m_sampler_anisotropic_wrap;

        // Line rendering
		std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer_lines;
		std::vector<RHI_Vertex_PosCol> m_lines_list_depth_enabled;
		std::vector<RHI_Vertex_PosCol> m_lines_list_depth_disabled;

        // Gizmos
		std::unique_ptr<Transform_Gizmo> m_gizmo_transform;
		std::unique_ptr<Grid> m_gizmo_grid;
		Math::Rectangle m_gizmo_light_rect;

        // Resolution & Viewport
		Math::Vector2 m_resolution	= Math::Vector2(1920, 1080);
		RHI_Viewport m_viewport		= RHI_Viewport(0, 0, 1920, 1080);

        // Options
        uint64_t m_options = 0;
        std::unordered_map<Renderer_Option_Value, float> m_option_values;

        // Misc
		Math::Rectangle m_quad;
		std::unique_ptr<Font> m_font;
        Math::Vector2 m_taa_jitter              = Math::Vector2::Zero;
		Math::Vector2 m_taa_jitter_previous     = Math::Vector2::Zero;
		Renderer_Buffer_Type m_debug_buffer     = Renderer_Buffer_None;
		bool m_initialized                      = false;
        const uint32_t m_resolution_shadow_min  = 128;
        float m_near_plane                      = 0.0f;
        float m_far_plane                       = 0.0f;
        uint64_t m_frame_num                    = 0;
        bool m_is_odd_frame                     = false;
        bool m_is_rendering                     = false;
        bool m_brdf_specular_lut_rendered       = false;      
        const float m_gizmo_size_max            = 5.0f;
        const float m_gizmo_size_min            = 0.1f;
                                                                      
        //= BUFFERS ============================================
        BufferFrame m_buffer_frame_cpu;
        std::shared_ptr<RHI_ConstantBuffer> m_buffer_frame_gpu;

        BufferUber m_buffer_uber_cpu;
        BufferUber m_buffer_uber_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_buffer_uber_gpu;

        BufferObject m_buffer_object_cpu;
        BufferObject m_buffer_object_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_buffer_object_gpu;

        BufferLight m_buffer_light_cpu;
        BufferLight m_buffer_light_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_buffer_light_gpu;
        //======================================================

        // Entities & Components
        std::unordered_map<Renderer_Object_Type, std::vector<Entity*>> m_entities;
        std::shared_ptr<Camera> m_camera;

        // Core
        std::shared_ptr<RHI_Device> m_rhi_device;
        std::shared_ptr<RHI_SwapChain> m_swap_chain;
        std::shared_ptr<RHI_PipelineCache> m_pipeline_cache;

        // Dependencies
        Profiler* m_profiler            = nullptr;
        ResourceCache* m_resource_cache = nullptr;
    };
}
