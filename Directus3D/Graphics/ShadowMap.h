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
#include "../Core/Globals.h"
//===================================

class ShadowMap
{
public:
	ShadowMap(GraphicsDevice* device, int resolution, float nearPlane, float farPlane, float projectionSize)
	{
		m_resolution = resolution;
		m_nearPlane = nearPlane;
		m_farPlane = farPlane;
		m_projectionSize = projectionSize;

		m_depthMap = new D3D11RenderTexture();
		m_depthMap->Initialize(device, resolution, resolution);
	}
	~ShadowMap() { SafeDelete(m_depthMap); }

	void SetAsRenderTarget() const
	{
		m_depthMap->Clear(0, 0, 0, 0);
		m_depthMap->SetAsRenderTarget();
	}

	Directus::Math::Matrix GetProjectionMatrix() const { return Directus::Math::Matrix::CreateOrthographicLH(m_projectionSize, m_projectionSize, m_nearPlane, m_farPlane); }
	ID3D11ShaderResourceView* GetShaderResourceView() const { return m_depthMap->GetShaderResourceView(); }
	float GetSplit() const { return m_farPlane / 1000; } // divaded by camera far
	float GetFarPlane() const { return m_farPlane; } // divaded by camera far
	float GetProjectionSize() const
	{ return m_projectionSize; }

private:
	int m_resolution;
	float m_nearPlane;
	float m_farPlane;
	float m_projectionSize;
	D3D11RenderTexture* m_depthMap;
	Directus::Math::Matrix m_projectionMatrix;
};