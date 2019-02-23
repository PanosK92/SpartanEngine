/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "../../World/Components/Transform.h"
#include "../../World/Entity.h"
#include "../../Core/Settings.h"
#include "../../RHI/RHI_Shader.h"
#include "../../RHI/RHI_ConstantBuffer.h"
//===========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	LightShader::LightShader(std::shared_ptr<RHI_Device> rhiDevice) : RHI_Shader(rhiDevice)
	{
		// Create constant buffer
		m_cbuffer = make_shared<RHI_ConstantBuffer>(rhiDevice, (unsigned int)sizeof(LightBuffer));
	}

	void LightShader::UpdateConstantBuffer(
		const Matrix& mViewProjection_Orthographic,
		const Matrix& mView,
		const Matrix& mProjection,
		const vector<Entity*>& lights,
		bool doSSR
	)
	{
		if (GetState() != Shader_Built)
			return;

		if (lights.empty())
			return;

		// Get a pointer to the data in the constant buffer.
		auto buffer = (LightBuffer*)m_cbuffer->Map();
		if (!buffer)
		{
			LOG_ERROR("LightShader::UpdateConstantBuffer: Failed to map buffer");
			return;
		}

		buffer->mvp						= mViewProjection_Orthographic;
		buffer->viewProjectionInverse	= (mView * mProjection).Inverted();

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
			auto component = light->GetComponent<Light>();

			if (component->GetLightType() != LightType_Directional)
				continue;

			Vector3 direction = component->GetDirection();

			buffer->dirLightColor = component->GetColor();
			buffer->dirLightIntensity = Vector4(component->GetIntensity());
			buffer->dirLightDirection = Vector4(direction.x, direction.y, direction.z, 0.0f);
		}

		// Fill with point lights
		int pointIndex = 0;
		for (const auto& light : lights)
		{
			auto component = light->GetComponent<Light>();

			if (component->GetLightType() != LightType_Point)
				continue;

			Vector3 pos = light->GetTransform_PtrRaw()->GetPosition();

			buffer->pointLightPosition[pointIndex]		= Vector4(pos.x, pos.y, pos.z, 1.0f);
			buffer->pointLightColor[pointIndex]			= component->GetColor();
			buffer->pointLightIntenRange[pointIndex]	= Vector4(component->GetIntensity(), component->GetRange(), 0.0f, 0.0f);

			pointIndex++;
		}

		// Fill with spot lights
		int spotIndex = 0;
		for (const auto& light : lights)
		{
			auto component = light->GetComponent<Light>();

			if (component->GetLightType() != LightType_Spot)
				continue;

			Vector3 direction	= component->GetDirection();
			Vector3 pos			= light->GetTransform_PtrRaw()->GetPosition();

			buffer->spotLightColor[spotIndex]			= component->GetColor();
			buffer->spotLightPosition[spotIndex]		= Vector4(pos.x, pos.y, pos.z, 1.0f);
			buffer->spotLightDirection[spotIndex]		= Vector4(direction.x, direction.y, direction.z, 0.0f);
			buffer->spotLightIntenRangeAngle[spotIndex] = Vector4(component->GetIntensity(), component->GetRange(), component->GetAngle(), 0.0f);

			spotIndex++;
		}

		buffer->pointLightCount = (float)pointIndex;
		buffer->spotLightCount	= (float)spotIndex;
		buffer->padding			= Vector2(doSSR ? 1.0f : 0.0f, 0.0f);

		// Unmap buffer
		m_cbuffer->Unmap();
	}
}
