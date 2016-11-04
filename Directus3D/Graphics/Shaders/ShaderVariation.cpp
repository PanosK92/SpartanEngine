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
	m_matrixBuffer = nullptr;
	m_objectBuffer = nullptr;

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
	Graphics* graphicsDevice
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

void ShaderVariation::Set()
{
	if (m_D3D11Shader)
		m_D3D11Shader->Set();
}

void ShaderVariation::UpdateMatrixBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mProjection)
{
	if (!m_D3D11Shader->IsCompiled())
	{
		LOG_ERROR("Can't render using a shader variation that hasn't been loaded or failed to compile.");
		return;
	}

	Matrix world = mWorld;
	Matrix worldView = world * mView;
	Matrix worldViewProjection = worldView * mProjection;

	// Map/Unmap the buffer
	MatrixBufferType* buffer = (MatrixBufferType*)m_matrixBuffer->Map();
	buffer->mWorld = world.Transposed();
	buffer->mWorldView = worldView.Transposed();
	buffer->mWorldViewProjection = worldViewProjection.Transposed();
	m_matrixBuffer->Unmap();

	// Set to shader slot
	m_matrixBuffer->SetVS(0);
	m_matrixBuffer->SetPS(0);
}

void ShaderVariation::UpdateObjectBuffer(shared_ptr<Material> material, Light* directionalLight, bool receiveShadows, Camera* camera)
{
	if (!m_D3D11Shader->IsCompiled())
	{
		LOG_ERROR("Can't render using a shader variation that hasn't been loaded or failed to compile.");
		return;
	}

	if (!directionalLight || !camera)
		return;

	Matrix lightViewProjection1 = directionalLight->GetViewMatrix() * directionalLight->GetOrthographicProjectionMatrix(0);
	Matrix lightViewProjection2 = directionalLight->GetViewMatrix() * directionalLight->GetOrthographicProjectionMatrix(1);
	Matrix lightViewProjection3 = directionalLight->GetViewMatrix() * directionalLight->GetOrthographicProjectionMatrix(2);

	/*------------------------------------------------------------------------------
	[FILL THE BUFFER]
	------------------------------------------------------------------------------*/
	{ // this can be done only when needed - tested
		ObjectBufferType* defaultBufferType = (ObjectBufferType*)m_objectBuffer->Map();

		// Material
		defaultBufferType->matAlbedo = material->GetColorAlbedo();
		defaultBufferType->matTilingUV = material->GetTilingUV();
		defaultBufferType->matOffsetUV = material->GetOffsetUV();
		defaultBufferType->matRoughnessMul = material->GetRoughnessMultiplier();
		defaultBufferType->matMetallicMul = material->GetMetallicMultiplier();
		defaultBufferType->matOcclusionMul = material->GetOcclusionMultiplier();
		defaultBufferType->matNormalMul = material->GetNormalMultiplier();
		defaultBufferType->matSpecularMul = material->GetSpecularMultiplier();
		defaultBufferType->matShadingMode = float(material->GetShadingMode());
		defaultBufferType->padding = Vector2::Zero;

		// Misc
		defaultBufferType->viewport = GET_RESOLUTION;
		defaultBufferType->nearPlane = camera->GetNearPlane();
		defaultBufferType->farPlane = camera->GetFarPlane();
		defaultBufferType->mLightViewProjection[0] = lightViewProjection1.Transposed();
		defaultBufferType->mLightViewProjection[1] = lightViewProjection2.Transposed();
		defaultBufferType->mLightViewProjection[2] = lightViewProjection3.Transposed();
		defaultBufferType->shadowSplits = Vector4(directionalLight->GetCascadeSplit(0), directionalLight->GetCascadeSplit(1), directionalLight->GetCascadeSplit(2), directionalLight->GetCascadeSplit(2));
		defaultBufferType->lightDir = directionalLight->GetDirection();
		defaultBufferType->shadowBias = directionalLight->GetBias();
		defaultBufferType->shadowMapResolution = directionalLight->GetShadowMapResolution();
		defaultBufferType->shadowMappingQuality = directionalLight->GetShadowTypeAsFloat();
		defaultBufferType->receiveShadows = float(receiveShadows);
		defaultBufferType->padding2 = 0.0f;

		m_objectBuffer->Unmap();
	}

	// Set to shader slot
	m_objectBuffer->SetVS(1);
	m_objectBuffer->SetPS(1);
}

void ShaderVariation::UpdateTextures(const vector<ID3D11ShaderResourceView*>& textureArray)
{
	if (m_graphics)
		m_graphics->GetDeviceContext()->PSSetShaderResources(0, textureArray.size(), &textureArray.front());
}

void ShaderVariation::Render(int indexCount)
{
	if (m_graphics)
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
	m_D3D11Shader = make_shared<D3D11Shader>();
	m_D3D11Shader->Initialize(m_graphics);
	AddDefinesBasedOnMaterial(m_D3D11Shader);
	m_D3D11Shader->Load("Assets/Shaders/GBuffer.hlsl");
	m_D3D11Shader->SetInputLayout(PositionTextureNormalTangent);
	m_D3D11Shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS); // anisotropic

	// Matrix Buffer
	m_matrixBuffer = make_shared<D3D11Buffer>();
	m_matrixBuffer->Initialize(m_graphics);
	m_matrixBuffer->CreateConstantBuffer(sizeof(MatrixBufferType));

	// Object Buffer
	m_objectBuffer = make_shared<D3D11Buffer>();
	m_objectBuffer->Initialize(m_graphics);
	m_objectBuffer->CreateConstantBuffer(sizeof(ObjectBufferType));
}