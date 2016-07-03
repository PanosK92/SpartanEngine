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
#include "../../Core/Globals.h"
#include "../../Core/GUIDGenerator.h"
#include "../../IO/Log.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

ShaderVariation::ShaderVariation()
{
	m_D3D11Device = nullptr;
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
	DirectusSafeDelete(m_befaultBuffer);
	DirectusSafeDelete(m_D3D11Shader);
}

void ShaderVariation::Initialize(
	bool albedo,
	bool roughness,
	bool metallic,
	bool occlusion,
	bool normal,
	bool height,
	bool mask,
	bool cubemap,
	D3D11Device* d3d11device
	)
{
	// Save the properties of the material
	m_hasAlbedoTexture = albedo;
	m_hasRoughnessTexture = roughness;
	m_hasMetallicTexture = metallic;
	m_hasOcclusionTexture = occlusion;
	m_hasNormalTexture = normal;
	m_hasHeightTexture = height;
	m_hasMaskTexture = mask;
	m_hasCubeMap = cubemap;

	m_D3D11Device = d3d11device;
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
	m_D3D11Shader->AddDefine("OCCLUSION_MAP", m_hasOcclusionTexture);
	m_D3D11Shader->AddDefine("NORMAL_MAP", m_hasNormalTexture);
	m_D3D11Shader->AddDefine("HEIGHT_MAP", m_hasHeightTexture);
	m_D3D11Shader->AddDefine("MASK_MAP", m_hasMaskTexture);
	m_D3D11Shader->AddDefine("CUBE_MAP", m_hasCubeMap);
}

void ShaderVariation::Load()
{
	// load the vertex and the pixel shader
	m_D3D11Shader = new D3D11Shader();
	m_D3D11Shader->Initialize(m_D3D11Device);
	AddDefinesBasedOnMaterial();
	m_D3D11Shader->Load("Assets/Shaders/GBuffer.hlsl");
	m_D3D11Shader->SetInputLayout(PositionTextureNormalTangent);
	m_D3D11Shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_D3D11Shader->AddSampler(D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_COMPARISON_LESS_EQUAL);

	// material buffer
	m_befaultBuffer = new D3D11Buffer();
	m_befaultBuffer->Initialize(m_D3D11Device);
	m_befaultBuffer->CreateConstantBuffer(sizeof(DefaultBufferType));
}

void ShaderVariation::Render(int indexCount, Matrix mWorld, Matrix mView, Matrix mProjection, Light* directionalLight, Material* material)
{
	if (!m_D3D11Shader->IsCompiled())
	{
		LOG("Can't render using a shader variation that hasn't been loaded or failed to compile.", Log::Error);
		return;
	}

	Matrix world = mWorld;
	Matrix worldView = world * mView;
	Matrix worldViewProjection = worldView * mProjection;

	ID3D11ShaderResourceView* dirLightDepthTex = nullptr;
	Matrix viewProjectionDirectionaLight = Matrix::Identity();
	if (directionalLight)
	{
		dirLightDepthTex = directionalLight->GetDepthMap();
		directionalLight->GenerateOrthographicProjectionMatrix(100, 100, 0.3f, 1000.0f);
		directionalLight->GenerateViewMatrix();
		viewProjectionDirectionaLight = directionalLight->GetViewMatrix() * directionalLight->GetOrthographicProjectionMatrix();
	}

	/*------------------------------------------------------------------------------
							[FILL THE BUFFER]
	------------------------------------------------------------------------------*/
	{ // this can be done only when needed - tested
		// map the buffer
		DefaultBufferType* defaultBufferType = (DefaultBufferType*)m_befaultBuffer->Map();
		defaultBufferType->world = world.Transpose();
		defaultBufferType->worldView = worldView.Transpose();
		defaultBufferType->worldViewProjection = worldViewProjection.Transpose();
		defaultBufferType->viewProjectionDirLight = viewProjectionDirectionaLight.Transpose();
		defaultBufferType->materialAlbedoColor = material->GetColorAlbedo();
		defaultBufferType->roughness = material->GetRoughness();
		defaultBufferType->metallic = material->GetMetallic();
		defaultBufferType->occlusion = material->GetOcclusion();
		defaultBufferType->normalStrength = material->GetNormalStrength();
		defaultBufferType->reflectivity = material->GetReflectivity();
		defaultBufferType->shadingMode = float(material->GetShadingMode());
		defaultBufferType->materialTiling = material->GetTiling();
		defaultBufferType->bias = directionalLight->GetBias();
		defaultBufferType->lightDirection = directionalLight->GetDirection();
		m_befaultBuffer->Unmap();
	}
	m_befaultBuffer->SetVS(0); // set buffer in the vertex shader
	m_befaultBuffer->SetPS(0); // set buffer in the pixel shader
	/*------------------------------------------------------------------------------
								[TEXTURES]
	------------------------------------------------------------------------------*/
	ID3D11ShaderResourceView* albedoTexture = material->GetShaderResourceViewByTextureType(Albedo);
	ID3D11ShaderResourceView* roughnessTexture = material->GetShaderResourceViewByTextureType(Roughness);
	ID3D11ShaderResourceView* metallicTexture = material->GetShaderResourceViewByTextureType(Metallic);
	ID3D11ShaderResourceView* occlusionTexture = material->GetShaderResourceViewByTextureType(Occlusion);
	ID3D11ShaderResourceView* normalTexture = material->GetShaderResourceViewByTextureType(Normal);
	ID3D11ShaderResourceView* heightTexture = material->GetShaderResourceViewByTextureType(Height);
	ID3D11ShaderResourceView* maskTexture = material->GetShaderResourceViewByTextureType(Mask);

	// Set textures
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(0, 1, &albedoTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(1, 1, &roughnessTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(2, 1, &metallicTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(3, 1, &occlusionTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(4, 1, &normalTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(5, 1, &heightTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(6, 1, &maskTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(7, 1, &dirLightDepthTex);

	// Draw
	m_D3D11Device->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}

string ShaderVariation::GetID()
{
	return m_ID;
}

bool ShaderVariation::HasAlbedoTexture()
{
	return m_hasAlbedoTexture;
}

bool ShaderVariation::HasRoughnessTexture()
{
	return m_hasRoughnessTexture;
}

bool ShaderVariation::HasMetallicTexture()
{
	return m_hasMetallicTexture;
}

bool ShaderVariation::HasOcclusionTexture()
{
	return m_hasOcclusionTexture;
}

bool ShaderVariation::HasNormalTexture()
{
	return m_hasNormalTexture;
}

bool ShaderVariation::HasHeightTexture()
{
	return m_hasHeightTexture;
}

bool ShaderVariation::HasMaskTexture()
{
	return m_hasMaskTexture;
}

bool ShaderVariation::HasCubeMapTexture()
{
	return m_hasCubeMap;
}
