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
#include "../../Core/Helper.h"
#include "../../Core/GUIDGenerator.h"
#include "../../IO/Log.h"
#include "../../Core/Settings.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

ShaderVariation::ShaderVariation()
{
	m_graphicsDevice = nullptr;
	m_D3D11Shader = nullptr;
	m_befaultBuffer = nullptr;

	m_hasAlbedoTexture = false;
	m_hasRoughnessTexture = false;
	m_hasMetallicTexture = false;
	m_hasOcclusionTexture = false;
	m_hasNormalTexture = false;
	m_hasHeightTexture = false;
	m_hasMaskTexture = false;
	m_hasCubeMap = false;
}

ShaderVariation::~ShaderVariation()
{
	SafeDelete(m_befaultBuffer);
	SafeDelete(m_D3D11Shader);
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
	GraphicsDevice* graphicsDevice
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

	m_graphicsDevice = graphicsDevice;
	m_ID = GENERATE_GUID; // generate an ID for this shader
	Load(); // load the shader
}

void ShaderVariation::Set()
{
	m_D3D11Shader->Set();
}

void ShaderVariation::AddDefinesBasedOnMaterial()
{
	// Write the properties of the material as defines
	m_D3D11Shader->AddDefine("ALBEDO_MAP", m_hasAlbedoTexture);
	m_D3D11Shader->AddDefine("ROUGHNESS_MAP", m_hasRoughnessTexture);
	m_D3D11Shader->AddDefine("METALLIC_MAP", m_hasMetallicTexture);
	m_D3D11Shader->AddDefine("NORMAL_MAP", m_hasNormalTexture);
	m_D3D11Shader->AddDefine("HEIGHT_MAP", m_hasHeightTexture);
	m_D3D11Shader->AddDefine("OCCLUSION_MAP", m_hasOcclusionTexture);
	m_D3D11Shader->AddDefine("EMISSION_MAP", m_hasEmissionTexture);
	m_D3D11Shader->AddDefine("MASK_MAP", m_hasMaskTexture);
	m_D3D11Shader->AddDefine("CUBE_MAP", m_hasCubeMap);
}

void ShaderVariation::Load()
{
	// load the vertex and the pixel shader
	m_D3D11Shader = new D3D11Shader();
	m_D3D11Shader->Initialize(m_graphicsDevice);
	AddDefinesBasedOnMaterial();
	m_D3D11Shader->Load("Assets/Shaders/GBuffer.hlsl");
	m_D3D11Shader->SetInputLayout(PositionTextureNormalTangent);
	m_D3D11Shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS); // anisotropic

	// material buffer
	m_befaultBuffer = new D3D11Buffer();
	m_befaultBuffer->Initialize(m_graphicsDevice);
	m_befaultBuffer->CreateConstantBuffer(sizeof(DefaultBufferType));
}

void ShaderVariation::Render(int indexCount,
	const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection,
	Material* material, const vector<ID3D11ShaderResourceView*>& textureArray, Light* directionalLight, bool receiveShadows, Camera* camera) const
{
	if (!m_D3D11Shader->IsCompiled())
	{
		LOG_ERROR("Can't render using a shader variation that hasn't been loaded or failed to compile.");
		return;
	}

	if (!directionalLight || !camera)
		return;

	Matrix world = mWorld;
	Matrix worldView = world * mView;
	Matrix worldViewProjection = worldView * mProjection;

	Matrix lightViewProjection1 = directionalLight->GetViewMatrix() * directionalLight->GetOrthographicProjectionMatrix(0);
	Matrix lightViewProjection2 = directionalLight->GetViewMatrix() * directionalLight->GetOrthographicProjectionMatrix(1);
	Matrix lightViewProjection3 = directionalLight->GetViewMatrix() * directionalLight->GetOrthographicProjectionMatrix(2);

	/*------------------------------------------------------------------------------
							[FILL THE BUFFER]
	------------------------------------------------------------------------------*/
	{ // this can be done only when needed - tested
		DefaultBufferType* defaultBufferType = (DefaultBufferType*)m_befaultBuffer->Map();
		defaultBufferType->mWorld = world.Transposed();
		defaultBufferType->mWorldView = worldView.Transposed();
		defaultBufferType->mWorldViewProjection = worldViewProjection.Transposed();
		defaultBufferType->mLightViewProjection[0] = lightViewProjection1.Transposed();
		defaultBufferType->mLightViewProjection[1] = lightViewProjection2.Transposed();
		defaultBufferType->mLightViewProjection[2] = lightViewProjection3.Transposed();
		defaultBufferType->shadowSplits = Vector4(directionalLight->GetCascadeSplit(0), directionalLight->GetCascadeSplit(1), directionalLight->GetCascadeSplit(2), directionalLight->GetCascadeSplit(2));
		defaultBufferType->albedoColor = material->GetColorAlbedo();
		defaultBufferType->tilingUV = material->GetTilingUV();
		defaultBufferType->offsetUV = material->GetOffsetUV();
		defaultBufferType->viewport = GET_RESOLUTION;
		defaultBufferType->roughnessMultiplier = material->GetRoughnessMultiplier();
		defaultBufferType->metallicMultiplier = material->GetMetallicMultiplier();
		defaultBufferType->occlusionMultiplier = material->GetOcclusionMultiplier();
		defaultBufferType->normalMultiplier = material->GetNormalMultiplier();
		defaultBufferType->specularMultiplier = material->GetSpecularMultiplier();
		defaultBufferType->shadingMode = float(material->GetShadingMode());	
		defaultBufferType->receiveShadows = float(receiveShadows);
		defaultBufferType->shadowBias = directionalLight->GetBias();
		defaultBufferType->shadowMapResolution = directionalLight->GetShadowMapResolution();
		defaultBufferType->shadowMappingQuality = directionalLight->GetShadowTypeAsFloat();
		defaultBufferType->lightDir = directionalLight->GetDirection();
		defaultBufferType->nearPlane = camera->GetNearPlane();
		defaultBufferType->farPlane = camera->GetFarPlane();
		defaultBufferType->padding = Vector3::Zero;
		m_befaultBuffer->Unmap();
	}
	m_befaultBuffer->SetVS(0); // set buffer in the vertex shader
	m_befaultBuffer->SetPS(0); // set buffer in the pixel shader

	//= SET TEXTURES ========================================================================================
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(0, textureArray.size(), &textureArray.front());

	//= DRAW ===========================================================
	m_graphicsDevice->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}

string ShaderVariation::GetID() const
{
	return m_ID;
}

bool ShaderVariation::HasAlbedoTexture() const
{
	return m_hasAlbedoTexture;
}

bool ShaderVariation::HasRoughnessTexture() const
{
	return m_hasRoughnessTexture;
}

bool ShaderVariation::HasMetallicTexture() const
{
	return m_hasMetallicTexture;
}

bool ShaderVariation::HasNormalTexture() const
{
	return m_hasNormalTexture;
}

bool ShaderVariation::HasHeightTexture() const
{
	return m_hasHeightTexture;
}

bool ShaderVariation::HasOcclusionTexture() const
{
	return m_hasOcclusionTexture;
}

bool ShaderVariation::HasEmissionTexture() const
{
	return m_hasEmissionTexture;
}

bool ShaderVariation::HasMaskTexture() const
{
	return m_hasMaskTexture;
}

bool ShaderVariation::HasCubeMapTexture() const
{
	return m_hasCubeMap;
}
