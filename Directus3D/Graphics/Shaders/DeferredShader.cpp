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
#include "../../Core/Settings.h"
//=====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

DeferredShader::DeferredShader()
{
	m_graphicsDevice = nullptr;
	m_constantBuffer = nullptr;
	m_shader = nullptr;
}

DeferredShader::~DeferredShader()
{
	SafeDelete(m_constantBuffer);
	SafeDelete(m_shader);
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

	//= CREATE DEFAULT CONSTANT BUFFER ===========================
	m_constantBuffer = new D3D11Buffer();
	m_constantBuffer->Initialize(m_graphicsDevice);
	m_constantBuffer->CreateConstantBuffer(sizeof(DefaultBuffer));
	//============================================================
}

void DeferredShader::Render(
	int indexCount, const Matrix& mWorld, const Matrix& mView, const Matrix& mBaseView,
	const Matrix& mPerspectiveProjection, const Matrix& mOrthographicProjection,
	vector<GameObject*> directionalLights, vector<GameObject*> pointLights, Camera* camera,
	vector<ID3D11ShaderResourceView*> textures, ID3D11ShaderResourceView* environmentTex, ID3D11ShaderResourceView* irradienceTex)
{
	if (!m_shader->IsCompiled())
	{
		LOG_ERROR("Failed to compile deferred shader.");
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
	DefaultBuffer* buffer = (DefaultBuffer*)m_constantBuffer->Map();

	// Fill with matrices
	buffer->worldViewProjection = worlBaseViewProjection.Transpose();
	buffer->viewProjectionInverse = viewProjection.Inverse().Transpose();
	buffer->cameraPosition = Vector4(camPos.x, camPos.y, camPos.z, 1.0f);

	// Fill with directional lights
	for (unsigned int i = 0; i < directionalLights.size(); i++)
	{
		Light* light = directionalLights[i]->GetComponent<Light>();
		Vector3 direction = light->GetDirection();
		buffer->dirLightColor[i] = light->GetColor();
		buffer->dirLightDirection[i] = Vector4(direction.x, direction.y, direction.z, 1.0f);
		buffer->dirLightIntensity[i] = Vector4(light->GetIntensity());
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

	// Fill with misc data
	buffer->dirLightCount = directionalLights.size();
	buffer->pointLightCount = pointLights.size();
	buffer->nearPlane = camera->GetNearPlane();
	buffer->farPlane = camera->GetFarPlane();
	buffer->viewport = GET_RESOLUTION;
	buffer->padding = GET_RESOLUTION;

	// Unlock the constant buffer
	m_constantBuffer->Unmap();
	m_constantBuffer->SetVS(0);
	m_constantBuffer->SetPS(0);

	//= SET TEXTURES =======================================================================================
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(0, UINT(textures.size()), &textures.front());
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(UINT(textures.size()), 1, &environmentTex);
	m_graphicsDevice->GetDeviceContext()->PSSetShaderResources(UINT(textures.size()) + 1, 1, &irradienceTex);
	//======================================================================================================

	//= SET SHADER ===============================================================
	m_shader->Set();

	//= DRAW =====================================================================
	m_graphicsDevice->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
}

bool DeferredShader::IsCompiled()
{
	return m_shader->IsCompiled();
}
