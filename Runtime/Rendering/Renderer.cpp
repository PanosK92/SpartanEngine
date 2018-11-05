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

//= INCLUDES ==============================
#include "Renderer.h"
#include "Rectangle.h"
#include "Grid.h"
#include "Font.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/LightShader.h"
#include "Deferred/GBuffer.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommonBuffers.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../RHI/RHI_Sampler.h"
#include "../RHI/RHI_PipelineState.h"
#include "../RHI/RHI_RenderTexture.h"
#include "../RHI/RHI_Shader.h"
#include "../World/Actor.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Skybox.h"
#include "../Physics/Physics.h"
#include "../Physics/PhysicsDebugDraw.h"
#include "../Profiling/Profiler.h"
#include "../Core/Context.h"
#include "../Math/BoundingBox.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
using namespace Helper;
//=============================

#define GIZMO_MAX_SIZE 5.0f
#define GIZMO_MIN_SIZE 0.1f

namespace Directus
{
	static ResourceManager* g_resourceMng	= nullptr;
	unsigned long Renderer::m_flags;
	bool Renderer::m_isRendering			= false;
	uint64_t Renderer::m_frame				= 0;

	Renderer::Renderer(Context* context, void* drawHandle) : Subsystem(context)
	{
		
		m_nearPlane		= 0.0f;
		m_farPlane		= 0.0f;
		m_camera		= nullptr;
		m_rhiDevice		= nullptr;	
		m_flags			= 0;
		m_flags			|= Render_Physics;
		m_flags			|= Render_SceneGrid;
		m_flags			|= Render_Light;
		m_flags			|= Render_Bloom;
		m_flags			|= Render_FXAA;
		m_flags			|= Render_Sharpening;
		//m_flags		|= Render_ChromaticAberration;
		m_flags			|= Render_Correction;

		// Create RHI device
		m_rhiDevice			= make_shared<RHI_Device>(drawHandle);
		m_rhiPipelineState	= make_shared<RHI_PipelineState>(m_rhiDevice);

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(EVENT_RENDER, EVENT_HANDLER(Render));
		SUBSCRIBE_TO_EVENT(EVENT_WORLD_SUBMIT, EVENT_HANDLER_VARIANT(Renderables_Acquire));
		SUBSCRIBE_TO_EVENT(EVENT_WORLD_UNLOAD, [this](Variant) { Clear(); });
	}

	Renderer::~Renderer()
	{

	}

