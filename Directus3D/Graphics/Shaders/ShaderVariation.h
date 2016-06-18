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

//= INCLUDES ======================
#include "../D3D11/D3D11Device.h"
#include "../D3D11/D3D11Buffer.h"
#include "../D3D11/D3D11Shader.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector3.h"
#include "../../Math/Vector4.h"
#include "../../Core/Material.h"
#include "../../Components/Light.h"
//=================================

#define NULL_SHADER_ID "-1";

class ShaderVariation
{
public:
	ShaderVariation();
	~ShaderVariation();

	void Initialize(Material* material, D3D11Device* d3d11device);
	void Set();
	void Render(int indexCount, Directus::Math::Matrix mWorld, Directus::Math::Matrix mView, Directus::Math::Matrix mProjection, Light* directionalLight, Material* material);
	std::string GetID();
	bool MatchesMaterial(Material* material);

private:
	void AddDefinesBasedOnMaterial(Material* material);
	void Load(Material* material);

	/*------------------------------------------------------------------------------
									[PROPERTIES]
	------------------------------------------------------------------------------*/
	std::string m_ID;
	bool m_hasAlbedoTexture;
	bool m_hasRoughnessTexture;
	bool m_hasMetallicTexture;
	bool m_hasOcclusionTexture;
	bool m_hasNormalTexture;
	bool m_hasHeightTexture;
	bool m_hasMaskTexture;
	bool m_hasCubeMap;

	/*------------------------------------------------------------------------------
									[MISC]
	------------------------------------------------------------------------------*/
	D3D11Device* m_D3D11Device;
	D3D11Buffer* m_befaultBuffer;
	D3D11Shader* m_D3D11Shader;

	/*------------------------------------------------------------------------------
									[BUFFER TYPE]
	------------------------------------------------------------------------------*/
	struct DefaultBufferType
	{
		Directus::Math::Matrix world;
		Directus::Math::Matrix worldView;
		Directus::Math::Matrix worldViewProjection;
		Directus::Math::Matrix viewProjectionDirLight;
		Directus::Math::Vector4 materialAlbedoColor;
		float roughness;
		float metallic;
		float occlusion;
		float normalStrength;
		float reflectivity;
		float shadingMode;
		Directus::Math::Vector2 materialTiling;
		float bias;
		Directus::Math::Vector3 lightDirection;
		Directus::Math::Vector3 cameraPos;
		float padding;
	};
};
