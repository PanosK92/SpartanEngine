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

//= INCLUDES ===========================
#include "Renderer.h"
#include "../Core/Globals.h"
#include "../Core/Settings.h"
#include "Shaders/PostProcessShader.h"
#include "Shaders/DebugShader.h"
#include "Shaders/DepthShader.h"
#include "Shaders/DeferredShader.h"
#include "../Core/GameObject.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Mesh.h"
#include "../Components/LineRenderer.h"
#include "../Physics/PhysicsEngine.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "D3D11/D3D11RenderTexture.h"
#include "../Core/Scene.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Renderer::Renderer()
{
	m_graphicsDevice = nullptr;
	m_GBuffer = nullptr;
	m_fullScreenQuad = nullptr;
	m_renderedMeshesCount = 0;
	m_meshesRendered = 0;
	m_renderTexturePing = nullptr;
	m_renderTexturePong = nullptr;
	m_shaderDeferred = nullptr;
	m_depthShader = nullptr;
	m_debugShader = nullptr;
	m_shaderFXAA = nullptr;
	m_shaderSharpening = nullptr;
	m_noiseMap = nullptr;
	m_frustrum = nullptr;
	m_skybox = nullptr;
	m_physics = nullptr;
	m_scene = nullptr;
	m_timer = nullptr;
	m_camera = nullptr;
	m_renderStartTime = 0;
	m_renderTime = 0;
	m_frameCount = 0;
	m_fpsLastKnownTime = 0;
	m_fps = 0;
	m_isDirty = true;
}

Renderer::~Renderer()
{
	// misc
	DirectusSafeDelete(m_frustrum);
	DirectusSafeDelete(m_fullScreenQuad);
	DirectusSafeDelete(m_GBuffer);

	// shaders
	DirectusSafeDelete(m_shaderDeferred);
	DirectusSafeDelete(m_depthShader);
	DirectusSafeDelete(m_debugShader);
	DirectusSafeDelete(m_shaderFXAA);
	DirectusSafeDelete(m_shaderSharpening);

	// textures
	DirectusSafeDelete(m_renderTexturePing);
	DirectusSafeDelete(m_renderTexturePong);
}

