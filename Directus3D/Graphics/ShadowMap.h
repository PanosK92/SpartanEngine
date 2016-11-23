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

#pragma once

//= INCLUDES ========================
#include "D3D11/D3D11RenderTexture.h"
#include "../Components/Light.h"
#include "../Components/Camera.h"
//===================================

//= NAMESPACES =====
using namespace std;
//==================

class ShadowMap
{
public:
	ShadowMap(Graphics* device, int cascadeNumber, Light* light, Camera* camera, int resolution)
	{
		m_resolution = resolution;
		m_depthMap = make_shared<D3D11RenderTexture>();
		m_depthMap->Initialize(device, resolution, resolution);
		m_camera = camera;
		m_light = light;
		m_cascadeNumber = cascadeNumber;
	}
	~ShadowMap();

	void SetAsRenderTarget() const
	{
		m_depthMap->Clear(0.0f, 0.0f, 0.0f, 1.0f);
		m_depthMap->SetAsRenderTarget();
	}

	Directus::Math::Matrix CalculateProjectionMatrix(Directus::Math::Matrix viewMatrix)
	{
		float radius = GetRadius();
		Directus::Math::Vector3 center = Directus::Math::Vector3::Transform(m_camera->g_transform->GetPosition(), viewMatrix);		
		Directus::Math::Vector3 min = center - Directus::Math::Vector3(radius, radius, radius);
		Directus::Math::Vector3 max = center + Directus::Math::Vector3(radius, radius, radius);

		return Directus::Math::Matrix::CreateOrthoOffCenterLH(min.x, max.x, min.y, max.y, -max.z, -min.z);
	}
	ID3D11ShaderResourceView* GetShaderResourceView() const { return m_depthMap->GetShaderResourceView(); }

	float GetSplit()
	{
		float split = 0;

		if (m_cascadeNumber == 1)
			split = 980;

		if (m_cascadeNumber == 2)
			split = 995;

		return split / 1000.0f;
	}

	float GetRadius()
	{
		float radius = m_camera->GetFarPlane() * 0.5f;

		if (m_cascadeNumber == 1)
			radius = 25;

		else if (m_cascadeNumber == 2)
			radius = 40;

		if (m_cascadeNumber == 3)
			radius = 100;

		return radius;
	}

private:
	int m_resolution;
	shared_ptr<D3D11RenderTexture> m_depthMap;
	Directus::Math::Matrix m_projectionMatrix;
	Light* m_light;
	Camera* m_camera;
	int m_cascadeNumber;
};