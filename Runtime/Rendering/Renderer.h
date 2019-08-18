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
#include "../Core/Settings.h"
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Viewport.h"
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
#include "../Math/Rectangle.h"
//================================

namespace Spartan
{
	class Entity;
	class Camera;
	class Skybox;
	class Light;
	class ResourceCache;
	class Font;
	class Variant;
	class Grid;
	class Transform_Gizmo;
	class ShaderComposition;
	class ShaderBuffered;
	class Profiler;
	namespace Math
	{
		class BoundingBox;
		class Frustum;
	}

	enum Renderer_Option : uint32_t
	{
		Render_Gizmo_AABB						= 1 << 0,
		Render_Gizmo_PickingRay					= 1 << 1,
		Render_Gizmo_Grid						= 1 << 2,
		Render_Gizmo_Transform					= 1 << 3,
		Render_Gizmo_Lights						= 1 << 4,
		Render_Gizmo_PerformanceMetrics			= 1 << 5,
		Render_Gizmo_Physics					= 1 << 6,
		Render_PostProcess_Bloom				= 1 << 7,
        Render_PostProcess_VolumetricLighting   = 1 << 8,
		Render_PostProcess_FXAA					= 1 << 9,
		Render_PostProcess_SSAO					= 1 << 10,
        Render_PostProcess_SSCS                 = 1 << 11,
		Render_PostProcess_SSR					= 1 << 12,
		Render_PostProcess_TAA					= 1 << 13,
		Render_PostProcess_MotionBlur			= 1 << 14,
		Render_PostProcess_Sharpening			= 1 << 15,
		Render_PostProcess_ChromaticAberration	= 1 << 16,
		Render_PostProcess_Dithering			= 1 << 17
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

	enum Renderer_ToneMapping_Type
	{
		ToneMapping_Off,
		ToneMapping_ACES,
		ToneMapping_Reinhard,
		ToneMapping_Uncharted2
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
        Shader_Sharpen_Taa_P,
		Shader_ChromaticAberration_P,	
		Shader_BloomDownsampleLuminance_P,
        Shader_BloomDownsample_P,
		Shader_BloomBlend_P,
		Shader_ToneMapping_P,
		Shader_GammaCorrection_P,
		Shader_Dithering_P,
		Shader_Upsample_P,
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
		Shader_Color_Vp,
		Shader_Font_Vp,
		Shader_Ssao_P,
        Shader_Ssr_P,
		Shader_GizmoTransform_Vp,
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
        // Specular BRDF IBL
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
        RenderTarget_Ssao_Half,
        RenderTarget_Ssao_Half_Blurred,
        RenderTarget_Ssao,
        // SSR
        RenderTarget_Ssr
    };

	class SPARTAN_CLASS Renderer : public ISubsystem
	{
	public:
		Renderer(Context* context);
		~Renderer();

		//= Subsystem =======================
		bool Initialize() override;
		void Tick(float delta_time) override;
		//===================================

		//= LINE RENDERING ============================================================================================================================================================
		#define DebugColor Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)
		void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DebugColor, const Math::Vector4& color_to = DebugColor, bool depth = true);
		void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DebugColor, bool depth = true);
		//=============================================================================================================================================================================

		//= VIEWPORT & RESOLUTION ================================================
		const auto& GetViewport() const			        { return m_viewport; }
		void SetViewport(const RHI_Viewport& viewport)	{ m_viewport = viewport; }
		
        const auto& GetResolution() const { return m_resolution; }
        void SetResolution(uint32_t width, uint32_t height);

        Math::Vector2 viewport_editor_offset;
		//========================================================================

		//= EDITOR ======================================================================================
		float m_gizmo_transform_size = 0.015f;
		float m_gizmo_transform_speed = 12.0f;
        const std::shared_ptr<Entity>& SnapTransformGizmoTo(const std::shared_ptr<Entity>& entity) const;
		//===============================================================================================
		
		// DEBUG ===========================================================================
		void SetDebugBuffer(const Renderer_Buffer_Type buffer)	{ m_debug_buffer = buffer; }
		auto GetDebugBuffer() const				                { return m_debug_buffer; }
		//==================================================================================

		//= RHI INTERNALS ================================================
		const auto& GetRhiDevice()		const { return m_rhi_device; }
        const auto& GetSwapChain()      const { return m_swap_chain; }
		const auto& GetPipelineCache()	const { return m_pipeline_cache; }
		const auto& GetCmdList()		const { return m_cmd_list; }
		//================================================================

