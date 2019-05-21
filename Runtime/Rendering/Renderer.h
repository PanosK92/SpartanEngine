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
#include <unordered_map>
#include "../Core/ISubsystem.h"
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
#include "../Math/Rectangle.h"
#include "../Core/Settings.h"
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Viewport.h"
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
	class ShaderLight;
	class ShaderBuffered;
	class Profiler;

	namespace Math
	{
		class BoundingBox;
		class Frustum;
	}

	enum Renderer_Option : unsigned long
	{
		Render_Gizmo_AABB						= 1UL << 0,
		Render_Gizmo_PickingRay					= 1UL << 1,
		Render_Gizmo_Grid						= 1UL << 2,
		Render_Gizmo_Transform					= 1UL << 3,
		Render_Gizmo_Lights						= 1UL << 4,
		Render_Gizmo_PerformanceMetrics			= 1UL << 5,
		Render_Gizmo_Physics					= 1UL << 6,
		Render_PostProcess_Bloom				= 1UL << 7,
		Render_PostProcess_FXAA					= 1UL << 8,
		Render_PostProcess_SSAO					= 1UL << 9,
		Render_PostProcess_SSR					= 1UL << 10,
		Render_PostProcess_TAA					= 1UL << 11,
		Render_PostProcess_MotionBlur			= 1UL << 12,
		Render_PostProcess_Sharpening			= 1UL << 13,
		Render_PostProcess_ChromaticAberration	= 1UL << 14,
		Render_PostProcess_Dithering			= 1UL << 15
	};

	enum RendererDebug_Buffer
	{
		RendererDebug_None,
		RendererDebug_Albedo,
		RendererDebug_Normal,
		RendererDebug_Material,
		RendererDebug_Velocity,
		RendererDebug_Depth,
		RendererDebug_SSAO
	};

	enum ToneMapping_Type
	{
		ToneMapping_Off,
		ToneMapping_ACES,
		ToneMapping_Reinhard,
		ToneMapping_Uncharted2
	};

	enum RenderableType
	{
		Renderable_ObjectOpaque,
		Renderable_ObjectTransparent,
		Renderable_Light,
		Renderable_Camera
	};

	class SPARTAN_CLASS Renderer : public ISubsystem
	{
	public:
		Renderer(Context* context);
		~Renderer();

		//= Subsystem =============
		bool Initialize() override;
		void Tick() override;
		//=========================

		//= RENDER MODE ==============================================================
		// Enables an render mode flag
		void Flags_Enable(const Renderer_Option flag)		{ m_flags |= flag; }
		// Removes an render mode flag
		void Flags_Disable(const Renderer_Option flag)		{ m_flags &= ~flag; }
		// Returns whether render mode flag is set
		bool Flags_IsSet(const Renderer_Option flag) const	{ return m_flags & flag; }
		//============================================================================

		//= LINE RENDERING ============================================================================================================================================================
		#define DebugColor Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)
		void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DebugColor, const Math::Vector4& color_to = DebugColor, bool depth = true);
		void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DebugColor, bool depth = true);
		//=============================================================================================================================================================================

		//= VIEWPORT - INTERNAL ==================================================
		const RHI_Viewport& GetViewport() const			{ return m_viewport; }
		void SetViewport(const RHI_Viewport& viewport)	{ m_viewport = viewport; }
		Math::Vector2 viewport_editor_offset;
		//========================================================================

		//= RESOLUTION - INTERNAL =========================================
		const Math::Vector2& GetResolution() const { return m_resolution; }
		void SetResolution(uint32_t width, uint32_t height);
		//=================================================================

		//= Graphics Settings ====================================================================================================================================================
		ToneMapping_Type m_tonemapping	= ToneMapping_ACES;
		float m_exposure				= 1.0f;
		float m_gamma					= 2.2f;
		// FXAA
		float m_fxaa_sub_pixel			= 1.25f;	// The amount of sub-pixel aliasing removal														- Algorithm's default: 0.75f
		float m_fxaa_edge_threshold		= 0.125f;	// Edge detection threshold. The minimum amount of local contrast required to apply algorithm.  - Algorithm's default: 0.166f
		float m_fxaa_edge_threshold_min	= 0.0312f;	// Darkness threshold. Trims the algorithm from processing darks								- Algorithm's default: 0.0833f
		// Bloom
		float m_bloom_intensity			= 1.0f;		// The intensity of the bloom
		// Sharpening
		float m_sharpen_strength		= 1.0f;		// Strength of the sharpening
		float m_sharpen_clamp			= 0.35f;	// Limits maximum amount of sharpening a pixel receives											- Algorithm's default: 0.035f
		// Motion Blur
		float m_motion_blur_strength	= 2.0f;		// Strength of the motion blur
		//========================================================================================================================================================================

		//= EDITOR ================================================================================
		// Transform gizmo
		float m_gizmo_transform_size = 0.015f;
		float m_gizmo_transform_speed = 12.0f;
		std::shared_ptr<Entity>& SnapTransformGizmoTo(const std::shared_ptr<Entity>& entity) const;
		//=========================================================================================
		
		// DEBUG ===========================================================================
		void SetDebugBuffer(const RendererDebug_Buffer buffer)	{ m_debug_buffer = buffer; }
		RendererDebug_Buffer GetDebugBuffer() const				{ return m_debug_buffer; }
		//==================================================================================

		//= RHI INTERNALS ================================================
		const auto& GetRhiDevice()		const { return m_rhi_device; }
		const auto& GetPipelineCache()	const { return m_pipeline_cache; }
		const auto& GetCmdList()		const { return m_cmd_list; }
		//================================================================

		//= MISC ===============================================================================================================
		auto GetFrameTexture() const	{ return m_render_tex_full_hdr_light2.get(); }
		static auto IsRendering()		{ return m_is_rendering; }
		auto GetFrameNum() const		{ return m_frame_num; }
		const auto& GetCamera() const	{ return m_camera; }
		auto GetMaxResolution() const	{ return m_max_resolution; }
		auto IsInitialized() const		{ return m_initialized; }
		auto GetClearDepth()			{ return !Settings::Get().GetReverseZ() ? m_viewport.depth_max : m_viewport.depth_min; }
		//======================================================================================================================

	private:
		void CreateDepthStencilStates();
		void CreateRasterizerStates();
		void CreateBlendStates();
		void CreateFonts();
		void CreateTextures();
		void CreateShaders();
		void CreateSamplers();
		void CreateRenderTextures();
		void SetDefaultBuffer(uint32_t resolution_width, uint32_t resolution_height, const Math::Matrix& mMVP = Math::Matrix::Identity) const;
		void RenderablesAcquire(const Variant& renderables);
		void RenderablesSort(std::vector<Entity*>* renderables);
		std::shared_ptr<RHI_RasterizerState>& GetRasterizerState(RHI_Cull_Mode cull_mode, RHI_Fill_Mode fill_mode);

		//= PASSES =========================================================================================================================================================
		void Pass_Main();
		void Pass_LightDepth();
		void Pass_GBuffer();
		void Pass_PreLight(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_shadows_out,	std::shared_ptr<RHI_Texture>& tex_ssao_out);
		void Pass_Light(std::shared_ptr<RHI_Texture>& tex_shadows,				std::shared_ptr<RHI_Texture>& tex_ssao,			std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_PostLight(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_TAA(std::shared_ptr<RHI_Texture>& tex_in,						std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Transparent(std::shared_ptr<RHI_Texture>& tex_out);
		bool Pass_DebugBuffer(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ToneMapping(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_GammaCorrection(std::shared_ptr<RHI_Texture>& tex_in,			std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_FXAA(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Sharpening(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ChromaticAberration(std::shared_ptr<RHI_Texture>& tex_in,		std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_MotionBlur(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Dithering(std::shared_ptr<RHI_Texture>& tex_in,				std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Bloom(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_BlurBox(std::shared_ptr<RHI_Texture>& tex_in,					std::shared_ptr<RHI_Texture>& tex_out, float sigma);
		void Pass_BlurGaussian(std::shared_ptr<RHI_Texture>& tex_in,			std::shared_ptr<RHI_Texture>& tex_out, float sigma, float pixel_stride = 1.0f);
		void Pass_BlurBilateralGaussian(std::shared_ptr<RHI_Texture>& tex_in,	std::shared_ptr<RHI_Texture>& tex_out, float sigma, float pixel_stride = 1.0f);
		void Pass_SSAO(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_ShadowMapping(std::shared_ptr<RHI_Texture>& tex_out, Light* light_directional_in);
		void Pass_Lines(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_Gizmos(std::shared_ptr<RHI_Texture>& tex_out);
		void Pass_PerformanceMetrics(std::shared_ptr<RHI_Texture>& tex_out) const;
		//==================================================================================================================================================================

		//= RENDER TEXTURES =======================================
		// G-Buffer
		std::shared_ptr<RHI_Texture> m_g_buffer_albedo;
		std::shared_ptr<RHI_Texture> m_g_buffer_normal;
		std::shared_ptr<RHI_Texture> m_g_buffer_material;
		std::shared_ptr<RHI_Texture> m_g_buffer_velocity;
		std::shared_ptr<RHI_Texture> m_g_buffer_depth;
		// 1/1
		std::shared_ptr<RHI_Texture> m_render_tex_full_hdr_light;
		std::shared_ptr<RHI_Texture> m_render_tex_full_hdr_light2;
		std::shared_ptr<RHI_Texture> m_render_tex_full_taa_current;
		std::shared_ptr<RHI_Texture> m_render_tex_full_taa_history;
		std::shared_ptr<RHI_Texture> m_render_tex_full_spare;
		// 1/2
		std::shared_ptr<RHI_Texture> m_render_tex_half_shadows;
		std::shared_ptr<RHI_Texture> m_render_tex_half_ssao;
		std::shared_ptr<RHI_Texture> m_render_tex_half_spare;
		std::shared_ptr<RHI_Texture> m_render_tex_half_spare2;
		// 1/4
		std::shared_ptr<RHI_Texture> m_render_tex_quarter_blur1;
		std::shared_ptr<RHI_Texture> m_render_tex_quarter_blur2;
		//=========================================================
		
		//= SHADERS =====================================================
		std::shared_ptr<RHI_Shader> m_vs_gbuffer;
		std::shared_ptr<ShaderLight> m_vps_light;		
		std::shared_ptr<RHI_Shader> m_v_depth;
		std::shared_ptr<ShaderBuffered> m_vps_color;
		std::shared_ptr<ShaderBuffered> m_vps_font;
		std::shared_ptr<ShaderBuffered> m_vps_shadow_mapping_directional;
		std::shared_ptr<ShaderBuffered> m_ps_shadow_mapping_point;
		std::shared_ptr<ShaderBuffered> m_ps_shadow_mapping_spot;
		std::shared_ptr<ShaderBuffered> m_vps_ssao;
		std::shared_ptr<ShaderBuffered> m_vps_gizmo_transform;
		std::shared_ptr<ShaderBuffered> m_vps_transparent;	
		std::shared_ptr<RHI_Shader> m_vs_quad;
		std::shared_ptr<RHI_Shader> m_ps_texture;
		std::shared_ptr<RHI_Shader> m_ps_fxaa;
		std::shared_ptr<RHI_Shader> m_ps_luma;	
		std::shared_ptr<RHI_Shader> m_ps_taa;
		std::shared_ptr<RHI_Shader> m_ps_motion_blur;
		std::shared_ptr<RHI_Shader> m_ps_sharpening;
		std::shared_ptr<RHI_Shader> m_ps_chromatic_aberration;
		std::shared_ptr<RHI_Shader> m_ps_blur_box;
		std::shared_ptr<ShaderBuffered> m_ps_blur_gaussian;
		std::shared_ptr<ShaderBuffered> m_ps_blur_gaussian_bilateral;
		std::shared_ptr<RHI_Shader> m_ps_bloom_bright;
		std::shared_ptr<RHI_Shader> m_ps_bloom_blend;
		std::shared_ptr<RHI_Shader> m_ps_tone_mapping;
		std::shared_ptr<RHI_Shader> m_ps_gamma_correction;
		std::shared_ptr<RHI_Shader> m_ps_dithering;
		std::shared_ptr<RHI_Shader> m_ps_downsample_box;
		std::shared_ptr<RHI_Shader> m_ps_upsample_box;
		std::shared_ptr<RHI_Shader> m_ps_debug_normal_;
		std::shared_ptr<RHI_Shader> m_ps_debug_velocity;
		std::shared_ptr<RHI_Shader> m_ps_debug_depth;
		std::shared_ptr<RHI_Shader> m_ps_debug_ssao;
		//===============================================================

		//= DEPTH-STENCIL STATES =======================================
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_enabled;
		std::shared_ptr<RHI_DepthStencilState> m_depth_stencil_disabled;
		//==============================================================

		//= BLEND STATES ===================================
		std::shared_ptr<RHI_BlendState> m_blend_enabled;
		std::shared_ptr<RHI_BlendState> m_blend_disabled;
		std::shared_ptr<RHI_BlendState> m_blend_shadow_maps;
		//==================================================

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

		//= STANDARD TEXTURES =======================================
		std::shared_ptr<RHI_Texture> m_tex_noise_normal;
		std::shared_ptr<RHI_Texture> m_tex_white;
		std::shared_ptr<RHI_Texture> m_tex_black;
		std::shared_ptr<RHI_Texture> m_tex_lut_ibl;
		std::shared_ptr<RHI_Texture> m_gizmo_tex_light_directional;
		std::shared_ptr<RHI_Texture> m_gizmo_tex_light_point;
		std::shared_ptr<RHI_Texture> m_gizmo_tex_light_spot;
		//===========================================================

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

		//= CORE ================================================
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
		RendererDebug_Buffer m_debug_buffer = RendererDebug_None;
		unsigned long m_flags = 0;
		bool m_initialized = false;
		//=======================================================

		//= RHI ============================================
		std::shared_ptr<RHI_Device> m_rhi_device;
		std::shared_ptr<RHI_PipelineCache> m_pipeline_cache;
		//==================================================

		//= ENTITIES/COMPONENTS ============================================
		std::unordered_map<RenderableType, std::vector<Entity*>> m_entities;
		float m_near_plane;
		float m_far_plane;
		std::shared_ptr<Camera> m_camera;
		std::shared_ptr<Skybox> m_skybox;
		Math::Vector3 m_directional_light_avg_dir;
		//==================================================================

		//= STATS/PROFILING ==============
		Profiler* m_profiler	= nullptr;
		uint64_t m_frame_num	= 0;
		bool m_is_odd_frame		= false;
		static bool m_is_rendering;
		//================================
		
		// Global buffer (holds what is needed by almost every shader)
		struct ConstantBufferGlobal
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
			Math::Vector3 padding;
		};
		std::shared_ptr<RHI_ConstantBuffer> m_buffer_global;
	};
}