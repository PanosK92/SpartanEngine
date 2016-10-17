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

//= INCLUDES =======================
#include "../../Math/Matrix.h"
#include "../../Core/GameObject.h"
#include "../../Math/Vector4.h"
#include "../../Components/Camera.h"
#include "../Graphics.h"
#include "../D3D11/D3D11Buffer.h"
#include "../D3D11/D3D11Shader.h"
//==================================

class DeferredShader
{
public:
	DeferredShader();
	~DeferredShader();

	void Initialize(Graphics* graphicsDevice);
	void Render(
		int indexCount, const Directus::Math::Matrix& mWorld, const Directus::Math::Matrix& mView, const Directus::Math::Matrix& mBaseView,
		const Directus::Math::Matrix& mPerspectiveProjection, const Directus::Math::Matrix& mOrthographicProjection,
		std::vector<GameObject*> directionalLights, std::vector<GameObject*> pointLights, Camera* camera, 
		std::vector<ID3D11ShaderResourceView*> textures, ID3D11ShaderResourceView* environmentTex);
	bool IsCompiled();

private:
	const static int maxLights = 128;
	struct DefaultBuffer
	{
		Directus::Math::Matrix worldViewProjection;
		Directus::Math::Matrix viewProjectionInverse;
		Directus::Math::Vector4 cameraPosition;
		Directus::Math::Vector4 dirLightDirection[maxLights];
		Directus::Math::Vector4 dirLightColor[maxLights];
		Directus::Math::Vector4 dirLightIntensity[maxLights];
		Directus::Math::Vector4 pointLightPosition[maxLights];
		Directus::Math::Vector4 pointLightColor[maxLights];
		Directus::Math::Vector4 pointLightRange[maxLights];
		Directus::Math::Vector4 pointLightIntensity[maxLights];
		float dirLightCount;
		float pointLightCount;
		float nearPlane;
		float farPlane;
		Directus::Math::Vector2 viewport;
		Directus::Math::Vector2 padding;
	};

	std::shared_ptr<D3D11Buffer> m_constantBuffer;
	std::shared_ptr<D3D11Shader> m_shader;
	Graphics* m_graphics;
};
