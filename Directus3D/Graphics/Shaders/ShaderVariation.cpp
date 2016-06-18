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
#include "../../Misc/Globals.h"
#include "../../Misc/GUIDGenerator.h"
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

void ShaderVariation::Initialize(Material* material, D3D11Device* d3d11device)
{
	m_D3D11Device = d3d11device;
	m_ID = GENERATE_GUID; // generate an ID for this shader
	Load(material); // load the shader
}

void ShaderVariation::Set()
{
	m_D3D11Shader->Set();
}

void ShaderVariation::AddDefinesBasedOnMaterial(Material* material)
{
	// Save the properties of the material
	m_hasAlbedoTexture = material->HasTextureOfType(Albedo);
	m_hasRoughnessTexture = material->HasTextureOfType(Roughness);
	m_hasMetallicTexture = material->HasTextureOfType(Metallic);
	m_hasOcclusionTexture = material->HasTextureOfType(Occlusion);
	m_hasNormalTexture = material->HasTextureOfType(Normal);
	m_hasHeightTexture = material->HasTextureOfType(Height);
	m_hasMaskTexture = material->HasTextureOfType(Mask);
	m_hasCubeMap = material->HasTextureOfType(CubeMap);

	// Write the properties of the material as defines
	m_D3D11Shader->AddDefine("ALBEDO_MAP", material->HasTextureOfType(Albedo));
	m_D3D11Shader->AddDefine("ROUGHNESS_MAP", material->HasTextureOfType(Roughness));
	m_D3D11Shader->AddDefine("METALLIC_MAP", material->HasTextureOfType(Metallic));
	m_D3D11Shader->AddDefine("OCCLUSION_MAP", material->HasTextureOfType(Occlusion));
	m_D3D11Shader->AddDefine("NORMAL_MAP", material->HasTextureOfType(Normal));
	m_D3D11Shader->AddDefine("HEIGHT_MAP", material->HasTextureOfType(Height));
	m_D3D11Shader->AddDefine("MASK_MAP", material->HasTextureOfType(Mask));
	m_D3D11Shader->AddDefine("CUBE_MAP", material->HasTextureOfType(CubeMap));
}

void ShaderVariation::Load(Material* material)
{
	// load the vertex and the pixel shader
	m_D3D11Shader = new D3D11Shader();
	m_D3D11Shader->Initialize(m_D3D11Device);
	AddDefinesBasedOnMaterial(material);
	m_D3D11Shader->Load("Assets/Shaders/GBuffer.hlsl");
	m_D3D11Shader->SetInputLayout(PositionTextureNormalTangent);
	m_D3D11Shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_D3D11Shader->AddSampler(D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_MIRROR, D3D11_COMPARISON_LESS_EQUAL);

	// material buffer
	m_befaultBuffer = new D3D11Buffer();
	m_befaultBuffer->Initialize(sizeof(DefaultBufferType), m_D3D11Device);
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
	// map the buffer
	DefaultBufferType* defaultBufferType = (DefaultBufferType*)m_befaultBuffer->Map();

	// fill with data
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

	m_befaultBuffer->Unmap(); // unmap buffer
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

	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(0, 1, &albedoTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(1, 1, &roughnessTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(2, 1, &metallicTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(3, 1, &occlusionTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(4, 1, &normalTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(5, 1, &heightTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(6, 1, &maskTexture);
	m_D3D11Device->GetDeviceContext()->PSSetShaderResources(7, 1, &dirLightDepthTex);

	/*------------------------------------------------------------------------------
										[RENDER]
	------------------------------------------------------------------------------*/
	m_D3D11Device->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}

string ShaderVariation::GetID()
{
	return m_ID;
}

bool ShaderVariation::MatchesMaterial(Material* material)
{
	if (!material)
	{
		LOG("Can't compare with null material.", Warning);
		return false;
	}

	if (m_hasAlbedoTexture != material->HasTextureOfType(Albedo))
		return false;

	if (m_hasRoughnessTexture != material->HasTextureOfType(Roughness))
		return false;

	if (m_hasMetallicTexture != material->HasTextureOfType(Metallic))
		return false;

	if (m_hasOcclusionTexture != material->HasTextureOfType(Occlusion))
		return false;

	if (m_hasNormalTexture != material->HasTextureOfType(Normal))
		return false;

	if (m_hasHeightTexture != material->HasTextureOfType(Height))
		return false;

	if (m_hasMaskTexture != material->HasTextureOfType(Mask))
		return false;

	if (m_hasCubeMap != material->HasTextureOfType(CubeMap))
		return false;

	return true;
}
