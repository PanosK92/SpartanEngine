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

#pragma once

//= INCLUDES ======================
#include <vector>
#include <memory>
#include "IComponent.h"
#include "../../Math/Vector4.h"
#include "../../Math/Vector3.h"
#include "../../Math/Matrix.h"
#include "../../Core/Settings.h"
#include "../../Graphics/Cascade.h"
//=================================

namespace Directus
{
	class Camera;
	class Renderable;

	namespace Math
	{
		class Frustum;
	}

	enum LightType
	{
		LightType_Directional,
		LightType_Point,
		LightType_Spot
	};

	class ENGINE_CLASS Light : public IComponent
	{
	public:
		Light(Context* context, GameObject* gameObject, Transform* transform);
		~Light();

		//= COMPONENT ================================
		void OnInitialize() override;
		void OnStart() override;
		void OnUpdate() override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		LightType GetLightType() { return m_lightType; }
		void SetLightType(LightType type);

		void SetColor(float r, float g, float b, float a) { m_color = Math::Vector4(r, g, b, a); }
		void SetColor(Math::Vector4 color) { m_color = color; }
		Math::Vector4 GetColor() { return m_color; }

		void SetIntensity(float value) { m_intensity = value; }
		float GetIntensity() { return m_intensity; }

		bool GetCastShadows() { return m_castShadows; }
		void SetCastShadows(bool castShadows);

		void SetRange(float range);
		float GetRange() { return m_range; }

		void SetAngle(float angle);
		float GetAngle() { return m_angle; }

		void SetBias(float value) { m_bias = value; }
		float GetBias() { return m_bias; }

		Math::Vector3 GetDirection();
		void ClampRotation();

		Math::Matrix GetViewMatrix();
		Math::Matrix GetOrthographicProjectionMatrix(int cascadeIndex);

		// Cascaded shadow mapping
		void SetShadowCascadeAsRenderTarget(int cascade);
		std::weak_ptr<Cascade> GetShadowCascade(int cascadeIndex);
		int GetShadowCascadeResolution() { return SHADOWMAP_RESOLUTION; }
		int GetShadowCascadeCount() { return m_cascades; }
		float GetShadowCascadeSplit(int cascadeIndex);

		bool IsInViewFrustrum(Renderable* renderable);

	private:
		void EnableShadowMaps(bool enable);

		LightType m_lightType;
		bool m_castShadows;
		Math::Vector4 m_color;
		float m_range;
		float m_intensity;
		float m_angle;
		float m_bias;
		Math::Matrix m_viewMatrix;
		Math::Matrix m_projectionMatrix;
		std::shared_ptr<Math::Frustum> m_frustrum;
		int m_cascades;
		std::vector<std::shared_ptr<Cascade>> m_shadowMaps;
		Math::Quaternion m_lastRot;
		Math::Vector3 m_lastPos;
		bool m_isDirty;
	};
}