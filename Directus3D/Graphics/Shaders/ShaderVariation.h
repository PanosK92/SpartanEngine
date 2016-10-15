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

//= INCLUDES ====================
#include "../D3D11/D3D11Buffer.h"
#include "../D3D11/D3D11Shader.h"
#include "../../Math/Matrix.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector4.h"
#include "../Material.h"
#include "../Graphics.h"
//===============================

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
		std::shared_ptr<Graphics> d3d11device
	);
	void Set();
	void SetBuffers(const Directus::Math::Matrix& mWorld, const Directus::Math::Matrix& mView, const Directus::Math::Matrix& mProjection, std::shared_ptr<Material> material, Light* directionalLight, bool receiveShadows, Camera* camera);
	void SetResources(const std::vector<ID3D11ShaderResourceView*>& textureArray);
	void Draw(int indexCount);
	std::string GetID() const;

	bool HasAlbedoTexture() const;
	bool HasRoughnessTexture() const;
	bool HasMetallicTexture() const;
	bool HasNormalTexture() const;
	bool HasHeightTexture() const;
	bool HasOcclusionTexture() const;
	bool HasEmissionTexture() const;
	bool HasMaskTexture() const;
	bool HasCubeMapTexture() const;

private:
	void AddDefinesBasedOnMaterial(std::shared_ptr<D3D11Shader> shader);
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
	std::shared_ptr<Graphics> m_graphics;
	std::shared_ptr<D3D11Buffer> m_befaultBuffer;
	std::shared_ptr<D3D11Shader> m_D3D11Shader;

	/*------------------------------------------------------------------------------
									[BUFFER TYPE]
	------------------------------------------------------------------------------*/
	const static int cascades = 3;
	struct DefaultBufferType
	{
		Directus::Math::Matrix mWorld;
		Directus::Math::Matrix mWorldView;
		Directus::Math::Matrix mWorldViewProjection;
		Directus::Math::Matrix mLightViewProjection[cascades];
		Directus::Math::Vector4 shadowSplits;
		Directus::Math::Vector4 albedoColor;
		Directus::Math::Vector2 tilingUV;
		Directus::Math::Vector2 offsetUV;
		Directus::Math::Vector2 viewport;
		float roughnessMultiplier;
		float metallicMultiplier;
		float occlusionMultiplier;
		float normalMultiplier;
		float specularMultiplier;
		float shadingMode;	
		float receiveShadows;
		float shadowBias;
		float shadowMapResolution;
		float shadowMappingQuality;
		Directus::Math::Vector3 lightDir;
		float nearPlane;
		float farPlane;
		Directus::Math::Vector3 padding;

		/*
		matrix mWorld;
		matrix mWorldView;
		matrix mWorldViewProjection;
		matrix mLightViewProjection;
		float4 materialAlbedoColor;
		float2 materialTiling;
		float2 materialOffset;
		float2 viewport;
		float materialRoughness;
		float materialMetallic;
		float materialOcclusion;
		float materialNormalStrength;
		float materialSpecular;
		float materialShadingMode;
		float receiveShadows;
		float shadowBias;
		float2 padding;
		*/
	};
};
