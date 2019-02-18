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
#include "../RHI/RHI_Definition.h"
#include "../RHI/RHI_Pipeline.h"
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
#include "../Math/Rectangle.h"
#include "../Core/Settings.h"
//================================

namespace Directus
{
	class Entity;
	class Camera;
	class Skybox;
	class Light;
	class GBuffer;
	class LightShader;
	class ResourceCache;
	class Font;
	class Variant;
	class Grid;
	class Transform_Gizmo;
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

	class ENGINE_CLASS Renderer : public ISubsystem
	{
	public:
		Renderer(Context* context);
		~Renderer();

		//= Subsystem =============
		bool Initialize() override;
		void Tick() override;
		//=========================

		//= RENDER MODE ======================================================
		// Enables an render mode flag
		void Flags_Enable(Renderer_Option flag)		{ m_flags |= flag; }
		// Removes an render mode flag
		void Flags_Disable(Renderer_Option flag)	{ m_flags &= ~flag; }
		// Returns whether render mode flag is set
		bool Flags_IsSet(Renderer_Option flag)		{ return m_flags & flag; }
		//====================================================================

		//= LINE RENDERING ============================================================================================================================================================
		#define DebugColor Math::Vector4(0.41f, 0.86f, 1.0f, 1.0f)
		void DrawLine(const Math::Vector3& from, const Math::Vector3& to, const Math::Vector4& color_from = DebugColor, const Math::Vector4& color_to = DebugColor, bool depth = true);
		void DrawBox(const Math::BoundingBox& box, const Math::Vector4& color = DebugColor, bool depth = true);
		//=============================================================================================================================================================================

		//= SWAPCHAIN =================================================
		bool SwapChain_Resize(unsigned int width, unsigned int height);
		bool SwapChain_SetAsRenderTarget();
		bool SwapChain_Clear(const Math::Vector4& color);
		bool SwapChain_Present();
		//=============================================================

		//= VIEWPORT - INTERNAL ==================================================
		const RHI_Viewport& GetViewport()				{ return m_viewport; }
		void SetViewport(const RHI_Viewport& viewport)	{ m_viewport = viewport; }
		Math::Vector2 viewport_editorOffset;
		//========================================================================

		//= RESOLUTION - INTERNAL ===================================
		const Math::Vector2& GetResolution() { return m_resolution; }
		void SetResolution(unsigned int width, unsigned int height);
		//===========================================================

		//= MISC ==========================================================================
		void* GetFrameShaderResource();
		const std::shared_ptr<RHI_Device>& GetRHIDevice() { return m_rhiDevice; }
		const std::shared_ptr<RHI_Pipeline>& GetRHIPipeline() { return m_rhiPipeline; }
		static bool IsRendering() { return m_isRendering; }
		uint64_t GetFrameNum() { return m_frameNum; }
		std::shared_ptr<Camera> GetCamera() { return m_camera; }
		unsigned int GetMaxResolution() { return m_maxResolution; }
		//=================================================================================

		//= Graphics Settings ====================================================================================================================================================
		ToneMapping_Type m_tonemapping	= ToneMapping_ACES;
		float m_gamma					= 2.2f;
		// FXAA
		float m_fxaaSubPixel			= 1.25f;	// The amount of sub-pixel aliasing removal														- Algorithm's default: 0.75f
		float m_fxaaEdgeThreshold		= 0.125f;	// Edge detection threshold. The minimum amount of local contrast required to apply algorithm.  - Algorithm's default: 0.166f
		float m_fxaaEdgeThresholdMin	= 0.0312f;	// Darkness threshold. Trims the algorithm from processing darks								- Algorithm's default: 0.0833f
		// Bloom
		float m_bloomIntensity			= 0.02f;	// The intensity of the bloom
		// Sharpening
		float m_sharpenStrength			= 1.0f;		// Strength of the sharpening
		float m_sharpenClamp			= 0.35f;	// Limits maximum amount of sharpening a pixel receives											- Algorithm's default: 0.035f
		// Motion Blur
		float m_motionBlurStrength		= 2.0f;		// Strength of the motion blur
		//========================================================================================================================================================================

		//= Gizmo Settings ======================
		float m_gizmo_transform_size	= 0.015f;
		float m_gizmo_transform_speed	= 12.0f;
		//=======================================
		
		// DEBUG BUFFER ===============================================================
		void SetDebugBuffer(RendererDebug_Buffer buffer)	{ m_debugBuffer = buffer; }
		RendererDebug_Buffer GetDebugBuffer()				{ return m_debugBuffer; }
		//=============================================================================

