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
#include "../Core/Settings.h"
#include "Shaders/PostProcessShader.h"
#include "Shaders/DebugShader.h"
#include "Shaders/DepthShader.h"
#include "Shaders/DeferredShader.h"
#include "../Core/GameObject.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/LineRenderer.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "D3D11/D3D11RenderTexture.h"
#include "../Core/Scene.h"
#include "../Logging/Log.h"
#include "../Components/MeshFilter.h"
#include "../Signals/Signaling.h"
#include "../Core/Context.h"
#include "../Pools/ShaderPool.h"
#include "../Pools/MaterialPool.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Renderer::Renderer(Context* context): Object(context) 
{
	m_GBuffer = nullptr;
	m_fullScreenQuad = nullptr;
	m_renderedMeshesCount = 0;
	m_meshesRendered = 0;
	m_renderTexPing = nullptr;
	m_renderTexPong = nullptr;
	m_shaderDeferred = nullptr;
	m_shaderDepth = nullptr;
	m_shaderDebug = nullptr;
	m_shaderFXAA = nullptr;
	m_shaderSharpening = nullptr;
	m_texNoiseMap = nullptr;
	m_skybox = nullptr;
	m_camera = nullptr;
	m_texEnvironment = nullptr;
	m_lineRenderer = nullptr;
	m_directionalLight = nullptr;
	m_nearPlane = 0.0;
	m_farPlane = 0.0f;

	Graphics* graphics = g_context->GetSubsystem<Graphics>();

	m_GBuffer = make_shared<GBuffer>(graphics);
	m_GBuffer->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_fullScreenQuad = make_shared<FullScreenQuad>();
	m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, graphics);

	/*------------------------------------------------------------------------------
	[SHADERS]
	------------------------------------------------------------------------------*/
	m_shaderDeferred = make_shared<DeferredShader>();
	m_shaderDeferred->Initialize(graphics);

	m_shaderDepth = make_shared<DepthShader>();
	m_shaderDepth->Initialize(graphics);

	m_shaderDebug = make_shared<DebugShader>();
	m_shaderDebug->Initialize(graphics);

	m_shaderFXAA = make_shared<PostProcessShader>();
	m_shaderFXAA->Initialize("FXAA", graphics);

	m_shaderSharpening = make_shared<PostProcessShader>();
	m_shaderSharpening->Initialize("SHARPENING", graphics);

	/*------------------------------------------------------------------------------
	[RENDER TEXTURES]
	------------------------------------------------------------------------------*/
	m_renderTexPing = make_shared<D3D11RenderTexture>();
	m_renderTexPing->Initialize(graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_renderTexPong = make_shared<D3D11RenderTexture>();
	m_renderTexPong->Initialize(graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	/*------------------------------------------------------------------------------
	[MISC]
	------------------------------------------------------------------------------*/
	m_texNoiseMap = make_shared<Texture>();
	m_texNoiseMap->LoadFromFile("Assets/Shaders/noise.png", graphics);
	m_texNoiseMap->SetType(Normal);
}

Renderer::~Renderer()
{

}

void Renderer::Render()
{
	Graphics* graphics = g_context->GetSubsystem<Graphics>();

	StartCalculatingStats();
	AcquirePrerequisites();

	if (!m_camera)
	{
		graphics->Clear(Vector4(0, 0, 0, 1));
		graphics->Present();
		return;
	}

	if (m_renderables.empty())
	{
		graphics->Clear(m_camera->GetClearColor());
		graphics->Present();
		return;
	}

	// ENABLE Z-BUFFER
	graphics->EnableZBuffer(true);

	// Render light depth
	if (m_directionalLight)
		if (m_directionalLight->GetShadowType() != No_Shadows)
			DirectionalLightDepthPass();

	// G-Buffer Construction
	m_GBuffer->SetRenderTargets();
	m_GBuffer->Clear(m_camera->GetClearColor());
	GBufferPass();

	// DISABLE Z BUFFER - SET FULLSCREEN QUAD
	graphics->EnableZBuffer(false);
	m_fullScreenQuad->SetBuffers();

	// Deferred Pass
	DeferredPass();

	// Post Proessing
	PostProcessing();

	// Gizmos
	if (GET_ENGINE_MODE == Editor_Idle)
		Gizmos();

	// display frame
	graphics->Present();

	StopCalculatingStats();
}

void Renderer::SetResolution(int width, int height)
{
	// A resolution of 0 won'tcause a crash or anything crazy,
	// but it will cause the depth stancil buffer creation to fail,
	// various error messages to be displayed. I silently prevent that.
	if (width == 0 || height == 0)
		return;

	SET_RESOLUTION(width, height);
	Graphics* graphics = g_context->GetSubsystem<Graphics>();

	graphics->SetViewport(width, height);

	m_GBuffer.reset();
	m_fullScreenQuad.reset();
	m_renderTexPing.reset();
	m_renderTexPong.reset();

	m_GBuffer = make_shared<GBuffer>(graphics);
	m_GBuffer->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);
	
	m_fullScreenQuad = make_shared<FullScreenQuad>();
	m_fullScreenQuad->Initialize(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, graphics);

	m_renderTexPing = make_shared<D3D11RenderTexture>();
	m_renderTexPing->Initialize(graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

	m_renderTexPong = make_shared<D3D11RenderTexture>();
	m_renderTexPong->Initialize(graphics, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);
}

void Renderer::Clear()
{
	m_renderables.clear();
	m_renderables.shrink_to_fit();

	m_lightsDirectional.clear();
	m_lightsDirectional.shrink_to_fit();

	m_lightsPoint.clear();
	m_lightsPoint.shrink_to_fit();
}

void Renderer::Update(const vector<GameObject*>& renderables, const vector<GameObject*>& lightsDirectional, const vector<GameObject*>& lightsPoint)
{
	Clear();

	m_renderables = renderables;
	m_lightsDirectional = lightsDirectional;
	m_lightsPoint = lightsPoint;
}

const vector<GameObject*>& Renderer::GetRenderables() const
{
	return m_renderables;
}

void Renderer::AcquirePrerequisites()
{
	Scene* scene = g_context->GetSubsystem<Scene>();

	GameObject* camera = scene->GetMainCamera();
	if (camera)
	{
		m_camera = camera->GetComponent<Camera>();

		GameObject* skybox = scene->GetSkybox();
		if (skybox)
		{
			m_skybox = skybox->GetComponent<Skybox>();
			m_lineRenderer = skybox->GetComponent<LineRenderer>(); // Hush hush...
		}

		if (m_lightsDirectional.size() != 0)
			m_directionalLight = m_lightsDirectional[0]->GetComponent<Light>();
		else
			m_directionalLight = nullptr;

		mProjection = m_camera->GetProjectionMatrix();
		mOrthographicProjection = m_camera->GetOrthographicProjectionMatrix();
		mView = m_camera->GetViewMatrix();
		mBaseView = m_camera->GetBaseViewMatrix();
		m_nearPlane = m_camera->GetNearPlane();
		m_farPlane = m_camera->GetFarPlane();
	}
	else
	{
		m_camera = nullptr;
		m_skybox = nullptr;
		m_lineRenderer = nullptr;
		m_directionalLight = nullptr;
	}
}

void Renderer::DirectionalLightDepthPass()
{
	g_context->GetSubsystem<Graphics>()->SetCullMode(CullFront);

	for (int cascadeIndex = 0; cascadeIndex < m_directionalLight->GetCascadeCount(); cascadeIndex++)
	{
		m_directionalLight->SetShadowMapAsRenderTarget(cascadeIndex);
		for (const auto& gameObject : m_renderables)
		{
			auto meshRenderer = gameObject->GetComponent<MeshRenderer>();
			auto meshFilter = gameObject->GetComponent<MeshFilter>();
			auto mesh = meshFilter->GetMesh();

			if (mesh.expired() || !meshFilter || !meshRenderer)
				continue;

			// The skybox might be able to cast shadows, but in the real world it doesn't, 
			// because it doesn't exist, so skip it
			if (gameObject->GetComponent<Skybox>() || !meshRenderer->GetCastShadows())
				continue;

			if (meshFilter->SetBuffers())
			{
				m_shaderDepth->Render(
					mesh.lock()->GetIndexCount(),
					gameObject->GetTransform()->GetWorldTransform(),
					m_directionalLight->GetViewMatrix(),
					m_directionalLight->GetOrthographicProjectionMatrix(cascadeIndex)
				);
			}
		}
	}
}

void Renderer::GBufferPass()
{
	ShaderPool* shaderPool = g_context->GetSubsystem<ShaderPool>();
	MaterialPool* materialPool = g_context->GetSubsystem<MaterialPool>();
	Graphics* graphics = g_context->GetSubsystem<Graphics>();

	for (const auto currentShader : shaderPool->GetAllShaders()) // for each shader
	{	
		currentShader->Set();
		for (const auto currentMaterial : materialPool->GetAllMaterials()) // for each material...
		{
			
			if (currentMaterial->GetShader().expired())
				continue;

			// ... that uses the current shader
			if (currentMaterial->GetShader().lock()->GetID() != currentShader->GetID())
				continue;	

			//= Gather any used textures and bind them to the GPU ===============================
			m_textures.push_back(currentMaterial->GetShaderResourceViewByTextureType(Albedo));
			m_textures.push_back(currentMaterial->GetShaderResourceViewByTextureType(Roughness));
			m_textures.push_back(currentMaterial->GetShaderResourceViewByTextureType(Metallic));
			m_textures.push_back(currentMaterial->GetShaderResourceViewByTextureType(Occlusion));
			m_textures.push_back(currentMaterial->GetShaderResourceViewByTextureType(Normal));
			m_textures.push_back(currentMaterial->GetShaderResourceViewByTextureType(Height));
			m_textures.push_back(currentMaterial->GetShaderResourceViewByTextureType(Mask));
			if (m_directionalLight)
			{
				for (int i = 0; i < m_directionalLight->GetCascadeCount(); i++)
					m_textures.push_back(m_directionalLight->GetDepthMap(i));
			}
			else
				m_textures.push_back(nullptr);

			currentShader->SetResources(m_textures);
			//==================================================================================

			for (auto const gameObject : m_renderables) // for each mesh...
			{
				//= Get all that we need =====================================
				auto meshFilter = gameObject->GetComponent<MeshFilter>();
				auto mesh = meshFilter->GetMesh();
				auto meshRenderer = gameObject->GetComponent<MeshRenderer>();
				auto material = meshRenderer->GetMaterial();
				auto mWorld = gameObject->GetTransform()->GetWorldTransform();
				//============================================================

				// If any rendering requirement is missing, skip this GameObject
				if (!meshFilter || mesh.expired() || !meshRenderer || material.expired())
					continue;		

				//... that uses the current material
				if (currentMaterial->GetID() != material.lock()->GetID())
					continue;

				// Skip transparent meshes (for now)
				if (material.lock()->GetOpacity() < 1.0f)
					continue;

				// Make sure the mesh is actually in our view frustrum
				if (!IsInViewFrustrum(m_camera->GetFrustrum(), meshFilter))
					continue;

				// Set shader buffer(s)
				currentShader->SetBuffers(mWorld, mView, mProjection, currentMaterial, m_directionalLight, meshRenderer->GetReceiveShadows(), m_camera);
				
				// Set mesh buffer
				if (meshFilter->SetBuffers())
				{
					// Set face culling (changes only if required)
					graphics->SetCullMode(material.lock()->GetFaceCullMode());

					// Render the mesh, finally!				
					meshRenderer->Render(mesh.lock()->GetIndexCount());

					m_meshesRendered++;
				}
			} // renderable loop

			m_textures.clear();
			m_textures.shrink_to_fit();

		} // material loop
	} // shader loop
}

bool Renderer::IsInViewFrustrum(const shared_ptr<Frustrum>& cameraFrustrum, MeshFilter* meshFilter)
{
	Vector3 center = meshFilter->GetCenter();
	Vector3 extent = meshFilter->GetBoundingBox();

	float radius = max(abs(extent.x), abs(extent.y));
	radius = max(radius, abs(extent.z));

	if (cameraFrustrum->CheckSphere(center, radius) == Outside)
		return false;

	return true;
}

void Renderer::DeferredPass()
{
	if (!m_shaderDeferred->IsCompiled())
		return;

	Ping();

	// Setting a texture array instead of multiple textures is faster
	m_texArrayDeferredPass.clear();
	m_texArrayDeferredPass.shrink_to_fit();
	m_texArrayDeferredPass.push_back(m_GBuffer->GetShaderResourceView(0)); // albedo
	m_texArrayDeferredPass.push_back(m_GBuffer->GetShaderResourceView(1)); // normal
	m_texArrayDeferredPass.push_back(m_GBuffer->GetShaderResourceView(2)); // depth
	m_texArrayDeferredPass.push_back(m_GBuffer->GetShaderResourceView(3)); // material
	m_texArrayDeferredPass.push_back(m_texNoiseMap->GetID3D11ShaderResourceView());

	if (m_skybox)
		m_texEnvironment = m_skybox->GetEnvironmentTexture();
	else
		m_texEnvironment = nullptr;

	// deferred rendering
	m_shaderDeferred->Render(
		m_fullScreenQuad->GetIndexCount(),
		Matrix::Identity,
		mView,
		mBaseView,
		mProjection,
		mOrthographicProjection,
		m_lightsDirectional,
		m_lightsPoint,
		m_camera,
		m_texArrayDeferredPass,
		m_texEnvironment
	);
}

void Renderer::PostProcessing() const
{
	Pong();

	// fxaa pass
	m_shaderFXAA->Render(
		m_fullScreenQuad->GetIndexCount(),
		Matrix::Identity,
		mBaseView,
		mOrthographicProjection,
		m_renderTexPing->GetShaderResourceView()
	);

	g_context->GetSubsystem<Graphics>()->ResetRenderTarget();
	g_context->GetSubsystem<Graphics>()->ResetViewport();
	g_context->GetSubsystem<Graphics>()->Clear(m_camera->GetClearColor());

	// sharpening pass
	m_shaderSharpening->Render(
		m_fullScreenQuad->GetIndexCount(),
		Matrix::Identity,
		mBaseView,
		mOrthographicProjection,
		m_renderTexPong->GetShaderResourceView()
	);
}

void Renderer::Gizmos() const
{
	if (!m_lineRenderer)
		return;

	if (!g_context->GetSubsystem<PhysicsWorld>()->GetPhysicsDebugDraw()->IsDirty())
		return;

	// Pass the line list from bullet to the line renderer component
	m_lineRenderer->AddLineList(g_context->GetSubsystem<PhysicsWorld>()->GetPhysicsDebugDraw()->GetLines());

	// Set the buffer
	m_lineRenderer->SetBuffer();

	// Render
	m_shaderDebug->Render(
		m_lineRenderer->GetVertexCount(),
		Matrix::Identity,
		m_camera->GetViewMatrix(),
		m_camera->GetProjectionMatrix(),
		m_GBuffer->GetShaderResourceView(2) // depth
	);
}

void Renderer::Ping() const
{
	Vector4 clearColor = m_camera ? m_camera->GetClearColor() : Vector4(0.0f, 0.0f, 0.0f, 1.0f);

	m_renderTexPing->SetAsRenderTarget();
	m_renderTexPing->Clear(clearColor);
}

void Renderer::Pong() const
{
	Vector4 clearColor = m_camera ? m_camera->GetClearColor() : Vector4(0.0f, 0.0f, 0.0f, 1.0f);

	m_renderTexPong->SetAsRenderTarget();
	m_renderTexPong->Clear(clearColor);
}

//= STATS ============================
void Renderer::StartCalculatingStats()
{
	m_meshesRendered = 0;
}

void Renderer::StopCalculatingStats()
{
	// meshes rendered
	m_renderedMeshesCount = m_meshesRendered;
}

int Renderer::GetRenderedMeshesCount() const
{
	return m_renderedMeshesCount;
}
//====================================