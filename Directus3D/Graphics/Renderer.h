/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES =========================
#include "GBuffer.h"
#include "FullScreenQuad.h"
#include "../Core/GameObject.h"
#include "../Components/Camera.h"
#include "../Components/Skybox.h"
#include "../Components/Light.h"
#include "Shaders/DepthShader.h"
#include "Shaders/DebugShader.h"
#include "Shaders/PostProcessShader.h"
#include "Shaders/DeferredShader.h"
#include <memory>
#include "../Graphics/Texture.h"
#include "../Components/LineRenderer.h"
#include "D3D11/D3D11RenderTexture.h"
#include "../Core/Subsystem.h"
//====================================

class MeshFilter;

class Renderer : public Subsystem
{
public:
	Renderer(Context* context);
	~Renderer();

	void Render();
	void SetResolution(int width, int height);
	void Clear();
	const std::vector<GameObject*>& GetRenderables() { return m_renderables; }

	//= STATS =========================
	void StartCalculatingStats();
	void StopCalculatingStats();
	float GetFPS() { return m_fps; }
	int GetRenderedMeshesCount() { return m_renderedMeshesPerFrame; }
	//=================================

private:
	std::shared_ptr<FullScreenQuad> m_fullScreenQuad;
	std::shared_ptr<GBuffer> m_GBuffer;

	// GAMEOBJECTS ==============================
	std::vector<GameObject*> m_renderables;
	std::vector<GameObject*> m_lightsDirectional;
	std::vector<GameObject*> m_lightsPoint;
	//===========================================

	//= RENDER TEXTURES ====================
	std::shared_ptr<D3D11RenderTexture> m_renderTexPing;
	std::shared_ptr<D3D11RenderTexture> m_renderTexPong;
	//======================================

	//= MISC =====================================================
	std::vector<ID3D11ShaderResourceView*> m_texArray;
	ID3D11ShaderResourceView* m_texEnvironment;
	std::shared_ptr<Texture> m_texNoiseMap;
	//============================================================

	//= SHADERS ==========================================
	std::shared_ptr<DeferredShader> m_shaderDeferred;
	std::shared_ptr<DepthShader> m_shaderDepth;
	std::shared_ptr<DebugShader> m_shaderDebug;
	std::shared_ptr<PostProcessShader> m_shaderFXAA;
	std::shared_ptr<PostProcessShader> m_shaderSharpening;
	std::shared_ptr<PostProcessShader> m_shaderBlur;
	//====================================================

	//= STATS ======================
	float m_fps;
	float m_timePassed;
	int m_frameCount;
	int m_renderedMeshesPerFrame;
	int m_renderedMeshesTempCounter;
	//==============================

	//= PREREQUISITES =================================
	Camera* m_camera;
	Skybox* m_skybox;
	LineRenderer* m_lineRenderer;
	Light* m_directionalLight;
	Directus::Math::Matrix mView;
	Directus::Math::Matrix mProjection;
	Directus::Math::Matrix mViewProjection;
	Directus::Math::Matrix mOrthographicProjection;
	Directus::Math::Matrix mBaseView;
	float m_nearPlane;
	float m_farPlane;
	std::vector<ID3D11ShaderResourceView*> m_textures;
	//================================================

	//= HELPER FUNCTIONS ==========================
	bool IsInViewFrustrum(const std::shared_ptr<Frustrum>& cameraFrustrum, MeshFilter* meshFilte);
	void AcquirePrerequisites();
	void DirectionalLightDepthPass();
	void GBufferPass();
	void DeferredPass();
	void PostProcessing();
	void Gizmos() const;
	const Directus::Math::Vector4& GetClearColor();
	//=============================================
};