	private:
		void CreateDepthStencilStates();
		void CreateRasterizerStates();
		void CreateBlendStates();
		void CreateFonts();
		void CreateTextures();
		void CreateShaders();
		void CreateSamplers();
		void CreateRenderTextures();
		void SetDefault_Pipeline_State();
		void SetDefault_Buffer(	
			unsigned int resolutionWidth,
			unsigned int resolutionHeight,
			const Math::Matrix& mMVP			= Math::Matrix::Identity,
			float blur_sigma					= 0.0f,
			const Math::Vector2& blur_direction	= Math::Vector2::Zero
		);
		void Renderables_Acquire(const Variant& renderables);
		void Renderables_Sort(std::vector<Entity*>* renderables);
		std::shared_ptr<RHI_RasterizerState>& GetRasterizerState(RHI_Cull_Mode cullMode, RHI_Fill_Mode fillMode);

		//= PASSES ==============================================================================================================================================
		void Pass_DepthDirectionalLight(Light* directionalLight);
		void Pass_GBuffer();
		void Pass_PreLight(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut, std::shared_ptr<RHI_RenderTexture>& texOut2);
		void Pass_Light(std::shared_ptr<RHI_RenderTexture>& texShadows, std::shared_ptr<RHI_RenderTexture>& texSSAO, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_PostLight(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_TAA(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_Transparent(std::shared_ptr<RHI_RenderTexture>& texOut);
		bool Pass_DebugBuffer(std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_ToneMapping(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_GammaCorrection(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_FXAA(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_Sharpening(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_ChromaticAberration(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_MotionBlur(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_Dithering(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_Bloom(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_BlurBox(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut, float sigma);
		void Pass_BlurGaussian(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut, float sigma);
		void Pass_BlurBilateralGaussian(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut, float sigma, float pixelStride);
		void Pass_SSAO(std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_ShadowMapping(std::shared_ptr<RHI_RenderTexture>& texOut, Light* inDirectionalLight);
		void Pass_Lines(std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_Gizmos(std::shared_ptr<RHI_RenderTexture>& texOut);
		void Pass_PerformanceMetrics(std::shared_ptr<RHI_RenderTexture>& texOut);
		//=======================================================================================================================================================

		//= RESOLUTION & VIEWPORT =======================================
		Math::Vector2 m_resolution		= Math::Vector2(1920, 1080);
		RHI_Viewport m_viewport			= RHI_Viewport(0, 0, 1920, 1080);
		unsigned int m_maxResolution	= 16384;
		//===============================================================

		//= RENDER TEXTURES ===========================================
		// 1/1
		std::shared_ptr<RHI_RenderTexture> m_renderTexFull_HDR_Light;
		std::shared_ptr<RHI_RenderTexture> m_renderTexFull_TAA_Current;
		std::shared_ptr<RHI_RenderTexture> m_renderTexFull_TAA_History;
		std::shared_ptr<RHI_RenderTexture> m_renderTexFull_HDR_Light2;
		// 1/2
		std::shared_ptr<RHI_RenderTexture> m_renderTexHalf_Shadows;
		std::shared_ptr<RHI_RenderTexture> m_renderTexHalf_SSAO;
		std::shared_ptr<RHI_RenderTexture> m_renderTexHalf_Spare;
		// 1/4
		std::shared_ptr<RHI_RenderTexture> m_renderTexQuarter_Blur1;
		std::shared_ptr<RHI_RenderTexture> m_renderTexQuarter_Blur2;
		//=============================================================

		//= SHADERS ===========================================
		std::shared_ptr<RHI_Shader> m_vs_gbuffer;
		std::shared_ptr<LightShader> m_vps_light;
		std::shared_ptr<RHI_Shader> m_vps_depth;
		std::shared_ptr<RHI_Shader> m_vps_color;
		std::shared_ptr<RHI_Shader> m_vps_font;
		std::shared_ptr<RHI_Shader> m_vps_shadowMapping;
		std::shared_ptr<RHI_Shader> m_vps_ssao;
		std::shared_ptr<RHI_Shader> m_vps_gizmoTransform;
		std::shared_ptr<RHI_Shader> m_vps_transparent;
		std::shared_ptr<RHI_Shader> m_vs_quad;
		std::shared_ptr<RHI_Shader> m_ps_texture;
		std::shared_ptr<RHI_Shader> m_ps_fxaa;
		std::shared_ptr<RHI_Shader> m_ps_luma;	
		std::shared_ptr<RHI_Shader> m_ps_taa;
		std::shared_ptr<RHI_Shader> m_ps_motionBlur;
		std::shared_ptr<RHI_Shader> m_ps_sharpening;
		std::shared_ptr<RHI_Shader> m_ps_chromaticAberration;
		std::shared_ptr<RHI_Shader> m_ps_blurBox;
		std::shared_ptr<RHI_Shader> m_ps_blurGaussian;
		std::shared_ptr<RHI_Shader> m_ps_blurGaussianBilateral;
		std::shared_ptr<RHI_Shader> m_ps_bloomBright;
		std::shared_ptr<RHI_Shader> m_ps_bloomBlend;
		std::shared_ptr<RHI_Shader> m_ps_toneMapping;
		std::shared_ptr<RHI_Shader> m_ps_gammaCorrection;
		std::shared_ptr<RHI_Shader> m_ps_dithering;
		std::shared_ptr<RHI_Shader> m_ps_debugNormal;
		std::shared_ptr<RHI_Shader> m_ps_debugVelocity;
		std::shared_ptr<RHI_Shader> m_ps_debugDepth;
		std::shared_ptr<RHI_Shader> m_ps_debugSSAO;
		//=====================================================

		//= DEPTH-STENCIL STATES ======================================
		std::shared_ptr<RHI_DepthStencilState> m_depthStencil_enabled;
		std::shared_ptr<RHI_DepthStencilState> m_depthStencil_disabled;
		//=============================================================

		//= BLEND STATES ================================
		std::shared_ptr<RHI_BlendState> m_blend_enabled;
		std::shared_ptr<RHI_BlendState> m_blend_disabled;
		//===============================================

		//= RASTERIZER STATES ================================================
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cullBack_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cullFront_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cullNone_solid;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cullBack_wireframe;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cullFront_wireframe;
		std::shared_ptr<RHI_RasterizerState> m_rasterizer_cullNone_wireframe;
		//====================================================================

		//= SAMPLERS =========================================
		std::shared_ptr<RHI_Sampler> m_samplerCompareDepth;
		std::shared_ptr<RHI_Sampler> m_samplerPointClamp;
		std::shared_ptr<RHI_Sampler> m_samplerBilinearClamp;
		std::shared_ptr<RHI_Sampler> m_samplerBilinearWrap;
		std::shared_ptr<RHI_Sampler> m_samplerTrilinearClamp;
		std::shared_ptr<RHI_Sampler> m_samplerAnisotropicWrap;
		//====================================================

		//= STANDARD TEXTURES ==================================
		std::shared_ptr<RHI_Texture> m_texNoiseNormal;
		std::shared_ptr<RHI_Texture> m_texWhite;
		std::shared_ptr<RHI_Texture> m_texBlack;
		std::shared_ptr<RHI_Texture> m_texLUT_IBL;
		std::shared_ptr<RHI_Texture> m_gizmoTexLightDirectional;
		std::shared_ptr<RHI_Texture> m_gizmoTexLightPoint;
		std::shared_ptr<RHI_Texture> m_gizmoTexLightSpot;
		//======================================================

		//= LINE RENDERING ======================================
		std::shared_ptr<RHI_VertexBuffer> m_vertexBufferLines;
		std::vector<RHI_Vertex_PosCol> m_linesList_depthEnabled;
		std::vector<RHI_Vertex_PosCol> m_linesList_depthDisabled;
		//=======================================================

		//= EDITOR =======================================
		std::unique_ptr<Transform_Gizmo> m_transformGizmo;
		std::unique_ptr<Grid> m_grid;
		Math::Rectangle m_gizmoRectLight;
		//================================================

		//= CORE ===================================
		std::shared_ptr<RHI_Device> m_rhiDevice;
		std::shared_ptr<RHI_Pipeline> m_rhiPipeline;
		std::unique_ptr<RHI_SwapChain> m_swapChain;
		//==========================================

		//= MISC ========================================================
		Light* GetLightDirectional();
		std::unique_ptr<GBuffer> m_gbuffer;	
		Math::Rectangle m_quad;
		std::unordered_map<RenderableType, std::vector<Entity*>> m_entities;
		Math::Matrix m_view;
		Math::Matrix m_viewBase;
		Math::Matrix m_projection;
		Math::Matrix m_projectionOrthographic;
		Math::Matrix m_viewProjection;
		Math::Matrix m_viewProjection_Orthographic;
		float m_nearPlane;
		float m_farPlane;
		std::shared_ptr<Camera> m_camera;
		std::shared_ptr<Skybox> m_skybox;
		static bool m_isRendering;
		std::unique_ptr<Font> m_font;	
		unsigned long m_flags;
		uint64_t m_frameNum;
		bool m_isOddFrame;
		Math::Vector2 m_taa_jitter;
		Math::Vector2 m_taa_jitterPrevious;
		Profiler* m_profiler;
		RendererDebug_Buffer m_debugBuffer = RendererDebug_None;
		//===============================================================
		
		// Global buffer (holds what is needed by almost every shader)
		struct ConstantBuffer_Global
		{
			Math::Matrix mMVP;
			Math::Matrix mView;
			Math::Matrix mProjection;
			Math::Matrix mProjectionOrtho;
			Math::Matrix mViewProjection;
			Math::Matrix mViewProjectionOrtho;

			float camera_near;
			float camera_far;
			Math::Vector2 resolution;

			Math::Vector3 camera_position;
			float fxaa_subPixel;

			float fxaa_edgeThreshold;
			float fxaa_edgeThresholdMin;
			Math::Vector2 blur_direction;

			float blur_sigma;
			float bloom_intensity;
			float sharpen_strength;
			float sharpen_clamp;

			float motionBlur_strength;
			float fps_current;
			float fps_target;
			float gamma;

			Math::Vector2 taa_jitterOffset;
			float tonemapping;
			float padding;
		};
		std::shared_ptr<RHI_ConstantBuffer> m_bufferGlobal;
	};
}