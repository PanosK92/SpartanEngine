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

//= INCLUDES ========================
#include "ShaderVariation.h"
#include "../../Core/GUIDGenerator.h"
#include "../../Logging/Log.h"
#include "../../Core/Settings.h"
#include "../../IO/Serializer.h"
//===================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ShaderVariation::ShaderVariation()
	{
		// Resource
		m_resourceID = GENERATE_GUID;
		m_resourceType = Shader_Resource;

		// Shader
		m_graphics = nullptr;
		m_D3D11Shader = nullptr;
		m_perObjectBuffer = nullptr;
		m_materialBuffer = nullptr;
		m_miscBuffer = nullptr;

		m_hasAlbedoTexture = false;
		m_hasRoughnessTexture = false;
		m_hasMetallicTexture = false;
		m_hasOcclusionTexture = false;
		m_hasEmissionTexture = false;
		m_hasNormalTexture = false;
		m_hasHeightTexture = false;
		m_hasMaskTexture = false;
		m_hasCubeMap = false;
	}

	ShaderVariation::~ShaderVariation()
	{

	}

	void ShaderVariation::Initialize(
		Context* context,
		bool albedo,
		bool roughness,
		bool metallic,
		bool normal,
		bool height,
		bool occlusion,
		bool emission,
		bool mask,
		bool cubemap
	)
	{
		// Save the properties of the material
		m_hasAlbedoTexture = albedo;
		m_hasRoughnessTexture = roughness;
		m_hasMetallicTexture = metallic;
		m_hasNormalTexture = normal;
		m_hasHeightTexture = height;
		m_hasOcclusionTexture = occlusion;
		m_hasEmissionTexture = emission;
		m_hasMaskTexture = mask;
		m_hasCubeMap = cubemap;

		m_context = context;
		m_graphics = m_context->GetSubsystem<Graphics>();

		Compile(m_resourceFilePath);
	}

	bool ShaderVariation::LoadFromFile(const string& filePath)
	{
		if (!Serializer::StartReading(filePath))
			return false;

		m_resourceID = Serializer::ReadSTR();
		m_resourceName = Serializer::ReadSTR();
		m_resourceFilePath = Serializer::ReadSTR();
		m_hasAlbedoTexture = Serializer::ReadBool();
		m_hasRoughnessTexture = Serializer::ReadBool();
		m_hasMetallicTexture = Serializer::ReadBool();
		m_hasNormalTexture = Serializer::ReadBool();
		m_hasHeightTexture = Serializer::ReadBool();
		m_hasOcclusionTexture = Serializer::ReadBool();
		m_hasEmissionTexture = Serializer::ReadBool();
		m_hasMaskTexture = Serializer::ReadBool();
		m_hasCubeMap = Serializer::ReadBool();

		Serializer::StopReading();

		return true;
	}

	bool ShaderVariation::SaveToFile(const string& filePath)
	{
		string savePath = filePath;

		if (savePath == RESOURCE_SAVE)
		{
			savePath = m_resourceFilePath;
		}

		// Add shader extension if missing
		if (FileSystem::GetExtensionFromFilePath(filePath) != SHADER_EXTENSION)
		{
			savePath += SHADER_EXTENSION;
		}

		if (!Serializer::StartWriting(savePath))
			return false;

		Serializer::WriteSTR(m_resourceID);
		Serializer::WriteSTR(m_resourceName);
		Serializer::WriteSTR(m_resourceFilePath);
		Serializer::WriteBool(m_hasAlbedoTexture);
		Serializer::WriteBool(m_hasRoughnessTexture);
		Serializer::WriteBool(m_hasMetallicTexture);
		Serializer::WriteBool(m_hasNormalTexture);
		Serializer::WriteBool(m_hasHeightTexture);
		Serializer::WriteBool(m_hasOcclusionTexture);
		Serializer::WriteBool(m_hasEmissionTexture);
		Serializer::WriteBool(m_hasMaskTexture);
		Serializer::WriteBool(m_hasCubeMap);
	
		Serializer::StopWriting();

		return true;
	}

	void ShaderVariation::Set()
	{
		if (!m_D3D11Shader)
		{
			LOG_WARNING("Can't set uninitialized shader");
			return;
		}

		m_D3D11Shader->Set();
	}

	void ShaderVariation::UpdatePerFrameBuffer(Light* directionalLight, Camera* camera)
	{
		if (!m_D3D11Shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per frame buffer.");
			return;
		}

		if (!directionalLight || !camera)
			return;

		//= BUFFER UPDATE ========================================================================
		PerFrameBufferType* buffer = (PerFrameBufferType*)m_miscBuffer->Map();

		buffer->viewport = GET_RESOLUTION;
		buffer->nearPlane = camera->GetNearPlane();
		buffer->farPlane = camera->GetFarPlane();
		buffer->mLightViewProjection[0] = directionalLight->ComputeViewMatrix() * directionalLight->ComputeOrthographicProjectionMatrix(0);
		buffer->mLightViewProjection[1] = directionalLight->ComputeViewMatrix() * directionalLight->ComputeOrthographicProjectionMatrix(1);
		buffer->mLightViewProjection[2] = directionalLight->ComputeViewMatrix() * directionalLight->ComputeOrthographicProjectionMatrix(2);
		buffer->shadowSplits = Vector4(directionalLight->GetShadowCascadeSplit(0), directionalLight->GetShadowCascadeSplit(1), directionalLight->GetShadowCascadeSplit(2), directionalLight->GetShadowCascadeSplit(3));
		buffer->lightDir = directionalLight->GetDirection();
		buffer->shadowBias = directionalLight->GetBias();
		buffer->shadowMapResolution = directionalLight->GetShadowCascadeResolution();
		buffer->shadowMappingQuality = directionalLight->GetShadowTypeAsFloat();
		buffer->padding = Vector2::Zero;

		m_miscBuffer->Unmap();
		//========================================================================================

		// Set to shader slot
		m_miscBuffer->SetVS(0);
		m_miscBuffer->SetPS(0);
	}

	void ShaderVariation::UpdatePerMaterialBuffer(weak_ptr<Material> material)
	{
		if (material.expired())
			return;

		auto materialRaw = material._Get();

		if (!m_D3D11Shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per material buffer.");
			return;
		}

		// Determine if the material buffer needs to update
		bool update = false;
		update = perMaterialBufferCPU.matAlbedo != materialRaw->GetColorAlbedo() ? true : update;
		update = perMaterialBufferCPU.matTilingUV != materialRaw->GetTilingUV() ? true : update;
		update = perMaterialBufferCPU.matOffsetUV != materialRaw->GetOffsetUV() ? true : update;
		update = perMaterialBufferCPU.matRoughnessMul != materialRaw->GetRoughnessMultiplier() ? true : update;
		update = perMaterialBufferCPU.matMetallicMul != materialRaw->GetMetallicMultiplier() ? true : update;
		update = perMaterialBufferCPU.matOcclusionMul != materialRaw->GetOcclusionMultiplier() ? true : update;
		update = perMaterialBufferCPU.matNormalMul != materialRaw->GetNormalMultiplier() ? true : update;
		update = perMaterialBufferCPU.matSpecularMul != materialRaw->GetSpecularMultiplier() ? true : update;
		update = perMaterialBufferCPU.matShadingMode != float(materialRaw->GetShadingMode()) ? true : update;

		if (update)
		{
			//= BUFFER UPDATE =========================================================================
			PerMaterialBufferType* buffer = (PerMaterialBufferType*)m_materialBuffer->Map();

			buffer->matAlbedo = perMaterialBufferCPU.matAlbedo = materialRaw->GetColorAlbedo();
			buffer->matTilingUV = perMaterialBufferCPU.matTilingUV = materialRaw->GetTilingUV();
			buffer->matOffsetUV = perMaterialBufferCPU.matOffsetUV = materialRaw->GetOffsetUV();
			buffer->matRoughnessMul = perMaterialBufferCPU.matRoughnessMul = materialRaw->GetRoughnessMultiplier();
			buffer->matMetallicMul = perMaterialBufferCPU.matMetallicMul = materialRaw->GetMetallicMultiplier();
			buffer->matOcclusionMul = perMaterialBufferCPU.matOcclusionMul = materialRaw->GetOcclusionMultiplier();
			buffer->matNormalMul = perMaterialBufferCPU.matNormalMul = materialRaw->GetNormalMultiplier();
			buffer->matSpecularMul = perMaterialBufferCPU.matSpecularMul = materialRaw->GetSpecularMultiplier();
			buffer->matShadingMode = perMaterialBufferCPU.matShadingMode = float(materialRaw->GetShadingMode());
			buffer->padding = Vector2::Zero;

			m_materialBuffer->Unmap();
			//========================================================================================
		}

		// Set to shader slot
		m_materialBuffer->SetVS(1);
		m_materialBuffer->SetPS(1);
	}

	void ShaderVariation::UpdatePerObjectBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, bool receiveShadows)
	{
		if (!m_D3D11Shader->IsCompiled())
		{
			LOG_ERROR("Shader hasn't been loaded or failed to compile. Can't update per object buffer.");
			return;
		}

		Matrix world = mWorld;
		Matrix worldView = mWorld * mView;
		Matrix worldViewProjection = worldView * mProjection;

		// Determine if the buffer actually needs to update
		bool update = false;
		update = perObjectBufferCPU.mWorld != world ? true : update;
		update = perObjectBufferCPU.mWorldView != worldView ? true : update;
		update = perObjectBufferCPU.mWorldViewProjection != worldViewProjection ? true : update;
		update = perObjectBufferCPU.receiveShadows != (float)receiveShadows ? true : update;

		if (update)
		{
			//= BUFFER UPDATE =======================================================================
			PerObjectBufferType* buffer = (PerObjectBufferType*)m_perObjectBuffer->Map();

			buffer->mWorld = perObjectBufferCPU.mWorld = world;
			buffer->mWorldView = perObjectBufferCPU.mWorldView = worldView;
			buffer->mWorldViewProjection = perObjectBufferCPU.mWorldViewProjection = worldViewProjection;
			buffer->receiveShadows = perObjectBufferCPU.receiveShadows = (float)receiveShadows;
			buffer->padding = Vector3::Zero;

			m_perObjectBuffer->Unmap();
			//=======================================================================================
		}

		// Set to shader slot
		m_perObjectBuffer->SetVS(2);
		m_perObjectBuffer->SetPS(2);
	}

	void ShaderVariation::UpdateTextures(const vector<ID3D11ShaderResourceView*>& textureArray)
	{
		if (!m_graphics)
		{
			LOG_INFO("GraphicsDevice is expired. Cant't update shader textures.");
			return;
		}

		m_graphics->GetDeviceContext()->PSSetShaderResources(0, (UINT)textureArray.size(), &textureArray.front());
	}

	void ShaderVariation::Render(int indexCount)
	{
		if (!m_graphics)
		{
			LOG_INFO("GraphicsDevice is expired. Cant't render with shader");
			return;
		}

		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
	}

	void ShaderVariation::AddDefinesBasedOnMaterial(shared_ptr<D3D11Shader> shader)
	{
		if (!shader)
			return;

		// Define in the shader what kind of textures it should expect
		shader->AddDefine("ALBEDO_MAP", m_hasAlbedoTexture);
		shader->AddDefine("ROUGHNESS_MAP", m_hasRoughnessTexture);
		shader->AddDefine("METALLIC_MAP", m_hasMetallicTexture);
		shader->AddDefine("NORMAL_MAP", m_hasNormalTexture);
		shader->AddDefine("HEIGHT_MAP", m_hasHeightTexture);
		shader->AddDefine("OCCLUSION_MAP", m_hasOcclusionTexture);
		shader->AddDefine("EMISSION_MAP", m_hasEmissionTexture);
		shader->AddDefine("MASK_MAP", m_hasMaskTexture);
		shader->AddDefine("CUBE_MAP", m_hasCubeMap);
	}

	void ShaderVariation::Compile(const string& filePath)
	{
		if (!m_graphics)
		{
			LOG_INFO("GraphicsDevice is expired. Cant't compile shader");
			return;
		}

		// Load and compile the vertex and the pixel shader
		m_D3D11Shader = make_shared<D3D11Shader>(m_graphics);
		AddDefinesBasedOnMaterial(m_D3D11Shader);
		m_D3D11Shader->Load(filePath);
		m_D3D11Shader->SetInputLayout(PositionTextureNormalTangent);
		m_D3D11Shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

		// Matrix Buffer
		m_perObjectBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
		m_perObjectBuffer->Create(sizeof(PerObjectBufferType));

		// Object Buffer
		m_materialBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
		m_materialBuffer->Create(sizeof(PerMaterialBufferType));

		// Object Buffer
		m_miscBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
		m_miscBuffer->Create(sizeof(PerFrameBufferType));
	}
}