	bool Renderer::Initialize()
	{
		// Create/Get required systems		
		g_resourceMng		= m_context->GetSubsystem<ResourceManager>();

		// Get standard resource directories
		string fontDir			= g_resourceMng->GetStandardResourceDirectory(Resource_Font);
		string shaderDirectory	= g_resourceMng->GetStandardResourceDirectory(Resource_Shader);
		string textureDirectory = g_resourceMng->GetStandardResourceDirectory(Resource_Texture);

		// Load a font (used for performance metrics)
		m_font = make_unique<Font>(m_context, fontDir + "CalibriBold.ttf", 12, Vector4(0.7f, 0.7f, 0.7f, 1.0f));
		// Make a grid (used in editor)
		m_grid = make_unique<Grid>(m_rhiDevice);
		// Light gizmo icon rectangle
		m_gizmoRectLight = make_unique<Rectangle>(m_context);

		RenderTargets_Create(Settings::Get().Resolution_GetWidth(), Settings::Get().Resolution_GetHeight());

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
			m_shaderLight = make_shared<LightShader>(m_rhiDevice);
			m_shaderLight->Compile(shaderDirectory + "Light.hlsl", m_context);

			// Line
			m_shaderLine = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderLine->Compile_VertexPixel_Async(shaderDirectory + "Line.hlsl", Input_PositionColor, m_context);
			m_shaderLine->AddBuffer<Struct_Matrix_Matrix_Matrix>(0, Buffer_VertexShader);

			// Depth
			m_shaderLightDepth = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderLightDepth->Compile_VertexPixel_Async(shaderDirectory + "ShadowingDepth.hlsl", Input_Position, m_context);
			m_shaderLightDepth->AddBuffer<Struct_Matrix>(0, Buffer_VertexShader);

			// Grid
			m_shaderGrid = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderGrid->Compile_VertexPixel_Async(shaderDirectory + "Grid.hlsl", Input_PositionColor, m_context);
			m_shaderGrid->AddBuffer<Struct_Matrix>(0, Buffer_VertexShader);

			// Font
			m_shaderFont = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderFont->Compile_VertexPixel_Async(shaderDirectory + "Font.hlsl", Input_PositionTexture, m_context);
			m_shaderFont->AddBuffer<Struct_Matrix_Vector4>(0, Buffer_Global);

			// Texture
			m_shaderTexture = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTexture->Compile_VertexPixel_Async(shaderDirectory + "Texture.hlsl", Input_PositionTexture, m_context);
			m_shaderTexture->AddBuffer<Struct_Matrix>(0, Buffer_VertexShader);

			// FXAA
			m_shaderFXAA = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderFXAA->AddDefine("PASS_FXAA");
			m_shaderFXAA->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderFXAA->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Sharpening
			m_shaderSharpening = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderSharpening->AddDefine("PASS_SHARPENING");
			m_shaderSharpening->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderSharpening->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Chromatic aberration
			m_shaderChromaticAberration = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderChromaticAberration->AddDefine("PASS_CHROMATIC_ABERRATION");
			m_shaderChromaticAberration->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderChromaticAberration->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Blur Box
			m_shaderBlurBox = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurBox->AddDefine("PASS_BLUR_BOX");
			m_shaderBlurBox->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderBlurBox->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Blur Gaussian Horizontal
			m_shaderBlurGaussianH = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurGaussianH->AddDefine("PASS_BLUR_GAUSSIAN_H");
			m_shaderBlurGaussianH->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderBlurGaussianH->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Blur Gaussian Vertical
			m_shaderBlurGaussianV = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBlurGaussianV->AddDefine("PASS_BLUR_GAUSSIAN_V", "1");
			m_shaderBlurGaussianV->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderBlurGaussianV->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Bloom - bright
			m_shaderBloom_Bright = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBloom_Bright->AddDefine("PASS_BRIGHT", "1");
			m_shaderBloom_Bright->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderBloom_Bright->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Bloom - blend
			m_shaderBloom_BlurBlend = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderBloom_BlurBlend->AddDefine("PASS_BLEND_ADDITIVE");
			m_shaderBloom_BlurBlend->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderBloom_BlurBlend->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Tone-mapping
			m_shaderCorrection = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderCorrection->AddDefine("PASS_CORRECTION");
			m_shaderCorrection->Compile_VertexPixel_Async(shaderDirectory + "PostProcess.hlsl", Input_PositionTexture, m_context);
			m_shaderCorrection->AddBuffer<Struct_Matrix_Vector2>(0, Buffer_Global);

			// Transformation gizmo
			m_shaderTransformationGizmo = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTransformationGizmo->Compile_VertexPixel_Async(shaderDirectory + "TransformationGizmo.hlsl", Input_PositionTextureTBN, m_context);
			m_shaderTransformationGizmo->AddBuffer<Struct_Matrix_Vector3_Vector3>(0, Buffer_Global);

			// Shadowing (shadow mapping & SSAO)
			m_shaderShadowing = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderShadowing->Compile_VertexPixel_Async(shaderDirectory + "Shadowing.hlsl", Input_PositionTexture, m_context);
			m_shaderShadowing->AddBuffer<Struct_Shadowing>(0, Buffer_Global);

			// Transparent
			m_shaderTransparent = make_shared<RHI_Shader>(m_rhiDevice);
			m_shaderTransparent->Compile_VertexPixel_Async(shaderDirectory + "Transparent.hlsl", Input_PositionTextureTBN, m_context);
			m_shaderTransparent->AddBuffer<Struct_Transparency>(0, Buffer_Global);
		}

		// TEXTURES
		{
			// Noise texture (used by SSAO shader)
			m_texNoiseMap = make_shared<RHI_Texture>(m_context);
			m_texNoiseMap->LoadFromFile(textureDirectory + "noise.png");

			// Gizmo icons
			m_gizmoTexLightDirectional = make_shared<RHI_Texture>(m_context);
			m_gizmoTexLightDirectional->LoadFromFile(textureDirectory + "sun.png");

			m_gizmoTexLightPoint = make_shared<RHI_Texture>(m_context);
			m_gizmoTexLightPoint->LoadFromFile(textureDirectory + "light_bulb.png");

			m_gizmoTexLightSpot = make_shared<RHI_Texture>(m_context);
			m_gizmoTexLightSpot->LoadFromFile(textureDirectory + "flashlight.png");
		}

