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
#include <vector>
#include "Texture.h"
#include "../Math/Vector2.h"
#include "../Math/Vector4.h"
#include "../Graphics/Renderer.h"
//===============================

#define MATERIAL_EXTENSION ".mat"

class ShaderVariation;
class ShaderPool;
class TexturePool;

enum ShadingMode
{
	Physically_Based,
	Unlit,
	Skysphere
};

class __declspec(dllexport) Material
{
public:
	Material(TexturePool* texturePool, ShaderPool* shaderPool);
	~Material();

	//= I/O =======================================================================
private:
	void Serialize();
	void Deserialize();
public:
	void SaveToDirectory(const std::string& directory);
	bool LoadFromFile(const std::string& filePath);
	//=============================================================================

	//= TEXTURES ==================================================================
	void SetTexture(Texture* texture);
	void SetTextureByID(const std::string& textureID);
	Texture* GetTextureByType(TextureType type);
	bool HasTextureOfType(TextureType type);
	bool HasTexture(const std::string& path);
	std::string GetTexturePathByType(TextureType type);
	std::vector<std::string> GetTexturePaths();
	//=============================================================================

	//= SHADER ====================================================================
	void AcquireShader();
	ShaderVariation* GetShader();
	bool HasShader();
	ID3D11ShaderResourceView* GetShaderResourceViewByTextureType(TextureType type);
	//=============================================================================

	//= PROPERTIES ================================================================
	void SetID(const std::string& ID) { m_ID = ID; }
	std::string GetID() { return m_ID; }

	void SetName(const std::string& name) { m_name = name; }
	std::string GetName() { return m_name; }

	void SetModelID(const std::string& ID) { m_modelID = ID; }
	std::string GetModelID() { return m_modelID; }

	void SetFilePath(const std::string& filepath) { m_filePath = filepath; }
	std::string GetFilePath() { return m_filePath; }

	void SetFaceCullMode(CullMode cullMode) { m_cullMode = cullMode; }
	CullMode GetFaceCullMode() { return m_cullMode; }

	void SetOpacity(float opacity)
	{
		m_opacity = opacity;
		m_alphaBlending = bool(opacity) ? true : false;
	}
	float GetOpacity() { return m_opacity; }

	void SetAlphaBlending(bool alphaBlending) { m_alphaBlending = alphaBlending; }
	bool GetAlphaBlending() { return m_alphaBlending; }

	void SetRoughnessMultiplier(float roughness) { m_roughnessMultiplier = roughness; }
	float GetRoughnessMultiplier() { return m_roughnessMultiplier; }

	void SetMetallicMultiplier(float metallic) { m_metallicMultiplier = metallic; }
	float GetMetallicMultiplier() { return m_metallicMultiplier; }

	void SetNormalMultiplier(float normal) { m_normalMultiplier = normal; }
	float GetNormalMultiplier() { return m_normalMultiplier; }

	void SetHeightMultiplier(float height) { m_heightMultiplier = height; }
	float GetHeightMultiplier() { return m_heightMultiplier; }

	void SetOcclusionMultiplier(float occlusion) { m_occlusionMultiplier = occlusion; }
	float GetOcclusionMultiplier() { return m_occlusionMultiplier; }

	void SetSpecularMultiplier(float specular) { m_specularMultiplier = specular; }
	float GetSpecularMultiplier() { return m_specularMultiplier; }

	void SetShadingMode(ShadingMode shadingMode) { m_shadingMode = shadingMode; }
	ShadingMode GetShadingMode() { return m_shadingMode; }

	void SetColorAlbedo(const Directus::Math::Vector4& color) { m_colorAlbedo = color; }
	Directus::Math::Vector4 GetColorAlbedo() { return m_colorAlbedo; }

	void SetTilingUV(const Directus::Math::Vector2& tiling) { m_tilingUV = tiling; }
	Directus::Math::Vector2 GetTilingUV() { return m_tilingUV; }

	void SetOffsetUV(const Directus::Math::Vector2& offset) { m_offsetUV = offset; }
	Directus::Math::Vector2 GetOffsetUV() { return m_offsetUV; }
	//=============================================================================

private:
	//= HELPER FUNCTIONS =======================
	int GetTextureIndexByType(TextureType type);
	void TextureBasedMultiplierAdjustment();
	//==========================================

	std::vector<Texture*> m_textures;
	std::string m_ID;
	std::string m_name;
	std::string m_modelID;
	std::string m_filePath;
	CullMode m_cullMode;
	float m_opacity;
	bool m_alphaBlending;
	Directus::Math::Vector4 m_colorAlbedo;
	float m_roughnessMultiplier;
	float m_metallicMultiplier;
	float m_normalMultiplier;
	float m_heightMultiplier;
	float m_occlusionMultiplier;
	float m_specularMultiplier;
	Directus::Math::Vector2 m_tilingUV;
	Directus::Math::Vector2 m_offsetUV;
	ShadingMode m_shadingMode;
	ShaderVariation* m_shader;

	//= DEPENDENCIES ==========
	TexturePool* m_texturePool;
	ShaderPool* m_shaderPool;
	//=========================
};
