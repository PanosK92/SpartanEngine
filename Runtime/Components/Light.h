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

#pragma once

//= INCLUDES ====================================
#include <vector>
#include "../Components/IComponent.h"
#include "../Math/Vector4.h"
#include "../Math/Vector3.h"
#include "../Math/Matrix.h"
#include "../Core/Settings.h"
#include <memory>
#include "../Graphics/D3D11/D3D11RenderTexture.h"
//===============================================

namespace Directus
{
	enum LightType
	{
		Directional,
		Point
	};

	enum ShadowType
	{
		No_Shadows,
		Hard_Shadows,
		Soft_Shadows
	};

	class Cascade
	{
	public:
		Cascade(int cascade, int resolution, Graphics* device)
		{
			m_cascade = cascade;
			m_depthMap = std::make_unique<D3D11RenderTexture>(device);
			m_depthMap->Create(resolution, resolution);
		}
		~Cascade() {}

		void SetAsRenderTarget()
		{
			m_depthMap->Clear(0.0f, 0.0f, 0.0f, 1.0f);
			m_depthMap->SetAsRenderTarget();
		}
		ID3D11ShaderResourceView* GetShaderResourceView() { return m_depthMap ? m_depthMap->GetShaderResourceView() : nullptr; }

		Math::Matrix CalculateProjectionMatrix(const Math::Vector3 centerPos, const Math::Matrix& viewMatrix)
		{
			float radius = GetRadius();
			Math::Vector3 center = centerPos * viewMatrix;
			Math::Vector3 min = center - Math::Vector3(radius, radius, radius);
			Math::Vector3 max = center + Math::Vector3(radius, radius, radius);

			return Math::Matrix::CreateOrthoOffCenterLH(min.x, max.x, min.y, max.y, -max.z, -min.z);
		}

		float GetSplit()
		{
			float split = 0;

			if (m_cascade == 1)
				split = 990;

			if (m_cascade == 2)
				split = 998;

			return split / 1000.0f;
		}

		float GetRadius()
		{
			float cameraFar = 1024;
			float radius = cameraFar * 0.5f;

			if (m_cascade == 1)
				radius = 40;

			else if (m_cascade == 2)
				radius = 75;

			if (m_cascade == 3)
				radius = 200;

			return radius;
		}

	private:
		int m_cascade;
		std::unique_ptr<D3D11RenderTexture> m_depthMap;
	};

	class DllExport Light : public IComponent
	{
	public:
		Light();
		~Light();

		virtual void Reset();
		virtual void Start();
		virtual void OnDisable();
		virtual void Remove();
		virtual void Update();
		virtual void Serialize();
		virtual void Deserialize();

		LightType GetLightType() { return m_lightType; }
		void SetLightType(LightType type);

		void SetColor(float r, float g, float b, float a) { m_color = Math::Vector4(r, g, b, a); }
		void SetColor(Math::Vector4 color) { m_color = color; }
		Math::Vector4 GetColor() { return m_color; }

		void SetIntensity(float value) { m_intensity = value; }
		float GetIntensity() { return m_intensity; }

		ShadowType GetShadowType() { return m_shadowType; }
		void SetShadowType(ShadowType shadowType) { m_shadowType = shadowType; }
		float GetShadowTypeAsFloat() const;

		void SetRange(float value) { m_range = value; }
		float GetRange() { return m_range; }

		void SetBias(float value) { m_bias = value; }
		float GetBias() { return m_bias; }

		Math::Vector3 GetDirection();

		Math::Matrix CalculateViewMatrix();
		Math::Matrix CalculateOrthographicProjectionMatrix(int cascade);

		// Cascaded shadow mapping
		void SetShadowCascadeAsRenderTarget(int cascade);
		std::weak_ptr<Cascade> GetShadowCascade(int cascade);
		int GetShadowCascadeResolution() { return SHADOWMAP_RESOLUTION; }
		int GetShadowCascadeCount() { return m_cascades; }
		float GetShadowCascadeSplit(int cascade);

	private:
		LightType m_lightType;
		ShadowType m_shadowType;
		Math::Vector4 m_color;
		float m_range;
		float m_intensity;
		float m_bias;

		Math::Matrix m_viewMatrix;

		int m_cascades;
		std::vector<std::shared_ptr<Cascade>> m_shadowMaps;
	};
}