void Renderer::Initialize(bool debugDraw, GraphicsDevice* d3d11device, Timer* timer, PhysicsEngine* physics, Scene* scene)
{
	m_timer = timer;
	m_physics = physics;
	m_scene = scene;

	m_graphicsDevice = d3d11device;

	SetPhysicsDebugDraw(debugDraw);

	m_GBuffer = new GBuffer(m_graphicsDevice);
	m_GBuffer->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_frustrum = new Frustrum();

	m_fullScreenQuad = new FullScreenQuad;
	m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_graphicsDevice);

	/*------------------------------------------------------------------------------
									[SHADERS]
	------------------------------------------------------------------------------*/
	m_shaderDeferred = new DeferredShader();
	m_shaderDeferred->Initialize(m_graphicsDevice);

	m_depthShader = new DepthShader();
	m_depthShader->Initialize(m_graphicsDevice);

	m_debugShader = new DebugShader();
	m_debugShader->Initialize(m_graphicsDevice);

	m_shaderFXAA = new PostProcessShader();
	m_shaderFXAA->Initialize("FXAA", m_graphicsDevice);

	m_shaderSharpening = new PostProcessShader();
	m_shaderSharpening->Initialize("SHARPENING", m_graphicsDevice);

	/*------------------------------------------------------------------------------
								[RENDER TEXTURES]
	------------------------------------------------------------------------------*/
	m_renderTexturePing = new D3D11RenderTexture;
	m_renderTexturePing->Initialize(m_graphicsDevice, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_renderTexturePong = new D3D11RenderTexture;
	m_renderTexturePong->Initialize(m_graphicsDevice, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	/*------------------------------------------------------------------------------
										[MISC]
	------------------------------------------------------------------------------*/
	m_noiseMap = make_shared<Texture>();
	m_noiseMap->LoadFromFile("Assets/Shaders/noise.png", Normal);
	m_noiseMap->SetType(Normal);
}

void Renderer::SetResolution(int width, int height)
{
	Settings::GetInstance().SetResolution(width, height);

	m_graphicsDevice->SetViewport(width, height);

	DirectusSafeDelete(m_GBuffer);
	m_GBuffer = new GBuffer(m_graphicsDevice);
	m_GBuffer->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	DirectusSafeDelete(m_fullScreenQuad);
	m_fullScreenQuad = new FullScreenQuad;
	m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_graphicsDevice);

	DirectusSafeDelete(m_renderTexturePing);
	m_renderTexturePing = new D3D11RenderTexture;
	m_renderTexturePing->Initialize(m_graphicsDevice, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	DirectusSafeDelete(m_renderTexturePong);
	m_renderTexturePong = new D3D11RenderTexture;
	m_renderTexturePong->Initialize(m_graphicsDevice, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);
}

void Renderer::UpdateFromScene()
{
	// Get renderables
	m_renderables.clear();
	m_renderables.shrink_to_fit();
	m_renderables = m_scene->GetRenderables();

	// Get directional lights
	m_directionalLights.clear();
	m_directionalLights.shrink_to_fit();
	m_directionalLights = m_scene->GetDirectionalLights();

	// Get point lights
	m_pointLights.clear();
	m_pointLights.shrink_to_fit();
	m_pointLights = m_scene->GetPointLights();
}

void Renderer::Render()
{
	// Stats
	StartCalculatingStats();

	if (m_isDirty)
	{
		UpdateFromScene();
		m_isDirty = false;
	}

	AcquirePrerequisites();

	if (!m_camera)
	{
		m_graphicsDevice->Begin();
		m_graphicsDevice->End();
		return;
	}

	// Construct frustum (if necessery)
	if (m_frustrum->GetProjectionMatrix() != mPerspectiveProjection || m_frustrum->GetViewMatrix() != mView)
	{
		m_frustrum->SetProjectionMatrix(mPerspectiveProjection);
		m_frustrum->SetViewMatrix(mView);
		m_frustrum->ConstructFrustum(m_farPlane);
	}

	// ENABLE Z-BUFFER
	m_graphicsDevice->EnableZBuffer(true);

	// Render light depth
	Light* dirLight = nullptr;
	if (m_directionalLights.size() != 0)
		dirLight = m_directionalLights[0]->GetComponent<Light>();
	if (dirLight)
	{
		m_graphicsDevice->SetCullMode(CullFront);
		dirLight->SetDepthMapAsRenderTarget();
		DirectionalLightDepthPass(m_renderables, dirLight->GetProjectionSize(), dirLight);
		m_graphicsDevice->SetCullMode(CullBack);
	}

	// G-Buffer Construction
	m_GBuffer->SetRenderTargets();
	m_GBuffer->ClearRenderTargets(0.0f, 0.0f, 0.0f, 1.0f);
	GBufferPass(m_renderables, dirLight);

	// DISABLE Z BUFFER - SET FULLSCREEN QUAD
	m_graphicsDevice->EnableZBuffer(false);
	m_fullScreenQuad->SetBuffers();

	// Deferred Pass
	DeferredPass();

	// Post Proessing
	PostProcessing();

	// Debug Draw - Colliders
	DebugDraw();

	// display frame
	m_graphicsDevice->End(); 

	StopCalculatingStats();
}

void Renderer::AcquirePrerequisites()
{
	GameObject* camera = m_scene->GetMainCamera();
	if (camera)
	{
		m_camera = camera->GetComponent<Camera>();
		m_skybox = camera->GetComponent<Skybox>();

		mPerspectiveProjection = m_camera->GetPerspectiveProjectionMatrix();
		mOrthographicProjection = m_camera->GetOrthographicProjectionMatrix();
		mView = m_camera->GetViewMatrix();
		mBaseView = m_camera->GetBaseViewMatrix();
		mWorld = Matrix::Identity();
		m_nearPlane = m_camera->GetNearPlane();
		m_farPlane = m_camera->GetFarPlane();
	}
}

void Renderer::DirectionalLightDepthPass(vector<GameObject*> renderableGameObjects, int projectionSize, Light* light) const
{
	light->GenerateOrthographicProjectionMatrix(projectionSize, projectionSize, m_nearPlane, m_farPlane);
	light->GenerateViewMatrix();

	for (auto i = 0; i < renderableGameObjects.size(); i++)
	{
		GameObject* gameObject = renderableGameObjects[i];
		Mesh* mesh = gameObject->GetComponent<Mesh>();
		MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();

		// if the gameObject can't cast shadows, don't bother
		if (!meshRenderer->GetCastShadows())
			continue;

		if (mesh->SetBuffers())
		{
			m_depthShader->Render(
				gameObject->GetComponent<Mesh>()->GetIndexCount(),
				gameObject->GetTransform()->GetWorldMatrix(),
				light->GetViewMatrix(),
				light->GetOrthographicProjectionMatrix()
			);
		}
	}
}

void Renderer::GBufferPass(vector<GameObject*> renderableGameObjects, Light* dirLight)
{
	for (auto i = 0; i < renderableGameObjects.size(); i++)
	{
		//= Get all that we need ===================================================
		GameObject* gameObject = renderableGameObjects[i];
		Mesh* mesh = gameObject->GetComponent<Mesh>();
		MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();
		Material* material = meshRenderer->GetMaterial();
		Matrix worldMatrix = gameObject->GetTransform()->GetWorldMatrix();
		//==========================================================================

		// If something is missing, skip this GameObject
		if (!mesh || !meshRenderer || !material)
			continue;
		
		//= Frustrum culling =======================================================
		Vector3 center = Vector3::Transform(mesh->GetCenter(), worldMatrix);
		Vector3 extent = mesh->GetExtent() * gameObject->GetTransform()->GetScale();

		float radius = abs(extent.x);
		if (abs(extent.y) > radius) radius = abs(extent.y);
		if (abs(extent.z) > radius) radius = abs(extent.z);

		if (m_frustrum->CheckSphere(center, radius) == Outside)
			continue;
		//==========================================================================

		//= Face culling ===========================================================
		m_graphicsDevice->SetCullMode(material->GetFaceCullMode());
		//==========================================================================

		//= Render =================================================================
		bool buffersHaveBeenSet = mesh->SetBuffers();
		if (buffersHaveBeenSet)
		{
			meshRenderer->Render(
				mesh->GetIndexCount(), 
				mView, 
				mPerspectiveProjection, 
				dirLight
			);

			m_meshesRendered++;
		}
		//==========================================================================
	}
}

void Renderer::DeferredPass()
{
	if (!m_shaderDeferred->IsCompiled())
		return;

	Ping();

	// Setting a texture array instead of multiple textures is faster
	vector<ID3D11ShaderResourceView*> textures;
	textures.push_back(m_GBuffer->GetShaderResourceView(0)); // albedo
	textures.push_back(m_GBuffer->GetShaderResourceView(1)); // normal
	textures.push_back(m_GBuffer->GetShaderResourceView(2)); // depth
	textures.push_back(m_GBuffer->GetShaderResourceView(3)); // material
	if (m_skybox)
	{
		textures.push_back(m_skybox->GetEnvironmentTexture());
		textures.push_back(m_skybox->GetIrradianceTexture());
	}
	else
	{
		textures.push_back(m_noiseMap->GetID3D11ShaderResourceView());
		textures.push_back(m_noiseMap->GetID3D11ShaderResourceView());
	}
	textures.push_back(m_noiseMap->GetID3D11ShaderResourceView());

	// deferred rendering
	m_shaderDeferred->Render(
		m_fullScreenQuad->GetIndexCount(),
		mWorld,
		mView,
		mBaseView,
		mPerspectiveProjection,
		mOrthographicProjection,
		m_directionalLights,
		m_pointLights,
		m_camera,
		textures
	);
}

void Renderer::PostProcessing() const
{
	Pong();

	// fxaa pass
	m_shaderFXAA->Render(
		m_fullScreenQuad->GetIndexCount(),
		mWorld,
		mBaseView,
		mOrthographicProjection,
		m_renderTexturePing->GetShaderResourceView()
	);

	m_graphicsDevice->ResetRenderTarget(); // Reset the render target back to the original back buffer and not the render to texture anymore.
	m_graphicsDevice->ResetViewport(); // Reset the viewport back to the original.
	m_graphicsDevice->Begin();

	// sharpening pass
	m_shaderSharpening->Render(
		m_fullScreenQuad->GetIndexCount(),
		mWorld,
		mBaseView,
		mOrthographicProjection,
		m_renderTexturePing->GetShaderResourceView()
	);
}

void Renderer::DebugDraw() const
{
	if (!m_physics->GetDebugDraw())
		return;

	if (!m_physics->GetPhysicsDebugDraw()->IsDirty())
		return;

	// Get the line renderer component
	LineRenderer* lineRenderer = m_camera->g_gameObject->GetComponent<LineRenderer>();
	if (!lineRenderer)
		return;

	// Pass the line list from bullet to the line renderer component
	lineRenderer->AddLineList(m_physics->GetPhysicsDebugDraw()->GetLines());

	// Set the buffer
	lineRenderer->SetBuffer();

	// Render
	m_debugShader->Render(
		lineRenderer->GetVertexCount(),
		Matrix::Identity(),
		m_camera->GetViewMatrix(),
		m_camera->GetProjectionMatrix(),
		m_GBuffer->GetShaderResourceView(2) // depth
	);

	// clear physics debug draw
	m_physics->GetPhysicsDebugDraw()->ClearLines();
}

void Renderer::Ping() const
{
	m_renderTexturePing->SetAsRenderTarget(); // Set the render target to be the render to texture. 
	m_renderTexturePing->Clear(0.0f, 0.0f, 0.0f, 1.0f); // Clear the render to texture.
}

void Renderer::Pong() const
{
	m_renderTexturePong->SetAsRenderTarget(); // Set the render target to be the render to texture. 
	m_renderTexturePong->Clear(0.0f, 0.0f, 0.0f, 1.0f); // Clear the render to texture.
}

void Renderer::StartCalculatingStats()
{
	m_renderStartTime = m_timer->GetTimeMs();
	m_meshesRendered = 0;
}

void Renderer::StopCalculatingStats()
{
	// meshes rendered
	m_renderedMeshesCount = m_meshesRendered;

	// get current time
	float currentTime = m_timer->GetTimeMs();

	// calculate render time
	m_renderTime = currentTime - m_renderStartTime;

	// fps
	m_frameCount++;
	if (currentTime >= m_fpsLastKnownTime + 1000)
	{
		m_fps = m_frameCount;
		m_frameCount = 0;

		m_fpsLastKnownTime = currentTime;
	}
}

void Renderer::SetPhysicsDebugDraw(bool enable) const
{
	m_physics->SetDebugDraw(enable);
}

void Renderer::SetCamera(Camera* camera)
{
	m_camera = camera;
}

void Renderer::SetSkybox(Skybox* skybox)
{
	m_skybox = skybox;
}

int Renderer::GetRenderedMeshesCount() const
{
	return m_renderedMeshesCount;
}

float Renderer::GetFPS() const
{
	return m_fps;
}

float Renderer::GetRenderTimeMs() const
{
	return m_renderTime;
}

void Renderer::MakeDirty()
{
	m_isDirty = true;
}
