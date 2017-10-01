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

//= INCLUDES ===============================
#include "Renderer.h"
#include "Gbuffer.h"
#include "Rectangle.h"
#include "Material.h"
#include "Mesh.h"
#include "Grid.h"
#include "D3D11/D3D11RenderTexture.h"
#include "../Components/MeshFilter.h"
#include "../Components/Transform.h"
#include "../Components/MeshRenderer.h"
#include "../Components/Skybox.h"
#include "../Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Logging/Log.h"
#include "../EventSystem/EventSystem.h"
#include "../Core/Scene.h"
#include "../Core/GameObject.h"
#include "../Core/Context.h"
#include "../Resource/ResourceManager.h"
#include "../Font/Font.h"
#include "../Profiling/PerformanceProfiler.h"
#include "DeferredShaders/ShaderVariation.h"
#include "DeferredShaders/DeferredShader.h"
#include "Shader.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Renderer::Renderer(Context* context) : Subsystem(context)
	{
		m_skybox = nullptr;
		m_camera = nullptr;
		m_texEnvironment = nullptr;
		m_lineRenderer = nullptr;
		m_nearPlane = 0.0f;
		m_farPlane = 0.0f;
		m_resourceMng = nullptr;
		m_graphics = nullptr;
		m_renderFlags = 0;
		m_renderFlags |= Render_Physics;
		m_renderFlags |= Render_Bounding_Boxes;
		m_renderFlags |= Render_Mouse_Picking_Ray;
		m_renderFlags |= Render_Grid;
		m_renderFlags |= Render_Performance_Metrics;
		m_renderFlags |= Render_Light;

		// Subscribe to render event
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, this, Renderer::Render);
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Get Graphics subsystem
		m_graphics = m_context->GetSubsystem<Graphics>();
		if (!m_graphics->IsInitialized())
		{
			LOG_ERROR("Renderer: Can't initialize, the Graphics subsystem uninitialized.");
			return false;
		}

		// Get ResourceManager subsystem
		m_resourceMng = m_context->GetSubsystem<ResourceManager>();

		// Create G-Buffer
		m_GBuffer = make_unique<GBuffer>(m_graphics);
		m_GBuffer->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		// Create fullscreen rectangle
		m_fullScreenRect = make_unique<Rectangle>(m_context);
		m_fullScreenRect->Create(0, 0, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		// Get standard resource directories
		string shaderDirectory = m_resourceMng->GetStandardResourceDirectory(Shader_Resource);
		string textureDirectory = m_resourceMng->GetStandardResourceDirectory(Texture_Resource);

		// Deferred shader
		m_shaderDeferred = make_unique<DeferredShader>();
		m_shaderDeferred->Load(shaderDirectory + "Deferred.hlsl", m_graphics);

		// Line shader
		m_shaderLine = make_unique<Shader>(m_context);
		m_shaderLine->Load(shaderDirectory + "Line.hlsl");
		m_shaderLine->SetInputLaytout(PositionColor);
		m_shaderLine->AddSampler(Linear_Sampler);
		m_shaderLine->AddBuffer(W_V_P, VertexShader);

		// Depth shader
		m_shaderDepth = make_unique<Shader>(m_context);
		m_shaderDepth->Load(shaderDirectory + "Depth.hlsl");
		m_shaderDepth->SetInputLaytout(Position);
		m_shaderDepth->AddBuffer(WVP, VertexShader);

		// Grid shader
		m_shaderGrid = make_unique<Shader>(m_context);
		m_shaderGrid->Load(shaderDirectory + "Grid.hlsl");
		m_shaderGrid->SetInputLaytout(PositionColor);
		m_shaderGrid->AddSampler(Anisotropic_Sampler);
		m_shaderGrid->AddBuffer(WVP, VertexShader);

		// Font shader
		m_shaderFont = make_unique<Shader>(m_context);
		m_shaderFont->Load(shaderDirectory + "Font.hlsl");
		m_shaderFont->SetInputLaytout(PositionTexture);
		m_shaderFont->AddSampler(Point_Sampler);
		m_shaderFont->AddBuffer(WVP_Color, Global);

		// Texture shader
		m_shaderTexture = make_unique<Shader>(m_context);
		m_shaderTexture->Load(shaderDirectory + "Texture.hlsl");
		m_shaderTexture->SetInputLaytout(PositionTexture);
		m_shaderTexture->AddSampler(Anisotropic_Sampler);
		m_shaderTexture->AddBuffer(WVP, PixelShader);

		// FXAA Shader
		m_shaderFXAA = make_unique<Shader>(m_context);
		m_shaderFXAA->AddDefine("FXAA");
		m_shaderFXAA->Load(shaderDirectory + "PostProcess.hlsl");
		m_shaderFXAA->SetInputLaytout(PositionTexture);
		m_shaderFXAA->AddSampler(Anisotropic_Sampler);
		m_shaderFXAA->AddSampler(Linear_Sampler);
		m_shaderFXAA->AddBuffer(WVP_Resolution, Global);

		// Sharpening shader
		m_shaderSharpening = make_unique<Shader>(m_context);
		m_shaderSharpening->AddDefine("SHARPENING");
		m_shaderSharpening->Load(shaderDirectory + "PostProcess.hlsl");
		m_shaderSharpening->SetInputLaytout(PositionTexture);
		m_shaderSharpening->AddSampler(Anisotropic_Sampler);
		m_shaderSharpening->AddSampler(Linear_Sampler);
		m_shaderSharpening->AddBuffer(WVP_Resolution, Global);

		// Blur shader
		m_shaderBlur = make_unique<Shader>(m_context);
		m_shaderBlur->AddDefine("BLUR");
		m_shaderBlur->Load(shaderDirectory + "PostProcess.hlsl");
		m_shaderBlur->SetInputLaytout(PositionTexture);
		m_shaderBlur->AddSampler(Anisotropic_Sampler);
		m_shaderBlur->AddSampler(Linear_Sampler);
		m_shaderBlur->AddBuffer(WVP_Resolution, Global);

		// Create render textures (used for post-processing)
		m_renderTexPing = make_unique<D3D11RenderTexture>(m_graphics);
		m_renderTexPing->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
		m_renderTexPong = make_unique<D3D11RenderTexture>(m_graphics);
		m_renderTexPong->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
	
		// Gizmo icons
		m_gizmoLightTex = make_unique<Texture>(m_context);
		m_gizmoLightTex->LoadFromFile(textureDirectory + "light.png");
		m_gizmoLightTex->SetTextureType(Albedo_Texture);
		m_gizmoLightRect = make_unique<Rectangle>(m_context);
		m_gizmoLightRect->Create(100, 100, m_gizmoLightTex->GetWidth(), m_gizmoLightTex->GetHeight());

		// Performance Metrics
		m_font = make_unique<Font>(m_context);
		string fontDir = m_resourceMng->GetStandardResourceDirectory(Font_Resource);
		m_font->SetSize(12);
		m_font->SetColor(Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		m_font->LoadFromFile(fontDir + "CalibriBold.ttf");
		m_grid = make_unique<Grid>(m_context);
		m_grid->BuildGrid();

		// Noise texture (used by SSAO shader)
		m_texNoiseMap = make_unique<Texture>(m_context);
		m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
		m_texNoiseMap->SetTextureType(Normal_Texture);

		return true;
	}

	void Renderer::Render()
	{
		if (!m_graphics)
			return;

		if (!m_graphics->IsInitialized())
			return;

		PerformanceProfiler::RenderingStarted();
		AcquirePrerequisites();

		// If there is no camera, clear to black and present
		if (!m_camera)
		{
			m_graphics->Clear(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
			m_graphics->Present();
			return;
		}

		// If there is nothing to render clear to camera's color and present
		if (m_renderables.empty())
		{
			m_graphics->Clear(m_camera->GetClearColor());
			m_graphics->Present();
			return;
		}

		// ENABLE Z-BUFFER
		m_graphics->EnableDepth(true);

		// Render light depth
		DirectionalLightDepthPass();

		// G-Buffer
		GBufferPass();

		// DISABLE Z-BUFFER
		m_graphics->EnableDepth(false);

		// Deferred Pass
		DeferredPass();

		// Post Processing
		PostProcessing();

		// Render debug info
		DebugDraw();

		// display frame
		m_graphics->Present();

		PerformanceProfiler::RenderingFinished();
	}

	void Renderer::SetResolution(int width, int height)
	{
		// A resolution of 0 will cause the depth stencil 
		// buffer creation to fail, let's prevent that.
		if (width <= 0 || height <= 0)
			return;

		SET_RESOLUTION(width, height);
		m_graphics->SetResolution(width, height);

		m_GBuffer.reset();
		m_GBuffer = make_unique<GBuffer>(m_graphics);
		m_GBuffer->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_fullScreenRect.reset();
		m_fullScreenRect = make_unique<Rectangle>(m_context);
		m_fullScreenRect->Create(0, 0, RESOLUTION_WIDTH, RESOLUTION_HEIGHT);

		m_renderTexPing.reset();
		m_renderTexPing = make_unique<D3D11RenderTexture>(m_graphics);
		m_renderTexPing->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);

		m_renderTexPong.reset();
		m_renderTexPong = make_unique<D3D11RenderTexture>(m_graphics);
		m_renderTexPong->Create(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, false);
	}

	void Renderer::SetViewport(float width, float height)
	{
		SET_VIEWPORT(width, height);
		m_graphics->SetViewport(width, height);
	}

	void Renderer::Clear()
	{
		m_renderables.clear();
		m_renderables.shrink_to_fit();

		m_lights.clear();
		m_lights.shrink_to_fit();
		m_directionalLight = nullptr;
	}

	void Renderer::AcquirePrerequisites()
	{
		Clear();
		Scene* scene = m_context->GetSubsystem<Scene>();
		m_renderables = scene->GetRenderables();
		m_lights = scene->GetLights();

		// Get directional light
		for (const auto& light : m_lights)
		{
			if (light->GetLightType() == Directional)
			{
				m_directionalLight = light;
				break;
			}
		}

		// Get camera and camera related properties
		weakGameObj camera = scene->GetMainCamera();
		if (!camera.expired())
		{
			m_camera = camera.lock()->GetComponent<Camera>();

			weakGameObj skybox = scene->GetSkybox();
			if (!skybox.expired())
			{
				m_skybox = skybox.lock()->GetComponent<Skybox>();
				m_lineRenderer = skybox.lock()->GetComponent<LineRenderer>(); // Hush hush...
			}

			mView = m_camera->GetViewMatrix();
			mProjection = m_camera->GetProjectionMatrix();
			mViewProjection = mView * mProjection;
			mOrthographicProjection = Matrix::CreateOrthographicLH(RESOLUTION_WIDTH, RESOLUTION_HEIGHT, m_nearPlane, m_farPlane);
			mBaseView = m_camera->GetBaseViewMatrix();
			m_nearPlane = m_camera->GetNearPlane();
			m_farPlane = m_camera->GetFarPlane();
		}
		else
		{
			m_camera = nullptr;
			m_skybox = nullptr;
			m_lineRenderer = nullptr;
		}
	}

	void Renderer::DirectionalLightDepthPass()
	{
		if (!m_directionalLight)
			return;

		if (m_directionalLight->GetShadowType() == No_Shadows)
			return;

		//m_graphics->SetCullMode(CullFront);
		m_shaderDepth->Set();

		for (int cascadeIndex = 0; cascadeIndex < m_directionalLight->GetShadowCascadeCount(); cascadeIndex++)
		{
			// Set appropriate shadow map as render target
			m_directionalLight->SetShadowCascadeAsRenderTarget(cascadeIndex);

			Matrix mViewLight = m_directionalLight->ComputeViewMatrix();
			Matrix mProjectionLight = m_directionalLight->ComputeOrthographicProjectionMatrix(cascadeIndex);

			for (const auto& gameObj : m_renderables)
			{
				if (gameObj.expired())
					continue;

				MeshFilter* meshFilter = gameObj._Get()->GetMeshFilter();
				MeshRenderer* meshRenderer = gameObj._Get()->GetMeshRenderer();
				auto material = meshRenderer->GetMaterial();
				auto mesh = meshFilter->GetMesh();

				// Make sure we have everything
				if (mesh.expired() || !meshFilter || !meshRenderer || material.expired())
					continue;

				// Skip meshes that don't cast shadows
				if (!meshRenderer->GetCastShadows())
					continue;

				// Skip transparent meshes (for now)
				if (material._Get()->GetOpacity() < 1.0f)
					continue;

				// skip objects outside of the view frustrum
				//if (!m_directionalLight->IsInViewFrustrum(meshFilter))
					//continue;

				if (meshFilter->SetBuffers())
				{
					m_shaderDepth->SetBuffer(gameObj._Get()->GetTransform()->GetWorldTransform(), mViewLight, mProjectionLight, 0);
					m_shaderDepth->DrawIndexed(mesh._Get()->GetIndexCount());
				}
			}
		}
	}

	void Renderer::GBufferPass()
	{
		if (!m_graphics)
			return;

		m_GBuffer->SetAsRenderTarget();
		m_graphics->SetViewport();
		m_GBuffer->Clear();

		vector<weak_ptr<Material>> materials = m_resourceMng->GetResourcesByType<Material>();
		vector<weak_ptr<ShaderVariation>> shaders = m_resourceMng->GetResourcesByType<ShaderVariation>();

		for (const auto& shader : shaders) // SHADER ITERATION
		{
			// Set the shader
			shader._Get()->Set();

			// UPDATE PER FRAME BUFFER
			shader._Get()->UpdatePerFrameBuffer(m_directionalLight, m_camera);

			for (const auto& material : materials) // MATERIAL ITERATION
			{
				// Continue only if the material at hand happens to use the already set shader
				if (!material._Get()->GetShader().expired() && (material._Get()->GetShader()._Get()->GetResourceID() != shader._Get()->GetResourceID()))
					continue;

				// UPDATE PER MATERIAL BUFFER
				shader._Get()->UpdatePerMaterialBuffer(material);

				// Order the textures they way the shader expects them
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Albedo_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Roughness_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Metallic_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Normal_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Height_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Occlusion_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Emission_Texture));
				m_textures.push_back((ID3D11ShaderResourceView*)material._Get()->GetShaderResource(Mask_Texture));

				if (m_directionalLight)
				{
					for (int i = 0; i < m_directionalLight->GetShadowCascadeCount(); i++)
					{
						auto shadowMap = m_directionalLight->GetShadowCascade(i).lock();
						m_textures.push_back(shadowMap ? shadowMap->GetShaderResource() : nullptr);
					}
				}
				else
				{
					m_textures.push_back(nullptr);
					m_textures.push_back(nullptr);
					m_textures.push_back(nullptr);
				}

				// UPDATE TEXTURE BUFFER
				shader._Get()->UpdateTextures(m_textures);
				//==================================================================================

				for (const auto& gameObj : m_renderables) // GAMEOBJECT/MESH ITERATION
				{
					if (gameObj.expired())
						continue;

					//= Get all that we need =========================================
					MeshFilter* meshFilter = gameObj._Get()->GetMeshFilter();
					MeshRenderer* meshRenderer = gameObj._Get()->GetMeshRenderer();
					auto objMesh = meshFilter->GetMesh()._Get();
					auto objMaterial = meshRenderer->GetMaterial()._Get();
					auto mWorld = gameObj._Get()->GetTransform()->GetWorldTransform();
					//================================================================

					// skip objects that are missing required components
					if (!meshFilter || !objMesh || !meshRenderer || !objMaterial)
						continue;

					// skip objects that use a different material
					if (material._Get()->GetResourceID() != objMaterial->GetResourceID())
						continue;

					// skip transparent objects (for now)
					if (objMaterial->GetOpacity() < 1.0f)
						continue;

					// skip objects outside of the view frustrum
					if (!m_camera->IsInViewFrustrum(meshFilter))
						continue;

					// UPDATE PER OBJECT BUFFER
					shader._Get()->UpdatePerObjectBuffer(mWorld, mView, mProjection, meshRenderer->GetReceiveShadows());

					// Set mesh buffer
					if (meshFilter->HasMesh())
					{
						if (meshFilter->SetBuffers())
						{
							// Set face culling (changes only if required)
							m_graphics->SetCullMode(objMaterial->GetCullMode());

							// Render the mesh, finally!				
							meshRenderer->Render(objMesh->GetIndexCount());

							PerformanceProfiler::RenderingMesh();
						}
					}
				} // GAMEOBJECT/MESH ITERATION

				m_textures.clear();

			} // MATERIAL ITERATION
		} // SHADER ITERATION
	}

	//= HELPER FUNCTIONS ==============================================================================================
	void Renderer::DeferredPass()
	{
		m_fullScreenRect->SetBuffer();
		m_graphics->SetCullMode(CullBack);

		//= SHADOW BLUR ========================================
		if (m_directionalLight)
		{
			if (m_directionalLight->GetShadowType() == Soft_Shadows)
			{
				// Set pong texture as render target
				m_renderTexPong->SetAsRenderTarget();
				m_renderTexPong->Clear(GetClearColor());

				// BLUR
				m_shaderBlur->Set();
				m_shaderBlur->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, GET_RESOLUTION, 0);
				m_shaderBlur->SetTexture(m_GBuffer->GetShaderResource(1), 0); // Normal tex but shadows are in alpha channel
				m_shaderBlur->DrawIndexed(m_fullScreenRect->GetIndexCount());
			}
		}
		//=====================================================

		if (!m_shaderDeferred->IsCompiled())
			return;

		// Set the deferred shader
		m_shaderDeferred->Set();

		// Set render target
		m_renderTexPing->SetAsRenderTarget();
		m_renderTexPing->Clear(GetClearColor());

		// Update buffers
		m_shaderDeferred->UpdateMatrixBuffer(Matrix::Identity, mView, mBaseView, mProjection, mOrthographicProjection);
		m_shaderDeferred->UpdateMiscBuffer(m_lights, m_camera);

		//= Update textures ===========================================================
		m_texArray.clear();
		m_texArray.shrink_to_fit();
		m_texArray.push_back(m_GBuffer->GetShaderResource(0)); // albedo
		m_texArray.push_back(m_GBuffer->GetShaderResource(1)); // normal
		m_texArray.push_back(m_GBuffer->GetShaderResource(2)); // depth
		m_texArray.push_back(m_GBuffer->GetShaderResource(3)); // material
		m_texArray.push_back((ID3D11ShaderResourceView*)m_texNoiseMap->GetShaderResource());
		m_texArray.push_back(m_renderTexPong->GetShaderResourceView()); // contains blurred shadows
		m_texArray.push_back(m_skybox ? (ID3D11ShaderResourceView*)m_skybox->GetEnvironmentTexture() : nullptr);

		m_shaderDeferred->UpdateTextures(m_texArray);
		//=============================================================================

		m_shaderDeferred->Render(m_fullScreenRect->GetIndexCount());
	}

	void Renderer::PostProcessing()
	{
		m_fullScreenRect->SetBuffer();
		m_graphics->SetCullMode(CullBack);

		if ((m_renderFlags & Render_Albedo) || (m_renderFlags & Render_Normal) || (m_renderFlags & Render_Depth) || (m_renderFlags & Render_Material))
		{
			m_graphics->SetBackBufferAsRenderTarget();
			m_graphics->SetViewport();
			m_graphics->Clear(m_camera->GetClearColor());

			int texIndex = 0;
			if (m_renderFlags & Render_Albedo)
			{
				texIndex = 0;
			}
			else if (m_renderFlags & Render_Normal)
			{
				texIndex = 1;
			}
			else if (m_renderFlags & Render_Depth)
			{
				texIndex = 2;
			}
			else if (m_renderFlags & Render_Material)
			{
				texIndex = 3;
			}

			// TEXTURE
			m_shaderTexture->Set();
			m_shaderTexture->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, 0);
			m_shaderTexture->SetTexture(m_GBuffer->GetShaderResource(texIndex), 0);
			m_shaderTexture->DrawIndexed(m_fullScreenRect->GetIndexCount());

			return;
		}

		// Set pong texture as render target
		m_renderTexPong->SetAsRenderTarget();
		m_renderTexPong->Clear(GetClearColor());

		// FXAA
		m_shaderFXAA->Set();
		m_shaderFXAA->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, GET_RESOLUTION, 0);
		m_shaderFXAA->SetTexture(m_renderTexPing->GetShaderResourceView(), 0);
		m_shaderFXAA->DrawIndexed(m_fullScreenRect->GetIndexCount());

		// Set back-buffer
		m_graphics->SetBackBufferAsRenderTarget();
		m_graphics->SetViewport();
		m_graphics->Clear(m_camera->GetClearColor());

		// SHARPENING
		m_shaderSharpening->Set();
		m_shaderSharpening->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, GET_RESOLUTION, 0);
		m_shaderSharpening->SetTexture(m_renderTexPong->GetShaderResourceView(), 0);
		m_shaderSharpening->DrawIndexed(m_fullScreenRect->GetIndexCount());
	}

	void Renderer::DebugDraw()
	{
		//= PRIMITIVES ===================================================================================
		// Anything that is a bunch of vertices (doesn't have a vertex and and index buffer) get's rendered here
		// by passing it's vertices (VertexPosCol) to the LineRenderer. Typically used only for debugging.
		if (m_lineRenderer)
		{
			m_lineRenderer->ClearVertices();

			// Physics
			if (m_renderFlags & Render_Physics)
			{
				Physics* physics = m_context->GetSubsystem<Physics>();
				physics->DebugDraw();
				if (physics->GetPhysicsDebugDraw()->IsDirty())
				{
					m_lineRenderer->AddLines(physics->GetPhysicsDebugDraw()->GetLines());
				}
			}

			// Picking ray
			if (m_renderFlags & Render_Mouse_Picking_Ray)
			{
				m_lineRenderer->AddLines(m_camera->GetPickingRay());
			}

			// bounding boxes
			if (m_renderFlags & Render_Bounding_Boxes)
			{
				for (const auto& gameObject : m_renderables)
				{
					auto meshFilter = gameObject._Get()->GetComponent<MeshFilter>();
					m_lineRenderer->AddBoundigBox(meshFilter->GetBoundingBoxTransformed(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
				}
			}

			if (m_lineRenderer->GetVertexCount() != 0)
			{
				// Render
				m_lineRenderer->SetBuffer();
				m_shaderLine->Set();
				m_shaderLine->SetBuffer(Matrix::Identity, m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix(), 0);
				m_shaderLine->SetTexture(m_GBuffer->GetShaderResource(2), 0); // depth
				m_shaderLine->Draw(m_lineRenderer->GetVertexCount());
			}
		}
		//============================================================================================================

		m_graphics->EnableAlphaBlending(true);

		// Grid
		if (m_renderFlags & Render_Grid)
		{
			m_grid->SetBuffer();
			m_shaderGrid->Set();
			m_shaderGrid->SetBuffer(m_grid->ComputeWorldMatrix(m_camera->g_transform), m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix(), 0);
			m_shaderGrid->SetTexture(m_GBuffer->GetShaderResource(2), 0);
			m_shaderGrid->DrawIndexed(m_grid->GetIndexCount());
		}

		// Light gizmo
		if (m_renderFlags & Render_Light)
		{
			m_gizmoLightRect->SetBuffer();
			m_shaderTexture->Set();
			m_shaderTexture->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, 0);
			m_shaderTexture->SetTexture((ID3D11ShaderResourceView*)m_gizmoLightTex->GetShaderResource(), 0);
			m_shaderTexture->DrawIndexed(m_gizmoLightRect->GetIndexCount());
		}

		// Performance metrics
		if (m_renderFlags & Render_Performance_Metrics)
		{
			m_font->SetText(PerformanceProfiler::GetMetrics(), Vector2(-RESOLUTION_WIDTH * 0.5f + 1.0f, RESOLUTION_HEIGHT * 0.5f));
			m_font->SetBuffer();

			m_shaderFont->Set();
			m_shaderFont->SetBuffer(Matrix::Identity, mBaseView, mOrthographicProjection, m_font->GetColor(), 0);
			m_shaderFont->SetTexture((ID3D11ShaderResourceView*)m_font->GetShaderResource(), 0);
			m_shaderFont->DrawIndexed(m_font->GetIndexCount());
		}

		m_graphics->EnableAlphaBlending(false);
	}

	const Vector4& Renderer::GetClearColor()
	{
		return m_camera ? m_camera->GetClearColor() : Vector4(0.0f, 0.0f, 0.0f, 1.0f);
	}
	//===============================================================================================================
}
