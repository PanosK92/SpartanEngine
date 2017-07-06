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
//======================================

class ID3D11ShaderResourceView;

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
	class DebugShader;
	class PostProcessShader;
	class Texture;
	class Frustrum;
	class ResourceManager;
	class D3D11RenderTexture;
	class D3D11GraphicsDevice;

	class Renderer : public Subsystem
	{
	public:
		Renderer(Context* context);
		~Renderer();

		//= Subsystem ============
		virtual bool Initialize();
		//========================

		void Render();
		void SetResolution(int width, int height);
		void Clear();
		const std::vector<weakGameObj>& GetRenderables() { return m_renderables; }

		//= STATS =======================================================
		void StartCalculatingStats();
		void StopCalculatingStats();
		int GetRenderedMeshesCount() { return m_renderedMeshesPerFrame; }
		int GetRenderTime() { return m_renderTimeMs; }
		//===============================================================

	private:
		std::shared_ptr<FullScreenQuad> m_fullScreenQuad;
		std::shared_ptr<GBuffer> m_GBuffer;

		// GAMEOBJECTS ==============================
		std::vector<weakGameObj> m_renderables;
		std::vector<weakGameObj> m_lightsDirectional;
		std::vector<weakGameObj> m_lightsPoint;
		//===========================================

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
		std::shared_ptr<DebugShader> m_shaderDebug;
		std::shared_ptr<PostProcessShader> m_shaderFXAA;
		std::shared_ptr<PostProcessShader> m_shaderSharpening;
		std::shared_ptr<PostProcessShader> m_shaderBlur;
		//====================================================

		//= STATS ======================
		int m_renderedMeshesPerFrame;
		int m_renderedMeshesTempCounter;
		int m_renderTimeMs;
		//==============================

		//= PREREQUISITES ================================
		Camera* m_camera;
		Skybox* m_skybox;
		LineRenderer* m_lineRenderer;
		Light* m_directionalLight;
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
		//================================================

		//= HELPER FUNCTIONS =========================================================================
		void AcquirePrerequisites();
		void DirectionalLightDepthPass();
		void GBufferPass();
		void DeferredPass();
		void PostProcessing();
		void Gizmos() const;
		const Math::Vector4& GetClearColor();
		//============================================================================================
	};
}