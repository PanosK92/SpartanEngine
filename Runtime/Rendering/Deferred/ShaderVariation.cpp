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
#include "ShaderVariation.h"
#include "../Material.h"
#include "../../RHI/IRHI_Implementation.h"
#include "../../Scene/Components/Transform.h"
#include "../../Scene/Components/Camera.h"
#include "../Renderer.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ShaderVariation::ShaderVariation(Context* context): IResource(context)
	{
		//= IResource ======================
		RegisterResource<ShaderVariation>();
		//==================================

		m_rhiDevice		= m_context->GetSubsystem<Renderer>()->GetRHIDevice();
		m_shaderFlags	= 0;
	}

	void ShaderVariation::Compile(const string& filePath, unsigned long shaderFlags)
	{
		m_shaderFlags = shaderFlags;
		if (!m_rhiDevice)
		{
			LOG_INFO("GraphicsDevice is expired. Cant't compile shader");
			return;
		}

		// Load and compile the vertex and the pixel shader
		m_shader = make_shared<RHI_Shader>(m_rhiDevice);
		AddDefinesBasedOnMaterial(m_shader);
		m_shader->Compile(filePath, Input_PositionTextureTBN);

		// Matrix Buffer
		m_perObjectBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_perObjectBuffer->Create(sizeof(PerObjectBufferType));

		// Object Buffer
		m_materialBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_materialBuffer->Create(sizeof(PerMaterialBufferType));

		// Object Buffer
		m_perfFrameBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_perfFrameBuffer->Create(sizeof(PerFrameBufferType));
	}

	void ShaderVariation::UpdatePerFrameBuffer(Camera* camera)
	{
		if (!m_shader || !m_shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per frame buffer.");
			return;
		}

		if (!camera)
			return;

		//= BUFFER UPDATE ==========================================
		auto buffer = (PerFrameBufferType*)m_perfFrameBuffer->Map();

		buffer->cameraPos	= camera->GetTransform()->GetPosition();
		buffer->padding		= 0.0f;
		buffer->viewport	= Settings::Get().GetResolution();
		buffer->padding2	= Vector2::Zero;
		
		m_perfFrameBuffer->Unmap();
		//==========================================================
	}

	void ShaderVariation::UpdatePerMaterialBuffer(Material* material)
	{
		if (!material)
			return;

		if (!m_shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per material buffer.");
			return;
		}

		// Determine if the material buffer needs to update
		bool update = false;
		update = perMaterialBufferCPU.matAlbedo			!= material->GetColorAlbedo()			? true : update;
		update = perMaterialBufferCPU.matTilingUV		!= material->GetTiling()				? true : update;
		update = perMaterialBufferCPU.matOffsetUV		!= material->GetOffset()				? true : update;
		update = perMaterialBufferCPU.matRoughnessMul	!= material->GetRoughnessMultiplier()	? true : update;
		update = perMaterialBufferCPU.matMetallicMul	!= material->GetMetallicMultiplier()	? true : update;
		update = perMaterialBufferCPU.matNormalMul		!= material->GetNormalMultiplier()		? true : update;
		update = perMaterialBufferCPU.matShadingMode	!= float(material->GetShadingMode())	? true : update;

		if (update)
		{
			//= BUFFER UPDATE ===================================================================================
			auto buffer = (PerMaterialBufferType*)m_materialBuffer->Map();

			buffer->matAlbedo		= perMaterialBufferCPU.matAlbedo		= material->GetColorAlbedo();
			buffer->matTilingUV		= perMaterialBufferCPU.matTilingUV		= material->GetTiling();
			buffer->matOffsetUV		= perMaterialBufferCPU.matOffsetUV		= material->GetOffset();
			buffer->matRoughnessMul = perMaterialBufferCPU.matRoughnessMul	= material->GetRoughnessMultiplier();
			buffer->matMetallicMul	= perMaterialBufferCPU.matMetallicMul	= material->GetMetallicMultiplier();
			buffer->matNormalMul	= perMaterialBufferCPU.matNormalMul		= material->GetNormalMultiplier();
			buffer->matHeightMul	= perMaterialBufferCPU.matNormalMul		= material->GetHeightMultiplier();
			buffer->matShadingMode	= perMaterialBufferCPU.matShadingMode	= float(material->GetShadingMode());
			buffer->paddding		= Vector3::Zero;

			m_materialBuffer->Unmap();
			//===================================================================================================
		}
	}

	void ShaderVariation::UpdatePerObjectBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection)
	{
		if (!m_shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per object buffer.");
			return;
		}

		Matrix world				= mWorld;
		Matrix worldView			= mWorld * mView;
		Matrix worldViewProjection	= worldView * mProjection;

		// Determine if the buffer actually needs to update
		bool update = false;
		update = perObjectBufferCPU.mWorld					!= world ? true : update;
		update = perObjectBufferCPU.mWorldView				!= worldView ? true : update;
		update = perObjectBufferCPU.mWorldViewProjection	!= worldViewProjection ? true : update;

		if (update)
		{
			//= BUFFER UPDATE ============================================================================
			auto* buffer = (PerObjectBufferType*)m_perObjectBuffer->Map();

			buffer->mWorld = perObjectBufferCPU.mWorld								= world;
			buffer->mWorldView = perObjectBufferCPU.mWorldView						= worldView;
			buffer->mWorldViewProjection = perObjectBufferCPU.mWorldViewProjection	= worldViewProjection;

			m_perObjectBuffer->Unmap();
			//============================================================================================
		}
	}

	void ShaderVariation::AddDefinesBasedOnMaterial(const shared_ptr<RHI_Shader>& shader)
	{
		if (!shader)
			return;

		// Define in the shader what kind of textures it should expect
		shader->AddDefine("ALBEDO_MAP",		HasAlbedoTexture()		? "1" : "0");
		shader->AddDefine("ROUGHNESS_MAP",	HasRoughnessTexture()	? "1" : "0");
		shader->AddDefine("METALLIC_MAP",	HasMetallicTexture()	? "1" : "0");
		shader->AddDefine("NORMAL_MAP",		HasNormalTexture()		? "1" : "0");
		shader->AddDefine("HEIGHT_MAP",		HasHeightTexture()		? "1" : "0");
		shader->AddDefine("OCCLUSION_MAP",	HasOcclusionTexture()	? "1" : "0");
		shader->AddDefine("EMISSION_MAP",	HasEmissionTexture()	? "1" : "0");
		shader->AddDefine("MASK_MAP",		HasMaskTexture()		? "1" : "0");
		shader->AddDefine("CUBE_MAP",		HasCubeMapTexture()		? "1" : "0");
	}
}