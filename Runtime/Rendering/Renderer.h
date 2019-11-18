/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =====================
#include <memory>
#include <vector>
#include <atomic>
#include <map>
#include <unordered_map>
#include "../Core/ISubsystem.h"
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Viewport.h"
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
#include "../Math/Rectangle.h"
//================================

namespace Spartan
{
    // Global properties
    static const int g_cascade_count = 4;

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
		Render_Debug_AABB				= 1 << 0,
		Render_Debug_PickingRay			= 1 << 1,
		Render_Debug_Grid				= 1 << 2,
		Render_Debug_Transform			= 1 << 3,
		Render_Debug_Lights				= 1 << 4,
		Render_Debug_PerformanceMetrics	= 1 << 5,
		Render_Debug_Physics			= 1 << 6,
        Render_Debug_Wireframe          = 1 << 7,
		Render_Bloom				    = 1 << 8,
        Render_VolumetricLighting       = 1 << 9,
		Render_AntiAliasing_FXAA	    = 1 << 10,
        Render_AntiAliasing_TAA         = 1 << 11,
		Render_SSAO					    = 1 << 12,
        Render_SSCS                     = 1 << 13,
		Render_SSR					    = 1 << 14,
		Render_MotionBlur			    = 1 << 15,
		Render_Sharpening_LumaSharpen	= 1 << 16,
		Render_ChromaticAberration	    = 1 << 17,
		Render_Dithering			    = 1 << 18,
        Render_ReverseZ                 = 1 << 19,
        Render_DepthPrepass             = 1 << 20
	};

    enum Renderer_Option_Value
    {
        Option_Value_Anisotropy,
        Option_Value_ShadowResolution,
        Option_Value_Tonemapping,
        Option_Value_Exposure,
        Option_Value_Gamma,
        Option_Value_Fxaa_Sub_Pixel,          // The amount of sub-pixel aliasing removal														- Algorithm's default: 0.75f
        Option_Value_Fxaa_Edge_Threshold,     // Edge detection threshold. The minimum amount of local contrast required to apply algorithm.    - Algorithm's default: 0.166f
        Option_Value_Fxaa_Edge_Threshold_Min, // Darkness threshold. Trims the algorithm from processing darks								    - Algorithm's default: 0.0833f
        Option_Value_Bloom_Intensity,
        Option_Value_Sharpen_Strength,
        Option_Value_Sharpen_Clamp,           // Limits maximum amount of sharpening a pixel receives - Algorithm's default: 0.035f
        Option_Value_Motion_Blur_Intensity,
        Option_Value_Ssao_Scale
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
		Shader_BlurGaussianBilateral_P
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
        RenderTarget_Light_Volumetric_Blurred,
        // Composition
        RenderTarget_Composition_Hdr,
        RenderTarget_Composition_Hdr_2,
        RenderTarget_Composition_Ldr,
        RenderTarget_Composition_Ldr_2,
        RenderTarget_Composition_Hdr_History,
        RenderTarget_Composition_Hdr_History_2,
        // SSAO
        RenderTarget_Ssao_Raw,
        RenderTarget_Ssao_Blurred,
        RenderTarget_Ssao,
        // SSR
        RenderTarget_Ssr,
        RenderTarget_Ssr_Blurred
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
        const std::shared_ptr<Entity>& SnapTransformGizmoTo(const std::shared_ptr<Entity>& entity) const;

		// Debug
		void SetDebugBuffer(const Renderer_Buffer_Type buffer)	{ m_debug_buffer = buffer; }
		auto GetDebugBuffer() const				                { return m_debug_buffer; }

        // Depth
        auto GetClearDepth()            { return GetOptionValue(Render_ReverseZ) ? m_viewport.depth_min : m_viewport.depth_max; }
        auto GetComparisonFunction()    { return GetOptionValue(Render_ReverseZ) ? RHI_Comparison_GreaterEqual : RHI_Comparison_LessEqual; }

        // Environment
        const std::shared_ptr<RHI_Texture>& GetEnvironmentTexture();
        void SetEnvironmentTexture(const std::shared_ptr<RHI_Texture>& texture);

        // Shadows
        void SetShadowResolution(uint32_t resolution);

        // Options
        auto GetOptions()                                       { return m_options; }
        void SetOptions(uint64_t flags)                         { m_options = flags; }
        bool GetOptionValue(Renderer_Option flag)                { return m_options & flag; }
        void SetOptionValue(Renderer_Option flag, bool enable)   { enable ? (m_options |= flag) : (m_options &= ~flag); }
        
        // Options values
        template<typename T>
        T GetOptionValue(Renderer_Option_Value option) { return static_cast<T>(m_option_values[option]); }
        void SetOptionValue(Renderer_Option_Value option, float value, float min = std::numeric_limits<float>::lowest(), float max = std::numeric_limits<float>::max());

        // Misc
        const auto& GetRhiDevice()		const { return m_rhi_device; }
        const auto& GetSwapChain()      const { return m_swap_chain; }
        const auto& GetPipelineCache()	const { return m_pipeline_cache; }
        const auto& GetCmdList()		const { return m_cmd_list; }
        const auto& GetFrameTexture()         { return m_render_targets[RenderTarget_Composition_Ldr]; }
        const auto GetFrameNum()        const { return m_frame_num; }
        const auto& GetCamera()         const { return m_camera; }
        const  auto IsInitialized()     const { return m_initialized; }
        auto& GetShaders()              const { return m_shaders; }
        const auto GetMaxResolution()   const { return m_max_resolution; }
        const auto IsRendering()        const { return m_is_rendering; }

        // Globals
        void SetShaderTransform(const Math::Matrix& transform) { m_buffer_uber_cpu.transform = transform; UpdateUberBuffer(); }
        void SetGlobalSamplersAndConstantBuffers();
        RHI_Texture* GetBlackTexture() { return m_tex_black.get(); }

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
		void Pass_Main();
		void Pass_LightDepth();
        void Pass_DepthPrePass();
		void Pass_GBuffer();
		void Pass_Ssao();
        void Pass_Ssr();
        void Pass_Light();
		void Pass_Composition();
		void Pass_PostProcess();
		void Pass_TAA(std::shared_ptr<RHI_Texture>& tex_in,						std::shared_ptr<RHI_Texture>& tex_out);
		bool Pass_DebugBuffer(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ToneMapping(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_GammaCorrection(std::shared_ptr<RHI_Texture>& tex_in,			std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_FXAA(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_LumaSharpen(std::shared_ptr<RHI_Texture>& tex_in,             std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ChromaticAberration(std::shared_ptr<RHI_Texture>& tex_in,		std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_MotionBlur(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Dithering(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Bloom(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Upsample(std::shared_ptr<RHI_Texture>& tex_in,                std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Downsample(std::shared_ptr<RHI_Texture>& tex_in,              std::shared_ptr<RHI_Texture>& tex_out, Renderer_Shader_Type pixel_shader);
		void Pass_BlurBox(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out, float sigma);
		void Pass_BlurGaussian(std::shared_ptr<RHI_Texture>& tex_in,			std::shared_ptr<RHI_Texture>& tex_out, float sigma, float pixel_stride = 1.0f);
		void Pass_BlurBilateralGaussian(std::shared_ptr<RHI_Texture>& tex_in,	std::shared_ptr<RHI_Texture>& tex_out, float sigma, float pixel_stride = 1.0f);
		void Pass_Lines(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Gizmos(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_PerformanceMetrics(std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_BrdfSpecularLut();
        void Pass_Copy(std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);

        // Misc
        bool UpdateFrameBuffer();
        bool UpdateUberBuffer();
        bool UpdateLightBuffer(const std::vector<Entity*>& entities);
        void RenderablesAcquire(const Variant& renderables);
        void RenderablesSort(std::vector<Entity*>* renderables);
        std::shared_ptr<RHI_RasterizerState>& GetRasterizerState(RHI_Cull_Mode cull_mode, RHI_Fill_Mode fill_mode);
        void ClearEntities() { m_entities.clear(); }

        // Render textures
        std::map<Renderer_RenderTarget_Type, std::shared_ptr<RHI_Texture>> m_render_targets;
        std::vector<std::shared_ptr<RHI_Texture>> m_render_tex_bloom;

        // Standard textures
        std::shared_ptr<RHI_Texture> m_tex_noise_normal;
        std::shared_ptr<RHI_Texture> m_tex_white;
        std::shared_ptr<RHI_Texture> m_tex_black;
        std::shared_ptr<RHI_Texture> m_gizmo_tex_light_directional;
        std::shared_ptr<RHI_Texture> m_gizmo_tex_light_point;
        std::shared_ptr<RHI_Texture> m_gizmo_tex_light_spot;

		// Shaders
		std::map<Renderer_Shader_Type, std::shared_ptr<RHI_Shader>> m_shaders;

		// Depth-stencil states
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_enabled_write;
        std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_enabled_no_write;
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_disabled;

        // Blend states
        std::shared_ptr<RHI_BlendState> m_blend_enabled;
        std::shared_ptr<RHI_BlendState> m_blend_disabled;
        std::shared_ptr<RHI_BlendState> m_blend_color_add;
        std::shared_ptr<RHI_BlendState> m_blend_bloom;

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
		uint32_t m_max_resolution	= 16384;

        // Options
        uint64_t m_options = 0;
        std::map<Renderer_Option_Value, float> m_option_values;

		//= CORE ======================================================
        std::shared_ptr<RHI_Device> m_rhi_device;
        std::shared_ptr<RHI_SwapChain> m_swap_chain;
        std::shared_ptr<RHI_PipelineCache> m_pipeline_cache;
        std::shared_ptr<RHI_CommandList> m_cmd_list;
		Math::Rectangle m_quad;
		std::unique_ptr<Font> m_font;
		Math::Vector2 m_taa_jitter;
		Math::Vector2 m_taa_jitter_previous;
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
		//=============================================================
                                                                      
		// Entities & Components
		std::unordered_map<Renderer_Object_Type, std::vector<Entity*>> m_entities;
		std::shared_ptr<Camera> m_camera;

        // Updates once every frame
        struct FrameBuffer
        {
            Math::Matrix view;
            Math::Matrix projection;
            Math::Matrix projection_ortho;
            Math::Matrix view_projection;
            Math::Matrix view_projection_inv;
            Math::Matrix view_projection_ortho;
            Math::Matrix view_projection_unjittered;

            float delta_time;
            float time;
            float camera_near;
            float camera_far;

            Math::Vector3 camera_position;
            float fxaa_sub_pixel;

            float fxaa_edge_threshold;
            float fxaa_edge_threshold_min;
            float bloom_intensity;
            float sharpen_strength;

            float sharpen_clamp;
            float motion_blur_strength;
            float gamma;
            float tonemapping;

            Math::Vector2 taa_jitter_offset;
            float exposure;
            float directional_light_intensity;

            float ssr_enabled;
            float shadow_resolution;
            float ssao_scale;
            float padding;
        };
        FrameBuffer m_buffer_frame_cpu;
        std::shared_ptr<RHI_ConstantBuffer> m_buffer_frame_gpu;

		// Updates multiple times per frame
		struct UberBuffer
		{
            Math::Matrix transform;
            Math::Matrix wvp_current;
            Math::Matrix wvp_previous;

            Math::Vector4 mat_albedo;

            Math::Vector2 mat_tiling_uv;
            Math::Vector2 mat_offset_uv;

            float mat_roughness_mul;
            float mat_metallic_mul;
            float mat_normal_mul;
            float mat_height_mul;

            float mat_shading_mode;
            Math::Vector3 padding;

            Math::Vector4 color;

            Math::Vector3 transform_axis;
            float blur_sigma;

            Math::Vector2 blur_direction;
            Math::Vector2 resolution;

            bool operator==(const UberBuffer& rhs)
            {
                return
                    transform           == rhs.transform            &&
                    wvp_current         == rhs.wvp_current          &&
                    wvp_previous        == rhs.wvp_previous         &&
                    mat_albedo          == rhs.mat_albedo           &&
                    mat_tiling_uv       == rhs.mat_tiling_uv        &&
                    mat_offset_uv       == rhs.mat_offset_uv        &&
                    mat_roughness_mul   == rhs.mat_roughness_mul    &&
                    mat_metallic_mul    == rhs.mat_metallic_mul     &&
                    mat_normal_mul      == rhs.mat_normal_mul       &&
                    mat_height_mul      == rhs.mat_height_mul       &&
                    mat_shading_mode    == rhs.mat_shading_mode     &&
                    color               == rhs.color                &&
                    transform_axis      == rhs.transform_axis       &&
                    blur_sigma          == rhs.blur_sigma           &&
                    blur_direction      == rhs.blur_direction       &&
                    resolution          == rhs.resolution;
            }
		};
        UberBuffer m_buffer_uber_cpu;
        UberBuffer m_buffer_uber_cpu_previous;
		std::shared_ptr<RHI_ConstantBuffer> m_buffer_uber_gpu;

        // Light buffer
        static const int g_max_lights = 100;
        struct LightBuffer
        {
            Math::Matrix view_projection[g_max_lights][g_cascade_count];
            Math::Vector4 intensity_range_angle_bias[g_max_lights];
            Math::Vector4 normalBias_shadow_volumetric_contact[g_max_lights];
            Math::Vector4 color[g_max_lights];
            Math::Vector4 position[g_max_lights];
            Math::Vector4 direction[g_max_lights];

            float light_count;
            Math::Vector3 g_padding2;

            bool operator==(const LightBuffer& rhs)
            {
                return
                    view_projection                         == rhs.view_projection                      &&
                    intensity_range_angle_bias              == rhs.intensity_range_angle_bias           &&
                    normalBias_shadow_volumetric_contact    == rhs.normalBias_shadow_volumetric_contact &&
                    color                                   == rhs.color                                &&
                    position                                == rhs.position                             &&
                    direction                               == rhs.direction                            &&
                    light_count                             == rhs.light_count;
            }
        };
        LightBuffer m_buffer_light_cpu;
        LightBuffer m_buffer_light_cpu_previous;
        std::shared_ptr<RHI_ConstantBuffer> m_buffer_light_gpu;

        // Dependencies
        Profiler* m_profiler            = nullptr;
        ResourceCache* m_resource_cache = nullptr;
    };
}