		//= MISC ===============================================================================================================
		auto& GetFrameTexture() 	                    { return m_render_targets[RenderTarget_Composition_Ldr]; }
		auto GetFrameNum() const		                { return m_frame_num; }
		const auto& GetCamera() const	                { return m_camera; }
		auto IsInitialized() const		                { return m_initialized; }	
        auto& GetShaders()                              { return m_shaders; }    
        auto GetMaxResolution() const                   { return m_max_resolution; } 
        auto IsRendering() const                        { return m_is_rendering; }
        auto GetReverseZ() const                        { return m_reverse_z; }
        auto GetClearDepth()                            { return m_reverse_z ? m_viewport.depth_min : m_viewport.depth_max; }
        auto GetComparisonFunction()                    { return m_reverse_z ? Comparison_GreaterEqual : Comparison_LessEqual; }
        auto GetShadowResolution()                      { return m_resolution_shadow; }
        void SetShadowResolution(uint32_t resolution);
        auto GetAnisotropy()                            { return m_anisotropy; }
        void SetAnisotropy(uint32_t anisotropy);
        auto GetFlags()                                 { return m_flags; }
        void EnableFlag(Renderer_Option flag)           { m_flags |= flag; };
        void DisableFlag(Renderer_Option flag)          { m_flags &= ~flag; };
        bool FlagEnabled(Renderer_Option flag)          { return m_flags & flag; }
		//======================================================================================================================

        //= Graphics Settings ====================================================================================================================================================
        Renderer_ToneMapping_Type m_tonemapping  = ToneMapping_Uncharted2;
        float m_exposure                = 1.5f;
        float m_gamma                   = 2.2f;
        // FXAA
        float m_fxaa_sub_pixel          = 1.25f;	// The amount of sub-pixel aliasing removal														- Algorithm's default: 0.75f
        float m_fxaa_edge_threshold     = 0.125f;	// Edge detection threshold. The minimum amount of local contrast required to apply algorithm.  - Algorithm's default: 0.166f
        float m_fxaa_edge_threshold_min = 0.0312f;	// Darkness threshold. Trims the algorithm from processing darks								- Algorithm's default: 0.0833f
        // Bloom
        float m_bloom_intensity         = 0.02f;	// The intensity of the bloom
        // Sharpening
        float m_sharpen_strength        = 1.0f;		// Strength of the sharpening
        float m_sharpen_clamp           = 0.35f;	// Limits maximum amount of sharpening a pixel receives											- Algorithm's default: 0.035f
        // Motion Blur
        float m_motion_blur_intensity   = 4.0f;		// Strength of the motion blur
        //========================================================================================================================================================================

	private:
        //= STARTUP CREATION ===========
		void CreateDepthStencilStates();
		void CreateRasterizerStates();
		void CreateBlendStates();
		void CreateFonts();
		void CreateTextures();
		void CreateShaders();
		void CreateSamplers();
		void CreateRenderTextures();
        //==============================

