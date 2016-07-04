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
#include "../GraphicsDevice.h"
#include "../D3D11/D3D11Buffer.h"
#include "../D3D11/D3D11Shader.h"
//==================================

class DeferredShader
{
public:
	DeferredShader();
	~DeferredShader();

	void Initialize(GraphicsDevice* graphicsDevice);
	void Render(int indexCount, Directus::Math::Matrix mWorld, Directus::Math::Matrix mView, Directus::Math::Matrix mBaseView, Directus::Math::Matrix mPerspectiveProjection, Directus::Math::Matrix mOrthographicProjection,
	            std::vector<GameObject*> directionalLights, std::vector<GameObject*> pointLights,
	            Camera* camera, ID3D11ShaderResourceView* albedo, ID3D11ShaderResourceView* normal, ID3D11ShaderResourceView* depth, ID3D11ShaderResourceView* material,
	            ID3D11ShaderResourceView* environmentTexture, ID3D11ShaderResourceView* irradianceTexture, ID3D11ShaderResourceView* noiseTexture);
	bool IsCompiled();

private:
	struct MiscBufferType
	{
		Directus::Math::Matrix worldViewProjection;
		Directus::Math::Matrix viewProjectionInverse;
		Directus::Math::Vector4 cameraPosition;
		float nearPlane;
		float farPlane;
		Directus::Math::Vector2 padding;
	};

	D3D11Buffer* m_miscBuffer;

	const static int maxLights = 300;

	struct DirLightBufferType
	{
		Directus::Math::Matrix dirViewProjection[maxLights];
		Directus::Math::Vector4 dirLightDirection[maxLights];
		Directus::Math::Vector4 dirLightColor[maxLights];
		Directus::Math::Vector4 dirLightIntensity[maxLights];
		float dirLightCount;
		Directus::Math::Vector3 padding;
	};

	D3D11Buffer* m_dirLightBuffer;

	struct PointLightBufferType
	{
		Directus::Math::Vector4 pointLightPosition[maxLights];
		Directus::Math::Vector4 pointLightColor[maxLights];
		Directus::Math::Vector4 pointLightRange[maxLights];
		Directus::Math::Vector4 pointLightIntensity[maxLights];
		float pointLightCount;
		Directus::Math::Vector3 padding;
	};

	D3D11Buffer* m_pointLightBuffer;
	D3D11Shader* m_shader;
	GraphicsDevice* m_graphicsDevice;
};
