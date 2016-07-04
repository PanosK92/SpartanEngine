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

//= INCLUDES ==========================
#include "DeferredShader.h"
#include "../../Core/Globals.h"
#include "../../IO/Log.h"
#include "../../Components/Transform.h"
#include "../../Components/Light.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;

//=============================

DeferredShader::DeferredShader()
{
	m_graphicsDevice = nullptr;
	m_miscBuffer = nullptr;
	m_dirLightBuffer = nullptr;
	m_pointLightBuffer = nullptr;
	m_shader = nullptr;
}

DeferredShader::~DeferredShader()
{
	DirectusSafeDelete(m_miscBuffer);
	DirectusSafeDelete(m_dirLightBuffer);
	DirectusSafeDelete(m_pointLightBuffer);
	DirectusSafeDelete(m_shader);
}

void DeferredShader::Initialize(GraphicsDevice* graphicsDevice)
{
	m_graphicsDevice = graphicsDevice;

	// load the vertex and the pixel shader
	m_shader = new D3D11Shader();
	m_shader->Initialize(m_graphicsDevice);
	m_shader->Load("Assets/Shaders/Deferred.hlsl");
	m_shader->SetInputLayout(PositionTextureNormalTangent);
	m_shader->AddSampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);


	/*------------------------------------------------------------------------------
									[BUFFERS]
	------------------------------------------------------------------------------*/
	// misc buffer
	m_miscBuffer = new D3D11Buffer();
	m_miscBuffer->Initialize(m_graphicsDevice);
	m_miscBuffer->CreateConstantBuffer(sizeof(MiscBufferType));

	// dir light buffer
	m_dirLightBuffer = new D3D11Buffer();
	m_dirLightBuffer->Initialize(m_graphicsDevice);
	m_dirLightBuffer->CreateConstantBuffer(sizeof(DirLightBufferType));

	// point light buffer
	m_pointLightBuffer = new D3D11Buffer();
	m_pointLightBuffer->Initialize(m_graphicsDevice);
	m_pointLightBuffer->CreateConstantBuffer(sizeof(PointLightBufferType));
}

void DeferredShader::Render(int indexCount, Matrix mWorld, Matrix mView, Matrix mBaseView, Matrix mPerspectiveProjection, Matrix mOrthographicProjection,
                            vector<GameObject*> directionalLights, vector<GameObject*> pointLights,
                            Camera* camera, ID3D11ShaderResourceView* albedo, ID3D11ShaderResourceView* normal, ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* material,
                            ID3D11ShaderResourceView* environmentTexture, ID3D11ShaderResourceView* irradianceTexture, ID3D11ShaderResourceView* noiseTexture)
{
	if (!m_shader->IsCompiled())
	{
		LOG("Failed to compile deferred shader.", Log::Error);
		return;
	}

	/*------------------------------------------------------------------------------
								[MISC BUFFER]
	------------------------------------------------------------------------------*/
	// Get some stuff
	Matrix worlBaseViewProjection = mWorld * mBaseView * mOrthographicProjection;
	Matrix viewProjection = mView * mPerspectiveProjection;
	Vector3 camPos = camera->g_transform->GetPosition();

	// Get a pointer to the data in the constant buffer.
	MiscBufferType* buffer = (MiscBufferType*)m_miscBuffer->Map();

	// Fill buffer
	buffer->worldViewProjection = worlBaseViewProjection.Transpose();
	buffer->viewProjectionInverse = viewProjection.Inverse().Transpose();
	buffer->cameraPosition = Vector4(camPos.x, camPos.y, camPos.z, 1.0f);
	buffer->nearPlane = camera->GetNearPlane();
	buffer->farPlane = camera->GetFarPlane();
	buffer->padding = Vector2(0.0f, 0.0f);

	// Unlock the constant buffer
	m_miscBuffer->Unmap();
	m_miscBuffer->SetVS(0);
	m_miscBuffer->SetPS(0);

	/*------------------------------------------------------------------------------
							[DIRECTIONAL LIGHT BUFFER]
	------------------------------------------------------------------------------*/
	// Get a pointer to the data in the constant buffer.
	DirLightBufferType* dirLightBufferType = (DirLightBufferType*)m_dirLightBuffer->Map();

	// Fill buffer
	for (unsigned int i = 0; i < directionalLights.size(); i++)
	{
		Light* light = directionalLights[i]->GetComponent<Light>();

		Matrix lightView = light->GetViewMatrix();
		Matrix ligtProjection = light->GetOrthographicProjectionMatrix();

		dirLightBufferType->dirViewProjection[i] = Matrix::Transpose(lightView * ligtProjection);
		dirLightBufferType->dirLightColor[i] = light->GetColor();

		Vector3 direction = light->GetDirection();
		dirLightBufferType->dirLightDirection[i] = Vector4(direction.x, direction.y, direction.z, 1.0f);
		dirLightBufferType->dirLightIntensity[i] = Vector4(directionalLights[i]->GetComponent<Light>()->GetIntensity());
	}
	dirLightBufferType->dirLightCount = directionalLights.size();
	dirLightBufferType->padding = Vector3(0, 0, 0);

	// Unlock the constant buffer
	m_dirLightBuffer->Unmap();
	m_dirLightBuffer->SetPS(1);

	/*------------------------------------------------------------------------------
							[POINT LIGHT BUFFER]
	------------------------------------------------------------------------------*/
	// Get a pointer to the data in the constant buffer.
	PointLightBufferType* pointLightBufferType = static_cast<PointLightBufferType*>(m_pointLightBuffer->Map());

	// Fill buffer
	for (unsigned int i = 0; i < pointLights.size(); i++)
	{
		Vector3 pos = pointLights[i]->GetTransform()->GetPosition();
		pointLightBufferType->pointLightPosition[i] = Vector4(pos.x, pos.y, pos.z, 1.0f);
		pointLightBufferType->pointLightColor[i] = pointLights[i]->GetComponent<Light>()->GetColor();
		pointLightBufferType->pointLightIntensity[i] = Vector4(pointLights[i]->GetComponent<Light>()->GetIntensity());
		pointLightBufferType->pointLightRange[i] = Vector4(pointLights[i]->GetComponent<Light>()->GetRange());
	}
	pointLightBufferType->pointLightCount = pointLights.size();
	pointLightBufferType->padding = Vector3(0, 0, 0);

	// Unlock the constant buffer
	m_pointLightBuffer->Unmap();
	m_pointLightBuffer->SetPS(2);

	/*------------------------------------------------------------------------------
								[TEXTURES]
	------------------------------------------------------------------------------*/
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(0, 1, &albedo);
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(1, 1, &normal);
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(2, 1, &depth);
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(3, 1, &material);
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(4, 1, &noiseTexture);
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(5, 1, &environmentTexture);
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(6, 1, &irradianceTexture);

	/*------------------------------------------------------------------------------
									[RENDER]
	------------------------------------------------------------------------------*/
	m_shader->Set();
	// render
	m_graphicsDevice->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}

bool DeferredShader::IsCompiled()
{
	return m_shader->IsCompiled();
}
