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
#include "../D3D11/D3D11Buffer.h"
#include "../D3D11/D3D11Shader.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector4.h"
#include "../../Core/Material.h"
#include "../GraphicsDevice.h"
//=================================

#define NULL_SHADER_ID "-1";

class ShaderVariation
{
public:
	ShaderVariation();
	~ShaderVariation();

	void Initialize(
		bool albedo,
		bool roughness,
		bool metallic,
		bool normal,
		bool height,
		bool occlusion,
		bool emission,
		bool mask,
		bool cubemap,
		GraphicsDevice* d3d11device
	);
	void Set();
	void Render(int indexCount, 
		Directus::Math::Matrix mWorld, Directus::Math::Matrix mView, Directus::Math::Matrix mProjection, 
		Material* material, std::vector<ID3D11ShaderResourceView*> textureArray, Light* light, Camera* camera);
	std::string GetID();

	bool HasAlbedoTexture();
	bool HasRoughnessTexture();
	bool HasMetallicTexture();
	bool HasNormalTexture();
	bool HasHeightTexture();
	bool HasOcclusionTexture();
	bool HasEmissionTexture();
	bool HasMaskTexture();
	bool HasCubeMapTexture();

private:
	void AddDefinesBasedOnMaterial();
	void Load();

	/*------------------------------------------------------------------------------
									[PROPERTIES]
	------------------------------------------------------------------------------*/
	std::string m_ID;
	bool m_hasAlbedoTexture;
	bool m_hasRoughnessTexture;
	bool m_hasMetallicTexture;
	bool m_hasNormalTexture;
	bool m_hasHeightTexture;
	bool m_hasOcclusionTexture;
	bool m_hasEmissionTexture;
	bool m_hasMaskTexture;
	bool m_hasCubeMap;

	/*------------------------------------------------------------------------------
									[MISC]
	------------------------------------------------------------------------------*/
	GraphicsDevice* m_graphicsDevice;
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
		Directus::Math::Matrix lightViewProjection;
		Directus::Math::Vector4 materialAlbedoColor;
		float roughnessMultiplier;
		float metallicMultiplier;
		float occlusionMultiplier;
		float normalMultiplier;
		float specularMultiplier;
		float shadingMode;
		Directus::Math::Vector2 materialTiling;
		Directus::Math::Vector2 viewport;
		Directus::Math::Vector2 padding;
	};
};
