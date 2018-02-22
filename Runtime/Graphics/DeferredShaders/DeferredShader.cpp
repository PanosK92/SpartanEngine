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
#include "DeferredShader.h"
#include "../../Logging/Log.h"
#include "../../Scene/Components/Transform.h"
#include "../../Scene/Components/Light.h"
#include "../../Core/Settings.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	DeferredShader::DeferredShader()
	{
		m_graphics = nullptr;
	}

	DeferredShader::~DeferredShader()
	{

	}

	void DeferredShader::Load(const string& filePath, Graphics* graphics)
	{
		m_graphics = graphics;

		// load the vertex and the pixel shader
		m_shader = make_shared<D3D11Shader>(m_graphics);
		m_shader->Compile(filePath);
		m_shader->SetInputLayout(PositionTextureTBN);
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
		buffer->mProjection = mPerspectiveProjection;
		buffer->mProjectionInverse = mPerspectiveProjection.Inverted();
		buffer->mViewProjection = viewProjection;
		buffer->mViewProjectionInverse = viewProjection.Inverted();
		buffer->mView = mView;

		m_matrixBuffer->Unmap();

		// Set to shader slot
		m_matrixBuffer->SetVS(0);
		m_matrixBuffer->SetPS(0);
	}

	void DeferredShader::UpdateMiscBuffer(const vector<Light*>& lights, Camera* camera)
	{
		if (!IsCompiled())
		{
			LOG_ERROR("Failed to compile deferred shader.");
			return;
		}

		if (!camera || lights.empty())
			return;

		// Get a pointer to the data in the constant buffer.
		MiscBufferType* buffer = (MiscBufferType*)m_miscBuffer->Map();

		Vector3 camPos = camera->GetTransform()->GetPosition();
		buffer->cameraPosition = Vector4(camPos.x, camPos.y, camPos.z, 1.0f);

		// Reset any light buffer values because the shader will still use them
		for (int i = 0; i < maxLights; i++)
		{
			buffer->dirLightColor = Vector4::Zero;
			buffer->dirLightDirection = Vector4::Zero;
			buffer->dirLightIntensity = Vector4::Zero;
			buffer->pointLightPosition[maxLights] = Vector4::Zero;
			buffer->pointLightColor[maxLights] = Vector4::Zero;
			buffer->pointLightIntenRange[maxLights] = Vector4::Zero;
			buffer->spotLightPosition[maxLights] = Vector4::Zero;
			buffer->spotLightColor[maxLights] = Vector4::Zero;
			buffer->spotLightIntenRangeAngle[maxLights] = Vector4::Zero;
		}

		// Fill with directional lights
		for (const auto& light : lights)
		{
			if (light->GetLightType() != LightType_Directional)
				continue;

			Vector3 direction = light->GetDirection();

			buffer->dirLightColor = light->GetColor();	
			buffer->dirLightIntensity = Vector4(light->GetIntensity());
			buffer->dirLightDirection = Vector4(direction.x, direction.y, direction.z, 0.0f);
		}

		// Fill with point lights
		int pointIndex = 0;
		for (const auto& light : lights)
		{
			if (light->GetLightType() != LightType_Point)
				continue;

			Vector3 pos = light->GetTransform()->GetPosition();
			buffer->pointLightPosition[pointIndex] = Vector4(pos.x, pos.y, pos.z, 1.0f);
			buffer->pointLightColor[pointIndex] = light->GetColor();
			buffer->pointLightIntenRange[pointIndex] = Vector4(light->GetIntensity(), light->GetRange(), 0.0f, 0.0f);

			pointIndex++;
		}

		// Fill with spot lights
		int spotIndex = 0;
		for (const auto& light : lights)
		{
			if (light->GetLightType() != LightType_Spot)
				continue;

			Vector3 direction = light->GetDirection();
			Vector3 pos = light->GetTransform()->GetPosition();

			buffer->spotLightColor[spotIndex] = light->GetColor();
			buffer->spotLightPosition[spotIndex] = Vector4(pos.x, pos.y, pos.z, 1.0f);
			buffer->spotLightDirection[spotIndex] = Vector4(direction.x, direction.y, direction.z, 0.0f);
			buffer->spotLightIntenRangeAngle[spotIndex] = Vector4(light->GetIntensity(), light->GetRange(), light->GetAngle(), 0.0f);

			spotIndex++;
		}

		buffer->pointLightCount = (float)pointIndex;
		buffer->spotLightCount = (float)spotIndex;
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

	void DeferredShader::UpdateTextures(vector<void*> textures)
	{
		if (!m_graphics)
			return;

		ID3D11ShaderResourceView** ptr = (ID3D11ShaderResourceView**)textures.data();
		int length = (int)textures.size();
		auto tex = vector<ID3D11ShaderResourceView*>(ptr, ptr + length);

		m_graphics->GetDeviceContext()->PSSetShaderResources(0, unsigned int(textures.size()), &tex.front());
	}

	void DeferredShader::Set()
	{
		if (!m_shader)
		{
			LOG_INFO("Unintialized shader, can't set");
			return;
		}

		m_shader->Set();
	}

	void DeferredShader::Render(int indexCount)
	{
		if (!m_shader)
		{
			LOG_INFO("Unintialized shader, can't render");
			return;
		}

		m_graphics->GetDeviceContext()->DrawIndexed(indexCount, 0, 0);
	}

	bool DeferredShader::IsCompiled()
	{
		return m_shader ? m_shader->IsCompiled() : false;
	}
}
