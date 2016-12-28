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

//= INCLUDES ========================
#include "ShaderVariation.h"
#include "../../Core/GUIDGenerator.h"
#include "../../Logging/Log.h"
#include "../../Core/Settings.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

ShaderVariation::ShaderVariation()
{
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
	bool albedo,
	bool roughness,
	bool metallic,
	bool normal,
	bool height,
	bool occlusion,
	bool emission,
	bool mask,
	bool cubemap,
	D3D11GraphicsDevice* graphicsDevice
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

	m_graphics = graphicsDevice;
	m_ID = GENERATE_GUID; // generate an ID for this shader
	Load(); // load the shader
}

bool ShaderVariation::SaveMetadata()
{
	return true;
}

void ShaderVariation::Set()
{
	if (m_D3D11Shader)
		m_D3D11Shader->Set();
}

void ShaderVariation::UpdatePerFrameBuffer(Light* directionalLight, Camera* camera)
{
	if (!m_D3D11Shader->IsCompiled())
	{
		LOG_ERROR("Can't render using a shader variation that hasn't been loaded or failed to compile.");
		return;
	}

	if (!directionalLight || !camera)
		return;

	//= BUFFER UPDATE ========================================================================
	PerFrameBufferType* buffer = (PerFrameBufferType*)m_miscBuffer->Map();
	if (!buffer)
		return;

	buffer->viewport = GET_RESOLUTION;
	buffer->nearPlane = camera->GetNearPlane();
	buffer->farPlane = camera->GetFarPlane();
	buffer->mLightViewProjection[0] = directionalLight->CalculateViewMatrix() * directionalLight->CalculateOrthographicProjectionMatrix(0);
	buffer->mLightViewProjection[1] = directionalLight->CalculateViewMatrix() * directionalLight->CalculateOrthographicProjectionMatrix(1);
	buffer->mLightViewProjection[2] = directionalLight->CalculateViewMatrix() * directionalLight->CalculateOrthographicProjectionMatrix(2);
	buffer->shadowSplits = Vector4(directionalLight->GetShadowCascadeSplit(0), directionalLight->GetShadowCascadeSplit(1), directionalLight->GetShadowCascadeSplit(2), directionalLight->GetShadowCascadeSplit(2));
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

void ShaderVariation::UpdatePerMaterialBuffer(shared_ptr<Material> material)
{
	if (!m_D3D11Shader->IsCompiled())
	{
		LOG_ERROR("Can't render using a shader variation that hasn't been loaded or failed to compile.");
		return;
	}

	// Determine if the buffer actually needs to update
	bool update = false;
	update = perMaterialBufferCPU.matAlbedo != material->GetColorAlbedo() ? true : update;
	update = perMaterialBufferCPU.matTilingUV != material->GetTilingUV() ? true : update;
	update = perMaterialBufferCPU.matOffsetUV != material->GetOffsetUV() ? true : update;
	update = perMaterialBufferCPU.matRoughnessMul != material->GetRoughnessMultiplier() ? true : update;
	update = perMaterialBufferCPU.matMetallicMul != material->GetMetallicMultiplier() ? true : update;
	update = perMaterialBufferCPU.matOcclusionMul != material->GetOcclusionMultiplier() ? true : update;
	update = perMaterialBufferCPU.matNormalMul != material->GetNormalMultiplier() ? true : update;
	update = perMaterialBufferCPU.matSpecularMul != material->GetSpecularMultiplier() ? true : update;
	update = perMaterialBufferCPU.matShadingMode != float(material->GetShadingMode()) ? true : update;

	//if (!update)
		//return;

	//= BUFFER UPDATE =========================================================================
	PerMaterialBufferType* buffer = (PerMaterialBufferType*)m_materialBuffer->Map();

	buffer->matAlbedo = perMaterialBufferCPU.matAlbedo = material->GetColorAlbedo();
	buffer->matTilingUV = perMaterialBufferCPU.matTilingUV = material->GetTilingUV();
	buffer->matOffsetUV = perMaterialBufferCPU.matOffsetUV = material->GetOffsetUV();
	buffer->matRoughnessMul = perMaterialBufferCPU.matRoughnessMul = material->GetRoughnessMultiplier();
	buffer->matMetallicMul = perMaterialBufferCPU.matMetallicMul = material->GetMetallicMultiplier();
	buffer->matOcclusionMul = perMaterialBufferCPU.matOcclusionMul = material->GetOcclusionMultiplier();
	buffer->matNormalMul = perMaterialBufferCPU.matNormalMul = material->GetNormalMultiplier();
	buffer->matSpecularMul = perMaterialBufferCPU.matSpecularMul = material->GetSpecularMultiplier();
	buffer->matShadingMode = perMaterialBufferCPU.matShadingMode = float(material->GetShadingMode());
	buffer->padding = Vector2::Zero;

	m_materialBuffer->Unmap();
	//========================================================================================

	// Set to shader slot
	m_materialBuffer->SetVS(1);
	m_materialBuffer->SetPS(1);
}

void ShaderVariation::UpdatePerObjectBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection, bool receiveShadows)
{
	if (!m_D3D11Shader->IsCompiled())
	{
		LOG_ERROR("Can't render using a shader variation that hasn't been loaded or failed to compile.");
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

	if (!update)
		return;

	//= BUFFER UPDATE =======================================================================
	PerObjectBufferType* buffer = (PerObjectBufferType*)m_perObjectBuffer->Map();

	buffer->mWorld = perObjectBufferCPU.mWorld = world;
	buffer->mWorldView = perObjectBufferCPU.mWorldView = worldView;
	buffer->mWorldViewProjection = perObjectBufferCPU.mWorldViewProjection = worldViewProjection;
	buffer->receiveShadows = perObjectBufferCPU.receiveShadows = (float)receiveShadows;
	buffer->padding = Vector3::Zero;

	m_perObjectBuffer->Unmap();
	//=======================================================================================

	// Set to shader slot
	m_perObjectBuffer->SetVS(2);
	m_perObjectBuffer->SetPS(2);
}

void ShaderVariation::UpdateTextures(const vector<ID3D11ShaderResourceView*>& textureArray)
{
	if (!m_graphics)
		return;

	m_graphics->GetDeviceContext()->PSSetShaderResources(0, (UINT)textureArray.size(), &textureArray.front());
}

void ShaderVariation::Render(int indexCount)
{
	if (!m_graphics)
		return;

	m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}

void ShaderVariation::AddDefinesBasedOnMaterial(shared_ptr<D3D11Shader> shader)
{
	if (!shader)
		return;

	// Write the properties of the material as defines
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

void ShaderVariation::Load()
{
	// load the vertex and the pixel shader
	m_D3D11Shader = make_shared<D3D11Shader>(m_graphics);
	AddDefinesBasedOnMaterial(m_D3D11Shader);
	m_D3D11Shader->Load("Data/Shaders/GBuffer.hlsl");
	m_D3D11Shader->SetInputLayout(PositionTextureNormalTangent);
	m_D3D11Shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS); // anisotropic

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