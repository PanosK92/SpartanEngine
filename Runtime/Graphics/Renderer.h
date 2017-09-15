/*
Copyright(c) 2016-2017 Panos Karabelas

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
	class FullScreenQuad;
	class DeferredShader;
	class DepthShader;
	class LineShader;
	class PostProcessShader;
	class Texture;
	class ResourceManager;
	class D3D11RenderTexture;
	class D3D11GraphicsDevice;
	class Stopwatch;

	namespace Math
	{
		class Frustrum;
	}

	enum RenderOutput
	{
		Render_Default,
		Render_Albedo,
		Render_Normal,
		Render_Depth,
		Render_Material
	};

	class DLL_API Renderer : public Subsystem
	{
	public:
		Renderer(Context* context);
		~Renderer();

		//= Subsystem ============
		virtual bool Initialize();
		//========================

		void Render();

		// Resolution
		void SetResolution(int width, int height);

		// Viewport
		Math::Vector2 GetViewport() { return GET_VIEWPORT; }
		void SetViewport(float width, float height);

		void Clear();
		const std::vector<weakGameObj>& GetRenderables() { return m_renderables; }

		//= STATS =======================================================
		void StartCalculatingStats();
		void StopCalculatingStats();
		int GetRenderedMeshesCount() { return m_renderedMeshesPerFrame; }
		double GetRenderTime() { return m_renderTimeMs; }
		//===============================================================

	private:
		//= HELPER FUNCTIONS =================
		void AcquirePrerequisites();
		void DirectionalLightDepthPass();
		void GBufferPass();
		void DeferredPass();
		void PostProcessing();
		void DebugDraw();
		const Math::Vector4& GetClearColor();
		//===================================

		std::shared_ptr<FullScreenQuad> m_fullScreenQuad;
		std::shared_ptr<GBuffer> m_GBuffer;

		// GAMEOBJECTS ========================
		std::vector<weakGameObj> m_renderables;
		std::vector<Light*> m_lights;
		Light* m_directionalLight;
		//=====================================

		//= RENDER TEXTURES ================================
		std::shared_ptr<D3D11RenderTexture> m_renderTexPing;
		std::shared_ptr<D3D11RenderTexture> m_renderTexPong;
		//==================================================

		//= MISC =========================================
		std::vector<ID3D11ShaderResourceView*> m_texArray;
		ID3D11ShaderResourceView* m_texEnvironment;
		std::shared_ptr<Texture> m_texNoiseMap;
		//================================================

		//= SHADERS ==========================================
		std::shared_ptr<DeferredShader> m_shaderDeferred;
		std::shared_ptr<DepthShader> m_shaderDepth;
		std::shared_ptr<LineShader> m_shaderLine;
		std::shared_ptr<PostProcessShader> m_shaderFXAA;
		std::shared_ptr<PostProcessShader> m_shaderSharpening;
		std::shared_ptr<PostProcessShader> m_shaderBlur;
		std::shared_ptr<PostProcessShader> m_shaderTex;
		//====================================================

		//= STATS ===================================
		int m_renderedMeshesPerFrame;
		int m_renderedMeshesTempCounter;
		double m_renderTimeMs;
		std::unique_ptr<Stopwatch> m_renderStopwatch;
		//===========================================

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
		ResourceManager* m_resourceMng;
		RenderOutput m_renderOutput;
		//================================================
	};
}