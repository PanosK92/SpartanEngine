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

//= INCLUDES =================
#include "Light.h"
#include "Transform.h"
#include "../Core/Settings.h"
#include "../IO/Serializer.h"
#include "../Core/Scene.h"
//===========================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

Light::Light()
{
	m_lightType = Point;
	m_shadowType = Soft_Shadows;
	m_range = 1.0f;
	m_intensity = 5.0f;
	m_color = Vector4(
		255.0f / 255.0f,
		244.0f / 255.0f,
		214.0f / 255.0f,
		1.0f
	);
	m_bias = 0.00001;
}

Light::~Light()
{
}

void Light::Initialize()
{
	m_depthMap = new D3D11RenderTexture();
	m_depthMap->Initialize(g_graphicsDevice, SHADOWMAP_RESOLUTION, SHADOWMAP_RESOLUTION);
	m_projectionSize = 100;
}

void Light::Update()
{
}

void Light::Serialize()
{
	Serializer::SaveInt(int(m_lightType));
	Serializer::SaveInt(int(m_shadowType));
	Serializer::SaveVector4(m_color);
	Serializer::SaveFloat(m_range);
	Serializer::SaveFloat(m_intensity);
	Serializer::SaveFloat(m_bias);
}

void Light::Deserialize()
{
	m_lightType = LightType(Serializer::LoadInt());
	m_shadowType = ShadowType(Serializer::LoadInt());
	m_color = Serializer::LoadVector4();
	m_range = Serializer::LoadFloat();
	m_intensity = Serializer::LoadFloat();
	m_bias = Serializer::LoadFloat();
}

LightType Light::GetLightType()
{
	return m_lightType;
}

void Light::SetLightType(LightType type)
{
	m_lightType = type;
	g_scene->MakeDirty();
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

void Light::GenerateViewMatrix()
{
	Vector3 cameraPos = g_scene->GetMainCamera()->GetTransform()->GetPosition();
	Vector3 direction = GetDirection();
	Vector3 position = cameraPos - (g_transform->GetForward() * m_shadowTextureSize * 0.5f); // or center of scene's bounding box
	Vector3 lookAt = cameraPos + direction;
	Vector3 up = Vector3::Up;

	// Create the view matrix from the three vectors.
	m_viewMatrix = Matrix::CreateLookAtLH(position, lookAt, up);
}

Matrix Light::GetViewMatrix()
{
	return m_viewMatrix;
}

void Light::GenerateOrthographicProjectionMatrix(float width, float height, float nearPlane, float farPlane)
{
	m_shadowTextureSize = width;
	m_orthoMatrix = Matrix::CreateOrthographicLH(width, height, nearPlane, farPlane);
}

Matrix Light::GetOrthographicProjectionMatrix()
{
	return m_orthoMatrix;
}

void Light::SetDepthMapAsRenderTarget()
{
	m_depthMap->Clear(0, 0, 0, 0);
	m_depthMap->SetAsRenderTarget();
}

ID3D11ShaderResourceView* Light::GetDepthMap()
{
	return m_depthMap->GetShaderResourceView();
}

float Light::GetProjectionSize()
{
	return m_projectionSize;
}
