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
#include "LightShader.h"
#include "../../Logging/Log.h"
#include "../../Scene/Components/Transform.h"
#include "../../Scene/Components/Light.h"
#include "../../Core/Settings.h"
#include "../../RHI/IRHI_Implementation.h"
#include "../../RHI/D3D11/D3D11_Shader.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	LightShader::LightShader()
	{
		m_rhiDevice = nullptr;
	}

	LightShader::~LightShader()
	{

	}

	void LightShader::Compile(const string& filePath, RHI_Device* rhiDevice)
	{
		m_rhiDevice = rhiDevice;

		// load the vertex and the pixel shader
		m_shader = make_shared<D3D11_Shader>(m_rhiDevice);
		m_shader->Compile(filePath);
		m_shader->SetInputLayout(Input_PositionTextureTBN);

		// Create matrix buffer
		m_matrixBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_matrixBuffer->Create(sizeof(MatrixBufferType));

		// Create misc buffer
		m_miscBuffer = make_shared<RHI_ConstantBuffer>(m_rhiDevice);
		m_miscBuffer->Create(sizeof(MiscBufferType));
	}

	void LightShader::UpdateMatrixBuffer(const Matrix& mWorld, const Matrix& mView, const Matrix& mBaseView, const Matrix& mPerspectiveProjection, const Matrix& mOrthographicProjection)
	{
		if (!IsCompiled())
		{
			LOG_ERROR("Failed to compile deferred shader.");
			return;
		}

		// Get some stuff
		Matrix worlBaseViewProjection	= mWorld * mBaseView * mOrthographicProjection;
		Matrix viewProjection			= mView * mPerspectiveProjection;

		// Map/Unmap buffer
		auto buffer = (MatrixBufferType*)m_matrixBuffer->Map();

		buffer->worldViewProjection		= worlBaseViewProjection;
		buffer->mProjection				= mPerspectiveProjection;
		buffer->mProjectionInverse		= mPerspectiveProjection.Inverted();
		buffer->mViewProjection			= viewProjection;
		buffer->mViewProjectionInverse	= viewProjection.Inverted();
		buffer->mView = mView;

		m_matrixBuffer->Unmap();

		// Set to shader slot

		m_matrixBuffer->Bind(BufferScope_Global, 0);
	}

	void LightShader::UpdateMiscBuffer(const vector<Light*>& lights, Camera* camera)
	{
		if (!IsCompiled())
		{
			LOG_ERROR("Failed to compile deferred shader.");
			return;
		}

		if (!camera || lights.empty())
			return;

		// Get a pointer to the data in the constant buffer.
		auto buffer = (MiscBufferType*)m_miscBuffer->Map();

		Vector3 camPos = camera->GetTransform()->GetPosition();
		buffer->cameraPosition = Vector4(camPos.x, camPos.y, camPos.z, 1.0f);

		// Reset any light buffer values because the shader will still use them
		buffer->dirLightColor = Vector4::Zero;
		buffer->dirLightDirection = Vector4::Zero;
		buffer->dirLightIntensity = Vector4::Zero;
		for (int i = 0; i < maxLights; i++)
		{
			buffer->pointLightPosition[i]		= Vector4::Zero;
			buffer->pointLightColor[i]			= Vector4::Zero;
			buffer->pointLightIntenRange[i]		= Vector4::Zero;
			buffer->spotLightPosition[i]		= Vector4::Zero;
			buffer->spotLightColor[i]			= Vector4::Zero;
			buffer->spotLightIntenRangeAngle[i] = Vector4::Zero;
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
		buffer->viewport = Settings::Get().GetResolution();
		buffer->padding = Vector2::Zero;

		// Unmap buffer
		m_miscBuffer->Unmap();

		// Set to shader slot
		m_miscBuffer->Bind(BufferScope_Global, 1);
	}

	bool LightShader::IsCompiled()
	{
		return m_shader ? m_shader->IsCompiled() : false;
	}
}
