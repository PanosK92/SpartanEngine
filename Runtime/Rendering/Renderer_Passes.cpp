/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "Deferred/GBuffer.h"
#include "Deferred/ShaderVariation.h"
#include "Deferred/LightShader.h"
#include "Gizmos/Grid.h"
#include "Gizmos/Transform_Gizmo.h"
#include "Font/Font.h"
#include "../Profiling/Profiler.h"
#include "../RHI/RHI_Device.h"
#include "../RHI/RHI_CommonBuffers.h"
#include "../RHI/RHI_VertexBuffer.h"
#include "../World/Entity.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Transform.h"
#include "../World/Components/Skybox.h"
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
	void Renderer::Pass_DepthDirectionalLight(Light* light)
	{
		// Validate light
		if (!light || !light->GetCastShadows())
			return;

		// Validate light's shadow map
		auto& shadowMap = light->GetShadowMap();
		if (!shadowMap)
			return;

		// Validate entities
		auto& entities = m_entities[Renderable_ObjectOpaque];
		if (entities.empty())
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_DepthDirectionalLight");

		// Set common states	
		SetDefault_Pipeline_State();
		m_rhiPipeline->SetShader(m_vps_depth);
		m_rhiPipeline->SetViewport(shadowMap->GetViewport());

		// Variables that help reduce state changes
		unsigned int currentlyBoundGeometry = 0;
		for (unsigned int i = 0; i < light->GetShadowMap()->GetArraySize(); i++)
		{
			m_rhiDevice->EventBegin(("Pass_DepthDirectionalLight " + to_string(i)).c_str());
			m_rhiPipeline->SetRenderTarget(shadowMap->GetRenderTargetView(i), shadowMap->GetDepthStencilView(), true);

			for (const auto& entity : entities)
			{
				// Acquire renderable component
				auto renderable = entity->GetRenderable_PtrRaw();
				if (!renderable)
					continue;
	
				// Acquire material
				auto material = renderable->Material_Ptr();
				if (!material)
					continue;

				// Acquire geometry
				auto geometry = renderable->Geometry_Model();
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
					m_rhiPipeline->SetIndexBuffer(geometry->GetIndexBuffer());
					m_rhiPipeline->SetVertexBuffer(geometry->GetVertexBuffer());
					currentlyBoundGeometry = geometry->Resource_GetID();
				}

				SetDefault_Buffer((unsigned int)m_resolution.x, (unsigned int)m_resolution.y, entity->GetTransform_PtrRaw()->GetMatrix() * light->GetViewMatrix() * light->ShadowMap_GetProjectionMatrix(i));
				m_rhiPipeline->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
			}
			m_rhiDevice->EventEnd();
		}

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_GBuffer()
	{
		if (!m_rhiDevice)
			return;

		if (m_entities[Renderable_ObjectOpaque].empty())
		{
			m_gbuffer->Clear(); // zeroed material buffer causes sky sphere to render
		}

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_GBuffer");

		// Set common states
		SetDefault_Pipeline_State();
		m_rhiPipeline->SetDepthStencilState(m_depthStencil_enabled);
		bool clear = true;
		vector<void*> views
		{
			m_gbuffer->GetTexture(GBuffer_Target_Albedo)->GetRenderTargetView(),
			m_gbuffer->GetTexture(GBuffer_Target_Normal)->GetRenderTargetView(),
			m_gbuffer->GetTexture(GBuffer_Target_Material)->GetRenderTargetView(),
			m_gbuffer->GetTexture(GBuffer_Target_Velocity)->GetRenderTargetView(),
			m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetRenderTargetView()
		};
		m_rhiPipeline->SetRenderTarget(views, m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetDepthStencilView(), clear);
		m_rhiPipeline->SetViewport(m_gbuffer->GetTexture(GBuffer_Target_Albedo)->GetViewport());
		m_rhiPipeline->SetSampler(m_samplerAnisotropicWrap);
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetVertexShader(m_vs_gbuffer);
		SetDefault_Buffer((unsigned int)m_resolution.x, (unsigned int)m_resolution.y);

		// Variables that help reduce state changes
		unsigned int currentlyBoundGeometry = 0;
		unsigned int currentlyBoundShader = 0;
		unsigned int currentlyBoundMaterial = 0;

		for (auto entity : m_entities[Renderable_ObjectOpaque])
		{
			// Get renderable and material
			Renderable* renderable = entity->GetRenderable_PtrRaw();
			Material* material = renderable ? renderable->Material_Ptr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get shader and geometry
			auto shader = material->GetShader();
			auto model = renderable->Geometry_Model();

			// Validate shader
			if (!shader || shader->GetState() != Shader_Built)
				continue;

			// Validate geometry
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// Set face culling (changes only if required)
			m_rhiPipeline->SetRasterizerState(GetRasterizerState(material->GetCullMode(), Fill_Solid));

			// Bind geometry
			if (currentlyBoundGeometry != model->Resource_GetID())
			{
				m_rhiPipeline->SetIndexBuffer(model->GetIndexBuffer());
				m_rhiPipeline->SetVertexBuffer(model->GetVertexBuffer());
				currentlyBoundGeometry = model->Resource_GetID();
			}

			// Bind shader
			if (currentlyBoundShader != shader->RHI_GetID())
			{
				m_rhiPipeline->SetPixelShader(shared_ptr<RHI_Shader>(shader));
				currentlyBoundShader = shader->RHI_GetID();
			}

			// Bind textures
			if (currentlyBoundMaterial != material->Resource_GetID())
			{
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Albedo).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Roughness).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Metallic).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Normal).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Height).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Occlusion).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Emission).ptr);
				m_rhiPipeline->SetTexture(material->GetTextureSlotByType(TextureType_Mask).ptr);

				currentlyBoundMaterial = material->Resource_GetID();
			}

			// UPDATE PER OBJECT BUFFER
			shader->UpdatePerObjectBuffer(entity->GetTransform_PtrRaw(), material, m_view, m_projection);
			m_rhiPipeline->SetConstantBuffer(shader->GetPerObjectBuffer(), 1, Buffer_Global);

			// Render	
			m_rhiPipeline->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());
			m_profiler->m_rendererMeshesRendered++;

		} // entity/MESH ITERATION

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_PreLight(shared_ptr<RHI_RenderTexture>& texIn_Spare, shared_ptr<RHI_RenderTexture>& texOut_Shadows, shared_ptr<RHI_RenderTexture>& texOut_SSAO)
	{
		m_rhiDevice->EventBegin("Pass_PreLight");

		SetDefault_Pipeline_State();
		m_rhiPipeline->SetIndexBuffer(m_quad.GetIndexBuffer());
		m_rhiPipeline->SetVertexBuffer(m_quad.GetVertexBuffer());

		// Shadow mapping + Blur
		if (auto lightDir = GetLightDirectional())
		{
			if (lightDir->GetCastShadows())
			{
				Pass_ShadowMapping(texIn_Spare, GetLightDirectional());
				float sigma = 1.0f;
				float pixelStride = 1.0f;
				Pass_BlurBilateralGaussian(texIn_Spare, texOut_Shadows, sigma, pixelStride);
			}
			else
			{
				texOut_Shadows->Clear(1, 1, 1, 1);
			}
		}

		// SSAO + Blur
		if (m_flags & Render_PostProcess_SSAO)
		{
			Pass_SSAO(texIn_Spare);
			float sigma = 2.0f;
			float pixelStride = 2.0f;
			Pass_BlurBilateralGaussian(texIn_Spare, texOut_SSAO, sigma, pixelStride);
		}

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_Light(shared_ptr<RHI_RenderTexture>& texShadows, shared_ptr<RHI_RenderTexture>& texSSAO, shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (m_vps_light->GetState() != Shader_Built)
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Light");

		// Update constant buffer
		m_vps_light->UpdateConstantBuffer
		(
			m_viewProjection_Orthographic,
			m_view,
			m_projection,
			m_entities[Renderable_Light],
			Flags_IsSet(Render_PostProcess_SSR)
		);

		SetDefault_Pipeline_State();
		SetDefault_Buffer((unsigned int)m_resolution.x, (unsigned int)m_resolution.y);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetShader(shared_ptr<RHI_Shader>(m_vps_light));
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Albedo));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Material));
		m_rhiPipeline->SetTexture(texShadows);
		if (Flags_IsSet(Render_PostProcess_SSAO)) { m_rhiPipeline->SetTexture(texSSAO); }
		else { m_rhiPipeline->SetTexture(m_texWhite); }
		m_rhiPipeline->SetTexture(m_renderTexFull_HDR_Light2); // SSR
		m_rhiPipeline->SetTexture(m_skybox ? m_skybox->GetTexture() : m_texWhite);
		m_rhiPipeline->SetTexture(m_texLUT_IBL);
		m_rhiPipeline->SetSampler(m_samplerTrilinearClamp);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetConstantBuffer(m_vps_light->GetConstantBuffer(), 1, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Transparent(shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (!GetLightDirectional())
			return;

		auto& entities_transparent = m_entities[Renderable_ObjectTransparent];
		if (entities_transparent.empty())
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Transparent");
		SetDefault_Pipeline_State();

		m_rhiPipeline->SetBlendState(m_blend_enabled);	
		m_rhiPipeline->SetDepthStencilState(m_depthStencil_enabled);
		m_rhiPipeline->SetRenderTarget(texOut, m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetDepthStencilView());
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_skybox ? m_skybox->GetTexture() : nullptr);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetShader(m_vps_transparent);

		for (auto& entity : entities_transparent)
		{
			// Get renderable and material
			Renderable* renderable = entity->GetRenderable_PtrRaw();
			Material* material = renderable ? renderable->Material_Ptr().get() : nullptr;

			if (!renderable || !material)
				continue;

			// Get geometry
			auto model = renderable->Geometry_Model();
			if (!model || !model->GetVertexBuffer() || !model->GetIndexBuffer())
				continue;

			// Skip objects outside of the view frustum
			if (!m_camera->IsInViewFrustrum(renderable))
				continue;

			// Set the following per object
			m_rhiPipeline->SetRasterizerState(GetRasterizerState(material->GetCullMode(), Fill_Solid));
			m_rhiPipeline->SetIndexBuffer(model->GetIndexBuffer());
			m_rhiPipeline->SetVertexBuffer(model->GetVertexBuffer());

			// Constant buffer
			auto buffer = Struct_Transparency(
				entity->GetTransform_PtrRaw()->GetMatrix(),
				m_view,
				m_projection,
				material->GetColorAlbedo(),
				m_camera->GetTransform()->GetPosition(),
				GetLightDirectional()->GetDirection(),
				material->GetRoughnessMultiplier()
			);
			m_vps_transparent->UpdateBuffer(&buffer);
			m_rhiPipeline->SetConstantBuffer(m_vps_transparent->GetConstantBuffer(), 1, Buffer_Global);
			m_rhiPipeline->DrawIndexed(renderable->Geometry_IndexCount(), renderable->Geometry_IndexOffset(), renderable->Geometry_VertexOffset());

			m_profiler->m_rendererMeshesRendered++;

		} // entity/MESH ITERATION

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_PostLight(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		m_rhiDevice->EventBegin("Pass_PostLight");

		// All post-process passes share the following, so set them once here
		SetDefault_Pipeline_State();
		m_rhiPipeline->SetVertexBuffer(m_quad.GetVertexBuffer());
		m_rhiPipeline->SetIndexBuffer(m_quad.GetIndexBuffer());
		m_rhiPipeline->SetVertexShader(m_vs_quad);

		// Render target swapping
		auto SwapTargets = [&texIn, &texOut]() { texOut.swap(texIn); };

		// TAA	
		if (Flags_IsSet(Render_PostProcess_TAA))
		{
			Pass_TAA(texIn, texOut);
			SwapTargets();
		}

		// Bloom
		if (Flags_IsSet(Render_PostProcess_Bloom))
		{
			Pass_Bloom(texIn, texOut);
			SwapTargets();
		}

		// Motion Blur
		if (Flags_IsSet(Render_PostProcess_MotionBlur))
		{
			Pass_MotionBlur(texIn, texOut);
			SwapTargets();
		}

		// Dithering
		if (Flags_IsSet(Render_PostProcess_Dithering))
		{
			Pass_Dithering(texIn, texOut);
			SwapTargets();
		}

		// Tone-Mapping
		if (m_tonemapping != ToneMapping_Off)
		{
			Pass_ToneMapping(texIn, texOut);
			SwapTargets();
		}

		// FXAA
		if (Flags_IsSet(Render_PostProcess_FXAA))
		{
			Pass_FXAA(texIn, texOut);
			SwapTargets();
		}

		// Sharpening
		if (Flags_IsSet(Render_PostProcess_Sharpening))
		{
			Pass_Sharpening(texIn, texOut);
			SwapTargets();
		}

		// Chromatic aberration
		if (Flags_IsSet(Render_PostProcess_ChromaticAberration))
		{
			Pass_ChromaticAberration(texIn, texOut);
			SwapTargets();
		}

		// Gamma correction
		Pass_GammaCorrection(texIn, texOut);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_ShadowMapping(shared_ptr<RHI_RenderTexture>& texOut, Light* inDirectionalLight)
	{
		if (!inDirectionalLight)
			return;

		if (!inDirectionalLight->GetCastShadows())
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Shadowing");

		SetDefault_Pipeline_State();
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetShader(m_vps_shadowMapping);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(inDirectionalLight->GetShadowMap()); // Texture2DArray
		m_rhiPipeline->SetSampler(m_samplerCompareDepth);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight(), m_viewProjection_Orthographic);
		auto buffer = Struct_ShadowMapping((m_viewProjection).Inverted(), inDirectionalLight, m_camera.get());
		m_vps_shadowMapping->UpdateBuffer(&buffer);
		m_rhiPipeline->SetConstantBuffer(m_vps_shadowMapping->GetConstantBuffer(), 1, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_SSAO(shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_SSAO");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_texNoiseNormal);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetShader(m_vps_ssao);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);	// SSAO (clamp)
		m_rhiPipeline->SetSampler(m_samplerBilinearWrap);	// SSAO noise texture (wrap)
		auto buffer = Struct_Matrix_Matrix
		(
			m_viewProjection_Orthographic,
			(m_viewProjection).Inverted()
		);
		m_vps_ssao->UpdateBuffer(&buffer);
		m_rhiPipeline->SetConstantBuffer(m_vps_ssao->GetConstantBuffer(), 1, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_BlurBox(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut, float sigma)
	{
		m_rhiDevice->EventBegin("Pass_Blur");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight(), Matrix::Identity, sigma);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetVertexShader(m_vs_quad);
		m_rhiPipeline->SetPixelShader(m_ps_blurBox);
		m_rhiPipeline->SetTexture(texIn); // Shadows are in the alpha channel
		m_rhiPipeline->SetSampler(m_samplerTrilinearClamp);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_BlurGaussian(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut, float sigma)
	{
		if (texIn->GetWidth() != texOut->GetWidth() ||
			texIn->GetHeight() != texOut->GetHeight() ||
			texIn->GetFormat() != texOut->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped");
			return;
		}

		m_rhiDevice->EventBegin("Pass_BlurGaussian");

		// Set common states
		m_rhiPipeline->SetVertexShader(m_vs_quad);
		m_rhiPipeline->SetPixelShader(m_ps_blurGaussian);

		// Horizontal Gaussian blur	
		auto direction = Vector2(1.0f, 0.0f);
		SetDefault_Pipeline_State();	
		SetDefault_Buffer(texIn->GetWidth(), texIn->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		direction = Vector2(0.0f, 1.0f);
		SetDefault_Pipeline_State();
		SetDefault_Buffer(texIn->GetWidth(), texIn->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhiPipeline->SetRenderTarget(texIn);
		m_rhiPipeline->SetViewport(texIn->GetViewport());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetTexture(texOut);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		// Swap textures
		texIn.swap(texOut);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_BlurBilateralGaussian(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut, float sigma, float pixelStride)
	{
		if (texIn->GetWidth() != texOut->GetWidth() ||
			texIn->GetHeight() != texOut->GetHeight() ||
			texIn->GetFormat() != texOut->GetFormat())
		{
			LOG_ERROR("Invalid parameters, textures must match because they will get swapped.");
			return;
		}

		m_rhiDevice->EventBegin("Pass_BlurBilateralGaussian");

		// Set common states
		m_rhiPipeline->SetVertexShader(m_vs_quad);
		m_rhiPipeline->SetPixelShader(m_ps_blurGaussianBilateral);

		// Horizontal Gaussian blur
		SetDefault_Pipeline_State();	
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		auto direction = Vector2(pixelStride, 0.0f);
		SetDefault_Buffer(texIn->GetWidth(), texIn->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		// Vertical Gaussian blur
		SetDefault_Pipeline_State();
		m_rhiPipeline->SetRenderTarget(texIn);
		m_rhiPipeline->SetViewport(texIn->GetViewport());
		m_rhiPipeline->SetTexture(texOut);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		direction = Vector2(0.0f, pixelStride);
		SetDefault_Buffer(texIn->GetWidth(), texIn->GetHeight(), Matrix::Identity, sigma, direction);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		texIn.swap(texOut);

		m_rhiDevice->EventEnd();
	}

	void Renderer::Pass_TAA(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_TAA");
		SetDefault_Pipeline_State();

		// Resolve
		SetDefault_Buffer(m_renderTexFull_TAA_Current->GetWidth(), m_renderTexFull_TAA_Current->GetHeight());
		m_rhiPipeline->SetRenderTarget(m_renderTexFull_TAA_Current);
		m_rhiPipeline->SetViewport(m_renderTexFull_TAA_Current->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_taa);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetTexture(m_renderTexFull_TAA_History);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Velocity));
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		// Output to texOut
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_texture);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetTexture(m_renderTexFull_TAA_Current);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		// Swap textures so current becomes history
		m_renderTexFull_TAA_Current.swap(m_renderTexFull_TAA_History);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Bloom(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Bloom");

		// Bright pass
		SetDefault_Pipeline_State();
		SetDefault_Buffer(m_renderTexQuarter_Blur1->GetWidth(), m_renderTexQuarter_Blur1->GetHeight());
		m_rhiPipeline->SetRenderTarget(m_renderTexQuarter_Blur1);
		m_rhiPipeline->SetViewport(m_renderTexQuarter_Blur1->GetViewport());
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetPixelShader(m_ps_bloomBright);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		float sigma = 2.0f;
		Pass_BlurGaussian(m_renderTexQuarter_Blur1, m_renderTexQuarter_Blur2, sigma);

		// Additive blending
		SetDefault_Pipeline_State();	
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_renderTexQuarter_Blur2);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetPixelShader(m_ps_bloomBlend);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_ToneMapping(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_ToneMapping");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_toneMapping);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_GammaCorrection(std::shared_ptr<RHI_RenderTexture>& texIn, std::shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_GammaCorrection");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_gammaCorrection);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_FXAA(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_FXAA");

		// Common states
		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);

		// Luma
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_luma);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		// FXAA
		m_rhiPipeline->SetRenderTarget(texIn);
		m_rhiPipeline->SetViewport(texIn->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_fxaa);
		m_rhiPipeline->SetTexture(texOut);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		// Swap the textures
		texIn.swap(texOut);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_ChromaticAberration(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_ChromaticAberration");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_chromaticAberration);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_MotionBlur(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_MotionBlur");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetPixelShader(m_ps_motionBlur);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Velocity));
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Dithering(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Dithering");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_dithering);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Sharpening(shared_ptr<RHI_RenderTexture>& texIn, shared_ptr<RHI_RenderTexture>& texOut)
	{
		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Sharpening");

		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight());
		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetTexture(texIn);
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetPixelShader(m_ps_sharpening);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Lines(shared_ptr<RHI_RenderTexture>& texOut)
	{
		bool drawPickingRay = m_flags & Render_Gizmo_PickingRay;
		bool drawAABBs		= m_flags & Render_Gizmo_AABB;
		bool drawGrid		= m_flags & Render_Gizmo_Grid;
		bool drawLines		= !m_linesList_depthEnabled.empty() || !m_linesList_depthDisabled.empty(); // Any kind of lines, physics, user debug, etc.
		bool draw			= drawPickingRay || drawAABBs || drawGrid || drawLines;
		if (!draw)
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Lines");

		// Generate lines for debug primitives offered by the renderer
		{
			// Picking ray
			if (drawPickingRay)
			{
				const Ray& ray = m_camera->GetPickingRay();
				DrawLine(ray.GetStart(), ray.GetStart() + ray.GetDirection() * m_camera->GetFarPlane(), Vector4(0, 1, 0, 1));
			}

			// AABBs
			if (drawAABBs)
			{
				for (const auto& entity : m_entities[Renderable_ObjectOpaque])
				{
					if (auto renderable = entity->GetRenderable_PtrRaw())
					{
						DrawBox(renderable->Geometry_AABB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}

				for (const auto& entity : m_entities[Renderable_ObjectTransparent])
				{
					if (auto renderable = entity->GetRenderable_PtrRaw())
					{
						DrawBox(renderable->Geometry_AABB(), Vector4(0.41f, 0.86f, 1.0f, 1.0f));
					}
				}
			}
		}

		// Set common states
		SetDefault_Pipeline_State();
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_LineList);
		m_rhiPipeline->SetShader(m_vps_color);
		m_rhiPipeline->SetSampler(m_samplerPointClamp);
		m_rhiPipeline->SetRasterizerState(m_rasterizer_cullBack_wireframe);
		
		// unjittered matrix to avoid TAA jitter due to lack of motion vectors (line rendering is anti-aliased by D3D11, decently)
		Matrix viewProjection_unjittered = m_camera->GetViewMatrix() * m_camera->GetProjectionMatrix();

		// Draw lines that require depth
		m_rhiPipeline->SetDepthStencilState(m_depthStencil_enabled);
		m_rhiPipeline->SetRenderTarget(texOut, m_gbuffer->GetTexture(GBuffer_Target_Depth)->GetDepthStencilView());
		{
			// Grid
			if (drawGrid)
			{
				m_rhiPipeline->SetIndexBuffer(m_grid->GetIndexBuffer());
				m_rhiPipeline->SetVertexBuffer(m_grid->GetVertexBuffer());
				m_rhiPipeline->SetBlendState(m_blend_enabled);
				SetDefault_Buffer((unsigned int)m_resolution.x, (unsigned int)m_resolution.y, m_grid->ComputeWorldMatrix(m_camera->GetTransform()) * viewProjection_unjittered);
				m_rhiPipeline->DrawIndexed(m_grid->GetIndexCount(), 0, 0);
			}

			// Lines
			auto lineVertexBufferSize = (unsigned int)m_linesList_depthEnabled.size();
			if (lineVertexBufferSize != 0)
			{
				// Grow vertex buffer (if needed)
				if (lineVertexBufferSize > m_vertexBufferLines->GetVertexCount())
				{
					m_vertexBufferLines->CreateDynamic(sizeof(RHI_Vertex_PosCol), lineVertexBufferSize);
				}

				// Update vertex buffer
				auto buffer = (RHI_Vertex_PosCol*)m_vertexBufferLines->Map();
				copy(m_linesList_depthEnabled.begin(), m_linesList_depthEnabled.end(), buffer);
				m_vertexBufferLines->Unmap();

				// Set pipeline state
				m_rhiPipeline->SetVertexBuffer(m_vertexBufferLines);
				SetDefault_Buffer((unsigned int)m_resolution.x, (unsigned int)m_resolution.y, viewProjection_unjittered);
				m_rhiPipeline->Draw(lineVertexBufferSize);

				m_linesList_depthEnabled.clear();
			}
		}

		// Draw lines that don't require depth
		m_rhiPipeline->SetRenderTarget(texOut, nullptr);
		{
			// Lines
			auto lineVertexBufferSize = (unsigned int)m_linesList_depthDisabled.size();
			if (lineVertexBufferSize != 0)
			{
				// Grow vertex buffer (if needed)
				if (lineVertexBufferSize > m_vertexBufferLines->GetVertexCount())
				{
					m_vertexBufferLines->CreateDynamic(sizeof(RHI_Vertex_PosCol), lineVertexBufferSize);
				}

				// Update vertex buffer
				auto buffer = (RHI_Vertex_PosCol*)m_vertexBufferLines->Map();
				copy(m_linesList_depthDisabled.begin(), m_linesList_depthDisabled.end(), buffer);
				m_vertexBufferLines->Unmap();

				// Set pipeline state
				m_rhiPipeline->SetVertexBuffer(m_vertexBufferLines);
				SetDefault_Buffer((unsigned int)m_resolution.x, (unsigned int)m_resolution.y, viewProjection_unjittered);
				m_rhiPipeline->Draw(lineVertexBufferSize);

				m_linesList_depthDisabled.clear();
			}
		}

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_Gizmos(shared_ptr<RHI_RenderTexture>& texOut)
	{
		bool render_lights		= m_flags & Render_Gizmo_Lights;
		bool render_transform	= m_flags & Render_Gizmo_Transform;
		bool render				= render_lights || render_transform;
		if (!render)
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_Gizmos");

		// Set shared states
		SetDefault_Pipeline_State();
		m_rhiPipeline->SetRasterizerState(m_rasterizer_cullBack_solid);
		m_rhiPipeline->SetBlendState(m_blend_enabled);
		m_rhiPipeline->SetRenderTarget(texOut, nullptr);

		if (render_lights)
		{
			auto& lights = m_entities[Renderable_Light];
			if (lights.size() != 0)
			{
				m_rhiDevice->EventBegin("Gizmo_Lights");
				m_rhiPipeline->SetVertexShader(m_vs_quad);
				m_rhiPipeline->SetPixelShader(m_ps_texture);
				m_rhiPipeline->SetSampler(m_samplerBilinearClamp);

				for (const auto& entity : lights)
				{
					Vector3 position_light_world = entity->GetTransform_PtrRaw()->GetPosition();
					Vector3 position_camera_world = m_camera->GetTransform()->GetPosition();
					Vector3 direction_camera_to_light = (position_light_world - position_camera_world).Normalized();
					float VdL = Vector3::Dot(m_camera->GetTransform()->GetForward(), direction_camera_to_light);

					// Don't bother drawing if out of view
					if (VdL <= 0.5f)
						continue;

					// Compute light screen space position and scale (based on distance from the camera)
					Vector2 position_light_screen = m_camera->WorldToScreenPoint(position_light_world);
					float distance = (position_camera_world - position_light_world).Length() + M_EPSILON;
					float scale = GIZMO_MAX_SIZE / distance;
					scale = Clamp(scale, GIZMO_MIN_SIZE, GIZMO_MAX_SIZE);

					// Choose texture based on light type
					shared_ptr<RHI_Texture> lightTex = nullptr;
					LightType type = entity->GetComponent<Light>()->GetLightType();
					if (type == LightType_Directional)	lightTex = m_gizmoTexLightDirectional;
					else if (type == LightType_Point)	lightTex = m_gizmoTexLightPoint;
					else if (type == LightType_Spot)	lightTex = m_gizmoTexLightSpot;

					// Construct appropriate rectangle
					float texWidth = lightTex->GetWidth()	* scale;
					float texHeight = lightTex->GetHeight()	* scale;
					Rectangle rectangle = Rectangle(position_light_screen.x - texWidth * 0.5f, position_light_screen.y - texHeight * 0.5f, texWidth, texHeight);
					if (rectangle != m_gizmoRectLight)
					{
						m_gizmoRectLight = rectangle;
						m_gizmoRectLight.CreateBuffers(this);
					}

					SetDefault_Buffer((unsigned int)texWidth, (unsigned int)texWidth, m_viewProjection_Orthographic);
					m_rhiPipeline->SetTexture(lightTex);
					m_rhiPipeline->SetIndexBuffer(m_gizmoRectLight.GetIndexBuffer());
					m_rhiPipeline->SetVertexBuffer(m_gizmoRectLight.GetVertexBuffer());
					m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);
				}

				m_rhiDevice->EventEnd();
			}
		}

		// Transform
		if (render_transform)
		{
			if (m_transformGizmo->Update(m_context->GetSubsystem<World>()->GetSelectedentity(), m_camera.get(), m_gizmo_transform_size, m_gizmo_transform_speed))
			{
				m_rhiDevice->EventBegin("Gizmo_Transform");

				m_rhiPipeline->SetShader(m_vps_gizmoTransform);
				m_rhiPipeline->SetIndexBuffer(m_transformGizmo->GetIndexBuffer());
				m_rhiPipeline->SetVertexBuffer(m_transformGizmo->GetVertexBuffer());
				SetDefault_Buffer((unsigned int)m_resolution.x, (unsigned int)m_resolution.y);

				// Axis - X
				auto buffer = Struct_Matrix_Vector3(m_transformGizmo->GetHandle().GetTransform(Vector3::Right), m_transformGizmo->GetHandle().GetColor(Vector3::Right));
				m_vps_gizmoTransform->UpdateBuffer(&buffer);
				m_rhiPipeline->SetConstantBuffer(m_vps_gizmoTransform->GetConstantBuffer(), 1, Buffer_Global);
				m_rhiPipeline->DrawIndexed(m_transformGizmo->GetIndexCount(), 0, 0);

				// Axis - Y
				buffer = Struct_Matrix_Vector3(m_transformGizmo->GetHandle().GetTransform(Vector3::Up), m_transformGizmo->GetHandle().GetColor(Vector3::Up));
				m_vps_gizmoTransform->UpdateBuffer(&buffer);
				m_rhiPipeline->SetConstantBuffer(m_vps_gizmoTransform->GetConstantBuffer(), 1, Buffer_Global);
				m_rhiPipeline->DrawIndexed(m_transformGizmo->GetIndexCount(), 0, 0);

				// Axis - Z
				buffer = Struct_Matrix_Vector3(m_transformGizmo->GetHandle().GetTransform(Vector3::Forward), m_transformGizmo->GetHandle().GetColor(Vector3::Forward));
				m_vps_gizmoTransform->UpdateBuffer(&buffer);
				m_rhiPipeline->SetConstantBuffer(m_vps_gizmoTransform->GetConstantBuffer(), 1, Buffer_Global);
				m_rhiPipeline->DrawIndexed(m_transformGizmo->GetIndexCount(), 0, 0);

				// Axes - XYZ
				if (m_transformGizmo->DrawXYZ())
				{
					buffer = Struct_Matrix_Vector3(m_transformGizmo->GetHandle().GetTransform(Vector3::One), m_transformGizmo->GetHandle().GetColor(Vector3::One));
					m_vps_gizmoTransform->UpdateBuffer(&buffer);
					m_rhiPipeline->SetConstantBuffer(m_vps_gizmoTransform->GetConstantBuffer(), 1, Buffer_Global);
					m_rhiPipeline->DrawIndexed(m_transformGizmo->GetIndexCount(), 0, 0);
				}

				m_rhiDevice->EventEnd();
			}
		}

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	void Renderer::Pass_PerformanceMetrics(shared_ptr<RHI_RenderTexture>& texOut)
	{
		bool draw = m_flags & Render_Gizmo_PerformanceMetrics;
		if (!draw)
			return;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_PerformanceMetrics");
		SetDefault_Pipeline_State();

		// Updated text
		Vector2 textPos = Vector2(-(int)m_viewport.GetWidth() * 0.5f + 1.0f, (int)m_viewport.GetHeight() * 0.5f);
		m_font->SetText(m_profiler->GetMetrics(), textPos);
		// Updated constant buffer
		auto buffer = Struct_Matrix_Vector4(m_viewProjection_Orthographic, m_font->GetColor());
		m_vps_font->UpdateBuffer(&buffer);

		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetTexture(m_font->GetTexture());
		m_rhiPipeline->SetBlendState(m_blend_enabled);
		m_rhiPipeline->SetIndexBuffer(m_font->GetIndexBuffer());
		m_rhiPipeline->SetVertexBuffer(m_font->GetVertexBuffer());	
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->SetShader(m_vps_font);
		m_rhiPipeline->SetConstantBuffer(m_vps_font->GetConstantBuffer(), 0, Buffer_Global);
		m_rhiPipeline->DrawIndexed(m_font->GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);
	}

	bool Renderer::Pass_DebugBuffer(shared_ptr<RHI_RenderTexture>& texOut)
	{
		if (m_debugBuffer == RendererDebug_None)
			return true;

		TIME_BLOCK_START_MULTI(m_profiler);
		m_rhiDevice->EventBegin("Pass_DebugBuffer");
		SetDefault_Pipeline_State();
		SetDefault_Buffer(texOut->GetWidth(), texOut->GetHeight(), m_viewProjection_Orthographic);
		m_rhiPipeline->SetVertexShader(m_vs_quad);

		// Bind correct texture & shader pass
		if (m_debugBuffer == RendererDebug_Albedo)
		{
			m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Albedo));
			m_rhiPipeline->SetPixelShader(m_ps_texture);
		}

		if (m_debugBuffer == RendererDebug_Normal)
		{
			m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Normal));
			m_rhiPipeline->SetPixelShader(m_ps_debugNormal);
		}

		if (m_debugBuffer == RendererDebug_Material)
		{
			m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Material));
			m_rhiPipeline->SetPixelShader(m_ps_texture);
		}

		if (m_debugBuffer == RendererDebug_Velocity)
		{
			m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Velocity));
			m_rhiPipeline->SetPixelShader(m_ps_debugVelocity);
		}

		if (m_debugBuffer == RendererDebug_Depth)
		{
			m_rhiPipeline->SetTexture(m_gbuffer->GetTexture(GBuffer_Target_Depth));
			m_rhiPipeline->SetPixelShader(m_ps_debugDepth);
		}

		if ((m_debugBuffer == RendererDebug_SSAO))
		{
			if (Flags_IsSet(Render_PostProcess_SSAO))
			{
				m_rhiPipeline->SetTexture(m_renderTexHalf_SSAO);
			}
			else
			{
				m_rhiPipeline->SetTexture(m_texWhite);
			}
			m_rhiPipeline->SetPixelShader(m_ps_debugSSAO);
		}

		m_rhiPipeline->SetRenderTarget(texOut);
		m_rhiPipeline->SetViewport(texOut->GetViewport());
		m_rhiPipeline->SetVertexBuffer(m_quad.GetVertexBuffer());
		m_rhiPipeline->SetIndexBuffer(m_quad.GetIndexBuffer());
		m_rhiPipeline->SetPrimitiveTopology(PrimitiveTopology_TriangleList);
		m_rhiPipeline->SetRasterizerState(m_rasterizer_cullBack_solid);
		m_rhiPipeline->SetInputLayout(m_ps_texture->GetInputLayout());
		m_rhiPipeline->SetSampler(m_samplerBilinearClamp);
		m_rhiPipeline->DrawIndexed(m_quad.GetIndexCount(), 0, 0);

		m_rhiDevice->EventEnd();
		TIME_BLOCK_END_MULTI(m_profiler);

		return true;
	}
}