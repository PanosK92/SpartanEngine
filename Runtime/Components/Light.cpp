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

//= INCLUDES =================
#include "Light.h"
#include "Transform.h"
#include "../Core/Settings.h"
#include "../IO/Serializer.h"
#include "../Core/Scene.h"
#include "Camera.h"
#include "Transform.h"
//===========================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

Light::Light()
{
	m_lightType = Point;
	m_shadowType = Hard_Shadows;
	m_range = 1.0f;
	m_intensity = 4.0f;
	m_color = Vector4(
		255.0f / 255.0f,
		196.0f / 255.0f,
		147.0f / 255.0f,
		1.0f
	);
	m_bias = 0.03f;
	m_cascades = 3;
}

Light::~Light()
{
	m_shadowMaps.clear();
}

void Light::Reset()
{
	
}

void Light::Start()
{

}

void Light::OnDisable()
{

}

void Light::Remove()
{

}

void Light::Update()
{
	if (!m_shadowMaps.empty())
		return;

	Graphics* graphics = g_context->GetSubsystem<Graphics>();
	GameObject* camera = g_context->GetSubsystem<Scene>()->GetMainCamera();
	Camera* cameraComp = camera ? camera->GetComponent<Camera>() : nullptr;

	if (graphics && cameraComp)
		return;

	for (int i = 0; i < m_cascades; i++)
	{
		auto shadowMap = make_shared<Cascade>(i + 1, SHADOWMAP_RESOLUTION, graphics);
		m_shadowMaps.push_back(shadowMap);
	}
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

void Light::SetLightType(LightType type)
{
	m_lightType = type;
}

float Light::GetShadowTypeAsFloat() const
{
	if (m_shadowType == Hard_Shadows)
		return 0.5f;

	if (m_shadowType == Soft_Shadows)
		return 1.0f;

	return 0.0f;
}

Vector3 Light::GetDirection()
{
	return g_transform->GetForward();
}

Matrix Light::CalculateViewMatrix()
{
	Vector3 lightDirection = GetDirection();
	Vector3 position = lightDirection;
	Vector3 lookAt = position + lightDirection;
	Vector3 up = Vector3::Up;

	// Create the view matrix
	m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);

	return m_viewMatrix;
}

Matrix Light::CalculateOrthographicProjectionMatrix(int cascade)
{
	if (cascade >= m_shadowMaps.size())
		return Matrix::Identity;

	return m_shadowMaps[cascade]->CalculateProjectionMatrix(g_context->GetSubsystem<Scene>()->GetMainCamera()->GetTransform()->GetPosition(), CalculateViewMatrix());
}

void Light::SetShadowCascadeAsRenderTarget(int cascade)
{
	if (cascade < m_shadowMaps.size())
		m_shadowMaps[cascade]->SetAsRenderTarget();
}

weak_ptr<Cascade> Light::GetShadowCascade(int cascade)
{
	if (cascade < m_shadowMaps.size())
		return m_shadowMaps[cascade];

	return weak_ptr<Cascade>();
}

float Light::GetShadowCascadeSplit(int cascade)
{
	if (cascade < m_shadowMaps.size())
		return m_shadowMaps[cascade]->GetSplit();

	return 0.0f;
}
