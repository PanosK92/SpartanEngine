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

//= INCLUDES ======================
#include "Light.h"
#include "Transform.h"
#include "../Core/Settings.h"
#include "../IO/Serializer.h"
#include "../Core/Scene.h"
#include "../IO/Log.h"
#include "../Math/MathHelper.h"
#include "../Graphics/ShadowMap.h"
//================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

Light::Light()
{
	m_lightType = Point;
	m_shadowType = Hard_Shadows;
	m_range = 1.0f;
	m_intensity = 5.0f;
	m_color = Vector4(
		255.0f / 255.0f,
		244.0f / 255.0f,
		214.0f / 255.0f,
		1.0f
	);
	m_bias = 0.0003f;
	m_cascades = 3;
}

Light::~Light()
{

}

void Light::Initialize()
{
	float camNear = 0.1f;
	float camFar = 1000.0f;
	for (int i = 0; i < m_cascades; i++)
	{
		float n = i + 1;
		float farPlane = camNear * powf(camFar / camNear, n / m_cascades);
		float nearPlane = Clamp(farPlane - (camFar / m_cascades), camNear, camFar);
		m_shadowMaps.push_back(new ShadowMap(g_graphicsDevice, n, this, g_scene->GetMainCamera()->GetComponent<Camera>(), SHADOWMAP_RESOLUTION / n, nearPlane, farPlane));
	}
}

void Light::Start()
{

}

void Light::Remove()
{

}

void Light::Update()
{

}

void Light::Serialize()
{
	Serializer::WriteInt(int(m_lightType));
	Serializer::WriteInt(int(m_shadowType));
	Serializer::WriteVector4(m_color);
	Serializer::WriteFloat(m_range);
	Serializer::WriteFloat(m_intensity);
	Serializer::WriteFloat(m_bias);
}

void Light::Deserialize()
{
	m_lightType = LightType(Serializer::ReadInt());
	m_shadowType = ShadowType(Serializer::ReadInt());
	m_color = Serializer::ReadVector4();
	m_range = Serializer::ReadFloat();
	m_intensity = Serializer::ReadFloat();
	m_bias = Serializer::ReadFloat();
}

LightType Light::GetLightType()
{
	return m_lightType;
}

void Light::SetLightType(LightType type)
{
	m_lightType = type;
	g_scene->AnalyzeGameObjects();
}

Vector4 Light::GetColor()
{
	return m_color;
}

void Light::SetColor(float r, float g, float b, float a)
{
	m_color = Vector4(r, g, b, a);
}

void Light::SetColor(Vector4 color)
{
	m_color = color;
}

float Light::GetIntensity()
{
	return m_intensity;
}

ShadowType Light::GetShadowType()
{
	return m_shadowType;
}

void Light::SetShadowType(ShadowType shadowType)
{
	m_shadowType = shadowType;
}

float Light::GetShadowTypeAsFloat() const
{
	if (m_shadowType == Hard_Shadows)
		return 0.5f;

	if (m_shadowType == Soft_Shadows)
		return 1.0f;

	return 0.0f;
}

void Light::SetRange(float value)
{
	m_range = value;
}

float Light::GetRange()
{
	return m_range;
}

void Light::SetBias(float value)
{
	m_bias = value;
}

float Light::GetBias()
{
	return m_bias;
}

Vector3 Light::GetDirection()
{
	return g_transform->GetForward();
}

void Light::SetIntensity(float value)
{
	m_intensity = value;
}

Matrix Light::GetViewMatrix()
{
	GameObject* cameraGameObject = g_scene->GetMainCamera();
	if (!cameraGameObject)
		return Matrix::Identity;

	Transform* camera = cameraGameObject->GetTransform();
	Vector3 lightDirection = GetDirection();

	Vector3 position = camera->GetPosition(); //- lightDirection * m_shadowMaps[cascade]->GetFarPlane();
	Vector3 lookAt = camera->GetPosition() + lightDirection;
	Vector3 up = Vector3::Up;

	// Create the view matrix from the three vectors.
	m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);

	return m_viewMatrix;
}

Matrix Light::GetOrthographicProjectionMatrix(int cascade)
{
	if (m_shadowMaps.empty())
		return Matrix::Identity;

	return m_shadowMaps[cascade]->GetProjectionMatrix();
}

void Light::SetShadowMapAsRenderTarget(int cascade)
{
	if (m_shadowMaps.empty())
		return;

	m_shadowMaps[cascade]->SetAsRenderTarget();
}

ID3D11ShaderResourceView* Light::GetDepthMap(int cascade)
{
	if (m_shadowMaps.empty())
		return nullptr;

	return m_shadowMaps[cascade]->GetShaderResourceView();
}

float Light::GetShadowMapResolution()
{
	return SHADOWMAP_RESOLUTION;
}

int Light::GetCascadeCount()
{
	return m_cascades;
}

float Light::GetCascadeSplit(int cascade)
{
	return m_shadowMaps[cascade]->GetSplit();
}
