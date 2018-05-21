/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ===========================
#include <memory>
#include <vector>
#include "../Math/Matrix.h"
#include "../Core/Settings.h"
#include "../Core/SubSystem.h"
#include "../Core/Backends_Def.h"
#include "../Resource/ResourceManager.h"
//======================================

namespace Directus
{
	class GameObject;
	class Camera;
	class Skybox;
	class LineRenderer;
	class Light;
	class GBuffer;
	class Rectangle;
	class DeferredShader;
	class Shader;
	class Texture;
	class ResourceManager;
	class D3D11RenderTexture;
	class D3D11Graphics;
	class Font;
	class Grid;
	class Variant;	

	namespace Math
	{
		class Frustum;
	}

	enum RenderMode : unsigned long
	{
		Render_Albedo				= 1UL << 0,
		Render_Normal				= 1UL << 1,
		Render_Specular				= 1UL << 2,
		Render_Depth				= 1UL << 3,	
		Render_Physics				= 1UL << 4,
		Render_AABB					= 1UL << 5,
		Render_PickingRay			= 1UL << 6,
		Render_SceneGrid			= 1UL << 7,
		Render_PerformanceMetrics	= 1UL << 8,
		Render_Light				= 1UL << 9
	};

	class ENGINE_CLASS Renderer : public Subsystem
	{
	public:
		Renderer(Context* context);
		~Renderer();

		//= Subsystem ============
		bool Initialize() override;
		//========================

		// Rendering
		void SetRenderTarget(void* renderTarget, bool clear = true);
		void SetRenderTarget(const std::shared_ptr<D3D11RenderTexture>& renderTexture);
		void* GetFrame();
		void Present();
		void Render();

		// The back-buffer is the final output (should match the display size)
		void SetBackBufferSize(int width, int height);
		const Viewport& GetViewportBackBuffer();

		// The actual frame that all rendering takes palce (or the viewport window in the editor)
		void SetResolutionInternal(int width, int height);
		const Math::Vector2& GetViewportInternal();

		//= RENDER MODE ======================================================================
		// Returns all render mode flags
		static unsigned long RenderMode_GetAll()					{ return m_flags; }
		// Set's all render mode flags
		static void RenderMode_SetAll(unsigned long renderFlags)	{ m_flags = renderFlags; }
		// Enables an render mode flag
		static void RenderMode_Enable(RenderMode flag)				{ m_flags |= flag; }
		// Removes an render mode flag
		static void RenderMode_Disable(RenderMode flag)				{ m_flags &= ~flag; }
		// Returns whether render mode flag is set
		static bool RenderMode_IsSet(RenderMode flag)				{ return m_flags & flag; }
		//====================================================================================

		void Clear();
		const std::vector<GameObject*>& GetRenderables() { return m_renderables; }
		int GetRendereredMeshes() { return m_renderedMeshesCount; }

	private:	
		void Pass_RenderableAcquisition(const Variant& renderables);
		void Pass_DepthDirectionalLight(Light* directionalLight);
		void Pass_GBuffer();
		void Pass_PreDeferred(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, void* inRenderTexure, void* outRenderTextureShadowing);
		void Pass_Deferred(void* inTextureShadowing, void* outRenderTexture);	
		void Pass_PostDeferred(std::shared_ptr<D3D11RenderTexture>& inRenderTextureFrame, std::shared_ptr<D3D11RenderTexture>& outRenderTexture);
		bool Pass_DebugGBuffer();
		void Pass_Debug();
		void Pass_FXAA(void* texture, void* renderTarget);
		void Pass_Sharpening(void* texture, void* renderTarget);
		void Pass_Blur(void* texture, void* renderTarget, const Math::Vector2& blurScale);
		void Pass_Shadowing(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, Light* inDirectionalLight, void* outRenderTexture);

		const Math::Vector4& GetClearColor();

		std::unique_ptr<GBuffer> m_gbuffer;

		// GAMEOBJECTS ========================
		std::vector<GameObject*> m_renderables;
		std::vector<Light*> m_lights;
		Light* m_directionalLight{};
		//=====================================

		//= RENDER TEXTURES =======================================
		std::shared_ptr<D3D11RenderTexture> m_renderTexSpare;
		std::shared_ptr<D3D11RenderTexture> m_renderTexShadowing;
		std::shared_ptr<D3D11RenderTexture> m_renderTexFinalFrame;
		//=========================================================

		//= SHADERS =====================================
		std::unique_ptr<DeferredShader> m_shaderDeferred;
		std::unique_ptr<Shader> m_shaderDepth;
		std::unique_ptr<Shader> m_shaderLine;
		std::unique_ptr<Shader> m_shaderGrid;
		std::unique_ptr<Shader> m_shaderFont;
		std::unique_ptr<Shader> m_shaderTexture;
		std::unique_ptr<Shader> m_shaderFXAA;
		std::unique_ptr<Shader> m_shaderShadowing;
		std::unique_ptr<Shader> m_shaderSharpening;
		std::unique_ptr<Shader> m_shaderBlur;
		//==============================================

		//= DEBUG ==========================================
		std::unique_ptr<Font> m_font;
		std::unique_ptr<Grid> m_grid;
		std::unique_ptr<Texture> m_gizmoTexLightDirectional;
		std::unique_ptr<Texture> m_gizmoTexLightPoint;
		std::unique_ptr<Texture> m_gizmoTexLightSpot;
		std::unique_ptr<Rectangle> m_gizmoRectLight;
		static unsigned long m_flags;
		//==================================================

		//= MISC ==================================
		std::vector<void*> m_texArray;
		ID3D11ShaderResourceView* m_texEnvironment;
		std::unique_ptr<Texture> m_texNoiseMap;
		std::unique_ptr<Rectangle> m_quad;
		//=========================================

		//= PREREQUISITES ================================
		Camera* m_camera;
		Skybox* m_skybox;
		LineRenderer* m_lineRenderer;
		Math::Matrix m_mView;
		Math::Matrix m_mProjectionPersp;
		Math::Matrix m_mVP;
		Math::Matrix m_mProjectionOrtho;
		Math::Matrix m_mViewBase;
		float m_nearPlane;
		float m_farPlane;
		Graphics* m_graphics;
		//================================================

		// Metrics
		int m_renderedMeshesCount;
		int m_renderedMeshesPerFrame;
	};
}