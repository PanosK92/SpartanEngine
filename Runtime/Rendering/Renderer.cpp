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

//= INCLUDES ================================
#include "Renderer.h"
#include "Rectangle.h"
#include "Grid.h"
#include "Font.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/LightShader.h"
#include "Deferred/GBuffer.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommonBuffers.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_PipelineState.h"
#include "../RHI/RHI_RenderTexture.h"
#include "../RHI/RHI_Texture.h"
#include "../RHI/RHI_Shader.h"
#include "../Scene/Actor.h"
#include "../Scene/Components/Transform.h"
#include "../Scene/Components/Renderable.h"
#include "../Scene/Components/Skybox.h"
#include "../Scene/Components/LineRenderer.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
//===========================================

//= NAMESPACES ========================
using namespace std;
using namespace Directus::Math;
using namespace Directus::Math::Helper;
//=====================================

#define GIZMO_MAX_SIZE 5.0f
#define GIZMO_MIN_SIZE 0.1f

namespace Directus
{
	static Physics* g_physics				= nullptr;
	static ResourceManager* g_resourceMng	= nullptr;
	unsigned long Renderer::m_flags;

	Renderer::Renderer(Context* context, void* drawHandle) : Subsystem(context)
	{
		m_lineRenderer				= nullptr;
		m_nearPlane					= 0.0f;
		m_farPlane					= 0.0f;
		m_rhiDevice					= nullptr;
		m_flags						= 0;
		m_flags						|= Render_Physics;
		m_flags						|= Render_SceneGrid;
		m_flags						|= Render_Light;
		m_flags						|= Render_Bloom;
		m_flags						|= Render_FXAA;
		m_flags						|= Render_Sharpening;
		m_flags						|= Render_ChromaticAberration;
		m_flags						|= Render_Correction;

		// Create RHI device
		m_rhiDevice			= make_shared<RHI_Device>(drawHandle);
		m_rhiPipelineState	= make_shared<RHI_PipelineState>(m_rhiDevice);

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_SCENE_RESOLVED, EVENT_HANDLER_VARIANT(Renderables_Acquire));
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Create/Get required systems		
		g_resourceMng		= m_context->GetSubsystem<ResourceManager>();
		g_physics			= m_context->GetSubsystem<Physics>();

		// Get standard resource directories
		string fontDir			= g_resourceMng->GetStandardResourceDirectory(Resource_Font);
		string shaderDirectory	= g_resourceMng->GetStandardResourceDirectory(Resource_Shader);
		string textureDirectory = g_resourceMng->GetStandardResourceDirectory(Resource_Texture);

