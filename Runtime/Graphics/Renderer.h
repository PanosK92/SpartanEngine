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
#include "D3D11/D3D11GraphicsDevice.h"
#include "../Core/SubSystem.h"
#include "../Math/Matrix.h"
#include "../Resource/ResourceManager.h"
#include "../Core/Settings.h"
//======================================

namespace Directus
{
	class GameObject;
	class MeshFilter;
	class Camera;
	class Skybox;
	class LineRenderer;
	class Light;
	class MeshFilter;	
	class GBuffer;
	class Rectangle;
	class DeferredShader;
	class Shader;
	class Texture;
	class ResourceManager;
	class D3D11RenderTexture;
	class D3D11GraphicsDevice;
	class Font;
	class Grid;
	class Variant;

	namespace Math
	{
		class Frustrum;
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
		void SetRenderTarget(void* renderTarget);
		void SetRenderTarget(const std::shared_ptr<D3D11RenderTexture>& renderTexture);
		void* GetFrame();
		void Present();
		void Render();

		// Back-buffer rendering
		void SetResolutionBackBuffer(int width, int height);
		void SetViewportBackBuffer(float width, float height);
		Math::Vector4 GetViewportBackBuffer();

		// Internal rendering
		void SetResolution(int width, int height);
		void SetViewport(int width, int height);
		static const Math::Vector4& GetViewport() { return GET_VIEWPORT; }
		
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
		const std::vector<std::weak_ptr<GameObject>>& GetRenderables() { return m_renderables; }

	private:
		//= RENDER PATHS =============================================
		void AcquireRenderables(const Variant& renderables);
		void DirectionalLightDepthPass(Light* directionalLight);
		void GBufferPass();
		void PreDeferredPass(void* inTextureNormal, void* inTextureDepth, void* inTextureNormalNoise, void* inRenderTexure, void* outRenderTextureShadowing);
		void DeferredPass(void* inTextureShadowing, void* outRenderTexture);	
		void PostDeferredPass(std::shared_ptr<D3D11RenderTexture>& inRenderTextureFrame, std::shared_ptr<D3D11RenderTexture>& outRenderTexture
		);
		bool RenderGBuffer();
		void DebugDraw();
		const Math::Vector4& GetClearColor();
		//============================================================
	
		//= PASSES ===========================================================================================
		void Pass_FXAA(void* texture, void* renderTarget);
		void Pass_Sharpening(void* texture, void* renderTarget);
		void Pass_Blur(void* texture, void* renderTarget, const Math::Vector2& blurScale);
		void Pass_Shadowing(
			void* inTextureNormal,
			void* inTextureDepth,
			void* inTextureNormalNoise,
			Light* inDirectionalLight,
			void* outRenderTexture
		);
		//====================================================================================================
		
		std::unique_ptr<GBuffer> m_gbuffer;

		// GAMEOBJECTS ======================================
		std::vector<std::weak_ptr<GameObject>> m_renderables;
		std::vector<Light*> m_lights;
		Light* m_directionalLight{};
		//===================================================

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
		Math::Matrix mView;
		Math::Matrix mProjection;
		Math::Matrix mViewProjection;
		Math::Matrix mOrthographicProjection;
		Math::Matrix mBaseView;
		float m_nearPlane;
		float m_farPlane;
		std::vector<ID3D11ShaderResourceView*> m_textures;
		Graphics* m_graphics;
		//================================================
	};
}