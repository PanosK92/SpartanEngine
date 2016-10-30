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
	void SaveToDirectory(const std::string& directory, bool overwrite);
	bool LoadFromFile(const std::string& filePath);
	//=============================================================================

	//= TEXTURES ==================================================================
	void SetTexture(std::shared_ptr<Texture> texture);
	void SetTextureByID(const std::string& textureID);
	std::shared_ptr<Texture> GetTextureByType(TextureType type);
	bool HasTextureOfType(TextureType type);
	bool HasTexture(const std::string& path);
	std::string GetTexturePathByType(TextureType type);
	std::vector<std::string> GetTexturePaths();
	//=============================================================================

	//= SHADER ====================================================================
	void AcquireShader();
	std::shared_ptr<ShaderVariation> GetShader();
	bool HasShader();
	ID3D11ShaderResourceView* GetShaderResourceViewByTextureType(TextureType type);
	//=============================================================================

	//= PROPERTIES ================================================================
	std::string GetID() { return m_ID; }
	void SetID(const std::string& ID) { m_ID = ID; }
	
	std::string GetName() { return m_name; }
	void SetName(const std::string& name) { m_name = name; }
	
	std::string GetModelID() { return m_modelID; }
	void SetModelID(const std::string& ID) { m_modelID = ID; }
	
	std::string GetFilePath() { return m_filePath; }
	void SetFilePath(const std::string& filepath) { m_filePath = filepath; }

	CullMode GetFaceCullMode() { return m_cullMode; }
	void SetFaceCullMode(CullMode cullMode) { m_cullMode = cullMode; }
	
	float GetOpacity() { return m_opacity; }
	void SetOpacity(float opacity)
	{
		m_opacity = opacity;
		m_alphaBlending = bool(opacity) ? true : false;
	}
	
	bool GetAlphaBlending() { return m_alphaBlending; }
	void SetAlphaBlending(bool alphaBlending) { m_alphaBlending = alphaBlending; }
	
	float GetRoughnessMultiplier() { return m_roughnessMultiplier; }
	void SetRoughnessMultiplier(float roughness) { m_roughnessMultiplier = roughness; }
	
	float GetMetallicMultiplier() { return m_metallicMultiplier; }
	void SetMetallicMultiplier(float metallic) { m_metallicMultiplier = metallic; }
	
	float GetNormalMultiplier() { return m_normalMultiplier; }
	void SetNormalMultiplier(float normal) { m_normalMultiplier = normal; }
	
	float GetHeightMultiplier() { return m_heightMultiplier; }
	void SetHeightMultiplier(float height) { m_heightMultiplier = height; }
	
	float GetOcclusionMultiplier() { return m_occlusionMultiplier; }
	void SetOcclusionMultiplier(float occlusion) { m_occlusionMultiplier = occlusion; }
	
	float GetSpecularMultiplier() { return m_specularMultiplier; }
	void SetSpecularMultiplier(float specular) { m_specularMultiplier = specular; }
	
	ShadingMode GetShadingMode() { return m_shadingMode; }
	void SetShadingMode(ShadingMode shadingMode) { m_shadingMode = shadingMode; }
	
	Directus::Math::Vector4 GetColorAlbedo() { return m_colorAlbedo; }
	void SetColorAlbedo(const Directus::Math::Vector4& color) { m_colorAlbedo = color; }
	
	Directus::Math::Vector2 GetTilingUV() { return m_tilingUV; }
	void SetTilingUV(const Directus::Math::Vector2& tiling) { m_tilingUV = tiling; }
	
	Directus::Math::Vector2 GetOffsetUV() { return m_offsetUV; }
	void SetOffsetUV(const Directus::Math::Vector2& offset) { m_offsetUV = offset; }

	bool IsEditable() { return m_isEditable; }
	void SetIsEditable(bool isEditable) { m_isEditable = isEditable; }
	//=============================================================================

private:
	void TextureBasedMultiplierAdjustment();

	std::vector<std::shared_ptr<Texture>> m_textures;
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
	std::shared_ptr<ShaderVariation> m_shader;
	bool m_isEditable;

	//= DEPENDENCIES ==========
	TexturePool* m_texturePool;
	ShaderPool* m_shaderPool;
	//=========================
};