		// Load a font (used for performance metrics)
		m_font = make_unique<Font>(m_context, fontDir + "CalibriBold.ttf", 12, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		// Make a grid (used in editor)
		m_grid = make_unique<Grid>(m_context);
		// Light gizmo icon rectangle
		m_gizmoRectLight = make_unique<Rectangle>(m_context);

		RenderTargets_Create(Settings::Get().GetResolutionWidth(), Settings::Get().GetResolutionHeight());

		// SAMPLERS
		{
			m_samplerPointClampAlways		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Point,			Texture_Address_Clamp,	Texture_Comparison_Always);
			m_samplerPointClampGreater		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Point,			Texture_Address_Clamp,	Texture_Comparison_GreaterEqual);
			m_samplerLinearClampGreater		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Linear,			Texture_Address_Clamp,	Texture_Comparison_GreaterEqual);
			m_samplerLinearClampAlways		= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Linear,			Texture_Address_Clamp,	Texture_Comparison_Always);
			m_samplerBilinearClampAlways	= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Bilinear,		Texture_Address_Clamp,	Texture_Comparison_Always);
			m_samplerAnisotropicWrapAlways	= make_shared<RHI_Sampler>(m_rhiDevice, Texture_Sampler_Anisotropic,	Texture_Address_Wrap,	Texture_Comparison_Always);
		}

		// SHADERS
		{
			// Light
			m_shaderLight = make_shared<LightShader>();
			m_shaderLight->Compile(shaderDirectory + "Light.hlsl", m_rhiDevice);

			// Line
			m_shaderLine = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderLine->Compile_VertexPixel(shaderDirectory + "Line.hlsl", Input_PositionColor);
			m_shaderLine->AddBuffer<Struct_Matrix_Matrix_Matrix>(0, Buffer_VertexShader);

			// Depth
			m_shaderLightDepth = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderLightDepth->Compile_VertexPixel(shaderDirectory + "ShadowingDepth.hlsl", Input_Position);
			m_shaderLightDepth->AddBuffer<Struct_Matrix>(0, Buffer_VertexShader);

			// Grid
			m_shaderGrid = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderGrid->Compile_VertexPixel(shaderDirectory + "Grid.hlsl", Input_PositionColor);
			m_shaderGrid->AddBuffer<Struct_Matrix>(0, Buffer_VertexShader);;

			// Font
			m_shaderFont = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderFont->Compile_VertexPixel(shaderDirectory + "Font.hlsl", Input_PositionTexture);
			m_shaderFont->AddBuffer<Struct_Matrix_Vector4>(0, Buffer_Global);

			// Texture
			m_shaderTexture = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTexture->Compile_VertexPixel(shaderDirectory + "Texture.hlsl", Input_PositionTexture);
			m_shaderTexture->AddBuffer<Struct_Matrix>(0, Buffer_VertexShader);

			// FXAA
			m_shaderFXAA = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderFXAA->AddDefine("PASS_FXAA");
			m_shaderFXAA->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderFXAA->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Sharpening
			m_shaderSharpening = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderSharpening->AddDefine("PASS_SHARPENING");
			m_shaderSharpening->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderSharpening->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Chromatic aberration
			m_shaderChromaticAberration = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderChromaticAberration->AddDefine("PASS_CHROMATIC_ABERRATION");
			m_shaderChromaticAberration->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderChromaticAberration->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Blur Box
			m_shaderBlurBox = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurBox->AddDefine("PASS_BLUR_BOX");
			m_shaderBlurBox->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);	
			m_shaderBlurBox->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Blur Gaussian Horizontal
			m_shaderBlurGaussianH = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurGaussianH->AddDefine("PASS_BLUR_GAUSSIAN_H");
			m_shaderBlurGaussianH->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);		
			m_shaderBlurGaussianH->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Blur Gaussian Vertical
			m_shaderBlurGaussianV = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurGaussianV->AddDefine("PASS_BLUR_GAUSSIAN_V", "1");
			m_shaderBlurGaussianV->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBlurGaussianV->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Bloom - bright
			m_shaderBloom_Bright = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBloom_Bright->AddDefine("PASS_BRIGHT", "1");
			m_shaderBloom_Bright->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBloom_Bright->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Bloom - blend
			m_shaderBloom_BlurBlend = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBloom_BlurBlend->AddDefine("PASS_BLEND_ADDITIVE");
			m_shaderBloom_BlurBlend->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);
			m_shaderBloom_BlurBlend->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Tone-mapping
			m_shaderCorrection = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderCorrection->AddDefine("PASS_CORRECTION");
			m_shaderCorrection->Compile_VertexPixel(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture);		
			m_shaderCorrection->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Transformation gizmo
			m_shaderTransformationGizmo = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTransformationGizmo->Compile_VertexPixel(shaderDirectory + "TransformationGizmo.hlsl", Input_PositionTextureTBN);
			m_shaderTransformationGizmo->AddBuffer<Struct_Matrix_Vector3_Vector3>(0, Buffer_Global);

			// Shadowing (shadow mapping & SSAO)
			m_shaderShadowing = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderShadowing->Compile_VertexPixel(shaderDirectory + "Shadowing.hlsl", Input_PositionTexture);
			m_shaderShadowing->AddBuffer<Struct_Shadowing>(0, Buffer_Global);

			// Transparent
			m_shaderTransparent = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTransparent->Compile_VertexPixel(shaderDirectory + "Transparent.hlsl", Input_PositionTextureTBN);
			m_shaderTransparent->AddBuffer<Struct_Transparency>(0, Buffer_Global);
		}

		// TEXTURES
		{
			// Noise texture (used by SSAO shader)
			m_texNoiseMap = make_shared<RHI_Texture>(m_context);
			m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");
			m_texNoiseMap->SetType(TextureType_Normal);

			// Gizmo icons
			m_gizmoTexLightDirectional = make_shared<RHI_Texture>(m_context);
			m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");
			m_gizmoTexLightDirectional->SetType(TextureType_Albedo);

			m_gizmoTexLightPoint = make_shared<RHI_Texture>(m_context);
			m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");
			m_gizmoTexLightPoint->SetType(TextureType_Albedo);

			m_gizmoTexLightSpot = make_shared<RHI_Texture>(m_context);
			m_gizmoTexLightSpot->LoadFromFile(textureDirectory + "flashlight.png");
			m_gizmoTexLightSpot->SetType(TextureType_Albedo);
		}

		return true;
	}

	void Renderer::SetBackBufferAsRenderTarget(bool clear /*= true*/)
	{
		m_rhiDevice->Bind_BackBufferAsRenderTarget();
		m_rhiPipelineState->SetViewport((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight());
		m_rhiPipelineState->Bind();
		if (clear) m_rhiDevice->ClearBackBuffer(GetClearColor());
	}

	void* Renderer::GetFrame()
	{
		return m_renderTex3 ? m_renderTex3->GetShaderResource() : nullptr;
	}

	void Renderer::Present()
	{
		m_rhiDevice->Present();
	}

	void Renderer::Render()
	{
		if (!m_rhiDevice || !m_rhiDevice->IsInitialized())
			return;

		TIME_BLOCK_START_MULTI();
		Profiler::Get().Reset();

		// If there is a camera, render the scene
		if (GetCamera())
		{
			m_mV					= GetCamera()->GetViewMatrix();
			m_mV_base				= GetCamera()->GetBaseViewMatrix();
			m_mP_perspective		= GetCamera()->GetProjectionMatrix();
			m_mP_orthographic		= Matrix::CreateOrthographicLH((float)Settings::Get().GetResolutionWidth(), (float)Settings::Get().GetResolutionHeight(), m_nearPlane, m_farPlane);		
			m_wvp_perspective		= m_mV * m_mP_perspective;
			m_wvp_baseOrthographic	= m_mV_base * m_mP_orthographic;
			m_nearPlane				= GetCamera()->GetNearPlane();
			m_farPlane				= GetCamera()->GetFarPlane();

			// If there is nothing to render clear to camera's color and present
			if (m_renderables.empty())
			{
				m_rhiDevice->ClearBackBuffer(GetCamera()->GetClearColor());
				m_rhiDevice->Present();
				return;
			}

			Pass_DepthDirectionalLight(GetLightDirectional());
		
			Pass_GBuffer();

			Pass_PreLight(
				m_renderTex1,			// IN:	Render texture		
				m_renderTexShadowing	// OUT: Render texture	- Shadowing (Shadow mapping + SSAO)
			);

			Pass_Light(
				m_renderTexShadowing,	// IN:	Texture			- Shadowing (Shadow mapping + SSAO)
				m_renderTex1			// OUT: Render texture	- Result
			);

			Pass_PostLight(
				m_renderTex1,	// IN:	Render texture - Light pass result
				m_renderTex3	// OUT: Render texture - Result
			);

			Pass_Transparent();

			// Debug rendering (on the target that happens to be bound)
			Pass_DebugGBuffer();
			Pass_Debug();
		}		
		else // If there is no camera, clear to black
		{
			m_rhiDevice->ClearBackBuffer(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		TIME_BLOCK_END_MULTI();
	}

	void Renderer::SetBackBufferSize(int width, int height)
	{
		if (width == 0 || height == 0)
			return;

		m_rhiDevice->SetResolution(width, height);
		m_rhiPipelineState->SetViewport((float)width, (float)height);
		m_rhiPipelineState->Bind();
	}

	void Renderer::SetResolution(int width, int height)
	{
		// Return if resolution is invalid
		if (width <= 0 || height <= 0)
		{
			LOG_WARNING("Renderer::SetResolutionInternal: Invalid resolution");
			return;
		}

		// Return if resolution already set
		if (Settings::Get().GetResolution().x == width && Settings::Get().GetResolution().y == height)
			return;

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

		Settings::Get().SetResolution(Vector2((float)width, (float)height));
		RenderTargets_Create(width, height);
		LOGF_INFO("Renderer::SetResolution: Resolution was set to %dx%d", width, height);
	}

	void Renderer::Clear()
	{
		m_renderables.clear();
		m_lineRenderer	= nullptr;
	}

	void Renderer::RenderTargets_Create(int width, int height)
	{
		// Resize everything
		m_gbuffer.reset();
		m_gbuffer = make_unique<GBuffer>(m_rhiDevice, width, height);

		m_quad.reset();
		m_quad = make_unique<Rectangle>(m_context);
		m_quad->Create(0, 0, (float)width, (float)height);

		m_renderTex1.reset();
		m_renderTex1 = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTex2.reset();
		m_renderTex2 = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTex3.reset();
		m_renderTex3 = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexQuarterRes1.reset();
		m_renderTexQuarterRes1 = make_unique<RHI_RenderTexture>(m_rhiDevice, int(width * 0.25f), int(height * 0.25f), false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexQuarterRes2.reset();
		m_renderTexQuarterRes2 = make_unique<RHI_RenderTexture>(m_rhiDevice, int(width * 0.25f), int(height * 0.25f), false, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexShadowing.reset();
		m_renderTexShadowing = make_unique<RHI_RenderTexture>(m_rhiDevice, int(width * 0.5f), int(height * 0.5f), false, Texture_Format_R32G32_FLOAT);
	}

	//= RENDERABLES ============================================================================================
	void Renderer::Renderables_Acquire(const Variant& renderables)
	{
		TIME_BLOCK_START_CPU();

		Clear();
		auto renderablesVec = renderables.Get<vector<weak_ptr<Actor>>>();

		for (const auto& renderable : renderablesVec)
		{
			Actor* actor = renderable.lock().get();
			if (!actor)
				continue;

			// Get all the components we are interested in
			auto renderable = actor->GetComponent<Renderable>().lock();
			auto light		= actor->GetComponent<Light>().lock();
			auto skybox		= actor->GetComponent<Skybox>().lock();
			auto camera		= actor->GetComponent<Camera>().lock();

			if (renderable)
			{
				bool isTransparent = !renderable->Material_Exists() ? false : renderable->Material_PtrRaw()->GetColorAlbedo().w < 1.0f;
				m_renderables[isTransparent ? Renderable_ObjectTransparent : Renderable_ObjectOpaque].emplace_back(actor);
			}

			if (light)
			{
				m_renderables[Renderable_Light].emplace_back(actor);
			}

			if (skybox)
			{
				m_renderables[Renderable_Skybox].emplace_back(actor);
				m_lineRenderer = actor->GetComponent<LineRenderer>().lock().get(); // Hush hush...
			}

			if (camera)
			{
				m_renderables[Renderable_Camera].emplace_back(actor);
			}
		}

		Renderables_Sort(&m_renderables[Renderable_ObjectOpaque]);
		Renderables_Sort(&m_renderables[Renderable_ObjectTransparent]);

		TIME_BLOCK_END_CPU();
	}

	void Renderer::Renderables_Sort(vector<Actor*>* renderables)
	{
		if (renderables->size() <= 2)
			return;

		sort(renderables->begin(), renderables->end(),[](Actor* a, Actor* b)
		{
			// Get renderable component
			auto a_renderable = a->GetRenderable_PtrRaw();
			auto b_renderable = b->GetRenderable_PtrRaw();

			// Validate renderable components
			if (!a_renderable || !b_renderable)
				return false;

			// Get geometry parents
			auto a_geometryModel = a_renderable->Geometry_Model();
			auto b_geometryModel = b_renderable->Geometry_Model();

			// Validate geometry parents
			if (!a_geometryModel || !b_geometryModel)
				return false;

			// Get materials
			auto a_material = a_renderable->Material_PtrRaw();
			auto b_material = b_renderable->Material_PtrRaw();

			if (!a_material || !b_material)
				return false;

			// Get key for models
			auto a_keyModel = a_geometryModel->GetResourceID();
			auto b_keyModel = b_geometryModel->GetResourceID();

			// Get key for shaders
			auto a_keyShader = a_material->GetShader().lock()->GetResourceID();
			auto b_keyShader = b_material->GetShader().lock()->GetResourceID();

			// Get key for materials
			auto a_keyMaterial = a_material->GetResourceID();
			auto b_keyMaterial = b_material->GetResourceID();

			auto a_key = 
				(((unsigned long long)a_keyModel)		<< 48u)	| 
				(((unsigned long long)a_keyShader)		<< 32u)	|
				(((unsigned long long)a_keyMaterial)	<< 16u);

			auto b_key = 
				(((unsigned long long)b_keyModel)		<< 48u)	|
				(((unsigned long long)b_keyShader)		<< 32u)	|
				(((unsigned long long)b_keyMaterial)	<< 16u);
	
			return a_key < b_key;
		});
	}
	//==========================================================================================================

	//= PASSES =================================================================================================
	void Renderer::Pass_DepthDirectionalLight(Light* light)
	{
		if (!light || !light->GetCastShadows())
			return;

		TIME_BLOCK_START_MULTI();

		m_rhiDevice->EventBegin("Pass_DepthDirectionalLight");
		m_rhiDevice->EnableDepth(true);
		m_rhiPipelineState->SetShader(m_shaderLightDepth);
		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);

		// Variables that help reduce state changes
		unsigned int currentlyBoundGeometry = 0;

		if (!m_renderables.empty())
		{
			for (unsigned int i = 0; i < light->ShadowMap_GetCount(); i++)
			{
				m_rhiDevice->EventBegin("Pass_ShadowMap_" + to_string(i));

				m_rhiPipelineState->SetRenderTexture(light->ShadowMap_GetRenderTexture(i), true);			
				Matrix viewProjection = light->GetViewMatrix() * light->ShadowMap_GetProjectionMatrix(i);

				for (const auto& actor : m_renderables[Renderable_ObjectOpaque])
				{
					// Acquire renderable component
					Renderable* renderable = actor->GetRenderable_PtrRaw();
					if (!renderable)
						continue;

					// Acquire material
					Material* material = renderable ? renderable->Material_PtrRaw() : nullptr;
					if (!material)
						continue;

					// Acquire geometry
					Model* geometry = renderable->Geometry_Model();
					if (!geometry)
						continue;

					// Skip meshes that don't cast shadows
					if (!renderable->GetCastShadows())
						continue;

					// Skip transparent meshes (for now)
					if (material->GetColorAlbedo().w < 1.0f)
						continue;

					// Bind geometry
					if (currentlyBoundGeometry != geometry->GetResourceID())
					{
						m_rhiPipelineState->SetIndexBuffer(geometry->GetIndexBuffer());
						m_rhiPipelineState->SetVertexBuffer(geometry->GetVertexBuffer());					
						currentlyBoundGeometry = geometry->GetResourceID();
					}

					// skip objects outside of the view frustum
					//if (!m_directionalLight->IsInViewFrustrum(obj_renderable, i))
					//continue;
		
					m_shaderLightDepth->UpdateBuffer(&Struct_Matrix(actor->GetTransform_PtrRaw()->GetWorldTransform() * viewProjection));
					m_rhiPipelineState->SetConstantBuffer(m_shaderLightDepth->GetConstantBuffer());
					m_rhiPipelineState->Bind();

					m_rhiDevice->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
				}

				m_rhiDevice->EventEnd();
			}
		}

		m_rhiDevice->EnableDepth(false);
		m_rhiDevice->EventEnd();

		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhiDevice)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_GBuffer");

		// Bind G-Buffer
		m_gbuffer->SetAsRenderTarget(m_rhiPipelineState);
		m_gbuffer->Clear(m_rhiDevice);

		// Bind sampler 
		m_rhiPipelineState->SetSampler(m_samplerAnisotropicWrapAlways);

		// Variables that help reduce state changes
		bool vertexShaderBound				= false;
		unsigned int currentlyBoundGeometry = 0;
		unsigned int currentlyBoundShader	= 0;
		unsigned int currentlyBoundMaterial = 0;

		for (auto actor : m_renderables[Renderable_ObjectOpaque])
		{
			// Get renderable and material
			Renderable* obj_renderable	= actor->GetRenderable_PtrRaw();
			Material* obj_material		= obj_renderable ? obj_renderable->Material_PtrRaw() : nullptr;

			if (!obj_renderable || !obj_material)
				continue;

			// Get geometry and shader
			Model* obj_geometry			= obj_renderable->Geometry_Model();
			ShaderVariation* obj_shader	= obj_material->GetShader().lock().get();

			if (!obj_geometry || !obj_shader)
				continue;

			// Skip objects outside of the view frustum
			if (!GetCamera()->IsInViewFrustrum(obj_renderable))
				continue;

			// set face culling (changes only if required)
			m_rhiPipelineState->SetCullMode(obj_material->GetCullMode());

			// Bind geometry
			if (currentlyBoundGeometry != obj_geometry->GetResourceID())
			{	
				m_rhiPipelineState->SetIndexBuffer(obj_geometry->GetIndexBuffer());
				m_rhiPipelineState->SetVertexBuffer(obj_geometry->GetVertexBuffer());
				currentlyBoundGeometry = obj_geometry->GetResourceID();
			}

			// Bind shader
			if (currentlyBoundShader != obj_shader->GetResourceID())
			{
				if (!vertexShaderBound)
				{
					m_rhiPipelineState->SetVertexShader(obj_shader->GetShader());
					vertexShaderBound = true;
				}
				m_rhiPipelineState->SetPixelShader(obj_shader->GetShader());
				currentlyBoundShader = obj_shader->GetResourceID();
			}

			// Bind material
			if (currentlyBoundMaterial != obj_material->GetResourceID())
			{
				obj_shader->UpdatePerMaterialBuffer(GetCamera(), obj_material);

				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Albedo).lock());
				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Roughness).lock());
				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Metallic).lock());
				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Normal).lock());
				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Height).lock());
				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Occlusion).lock());
				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Emission).lock());
				m_rhiPipelineState->SetTexture(obj_material->GetTextureByType(TextureType_Mask).lock());

				currentlyBoundMaterial = obj_material->GetResourceID();
			}

			// UPDATE PER OBJECT BUFFER
			obj_shader->UpdatePerObjectBuffer(
				actor->GetTransform_PtrRaw()->GetWorldTransform(),
				m_mV, 
				m_mP_perspective
			);
		
			m_rhiPipelineState->SetConstantBuffer(obj_shader->GetMaterialBuffer());
			m_rhiPipelineState->SetConstantBuffer(obj_shader->GetPerObjectBuffer());

			m_rhiPipelineState->Bind();

			// Render	
			m_rhiDevice->DrawIndexed(obj_renderable->Geometry_IndexCount(), obj_renderable->Geometry_IndexOffset(), obj_renderable->Geometry_VertexOffset());
			Profiler::Get().m_rendererMeshesRendered++;

		} // Actor/MESH ITERATION

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_PreLight(
		shared_ptr<RHI_RenderTexture>& texIn,
		shared_ptr<RHI_RenderTexture>& texOut
	)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_PreLight");

		m_rhiPipelineState->SetIndexBuffer(m_quad->GetIndexBuffer());
		m_rhiPipelineState->SetVertexBuffer(m_quad->GetVertexBuffer());
		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		
		// Shadow mapping + SSAO
		Pass_Shadowing(GetLightDirectional(), texOut);

		// Blur the shadows and the SSAO
		Pass_Blur(texOut, texIn);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Light(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (!m_shaderLight->IsCompiled())
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Light");

		m_rhiDevice->EnableDepth(false);

		// Update buffers
		m_shaderLight->UpdateMatrixBuffer(Matrix::Identity, m_mV, m_mV_base, m_mP_perspective, m_mP_orthographic);
		m_shaderLight->UpdateMiscBuffer(m_renderables[Renderable_Light], GetCamera());

		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetShader(m_shaderLight->GetShader());
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Albedo));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Specular));
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->SetTexture(m_renderTex3); // previous frame for SSR // Todo SSR
		m_rhiPipelineState->SetTexture(GetSkybox() ? GetSkybox()->GetTexture() : nullptr);
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);	
		m_rhiPipelineState->SetConstantBuffer(m_shaderLight->GetMatrixBuffer());
		m_rhiPipelineState->SetConstantBuffer(m_shaderLight->GetMiscBuffer());
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_PostLight(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_PostLight");

		// All post-process passes share the following, so set them once here
		m_rhiPipelineState->SetVertexBuffer(m_quad->GetVertexBuffer());
		m_rhiPipelineState->SetIndexBuffer(m_quad->GetIndexBuffer());
		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipelineState->SetCullMode(Cull_Back);
		m_rhiPipelineState->SetSampler(m_samplerBilinearClampAlways); // FXAA and Bloom (Gaussian blur) require a bilinear sampler
		m_rhiPipelineState->SetVertexShader(m_shaderBloom_Bright);  // vertex shader is the same for every pass
		Vector2 computeLuma = Vector2(RenderFlags_IsSet(Render_FXAA) ? 1.0f : 0.0f, 0.0f);
		auto buffer = Struct_Matrix_Vector2(m_wvp_baseOrthographic, Vector2(texIn->GetWidth(), texIn->GetHeight()), computeLuma);
		m_shaderBloom_Bright->UpdateBuffer(&buffer);
		m_rhiPipelineState->SetConstantBuffer(m_shaderBloom_Bright->GetConstantBuffer());
		

		// Keep track of render target swapping
		auto SwapTargets = [&texIn, &texOut]() { texOut.swap(texIn); };

		SwapTargets();

		// BLOOM
		if (RenderFlags_IsSet(Render_Bloom))
		{
			SwapTargets();
			Pass_Bloom(texIn, texOut);
		}

		// CORRECTION
		if (RenderFlags_IsSet(Render_Correction))
		{
			SwapTargets();
			Pass_Correction(texIn, texOut);
		}

		// FXAA
		if (RenderFlags_IsSet(Render_FXAA))
		{
			SwapTargets();
			Pass_FXAA(texIn, texOut);
		}

		// CHROMATIC ABERRATION
		if (RenderFlags_IsSet(Render_ChromaticAberration))
		{
			SwapTargets();
			Pass_ChromaticAberration(texIn, texOut);
		}

		// SHARPENING
		if (RenderFlags_IsSet(Render_Sharpening))
		{
			SwapTargets();
			Pass_Sharpening(texIn, texOut);
		}

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Transparent()
	{
		if (!GetLightDirectional() || !GetCamera())
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Transparent");

		m_rhiDevice->EnableAlphaBlending(true);
		m_rhiPipelineState->SetShader(m_shaderTransparent);
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipelineState->SetSampler(m_samplerPointClampGreater);

		for (auto actor : m_renderables[Renderable_ObjectTransparent])
		{
			// Get renderable and material
			Renderable* obj_renderable	= actor->GetRenderable_PtrRaw();
			Material* obj_material		= obj_renderable ? obj_renderable->Material_PtrRaw() : nullptr;

			if (!obj_renderable || !obj_material)
				continue;

			// Get geometry
			Model* obj_geometry = obj_renderable->Geometry_Model();
			if (!obj_geometry)
				continue;

			// Skip objects outside of the view frustum
			if (!GetCamera()->IsInViewFrustrum(obj_renderable))
				continue;

			// Set face culling (changes only if required)
			m_rhiPipelineState->SetCullMode(obj_material->GetCullMode());

			// Bind geometry
			m_rhiPipelineState->SetIndexBuffer(obj_geometry->GetIndexBuffer());
			m_rhiPipelineState->SetVertexBuffer(obj_geometry->GetVertexBuffer());

			// Constant buffer
			m_shaderTransparent->UpdateBuffer(&Struct_Transparency(
				actor->GetTransform_PtrRaw()->GetWorldTransform(),
				m_mV,
				m_mP_perspective,
				obj_material->GetColorAlbedo(),
				GetCamera()->GetTransform()->GetPosition(),
				GetLightDirectional()->GetDirection(),
				obj_material->GetRoughnessMultiplier()
			));
			m_rhiPipelineState->SetConstantBuffer(m_shaderTransparent->GetConstantBuffer());

			m_rhiPipelineState->Bind();

			// Render	
			m_rhiDevice->DrawIndexed(obj_renderable->Geometry_IndexCount(), obj_renderable->Geometry_IndexOffset(), obj_renderable->Geometry_VertexOffset());
			Profiler::Get().m_rendererMeshesRendered++;

		} // Actor/MESH ITERATION

		m_rhiDevice->EnableAlphaBlending(false);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Bloom");

		// Bright pass
		m_rhiPipelineState->SetRenderTexture(m_renderTexQuarterRes1);
		m_rhiPipelineState->SetPixelShader(m_shaderBloom_Bright);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Horizontal Gaussian blur
		m_rhiPipelineState->SetRenderTexture(m_renderTexQuarterRes2);
		m_rhiPipelineState->SetPixelShader(m_shaderBlurGaussianH);
		m_rhiPipelineState->SetTexture(m_renderTexQuarterRes1);
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		m_rhiPipelineState->SetRenderTexture(m_renderTexQuarterRes1);
		m_rhiPipelineState->SetPixelShader(m_shaderBlurGaussianV);
		m_rhiPipelineState->SetTexture(m_renderTexQuarterRes2);
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Additive blending
		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetPixelShader(m_shaderBloom_BlurBlend);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->SetTexture(m_renderTexQuarterRes1);
		float bloomIntensity = 0.1f;
		m_shaderBloom_BlurBlend->UpdateBuffer(&Struct_Matrix_Vector2(m_wvp_baseOrthographic, Vector2(texIn->GetWidth(), texIn->GetHeight()), bloomIntensity));
		m_rhiPipelineState->SetConstantBuffer(m_shaderBloom_BlurBlend->GetConstantBuffer());
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Correction(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Correction");

		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetPixelShader(m_shaderCorrection);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_FXAA(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_FXAA");

		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetPixelShader(m_shaderFXAA);	
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_ChromaticAberration(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_ChromaticAberration");

		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetPixelShader(m_shaderChromaticAberration);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Sharpening(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Sharpening");

		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetPixelShader(m_shaderSharpening);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Blur(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Blur");

		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetShader(m_shaderBlurBox);
		m_rhiPipelineState->SetTexture(texIn); // Shadows are in the alpha channel
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		m_shaderBlurBox->UpdateBuffer(&Struct_Matrix_Vector2(m_wvp_baseOrthographic, Vector2(texIn->GetWidth(), texIn->GetHeight())));
		m_rhiPipelineState->SetConstantBuffer(m_shaderBlurBox->GetConstantBuffer());
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Shadowing(
		Light* inDirectionalLight,
		shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (!inDirectionalLight)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Shadowing");

		// SHADOWING (Shadow mapping + SSAO)
		m_rhiPipelineState->SetRenderTexture(texOut);
		m_rhiPipelineState->SetShader(m_shaderShadowing);
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipelineState->SetTexture(m_texNoiseMap);
		if (inDirectionalLight)
		{
			m_rhiPipelineState->SetTexture(inDirectionalLight->ShadowMap_GetRenderTexture(0));
			m_rhiPipelineState->SetTexture(inDirectionalLight->ShadowMap_GetRenderTexture(1));
			m_rhiPipelineState->SetTexture(inDirectionalLight->ShadowMap_GetRenderTexture(2));
		}
		m_rhiPipelineState->SetSampler(m_samplerPointClampGreater);		// Shadow mapping
		m_rhiPipelineState->SetSampler(m_samplerLinearClampGreater);	// SSAO
		m_shaderShadowing->UpdateBuffer(&Struct_Shadowing
			(
				m_wvp_baseOrthographic,
				m_wvp_perspective.Inverted(),
				Vector2(texOut->GetWidth(), texOut->GetHeight()),
				inDirectionalLight,
				GetCamera()
			)
		);	
		m_rhiPipelineState->SetConstantBuffer(m_shaderShadowing->GetConstantBuffer());
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}
	//=============================================================================================================

	bool Renderer::Pass_DebugGBuffer()
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_DebugGBuffer");

		GBuffer_Texture_Type texType = GBuffer_Target_Unknown;
		texType	= RenderFlags_IsSet(Render_Albedo)		? GBuffer_Target_Albedo		: texType;
		texType = RenderFlags_IsSet(Render_Normal)		? GBuffer_Target_Normal		: texType;
		texType = RenderFlags_IsSet(Render_Specular)	? GBuffer_Target_Specular	: texType;
		texType = RenderFlags_IsSet(Render_Depth)		? GBuffer_Target_Depth		: texType;

		if (texType != GBuffer_Target_Unknown)
		{
			// TEXTURE
			m_rhiPipelineState->SetShader(m_shaderTexture);
			m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(texType));
			m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
			m_shaderTexture->UpdateBuffer(&Struct_Matrix(m_wvp_baseOrthographic));
			m_rhiPipelineState->SetConstantBuffer(m_shaderTexture->GetConstantBuffer());
			m_rhiPipelineState->Bind();

			m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
		}

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
		return true;
	}

	void Renderer::Pass_Debug()
	{
		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Debug");

		//= PRIMITIVES ===================================================================================
		// Anything that is a bunch of vertices (doesn't have a vertex and and index buffer) gets rendered here
		// by passing it's vertices (VertexPosCol) to the LineRenderer. Typically used only for debugging.
		if (m_lineRenderer)
		{
			m_lineRenderer->ClearVertices();

			// Physics
			if (m_flags & Render_Physics)
			{
				g_physics->DebugDraw();
				if (g_physics->GetPhysicsDebugDraw()->IsDirty())
				{
					m_lineRenderer->AddLines(g_physics->GetPhysicsDebugDraw()->GetLines());
				}
			}

			// Picking ray
			if (m_flags & Render_PickingRay)
			{
				m_lineRenderer->AddLines(GetCamera()->GetPickingRay());
			}

			// bounding boxes
			if (m_flags & Render_AABB)
			{
				for (const auto& renderableWeak : m_renderables[Renderable_ObjectOpaque])
				{
					if (auto renderable = renderableWeak->GetRenderable_PtrRaw())
					{
						m_lineRenderer->AddBoundigBox(renderable->Geometry_BB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}

				for (const auto& renderableWeak : m_renderables[Renderable_ObjectTransparent])
				{
					if (auto renderable = renderableWeak->GetRenderable_PtrRaw())
					{
						m_lineRenderer->AddBoundigBox(renderable->Geometry_BB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}

			if (m_lineRenderer->GetVertexCount() != 0)
			{
				m_rhiDevice->EventBegin("Lines");

				// Render
				m_lineRenderer->Update();

				m_rhiPipelineState->SetShader(m_shaderLine);
				m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
				m_rhiPipelineState->SetSampler(m_samplerPointClampGreater);
				m_rhiPipelineState->SetVertexBuffer(m_lineRenderer->GetVertexBuffer());
				m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_LineList);
				m_shaderLine->UpdateBuffer(&Struct_Matrix_Matrix_Matrix(Matrix::Identity, GetCamera()->GetViewMatrix(), GetCamera()->GetProjectionMatrix()));
				m_rhiPipelineState->SetConstantBuffer(m_shaderLine->GetConstantBuffer());
				m_rhiPipelineState->Bind();

				m_rhiDevice->Draw(m_lineRenderer->GetVertexCount());

				m_rhiDevice->EventEnd();
			}			
		}
		//============================================================================================================

		m_rhiDevice->EnableAlphaBlending(true);

		// Grid
		if (m_flags & Render_SceneGrid)
		{
			m_rhiDevice->EventBegin("Grid");

			m_rhiPipelineState->SetShader(m_shaderGrid);
			m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
			m_rhiPipelineState->SetSampler(m_samplerPointClampGreater);
			m_rhiPipelineState->SetIndexBuffer(m_grid->GetIndexBuffer());
			m_rhiPipelineState->SetVertexBuffer(m_grid->GetVertexBuffer());
			m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_LineList);
			m_shaderGrid->UpdateBuffer(&Struct_Matrix(m_grid->ComputeWorldMatrix(GetCamera()->GetTransform()) * GetCamera()->GetViewMatrix() * GetCamera()->GetProjectionMatrix()));
			m_rhiPipelineState->SetConstantBuffer(m_shaderGrid->GetConstantBuffer());
			m_rhiPipelineState->Bind();

			m_rhiDevice->DrawIndexed(m_grid->GetIndexCount(), 0, 0);

			m_rhiDevice->EventEnd();
		}

		// Gizmos
		m_rhiDevice->EventBegin("Gizmos");
		{
			if (m_flags & Render_Light)
			{
				m_rhiDevice->EventBegin("Lights");
				for (const auto& light : m_renderables[Renderable_Light])
				{
					Vector3 lightWorldPos = light->GetTransform_PtrRaw()->GetPosition();
					Vector3 cameraWorldPos = GetCamera()->GetTransform()->GetPosition();

					// Compute light screen space position and scale (based on distance from the camera)
					Vector2 lightScreenPos	= GetCamera()->WorldToScreenPoint(lightWorldPos);
					float distance			= Vector3::Length(lightWorldPos, cameraWorldPos);
					float scale				= GIZMO_MAX_SIZE / distance;
					scale					= Clamp(scale, GIZMO_MIN_SIZE, GIZMO_MAX_SIZE);

					// Skip if the light is not in front of the camera
					if (!GetCamera()->IsInViewFrustrum(lightWorldPos, Vector3(1.0f)))
						continue;

					// Skip if the light if it's too small
					if (scale == GIZMO_MIN_SIZE)
						continue;

					shared_ptr<RHI_Texture> lightTex = nullptr;
					LightType type = light->GetComponent<Light>().lock()->GetLightType();
					if (type == LightType_Directional)
					{
						lightTex = m_gizmoTexLightDirectional;
					}
					else if (type == LightType_Point)
					{
						lightTex = m_gizmoTexLightPoint;
					}
					else if (type == LightType_Spot)
					{
						lightTex = m_gizmoTexLightSpot;
					}

					// Construct appropriate rectangle
					float texWidth = lightTex->GetWidth() * scale;
					float texHeight = lightTex->GetHeight() * scale;
					m_gizmoRectLight->Create(
						lightScreenPos.x - texWidth * 0.5f,
						lightScreenPos.y - texHeight * 0.5f,
						texWidth,
						texHeight
					);

					m_rhiPipelineState->SetShader(m_shaderTexture);
					m_rhiPipelineState->SetTexture(lightTex);
					m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
					m_rhiPipelineState->SetIndexBuffer(m_gizmoRectLight->GetIndexBuffer());
					m_rhiPipelineState->SetVertexBuffer(m_gizmoRectLight->GetVertexBuffer());
					m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
					m_shaderTexture->UpdateBuffer(&Struct_Matrix(m_wvp_baseOrthographic));
					m_rhiPipelineState->SetConstantBuffer(m_shaderTexture->GetConstantBuffer());
					m_rhiPipelineState->Bind();

					m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);
				}
				m_rhiDevice->EventEnd();
			}

			// Transformation Gizmo		
			//m_rhi->EventBegin("Transformation");
			//{
			//	TransformationGizmo* gizmo = m_camera->GetTransformationGizmo();
			//	gizmo->SetBuffers();
			//	m_shaderTransformationGizmo->Bind();

			//	// X - Axis
			//	m_shaderTransformationGizmo->Bind_Buffer(gizmo->GetTransformationX() * m_mV * m_mP_perspective, Vector3::Right, Vector3::Zero, 0);
			//	m_rhi->DrawIndexed(gizmo->GetIndexCount(), 0, 0);
			//	// Y - Axis
			//	m_shaderTransformationGizmo->Bind_Buffer(gizmo->GetTransformationY() * m_mV * m_mP_perspective, Vector3::Up, Vector3::Zero, 0);
			//	m_rhi->DrawIndexed(gizmo->GetIndexCount(), 0, 0);
			//	// Z - Axis
			//	m_shaderTransformationGizmo->Bind_Buffer(gizmo->GetTransformationZ() * m_mV * m_mP_perspective, Vector3::Forward, Vector3::Zero, 0);
			//	m_rhi->DrawIndexed(gizmo->GetIndexCount(), 0, 0);
			//}
			//m_rhi->EventEnd();
			
		}
		m_rhiDevice->EventEnd();

		// Performance metrics
		if (m_flags & Render_PerformanceMetrics)
		{
			m_font->SetText(Profiler::Get().GetMetrics(), Vector2(-Settings::Get().GetResolutionWidth() * 0.5f + 1.0f, Settings::Get().GetResolutionHeight() * 0.5f));

			m_rhiPipelineState->SetShader(m_shaderFont);
			m_rhiPipelineState->SetTexture(m_font->GetTexture());
			m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
			m_rhiPipelineState->SetIndexBuffer(m_font->GetIndexBuffer());
			m_rhiPipelineState->SetVertexBuffer(m_font->GetVertexBuffer());
			m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
			m_shaderFont->UpdateBuffer(&Struct_Matrix_Vector4(m_wvp_baseOrthographic, m_font->GetColor()));
			m_rhiPipelineState->SetConstantBuffer(m_shaderFont->GetConstantBuffer());
			m_rhiPipelineState->Bind();

			m_rhiDevice->DrawIndexed(m_font->GetIndexCount(), 0, 0);
		}

		m_rhiDevice->EnableAlphaBlending(false);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	const Vector4& Renderer::GetClearColor()
	{
		return GetCamera() ? GetCamera()->GetClearColor() : Vector4::Zero;
	}

	Camera* Renderer::GetCamera()
	{
		auto vec = m_renderables[Renderable_Camera];

		if (vec.empty())
			return nullptr;

		return vec.front()->GetComponent<Camera>().lock().get();
	}

	Light* Renderer::GetLightDirectional()
	{
		auto vec = m_renderables[Renderable_Light];

		for (const auto& actor : vec)
		{
			Light* light = actor->GetComponent<Light>().lock().get();
			if (light->GetLightType() == LightType_Directional)
				return light;
		}

		return nullptr;
	}

	Skybox* Renderer::GetSkybox()
	{
		auto vec = m_renderables[Renderable_Skybox];

		if (vec.empty())
			return nullptr;

		return vec.front()->GetComponent<Skybox>().lock().get();
	}
}
