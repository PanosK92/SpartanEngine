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

//= INCLUDES ==========================
#include "DeferredShader.h"
#include "../../Logging/Log.h"
#include "../../Components/Transform.h"
#include "../../Components/Light.h"
#include "../../Core/Settings.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

DeferredShader::DeferredShader()
{
	m_graphics = nullptr;
	m_matrixBuffer = nullptr;
	m_miscBuffer = nullptr;
	m_shader = nullptr;
}

DeferredShader::~DeferredShader()
{

}

void DeferredShader::Load(const std::string& filePath, Graphics* graphics)
{
	m_graphics = graphics;

	// load the vertex and the pixel shader
	m_shader = make_shared<D3D11Shader>(m_graphics);
	m_shader->Load(filePath);
	m_shader->SetInputLayout(PositionTextureNormalTangent);
	m_shader->AddSampler(D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);
	m_shader->AddSampler(D3D11_FILTER_ANISOTROPIC, D3D11_TEXTURE_ADDRESS_WRAP, D3D11_COMPARISON_ALWAYS);

	// Create matrix buffer
	m_matrixBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
	m_matrixBuffer->Create(sizeof(MatrixBufferType));

	// Create misc buffer
	m_miscBuffer = make_shared<D3D11ConstantBuffer>(m_graphics);
	m_miscBuffer->Create(sizeof(MiscBufferType));
}

void DeferredShader::UpdateMatrixBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mBaseView, const Matrix& mPerspectiveProjection, const Matrix& mOrthographicProjection)
{
	if (!IsCompiled())
	{
		LOG_ERROR("Failed to compile deferred shader.");
		return;
	}

	// Get some stuff
	Matrix worlBaseViewProjection = mWorld * mBaseView * mOrthographicProjection;
	Matrix viewProjection = mView * mPerspectiveProjection;

	// Map/Unmap buffer
	MatrixBufferType* buffer = (MatrixBufferType*)m_matrixBuffer->Map();

	buffer->worldViewProjection = worlBaseViewProjection;
	buffer->viewProjectionInverse = viewProjection.Inverted();
	buffer->mView = mView;

	m_matrixBuffer->Unmap();

	// Set to shader slot
	m_matrixBuffer->SetVS(0);
	m_matrixBuffer->SetPS(0);
}

void DeferredShader::UpdateMiscBuffer(Light* directionalLight, vector<GameObject*> pointLights, Camera* camera)
{
	if (!IsCompiled())
	{
		LOG_ERROR("Failed to compile deferred shader.");
		return;
	}

	// Get a pointer to the data in the constant buffer.
	MiscBufferType* buffer = (MiscBufferType*)m_miscBuffer->Map();

	Vector3 camPos = camera->g_transform->GetPosition();
	buffer->cameraPosition = Vector4(camPos.x, camPos.y, camPos.z, 1.0f);

	// Fill with directional lights
	if (directionalLight)
	{
		Vector3 direction = directionalLight->GetDirection();
		buffer->dirLightColor = directionalLight->GetColor();
		buffer->dirLightDirection = Vector4(direction.x, direction.y, direction.z, 1.0f);
		buffer->dirLightIntensity = Vector4(directionalLight->GetIntensity());
		buffer->softShadows = directionalLight->GetShadowType() == Soft_Shadows ? (float)true : (float)false;
	}

	// Fill with point lights
	for (unsigned int i = 0; i < pointLights.size(); i++)
	{
		Vector3 pos = pointLights[i]->GetTransform()->GetPosition();
		buffer->pointLightPosition[i] = Vector4(pos.x, pos.y, pos.z, 1.0f);
		buffer->pointLightColor[i] = pointLights[i]->GetComponent<Light>()->GetColor();
		buffer->pointLightIntensity[i] = Vector4(pointLights[i]->GetComponent<Light>()->GetIntensity());
		buffer->pointLightRange[i] = Vector4(pointLights[i]->GetComponent<Light>()->GetRange());
	}

	buffer->pointLightCount = (float)pointLights.size();
	buffer->nearPlane = camera->GetNearPlane();
	buffer->farPlane = camera->GetFarPlane();
	buffer->viewport = GET_RESOLUTION;
	buffer->padding = Vector2::Zero;

	// Unmap buffer
	m_miscBuffer->Unmap();

	// Set to shader slot
	m_miscBuffer->SetVS(1);
	m_miscBuffer->SetPS(1);
}

void DeferredShader::UpdateTextures(vector<ID3D11ShaderResourceView*> textures)
{
	m_graphics->GetDeviceContext()->PSSetShaderResources(0, UINT(textures.size()), &textures.front());
}

void DeferredShader::Set()
{
	if (m_shader)
		m_shader->Set();
}

void DeferredShader::Render(int indexCount)
{
	if (m_shader)
		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}

bool DeferredShader::IsCompiled()
{
	return m_shader ? m_shader->IsCompiled() : false;
}
