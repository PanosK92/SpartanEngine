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

//= INCLUDES =============================
#include "../Components/IComponent.h"
#include "../Math/Vector4.h"
#include "../Math/Vector3.h"
#include <vector>
#include "../Graphics/D3D11/D3D11Shader.h"
#include "../Math/Matrix.h"
//========================================

class ShadowMap;

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
	float GetShadowTypeAsFloat() const;

	void SetRange(float value);
	float GetRange();

	void SetBias(float value);
	float GetBias();

	Directus::Math::Vector3 GetDirection();

	Directus::Math::Matrix GetViewMatrix();
	Directus::Math::Matrix GetOrthographicProjectionMatrix(int cascade);
	void SetShadowMapAsRenderTarget(int cascade);
	ID3D11ShaderResourceView* GetDepthMap(int cascade);
	float GetShadowMapResolution();
	int GetCascadeCount();
	float GetCascadeSplit(int cascade);

private:
	LightType m_lightType;
	ShadowType m_shadowType;
	Directus::Math::Vector4 m_color;
	float m_range;
	float m_intensity;
	float m_bias;

	Directus::Math::Matrix m_viewMatrix;

	int m_cascades;
	std::vector<ShadowMap*> m_shadowMaps;
};
