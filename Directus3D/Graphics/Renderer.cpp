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

	// Get the main as a GameObject
	GameObject* mainCamera = m_scene->GetMainCamera();
	if (!mainCamera)
		return;

	// Get the camera component out of the camera GameObject
	Camera* camera = mainCamera->GetComponent<Camera>();
	if (!camera)
		return;

	Skybox* skybox = mainCamera->GetComponent<Skybox>();
	ID3D11ShaderResourceView* environmentMap = nullptr;
	if (skybox)
		environmentMap = skybox->GetEnvironmentTexture();

	// get all necessery data from the camera
	Matrix mPerspectiveProjection = camera->GetPerspectiveProjectionMatrix();
	Matrix mOrthographicProjection = camera->GetOrthographicProjectionMatrix();
	Matrix mView = camera->GetViewMatrix();
	Matrix mBaseView = camera->GetBaseViewMatrix();
	Matrix mWorld = Matrix::Identity();
	float nearPlane = camera->GetNearPlane();
	float farPlane = camera->GetFarPlane();

	// Construct frustum (if necessery)
	if (m_frustrum->GetProjectionMatrix() != mPerspectiveProjection || m_frustrum->GetViewMatrix() != mView)
	{
		m_frustrum->SetProjectionMatrix(mPerspectiveProjection);
		m_frustrum->SetViewMatrix(mView);
		m_frustrum->ConstructFrustum(farPlane);
	}

	Light* dirLight = nullptr;
	if (m_directionalLights.size() != 0)
		dirLight = m_directionalLights[0]->GetComponent<Light>();

	m_graphicsDevice->EnableZBuffer(true);

	if (dirLight)
	{
		// Render light depth
		m_graphicsDevice->SetCullMode(CullFront);
		dirLight->SetDepthMapAsRenderTarget();
		RenderLightDepthToTexture(m_renderables, dirLight->GetProjectionSize(), dirLight, nearPlane, farPlane);
		m_graphicsDevice->SetCullMode(CullBack);
	}

	//// Render G-Buffer
	//m_GBuffer->SetRenderTargets();
	//m_GBuffer->ClearRenderTargets(0.0f, 0.0f, 0.0f, 1.0f);
	//RenderToGBuffer(m_renderables, dirLight, mView, mPerspectiveProjection);
	m_graphicsDevice->EnableZBuffer(false);

	//// Post processing, fxaa, sharpening etc...
	m_fullScreenQuad->SetBuffers(); // set full screen quad buffers
	//PostProcessing(camera, skybox, mWorld, mView, mBaseView, mPerspectiveProjection, mOrthographicProjection);

	//// Debug draw - Colliders
	//DebugDraw(mainCamera);

	m_graphicsDevice->End(); // display frame

	StopCalculatingStats();
}

void Renderer::RenderLightDepthToTexture(vector<GameObject*> renderableGameObjects, int projectionSize, Light* light, float nearPlane, float farPlane)
{
	light->GenerateOrthographicProjectionMatrix(projectionSize, projectionSize, nearPlane, farPlane);
	light->GenerateViewMatrix();

	// Get the metrices
	Matrix viewMatrix = light->GetViewMatrix();
	Matrix orthographicMatrix = light->GetOrthographicProjectionMatrix();

	for (unsigned int i = 0; i < renderableGameObjects.size(); i++)
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
				viewMatrix,
				orthographicMatrix
			);
		}
	}
}

void Renderer::RenderToGBuffer(vector<GameObject*> renderableGameObjects, Light* dirLight, Matrix viewMatrix, Matrix projectionMatrix)
{
	for (unsigned int i = 0; i < renderableGameObjects.size(); i++)
	{
		// Gather some useful objects
		GameObject* gameObject = renderableGameObjects[i];
		Mesh* mesh = gameObject->GetComponent<Mesh>();
		MeshRenderer* meshRenderer = gameObject->GetComponent<MeshRenderer>();
		Material* material = meshRenderer->GetMaterial();
		Matrix worldMatrix = gameObject->GetTransform()->GetWorldMatrix();

		if (!mesh || !meshRenderer || !material)
			continue;

		//= Frustrum CullMode ==================================
		Vector3 center = Vector3::Transform(mesh->GetCenter(), worldMatrix);
		Vector3 extent = mesh->GetExtent() * gameObject->GetTransform()->GetScale();

		float radius = abs(extent.x);
		if (abs(extent.y) > radius) radius = abs(extent.y);
		if (abs(extent.z) > radius) radius = abs(extent.z);

		if (m_frustrum->CheckSphere(center, radius) == Outside)
			continue;
		//=====================================================

		// Handle face CullMode
		m_graphicsDevice->SetCullMode(material->GetFaceCullMode());

		// render mesh
		if (mesh->SetBuffers())
		{
			meshRenderer->Render(mesh->GetIndexCount(), viewMatrix, projectionMatrix, dirLight);
			m_meshesRendered++;
		}
	}
}

void Renderer::PostProcessing(Camera* camera, Skybox* skybox, Matrix mWorld, Matrix mView, Matrix mBaseView, Matrix mPerspectiveProjection, Matrix mOrthographicProjection)
{
	if (!m_shaderDeferred->IsCompiled())
		return;

	ID3D11ShaderResourceView* environmentTexture = nullptr;
	ID3D11ShaderResourceView* irradianceTexture = nullptr;

	if (skybox)
	{
		environmentTexture = skybox->GetEnvironmentTexture();
		irradianceTexture = skybox->GetIrradianceTexture();
	}

	Ping();

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
		camera,
		m_GBuffer->GetShaderResourceView(0), // albedo
		m_GBuffer->GetShaderResourceView(1), // normal
		m_GBuffer->GetShaderResourceView(2), // depth
		m_GBuffer->GetShaderResourceView(3), // material
		environmentTexture,
		irradianceTexture,
		m_noiseMap->GetID3D11ShaderResourceView()
	);

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

void Renderer::DebugDraw(GameObject* camera)
{
	if (!m_physics->GetDebugDraw())
		return;

	if (!m_physics->GetPhysicsDebugDraw()->IsDirty())
		return;

	// Get the line renderer component
	LineRenderer* lineRenderer = camera->GetComponent<LineRenderer>();
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
		camera->GetComponent<Camera>()->GetViewMatrix(),
		camera->GetComponent<Camera>()->GetProjectionMatrix(),
		m_GBuffer->GetShaderResourceView(2) // depth
	);

	// clear physics debug draw
	m_physics->GetPhysicsDebugDraw()->ClearLines();
}

void Renderer::Ping()
{
	m_renderTexturePing->SetAsRenderTarget(); // Set the render target to be the render to texture. 
	m_renderTexturePing->Clear(0.0f, 0.0f, 0.0f, 1.0f); // Clear the render to texture.
}

void Renderer::Pong()
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

void Renderer::SetResolution(int width, int height)
{
	//m_D3D11Device->SetResolution(width, height);

	//Release();
	//Initialize();
	//Render();
}

void Renderer::SetPhysicsDebugDraw(bool enable)
{
	m_physics->SetDebugDraw(enable);
}

int Renderer::GetRenderedMeshesCount()
{
	return m_renderedMeshesCount;
}

float Renderer::GetFPS()
{
	return m_fps;
}

float Renderer::GetRenderTimeMs()
{
	return m_renderTime;
}

void Renderer::MakeDirty()
{
	m_isDirty = true;
}