		return true;
	}

	void Renderer::SetBackBufferAsRenderTarget(bool clear /*= true*/)
	{
		m_rhiDevice->Set_BackBufferAsRenderTarget();
		m_rhiPipelineState->SetViewport((float)Settings::Get().Resolution_GetWidth(), (float)Settings::Get().Resolution_GetHeight());
		m_rhiPipelineState->Bind();
		if (clear) m_rhiDevice->ClearBackBuffer(m_camera ? m_camera->GetClearColor() : Vector4(0, 0, 0, 1));
	}

	void* Renderer::GetFrameShaderResource()
	{
		return m_renderTex3 ? m_renderTex3->GetShaderResource() : nullptr;
	}

	void Renderer::Present()
	{
		m_rhiDevice->Present();
	}

	void Renderer::Render()
	{
		TIME_BLOCK_START_MULTI();

		if (!m_rhiDevice || !m_rhiDevice->IsInitialized())
			return;

		m_isRendering = true;
		Profiler::Get().Reset();
		m_frame++;

		// If there is a camera, render the scene
		if (m_camera)
		{
			m_mV					= m_camera->GetViewMatrix();
			m_mV_base				= m_camera->GetBaseViewMatrix();
			m_mP_perspective		= m_camera->GetProjectionMatrix();
			m_mP_orthographic		= Matrix::CreateOrthographicLH((float)Settings::Get().Resolution_GetWidth(), (float)Settings::Get().Resolution_GetHeight(), m_nearPlane, m_farPlane);		
			m_wvp_perspective		= m_mV * m_mP_perspective;
			m_wvp_baseOrthographic	= m_mV_base * m_mP_orthographic;
			m_nearPlane				= m_camera->GetNearPlane();
			m_farPlane				= m_camera->GetFarPlane();

			// If there is nothing to render clear to camera's color and present
			if (m_actors.empty())
			{
				m_rhiDevice->ClearBackBuffer(m_camera->GetClearColor());
				m_rhiDevice->Present();
				m_isRendering = false;
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

			Pass_Transparent(m_renderTex3);
			Pass_DebugGBuffer(m_renderTex3);
			// Debug rendering (on the target that happens to be bound)
			Pass_Debug();
		}		
		else // If there is no camera, clear to black
		{
			m_rhiDevice->ClearBackBuffer(Vector4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		m_isRendering = false;

		TIME_BLOCK_END_MULTI();
	}

	void Renderer::SetBackBufferSize(int width, int height)
	{
		if (width == 0 || height == 0)
			return;

		m_rhiDevice->Set_Resolution(width, height);
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
		if (Settings::Get().Resolution_Get().x == width && Settings::Get().Resolution_Get().y == height)
			return;

		// Make sure we are pixel perfect
		width	-= (width	% 2 != 0) ? 1 : 0;
		height	-= (height	% 2 != 0) ? 1 : 0;

		Settings::Get().Resolution_Set(Vector2((float)width, (float)height));
		RenderTargets_Create(width, height);
		LOGF_INFO("Renderer::SetResolution: Resolution was set to %dx%d", width, height);
	}

	void Renderer::AddBoundigBox(const BoundingBox& box, const Vector4& color)
	{
		// Compute points from min and max
		Vector3 boundPoint1 = box.GetMin();
		Vector3 boundPoint2 = box.GetMax();
		Vector3 boundPoint3 = Vector3(boundPoint1.x, boundPoint1.y, boundPoint2.z);
		Vector3 boundPoint4 = Vector3(boundPoint1.x, boundPoint2.y, boundPoint1.z);
		Vector3 boundPoint5 = Vector3(boundPoint2.x, boundPoint1.y, boundPoint1.z);
		Vector3 boundPoint6 = Vector3(boundPoint1.x, boundPoint2.y, boundPoint2.z);
		Vector3 boundPoint7 = Vector3(boundPoint2.x, boundPoint1.y, boundPoint2.z);
		Vector3 boundPoint8 = Vector3(boundPoint2.x, boundPoint2.y, boundPoint1.z);

		// top of rectangular cuboid (6-2-8-4)
		AddLine(boundPoint6, boundPoint2, color);
		AddLine(boundPoint2, boundPoint8, color);
		AddLine(boundPoint8, boundPoint4, color);
		AddLine(boundPoint4, boundPoint6, color);

		// bottom of rectangular cuboid (3-7-5-1)
		AddLine(boundPoint3, boundPoint7, color);
		AddLine(boundPoint7, boundPoint5, color);
		AddLine(boundPoint5, boundPoint1, color);
		AddLine(boundPoint1, boundPoint3, color);

		// legs (6-3, 2-7, 8-5, 4-1)
		AddLine(boundPoint6, boundPoint3, color);
		AddLine(boundPoint2, boundPoint7, color);
		AddLine(boundPoint8, boundPoint5, color);
		AddLine(boundPoint4, boundPoint1, color);
	}

	void Renderer::AddLine(const Vector3& from, const Vector3& to, const Vector4& colorFrom, const Vector4& colorTo)
	{
		m_lineVertices.emplace_back(from, colorFrom);
		m_lineVertices.emplace_back(to, colorTo);
	}

	void Renderer::Clear()
	{
		m_actors.clear();
		m_camera = nullptr;
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
		m_renderTex1 = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTex2.reset();
		m_renderTex2 = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTex3.reset();
		m_renderTex3 = make_unique<RHI_RenderTexture>(m_rhiDevice, width, height, Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexQuarterRes1.reset();
		m_renderTexQuarterRes1 = make_unique<RHI_RenderTexture>(m_rhiDevice, int(width * 0.25f), int(height * 0.25f), Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexQuarterRes2.reset();
		m_renderTexQuarterRes2 = make_unique<RHI_RenderTexture>(m_rhiDevice, int(width * 0.25f), int(height * 0.25f), Texture_Format_R16G16B16A16_FLOAT);

		m_renderTexShadowing.reset();
		m_renderTexShadowing = make_unique<RHI_RenderTexture>(m_rhiDevice, int(width * 0.5f), int(height * 0.5f), Texture_Format_R32G32_FLOAT);
	}

	//= RENDERABLES ============================================================================================
	void Renderer::Renderables_Acquire(const Variant& actorsVariant)
	{
		TIME_BLOCK_START_CPU();

		Clear();
		auto actorsVec = actorsVariant.Get<vector<shared_ptr<Actor>>>();

		for (const auto& actorShared : actorsVec)
		{
			auto actor = actorShared.get();
			if (!actor)
				continue;

			// Get all the components we are interested in
			auto renderable = actor->GetComponent<Renderable>();
			auto light		= actor->GetComponent<Light>();
			auto skybox		= actor->GetComponent<Skybox>();
			auto camera		= actor->GetComponent<Camera>();

			if (renderable)
			{
				bool isTransparent = !renderable->Material_Exists() ? false : renderable->Material_Ptr()->GetColorAlbedo().w < 1.0f;
				m_actors[isTransparent ? Renderable_ObjectTransparent : Renderable_ObjectOpaque].emplace_back(actor);
			}

			if (light)
			{
				m_actors[Renderable_Light].emplace_back(actor);
			}

			if (skybox)
			{
				m_actors[Renderable_Skybox].emplace_back(actor);
			}

			if (camera)
			{
				m_actors[Renderable_Camera].emplace_back(actor);
				m_camera = camera.get();
			}
		}

		Renderables_Sort(&m_actors[Renderable_ObjectOpaque]);
		Renderables_Sort(&m_actors[Renderable_ObjectTransparent]);

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
			auto a_material = a_renderable->Material_Ptr();
			auto b_material = b_renderable->Material_Ptr();

			if (!a_material || !b_material)
				return false;

			// Get key for models
			auto a_keyModel = a_geometryModel->Resource_GetID();
			auto b_keyModel = b_geometryModel->Resource_GetID();

			// Get key for shaders
			auto a_keyShader = a_material->GetShader().lock()->Resource_GetID();
			auto b_keyShader = b_material->GetShader().lock()->Resource_GetID();

			// Get key for materials
			auto a_keyMaterial = a_material->Resource_GetID();
			auto b_keyMaterial = b_material->Resource_GetID();

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
		m_rhiPipelineState->SetShader(m_shaderLightDepth);
		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);

		// Variables that help reduce state changes
		unsigned int currentlyBoundGeometry = 0;

		auto& actors = m_actors[Renderable_ObjectOpaque];

		if (!actors.empty())
		{
			for (unsigned int i = 0; i < light->ShadowMap_GetCount(); i++)
			{
				m_rhiDevice->EventBegin("Pass_ShadowMap_" + to_string(i));

				if (auto shadowMap = light->ShadowMap_GetRenderTexture(i))
				{
					m_rhiPipelineState->SetRenderTarget(shadowMap, shadowMap->GetDepthStencilView(), true);
					m_rhiPipelineState->SetViewport(shadowMap->GetViewport());
				}

				for (const auto& actor : actors)
				{
					// Acquire renderable component
					Renderable* renderable = actor->GetRenderable_PtrRaw();
					if (!renderable)
						continue;

					// Acquire material
					Material* material = renderable ? renderable->Material_Ptr().get() : nullptr;
					if (!material)
						continue;

					// Acquire geometry
					Model* geometry = renderable->Geometry_Model();
					if (!geometry || !geometry->GetVertexBuffer() || !geometry->GetIndexBuffer())
						continue;

					// Skip meshes that don't cast shadows
					if (!renderable->GetCastShadows())
						continue;

					// Skip transparent meshes (for now)
					if (material->GetColorAlbedo().w < 1.0f)
						continue;

					// Bind geometry
					if (currentlyBoundGeometry != geometry->Resource_GetID())
					{
						m_rhiPipelineState->SetIndexBuffer(geometry->GetIndexBuffer());
						m_rhiPipelineState->SetVertexBuffer(geometry->GetVertexBuffer());					
						currentlyBoundGeometry = geometry->Resource_GetID();
					}

					// skip objects outside of the view frustum
					//if (!m_directionalLight->IsInViewFrustum(obj_renderable, i))
					//continue;

					auto buffer = Struct_Matrix(actor->GetTransform_PtrRaw()->GetWorldTransform() * light->GetViewMatrix() * light->ShadowMap_GetProjectionMatrix(i));
					m_shaderLightDepth->UpdateBuffer(&buffer);
					m_rhiPipelineState->SetConstantBuffer(m_shaderLightDepth->GetConstantBuffer());
					m_rhiPipelineState->Bind();

					m_rhiDevice->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
				}

				m_rhiDevice->EventEnd();
			}
		}

		m_rhiDevice->EventEnd();

		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhiDevice)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_GBuffer");

		//  Bind render target
		m_gbuffer->SetAsRenderTarget(m_rhiPipelineState);
		m_rhiPipelineState->SetSampler(m_samplerAnisotropicWrapAlways);
		m_rhiPipelineState->SetFillMode(Fill_Solid);
		m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);

		// Variables that help reduce state changes
		bool vertexShaderBound				= false;
		unsigned int currentlyBoundGeometry = 0;
		unsigned int currentlyBoundShader	= 0;
		unsigned int currentlyBoundMaterial = 0;

		for (auto actor : m_actors[Renderable_ObjectOpaque])
		{
			// Get renderable and material
			Renderable* renderable	= actor->GetRenderable_PtrRaw();
			Material* material		= renderable ? renderable->Material_Ptr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get shader and geometry
			auto shader		= material->GetShader().lock();
			Model* model	= renderable->Geometry_Model();

			// Validate shader
			if (!shader || shader->GetState() != Shader_Built)
				continue;

			// Validate geometry
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// set face culling (changes only if required)
			m_rhiPipelineState->SetCullMode(material->GetCullMode());

			// Bind geometry
			if (currentlyBoundGeometry != model->Resource_GetID())
			{	
				m_rhiPipelineState->SetIndexBuffer(model->GetIndexBuffer());
				m_rhiPipelineState->SetVertexBuffer(model->GetVertexBuffer());
				currentlyBoundGeometry = model->Resource_GetID();
			}

			// Bind shader
			if (currentlyBoundShader != shader->Resource_GetID())
			{
				if (!vertexShaderBound)
				{
					m_rhiPipelineState->SetVertexShader(shared_ptr<RHI_Shader>(shader));
					vertexShaderBound = true;
				}
				m_rhiPipelineState->SetPixelShader(shared_ptr<RHI_Shader>(shader));
				currentlyBoundShader = shader->Resource_GetID();
			}

			// Bind material
			if (currentlyBoundMaterial != material->Resource_GetID())
			{
				shader->UpdatePerMaterialBuffer(m_camera, material);

				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Albedo).ptr_raw);
				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Roughness).ptr_raw);
				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Metallic).ptr_raw);
				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Normal).ptr_raw);
				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Height).ptr_raw);
				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Occlusion).ptr_raw);
				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Emission).ptr_raw);
				m_rhiPipelineState->SetTexture(material->GetTextureSlotByType(TextureType_Mask).ptr_raw);

				currentlyBoundMaterial = material->Resource_GetID();
			}

			// UPDATE PER OBJECT BUFFER
			shader->UpdatePerObjectBuffer(
				actor->GetTransform_PtrRaw()->GetWorldTransform(),
				m_mV, 
				m_mP_perspective
			);
		
			m_rhiPipelineState->SetConstantBuffer(shader->GetMaterialBuffer());
			m_rhiPipelineState->SetConstantBuffer(shader->GetPerObjectBuffer());

			m_rhiPipelineState->Bind();

			// Render	
			m_rhiDevice->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
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
		if (m_shaderLight->GetState() != Shader_Built)
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Light");

		// Update constant buffer
		m_shaderLight->UpdateConstantBuffer(
			Matrix::Identity,
			m_mV,
			m_mV_base,
			m_mP_perspective,
			m_mP_orthographic,
			m_actors[Renderable_Light],
			m_camera
		);

		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
		m_rhiPipelineState->SetShader(shared_ptr<RHI_Shader>(m_shaderLight));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Albedo));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Specular));
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->SetTexture(m_renderTex3); // previous frame for SSR // Todo SSR
		m_rhiPipelineState->SetTexture(GetSkybox() ? GetSkybox()->GetTexture() : nullptr);
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);	
		m_rhiPipelineState->SetConstantBuffer(m_shaderLight->GetConstantBuffer());
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

	void Renderer::Pass_Transparent(shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (!GetLightDirectional())
			return;

		auto& actors_transparent = m_actors[Renderable_ObjectTransparent];
		if (actors_transparent.empty())
			return;

		TIME_BLOCK_START_MULTI();
		m_rhiDevice->EventBegin("Pass_Transparent");

		m_rhiDevice->Set_AlphaBlendingEnabled(true);
		m_rhiPipelineState->SetShader(m_shaderTransparent);
		m_rhiPipelineState->SetRenderTarget(texOut, m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetDepthStencilView());
		m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipelineState->SetTexture(GetSkybox() ? GetSkybox()->GetTexture() : nullptr);
		m_rhiPipelineState->SetSampler(m_samplerLinearClampGreater);

		for (auto& actor : actors_transparent)
		{
			// Get renderable and material
			Renderable* renderable	= actor->GetRenderable_PtrRaw();
			Material* material		= renderable ? renderable->Material_Ptr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get geometry
			Model* model = renderable->Geometry_Model();
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// Set the following per object
			m_rhiPipelineState->SetCullMode(material->GetCullMode());
			m_rhiPipelineState->SetIndexBuffer(model->GetIndexBuffer());
			m_rhiPipelineState->SetVertexBuffer(model->GetVertexBuffer());

			// Constant buffer
			auto buffer = Struct_Transparency(
				actor->GetTransform_PtrRaw()->GetWorldTransform(),
				m_mV,
				m_mP_perspective,
				material->GetColorAlbedo(),
				m_camera->GetTransform()->GetPosition(),
				GetLightDirectional()->GetDirection(),
				material->GetRoughnessMultiplier()
			);
			m_shaderTransparent->UpdateBuffer(&buffer);
			m_rhiPipelineState->SetConstantBuffer(m_shaderTransparent->GetConstantBuffer());

			m_rhiPipelineState->Bind();

			// Render	
			m_rhiDevice->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
			Profiler::Get().m_rendererMeshesRendered++;

		} // Actor/MESH ITERATION

		m_rhiDevice->Set_AlphaBlendingEnabled(false);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Bloom");

		// Bright pass
		m_rhiPipelineState->SetRenderTarget(m_renderTexQuarterRes1);
		m_rhiPipelineState->SetViewport(m_renderTexQuarterRes1->GetViewport());
		m_rhiPipelineState->SetPixelShader(m_shaderBloom_Bright);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Horizontal Gaussian blur
		m_rhiPipelineState->SetRenderTarget(m_renderTexQuarterRes2);
		m_rhiPipelineState->SetPixelShader(m_shaderBlurGaussianH);
		m_rhiPipelineState->SetTexture(m_renderTexQuarterRes1);
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		m_rhiPipelineState->SetRenderTarget(m_renderTexQuarterRes1);
		m_rhiPipelineState->SetPixelShader(m_shaderBlurGaussianV);
		m_rhiPipelineState->SetTexture(m_renderTexQuarterRes2);
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		// Additive blending
		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
		m_rhiPipelineState->SetPixelShader(m_shaderBloom_BlurBlend);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->SetTexture(m_renderTexQuarterRes1);
		float bloomIntensity = 0.2f;
		auto buffer = Struct_Matrix_Vector2(m_wvp_baseOrthographic, Vector2(texIn->GetWidth(), texIn->GetHeight()), bloomIntensity);
		m_shaderBloom_BlurBlend->UpdateBuffer(&buffer);
		m_rhiPipelineState->SetConstantBuffer(m_shaderBloom_BlurBlend->GetConstantBuffer());
		m_rhiPipelineState->Bind();
		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Correction(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Correction");

		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
		m_rhiPipelineState->SetPixelShader(m_shaderCorrection);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_FXAA(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_FXAA");

		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
		m_rhiPipelineState->SetPixelShader(m_shaderFXAA);	
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_ChromaticAberration(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_ChromaticAberration");

		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
		m_rhiPipelineState->SetPixelShader(m_shaderChromaticAberration);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Sharpening(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Sharpening");

		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
		m_rhiPipelineState->SetPixelShader(m_shaderSharpening);
		m_rhiPipelineState->SetTexture(texIn);
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Blur(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_Blur");

		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
		m_rhiPipelineState->SetShader(m_shaderBlurBox);
		m_rhiPipelineState->SetTexture(texIn); // Shadows are in the alpha channel
		m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
		auto buffer = Struct_Matrix_Vector2(m_wvp_baseOrthographic, Vector2(texIn->GetWidth(), texIn->GetHeight()));
		m_shaderBlurBox->UpdateBuffer(&buffer);
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
		m_rhiPipelineState->SetRenderTarget(texOut);
		m_rhiPipelineState->SetViewport(texOut->GetViewport());
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
		auto buffer = Struct_Shadowing
			(
				m_wvp_baseOrthographic,
				(m_mV * m_mP_perspective).Inverted(),
				Vector2(texOut->GetWidth(), texOut->GetHeight()),
				inDirectionalLight,
				m_camera
			);
		m_shaderShadowing->UpdateBuffer(&buffer);	
		m_rhiPipelineState->SetConstantBuffer(m_shaderShadowing->GetConstantBuffer());
		m_rhiPipelineState->Bind();

		m_rhiDevice->DrawIndexed(m_quad->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}
	//=============================================================================================================

	bool Renderer::Pass_DebugGBuffer(shared_ptr<RHI_RenderTexture>& texOut)
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
			m_rhiPipelineState->Clear();
			m_rhiPipelineState->SetVertexBuffer(m_quad->GetVertexBuffer());
			m_rhiPipelineState->SetIndexBuffer(m_quad->GetIndexBuffer());
			m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
			m_rhiPipelineState->SetFillMode(Fill_Solid);
			m_rhiPipelineState->SetCullMode(Cull_Back);
			m_rhiPipelineState->SetInputLayout(m_shaderTexture->GetInputLayout());
			m_rhiPipelineState->SetRenderTarget(texOut, m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetDepthStencilView());
			m_rhiPipelineState->SetShader(m_shaderTexture);
			m_rhiPipelineState->SetViewport(m_gbuffer->GetTexture(texType)->GetViewport());
			m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(texType));
			m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
			auto buffer = Struct_Matrix(m_wvp_baseOrthographic);
			m_shaderTexture->UpdateBuffer(&buffer);
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

		m_rhiDevice->EventBegin("Line_Rendering");
		{
			// Picking ray
			if (m_flags & Render_PickingRay)
			{
				const Ray& ray = m_camera->GetPickingRay();
				AddLine(ray.GetOrigin(), ray.GetEnd(), Vector4(0, 1, 0, 1));
			}

			// bounding boxes
			if (m_flags & Render_AABB)
			{
				for (const auto& actor : m_actors[Renderable_ObjectOpaque])
				{
					if (auto renderable = actor->GetRenderable_PtrRaw())
					{
						AddBoundigBox(renderable->Geometry_BB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}

				for (const auto& actor : m_actors[Renderable_ObjectTransparent])
				{
					if (auto renderable = actor->GetRenderable_PtrRaw())
					{
						AddBoundigBox(renderable->Geometry_BB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}

			auto lineVertexBufferSize = (unsigned int)m_lineVertices.size();
			if (lineVertexBufferSize != 0)
			{
				if (lineVertexBufferSize > m_lineVertexCount)
				{
					m_lineVertexBuffer = make_shared<RHI_VertexBuffer>(m_rhiDevice);
					m_lineVertexBuffer->CreateDynamic(sizeof(RHI_Vertex_PosCol), lineVertexBufferSize);
					m_lineVertexCount = lineVertexBufferSize;
				}

				// Update line vertex buffer
				void* data = m_lineVertexBuffer->Map();
				memcpy(data, &m_lineVertices[0], sizeof(RHI_Vertex_PosCol) * lineVertexBufferSize);
				m_lineVertexBuffer->Unmap();

				// Set pipeline state
				m_rhiPipelineState->SetShader(m_shaderLine);
				m_rhiPipelineState->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
				m_rhiPipelineState->SetSampler(m_samplerPointClampGreater);
				m_rhiPipelineState->SetVertexBuffer(m_lineVertexBuffer);
				m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_LineList);
				auto buffer = Struct_Matrix_Matrix_Matrix(Matrix::Identity, m_camera->GetViewMatrix(), m_camera->GetProjectionMatrix());
				m_shaderLine->UpdateBuffer(&buffer);
				m_rhiPipelineState->SetConstantBuffer(m_shaderLine->GetConstantBuffer());
				m_rhiPipelineState->Bind();
				// Draw
				m_rhiDevice->Draw(lineVertexBufferSize);

				m_lineVertices.clear();	
			}
		}
		m_rhiDevice->EventEnd();

		m_rhiDevice->Set_AlphaBlendingEnabled(true);

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
			auto buffer = Struct_Matrix(m_grid->ComputeWorldMatrix(m_camera->GetTransform()) * m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix());
			m_shaderGrid->UpdateBuffer(&buffer);
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
				for (const auto& actor : m_actors[Renderable_Light])
				{
					Vector3 lightWorldPos	= actor->GetTransform_PtrRaw()->GetPosition();
					Vector3 cameraWorldPos	= m_camera->GetTransform()->GetPosition();

					// Compute light screen space position and scale (based on distance from the camera)
					Vector2 lightScreenPos	= m_camera->WorldToScreenPoint(lightWorldPos);
					float distance			= Clamp(Vector3::Length(lightWorldPos, cameraWorldPos), 0.0f, FLT_MAX);
					float scale				= GIZMO_MAX_SIZE / distance;
					scale					= Clamp(scale, GIZMO_MIN_SIZE, GIZMO_MAX_SIZE);

					// Skip if the light is not in front of the camera
					if (!m_camera->IsInViewFrustrum(lightWorldPos, Vector3(1.0f)))
						continue;

					// Skip if the light if it's too small
					if (scale < GIZMO_MIN_SIZE)
						continue;

					shared_ptr<RHI_Texture> lightTex = nullptr;
					LightType type = actor->GetComponent<Light>()->GetLightType();
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
					auto buffer = Struct_Matrix(m_wvp_baseOrthographic);
					m_shaderTexture->UpdateBuffer(&buffer);
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
			Vector2 textPos = Vector2(-(int)Settings::Get().Viewport_GetWidth() * 0.5f + 1.0f, (int)Settings::Get().Viewport_GetHeight() * 0.5f);
			m_font->SetText(Profiler::Get().GetMetrics(), textPos);

			m_rhiPipelineState->SetShader(m_shaderFont);
			m_rhiPipelineState->SetTexture(m_font->GetTexture());
			m_rhiPipelineState->SetSampler(m_samplerLinearClampAlways);
			m_rhiPipelineState->SetIndexBuffer(m_font->GetIndexBuffer());
			m_rhiPipelineState->SetVertexBuffer(m_font->GetVertexBuffer());
			m_rhiPipelineState->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
			auto buffer = Struct_Matrix_Vector4(m_wvp_baseOrthographic, m_font->GetColor());
			m_shaderFont->UpdateBuffer(&buffer);
			m_rhiPipelineState->SetConstantBuffer(m_shaderFont->GetConstantBuffer());
			m_rhiPipelineState->Bind();

			m_rhiDevice->DrawIndexed(m_font->GetIndexCount(), 0, 0);
		}

		m_rhiDevice->Set_AlphaBlendingEnabled(false);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI();
	}

	Light* Renderer::GetLightDirectional()
	{
		auto actors = m_actors[Renderable_Light];

		for (const auto& actor : actors)
		{
			Light* light = actor->GetComponent<Light>().get();
			if (light->GetLightType() == LightType_Directional)
				return light;
		}

		return nullptr;
	}

	Skybox* Renderer::GetSkybox()
	{
		auto actors = m_actors[Renderable_Skybox];
		if (actors.empty())
			return nullptr;

		auto skyboxActor = actors.front();
		return skyboxActor ? skyboxActor->GetComponent<Skybox>().get() : nullptr;
	}
}
