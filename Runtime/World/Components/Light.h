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

#pragma once

//= INCLUDES =========================
#include <vector>
#include <memory>
#include "IComponent.h"
#include "../../Math/Vector4.h"
#include "../../Math/Vector3.h"
#include "../../Math/Matrix.h"
#include "../../RHI/RHI_Definition.h"
//====================================

namespace Directus
{
	class Camera;
	class Renderable;
	class Renderer;

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
		Light(Context* context, Entity* entity, Transform* transform);
		~Light();

		//= COMPONENT ================================
		void OnInitialize() override;
		void OnStart() override;
		void OnTick() override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		LightType GetLightType() { return m_lightType; }
		void SetLightType(LightType type);

		void SetColor(float r, float g, float b, float a)	{ m_color = Math::Vector4(r, g, b, a); }
		void SetColor(Math::Vector4 color)					{ m_color = color; }
		Math::Vector4 GetColor()							{ return m_color; }

		void SetIntensity(float value) { m_intensity = value; }
		float GetIntensity() { return m_intensity; }

		bool GetCastShadows() { return m_castShadows; }
		void SetCastShadows(bool castShadows);

		void SetRange(float range);
		float GetRange() { return m_range; }

		void SetAngle(float angle);
		float GetAngle() { return m_angle; }

		void SetBias(float value)	{ m_bias = value; }
		float GetBias()				{ return m_bias; }

		void SetNormalBias(float value) { m_normalBias = value; }
		float GetNormalBias()			{ return m_normalBias; }

		Math::Vector3 GetDirection();
		void ClampRotation();

		Math::Matrix GetViewMatrix() { return m_viewMatrix; }

		// Shadow maps
		const Math::Matrix& ShadowMap_GetProjectionMatrix(unsigned int index = 0);	
		std::shared_ptr<RHI_RenderTexture> GetShadowMap() { return m_shadowMap; }

	private:
		void ComputeViewMatrix();
		bool ShadowMap_ComputeProjectionMatrix(unsigned int index = 0);	
		void ShadowMap_Create(bool force);

		LightType m_lightType	= LightType_Point;
		bool m_castShadows		= true;
		float m_range			= 1.0f;
		float m_intensity		= 2.0f;
		float m_angle			= 0.5f; // about 30 degrees
		float m_bias			= 0.0008f;
		float m_normalBias		= 120.0f;	
		bool m_isDirty			= true;
		Math::Vector4 m_color;
		Math::Matrix m_viewMatrix;
		Math::Quaternion m_lastRotLight;
		Math::Vector3 m_lastPosLight;
		Math::Vector3 m_lastPosCamera;
		
		// Shadow map
		std::shared_ptr<RHI_RenderTexture> m_shadowMap;
		std::vector<Math::Matrix> m_shadowMapsProjectionMatrix;
		Renderer* m_renderer;
	};
}