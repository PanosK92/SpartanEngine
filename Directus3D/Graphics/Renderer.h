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
#include "D3D11/D3D11RenderTexture.h"
#include "Shaders/DepthShader.h"
#include "Shaders/DebugShader.h"
#include "Shaders/PostProcessShader.h"
#include "Shaders/DeferredShader.h"
//====================================

enum Culling;

class Renderer
{
public:
	Renderer();
	~Renderer();

	void Initialize(bool debugDraw, D3D11Device* d3d11device, Timer* timer, PhysicsEngine* physics, Scene* scene);
	void UpdateFromScene();
	void PostProcessing(Camera* camera, Skybox* skybox, Directus::Math::Matrix mWorld, Directus::Math::Matrix mView, Directus::Math::Matrix mBaseView, Directus::Math::Matrix mPerspectiveProjection, Directus::Math::Matrix mOrthographicProjection);
	void StartCalculatingStats();
	void StopCalculatingStats();
	void Render();
	void SetResolution(int width, int height);
	void SetPhysicsDebugDraw(bool enable);

	/*------------------------------------------------------------------------------
								[STATS]
	------------------------------------------------------------------------------*/
	int GetRenderedMeshesCount();
	float GetFPS();
	float GetRenderTime();
	void MakeDirty();

private:
	D3D11Device* m_D3D11Device;
	GBuffer* m_GBuffer;
	Frustrum* m_frustrum;
	FullScreenQuad* m_fullScreenQuad;
	Timer* m_timer;
	PhysicsEngine* m_physics;
	Scene* m_scene;

	std::vector<GameObject*> m_renderables;
	std::vector<GameObject*> m_directionalLights;
	std::vector<GameObject*> m_pointLights;
	bool m_isDirty;

	/*------------------------------------------------------------------------------
									[RENDER TEXTURES]
	------------------------------------------------------------------------------*/
	D3D11RenderTexture* m_renderTexturePing;
	D3D11RenderTexture* m_renderTexturePong;

	/*------------------------------------------------------------------------------
										[MISC]
	------------------------------------------------------------------------------*/
	std::shared_ptr<Texture> m_noiseMap;

	/*------------------------------------------------------------------------------
										[SHADERS]
	------------------------------------------------------------------------------*/
	DeferredShader* m_shaderDeferred;
	DepthShader* m_depthShader;
	DebugShader* m_debugShader;
	PostProcessShader* m_shaderFXAA;
	PostProcessShader* m_shaderSharpening;

	/*------------------------------------------------------------------------------
										[HELPER]
	------------------------------------------------------------------------------*/
	void RenderLightDepthToTexture(std::vector<GameObject*> renderableGameObjects, int projectionSize, Light* light, float nearPlane, float farPlane);
	void RenderToGBuffer(std::vector<GameObject*> renderableGameObjects, Light* dirLight, Directus::Math::Matrix viewMatrix, Directus::Math::Matrix projectionMatrix);
	void CheckCulling(Culling culling);
	void DebugDraw(GameObject* camera);
	void Ping();
	void Pong();

	/*------------------------------------------------------------------------------
										[STATS]
	------------------------------------------------------------------------------*/
	int m_renderedMeshesCount;
	int m_meshesRendered;
	float m_renderStartTime;
	float m_renderTime;
	int m_frameCount;
	float m_fpsLastKnownTime;
	float m_fps;
};
