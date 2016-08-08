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

//= INCLUDES ====================================
#include "../Components/IComponent.h"
#include "../Math/Vector4.h"
#include "../Math/Vector3.h"
#include "../Graphics/D3D11/D3D11RenderTexture.h"
//===============================================

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

class __declspec(dllexport) Light : public IComponent
{
public:
	Light();
	~Light();

	virtual void Initialize();
	virtual void Start();
	virtual void Remove();
	virtual void Update();
	virtual void Serialize();
	virtual void Deserialize();
	
	LightType GetLightType();
	void SetLightType(LightType type);

	void SetColor(float r, float g, float b, float a);
	void SetColor(Directus::Math::Vector4 color);
	Directus::Math::Vector4 GetColor();

	void SetIntensity(float value);
	float GetIntensity();

	ShadowType GetShadowType();
	void SetShadowType(ShadowType shadowType);

	void SetRange(float value);
	float GetRange();

	void SetBias(float value);
	float GetBias();

	Directus::Math::Vector3 GetDirection();

	Directus::Math::Matrix GetViewMatrix();
	void GenerateViewMatrix();
	void GenerateOrthographicProjectionMatrix();
	Directus::Math::Matrix GetOrthographicProjectionMatrix();

	void SetDepthMapAsRenderTarget();
	ID3D11ShaderResourceView* GetDepthMap();
	float GetProjectionSize();

private:
	LightType m_lightType;
	ShadowType m_shadowType;
	Directus::Math::Vector4 m_color;
	float m_range;
	float m_intensity;
	float m_bias;
	float m_nearPlane;
	float m_farPlane;

	Directus::Math::Matrix m_viewMatrix;
	Directus::Math::Matrix m_orthoMatrix;

	D3D11RenderTexture* m_depthMap;
	float m_projectionSize;
};