		//= PASSES ============================================================================================================================================
		void Pass_Main();
		void Pass_LightDepth();
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
		void Pass_TaaSharpen(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_LumaSharpen(std::shared_ptr<RHI_Texture>& tex_in,             std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ChromaticAberration(std::shared_ptr<RHI_Texture>& tex_in,		std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_MotionBlur(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Dithering(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Bloom(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_Upsample(std::shared_ptr<RHI_Texture>& tex_in,                std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_BlurBox(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out, float sigma);
		void Pass_BlurGaussian(std::shared_ptr<RHI_Texture>& tex_in,			std::shared_ptr<RHI_Texture>& tex_out, float sigma, float pixel_stride = 1.0f);
		void Pass_BlurBilateralGaussian(std::shared_ptr<RHI_Texture>& tex_in,	std::shared_ptr<RHI_Texture>& tex_out, float sigma, float pixel_stride = 1.0f);
		void Pass_Lines(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Gizmos(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_PerformanceMetrics(std::shared_ptr<RHI_Texture>& tex_out);
        void Pass_BrdfSpecularLut();
        void Pass_Copy(std::shared_ptr<RHI_Texture>& tex_in, std::shared_ptr<RHI_Texture>& tex_out);
		//=====================================================================================================================================================

        //= MISC =======================================================================================================================
        bool UpdateUberBuffer(uint32_t resolution_width, uint32_t resolution_height, const Math::Matrix& mMVP = Math::Matrix::Identity);
        void RenderablesAcquire(const Variant& renderables);
        void RenderablesSort(std::vector<Entity*>* renderables);
        std::shared_ptr<RHI_RasterizerState>& GetRasterizerState(RHI_Cull_Mode cull_mode, RHI_Fill_Mode fill_mode);
        //==============================================================================================================================

        //= RENDER TEXTURES ================================================================
        std::map<Renderer_RenderTarget_Type, std::shared_ptr<RHI_Texture>> m_render_targets;
        std::vector<std::shared_ptr<RHI_Texture>> m_render_tex_bloom;
        //==================================================================================

		//= SHADERS ==========================================================
		std::map<Renderer_Shader_Type, std::shared_ptr<RHI_Shader>> m_shaders;
		//====================================================================

		//= DEPTH-STENCIL STATES =======================================
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_enabled;
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_disabled;
		//==============================================================

        //= BLEND STATES =================================
        std::shared_ptr<RHI_BlendState> m_blend_enabled;
        std::shared_ptr<RHI_BlendState> m_blend_disabled;
        std::shared_ptr<RHI_BlendState> m_blend_color_add;
        std::shared_ptr<RHI_BlendState> m_blend_bloom;
        //================================================

		//= RASTERIZER STATES =================================================
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_front_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_none_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_back_wireframe;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_front_wireframe;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cull_none_wireframe;
		//=====================================================================

		//= SAMPLERS ===========================================
		std::shared_ptr<RHI_Sampler> m_sampler_compare_depth;
		std::shared_ptr<RHI_Sampler> m_sampler_point_clamp;
		std::shared_ptr<RHI_Sampler> m_sampler_bilinear_clamp;
		std::shared_ptr<RHI_Sampler> m_sampler_bilinear_wrap;
		std::shared_ptr<RHI_Sampler> m_sampler_trilinear_clamp;
		std::shared_ptr<RHI_Sampler> m_sampler_anisotropic_wrap;
		//======================================================

		//= STANDARD TEXTURES =====================================
		std::shared_ptr<RHI_Texture> m_tex_noise_normal;
		std::shared_ptr<RHI_Texture> m_tex_white;
		std::shared_ptr<RHI_Texture> m_tex_black;
		std::shared_ptr<RHI_Texture> m_gizmo_tex_light_directional;
		std::shared_ptr<RHI_Texture> m_gizmo_tex_light_point;
		std::shared_ptr<RHI_Texture> m_gizmo_tex_light_spot;
		//=========================================================

		//= LINE RENDERING ========================================
		std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer_lines;
		std::vector<RHI_Vertex_PosCol> m_lines_list_depth_enabled;
		std::vector<RHI_Vertex_PosCol> m_lines_list_depth_disabled;
		//=========================================================

		//= GIZMOS ========================================
		std::unique_ptr<Transform_Gizmo> m_gizmo_transform;
		std::unique_ptr<Grid> m_gizmo_grid;
		Math::Rectangle m_gizmo_light_rect;
		//=================================================

		//= RESOLUTION & VIEWPORT ===================================
		Math::Vector2 m_resolution	= Math::Vector2(1920, 1080);
		RHI_Viewport m_viewport		= RHI_Viewport(0, 0, 1920, 1080);
		uint32_t m_max_resolution	= 16384;
		//===========================================================

		//= CORE ==========================================================
		Math::Rectangle m_quad;
		std::shared_ptr<RHI_CommandList> m_cmd_list;
		std::unique_ptr<Font> m_font;	
		Math::Matrix m_view;
		Math::Matrix m_view_base;
		Math::Matrix m_projection;
		Math::Matrix m_projection_orthographic;
		Math::Matrix m_view_projection;
		Math::Matrix m_view_projection_inv;
		Math::Matrix m_view_projection_orthographic;
		Math::Vector2 m_taa_jitter;
		Math::Vector2 m_taa_jitter_previous;
		Renderer_Buffer_Type m_debug_buffer         = Renderer_Buffer_None;
		uint32_t m_flags                            = 0;
		bool m_initialized                          = false;
        bool m_reverse_z                            = true;
        uint32_t m_resolution_shadow                = 4096;
        uint32_t m_resolution_shadow_min            = 128;
        uint32_t m_anisotropy                       = 16;
        float m_near_plane                          = 0.0f;
        float m_far_plane                           = 0.0f;
        uint64_t m_frame_num                        = 0;
        bool m_is_odd_frame                         = false;
        bool m_is_rendering                         = false;
        bool m_brdf_specular_lut_rendered           = false;
        std::atomic<bool> m_acquiring_renderables   = false;
		//=================================================================

		//= RHI ============================================
		std::shared_ptr<RHI_Device> m_rhi_device;
        std::shared_ptr<RHI_SwapChain> m_swap_chain;
		std::shared_ptr<RHI_PipelineCache> m_pipeline_cache;
		//==================================================
                                                                                  
		//= ENTITIES/COMPONENTS ==================================================
		std::unordered_map<Renderer_Object_Type, std::vector<Entity*>> m_entities;
		std::shared_ptr<Camera> m_camera;                                         
		std::shared_ptr<Skybox> m_skybox;                                         
                       
		//========================================================================

		//= DEPENDENCIES =========================
		Profiler* m_profiler	        = nullptr;
        ResourceCache* m_resource_cache = nullptr;
		//========================================
		
		// Uber buffer (holds what is needed by almost every shader)
		struct UberBuffer
		{
			Math::Matrix m_mvp;
			Math::Matrix m_view;
			Math::Matrix m_projection;
			Math::Matrix m_projection_ortho;
			Math::Matrix m_view_projection;
			Math::Matrix m_view_projection_inv;
			Math::Matrix m_view_projection_ortho;

			float camera_near;
			float camera_far;
			Math::Vector2 resolution;

			Math::Vector3 camera_position;
			float fxaa_sub_pixel;

			float fxaa_edge_threshold;
			float fxaa_edge_threshold_min;
			float bloom_intensity;
			float sharpen_strength;

			float sharpen_clamp;
			float motion_blur_strength;
			float fps_current;
			float fps_target;

			float gamma;
			Math::Vector2 taa_jitter_offset;
			float tonemapping;

			float exposure;
			float directional_light_intensity;
            float ssr_enabled;
            float shadow_resolution;
		};
		std::shared_ptr<RHI_ConstantBuffer> m_uber_buffer;
	};
}
