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
#include "Frustrum.h"
#include "../Core/GameObject.h"
#include "../Core/Timer.h"
#include "../Components/Camera.h"
#include "../Components/Skybox.h"
#include "../Components/Light.h"
#include "GraphicsDevice.h"
#include "Shaders/DepthShader.h"
#include "Shaders/DebugShader.h"
#include "Shaders/PostProcessShader.h"
#include "Shaders/DeferredShader.h"
#include <memory>
#include "../Core/Texture.h"
//====================================

class Renderer
{
public:
	Renderer();
	~Renderer();

	void Initialize(bool debugDraw, GraphicsDevice* d3d11device, Timer* timer, PhysicsEngine* physics, Scene* scene);
	void Render();
	void SetResolution(int width, int height);
	void Clear();
	void Update(std::vector<GameObject*> renderables, std::vector<GameObject*> lightsDirectional, std::vector<GameObject*> lightsPoint);
	void SetPhysicsDebugDraw(bool enable) const;

	//= STATS =========================
	void StartCalculatingStats();
	void StopCalculatingStats();
	int GetRenderedMeshesCount() const;
	float GetFPS() const;
	float GetRenderTimeMs() const;
	//=================================

private:
	//= DEPENDENCIES ================
	GraphicsDevice* m_graphicsDevice;
	GBuffer* m_GBuffer;
	Frustrum* m_frustrum;
	FullScreenQuad* m_fullScreenQuad;
	Timer* m_timer;
	PhysicsEngine* m_physics;
	Scene* m_scene;
	//===============================

	// GAMEOBJECTS ==============================
	std::vector<GameObject*> m_renderables;
	std::vector<GameObject*> m_lightsDirectional;
	std::vector<GameObject*> m_lightsPoint;
	//===========================================

	//= RENDER TEXTURES ====================
	D3D11RenderTexture* m_renderTexPing;
	D3D11RenderTexture* m_renderTexPong;
	//======================================

	//= MISC =====================================================
	std::vector<ID3D11ShaderResourceView*> m_texArrayDeferredPass;
	ID3D11ShaderResourceView* m_texEnvironment;
	ID3D11ShaderResourceView* m_texIrradiance;
	std::shared_ptr<Texture> m_texNoiseMap;
	//============================================================

	//= SHADERS =========================
	DeferredShader* m_shaderDeferred;
	DepthShader* m_shaderDepth;
	DebugShader* m_shaderDebug;
	PostProcessShader* m_shaderFXAA;
	PostProcessShader* m_shaderSharpening;
	//====================================

	//= STATS ================
	int m_renderedMeshesCount;
	int m_meshesRendered;
	float m_renderStartTime;
	float m_renderTime;
	int m_frameCount;
	float m_fpsLastKnownTime;
	float m_fps;
	//========================

	//= PREREQUISITES =============================
	Camera* m_camera;
	Skybox* m_skybox;
	Directus::Math::Matrix mPerspectiveProjection;
	Directus::Math::Matrix mOrthographicProjection;
	Directus::Math::Matrix mView;
	Directus::Math::Matrix mBaseView;
	Directus::Math::Matrix mWorld;
	float m_nearPlane;
	float m_farPlane;
	//=============================================
	
	//= HELPER FUNCTIONS ==========================
	void AcquirePrerequisites();
	void DirectionalLightDepthPass(std::vector<GameObject*> renderableGameObjects, int projectionSize, Light* light) const;
	void GBufferPass(std::vector<GameObject*> renderableGameObjects, Light* dirLight);
	void DeferredPass();
	void PostProcessing() const;
	void DebugDraw() const;
	void Ping() const;
	void Pong() const;
	//=============================================
};
