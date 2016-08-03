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

//= INCLUDES =====================
#include "Material.h"
#include "../Pools/TexturePool.h"
#include "../IO/Serializer.h"
#include "../Core/GUIDGenerator.h"
#include "../Pools/ShaderPool.h"
#include "../IO/Log.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Material::Material(TexturePool* texturePool, ShaderPool* shaderPool)
{
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
	m_shader = nullptr;
	m_ID = GENERATE_GUID;
	m_name = "N/A";
	m_modelID = "N/A";
	m_cullMode = CullBack;
	m_opacity = 1.0f;
	m_alphaBlending = false;
	m_shadingMode = Physically_Based;
	m_colorAlbedo = false;
	m_roughnessMultiplier = 1.0f;
	m_metallicMultiplier = 0.0f;
	m_occlusionMultiplier = 0.0f;
	m_normalMultiplier = 0.0f;
	m_heightMultiplier = 0.0f;
	m_specularMultiplier = 0.0f;
	m_tiling = Vector2(1.0f, 1.0f);

	AcquireShader();
}

Material::~Material()
{

}

//= I/O ========================================================================
void Material::Serialize()
{
	Serializer::SaveSTR(m_ID);
	Serializer::SaveSTR(m_name);
	Serializer::SaveSTR(m_modelID);
	Serializer::SaveInt(m_cullMode);
	Serializer::SaveFloat(m_opacity);
	Serializer::SaveBool(m_alphaBlending);
	Serializer::SaveInt(m_shadingMode);
	Serializer::SaveVector4(m_colorAlbedo);
	Serializer::SaveFloat(m_roughnessMultiplier);
	Serializer::SaveFloat(m_metallicMultiplier);
	Serializer::SaveFloat(m_normalMultiplier);
	Serializer::SaveFloat(m_heightMultiplier);
	Serializer::SaveFloat(m_occlusionMultiplier);
	Serializer::SaveFloat(m_specularMultiplier);
	Serializer::SaveVector2(m_tiling);

	Serializer::SaveInt(int(m_textures.size()));
	for (auto i = 0; i < m_textures.size(); i++)
		Serializer::SaveSTR(m_textures[i]->GetID());
}

void Material::Deserialize()
{
	m_ID = Serializer::LoadSTR();
	m_name = Serializer::LoadSTR();
	m_modelID = Serializer::LoadSTR();
	m_cullMode = CullMode(Serializer::LoadInt());
	m_opacity = Serializer::LoadFloat();
	m_alphaBlending = Serializer::LoadBool();
	m_shadingMode = ShadingMode(Serializer::LoadInt());
	m_colorAlbedo = Serializer::LoadVector4();
	m_roughnessMultiplier = Serializer::LoadFloat();
	m_metallicMultiplier = Serializer::LoadFloat();
	m_normalMultiplier = Serializer::LoadFloat();
	m_heightMultiplier = Serializer::LoadFloat();
	m_occlusionMultiplier = Serializer::LoadFloat();	
	m_specularMultiplier = Serializer::LoadFloat();
	m_tiling = Serializer::LoadVector2();

	int textureCount = Serializer::LoadInt();
	for (int i = 0; i < textureCount; i++)
	{
		string textureID = Serializer::LoadSTR();
		Texture* texture = m_texturePool->GetTextureByID(textureID);

		if (texture)
			m_textures.push_back(texture);
	}

	AcquireShader();
}
//==============================================================================

//= TEXTURES ===================================================================
void Material::SetTexture(string textureID)
{
	// Get the texture from the pool
	Texture* texture = m_texturePool->GetTextureByID(textureID);

	// Make sure this texture exists
	if (!texture)
		return;

	// Overwrite
	if (HasTextureOfType(texture->GetType())) 	
	{
		int textureIndex = GetTextureIndexByType(texture->GetType());
		m_textures[textureIndex] = texture;
	}
	else // Add
		m_textures.push_back(texture);

	TextureBasedMultiplierAdjustment();
	AcquireShader();
}

Texture* Material::GetTextureByType(TextureType type)
{
	for (auto i = 0; i < m_textures.size(); i++)
	{
		if (m_textures[i]->GetType() == type)
			return m_textures[i];
	}

	return nullptr;
}

bool Material::HasTextureOfType(TextureType type)
{
	Texture* texture = GetTextureByType(type);
	if (texture)
		return true;

	return false;
}

bool Material::HasTexture(string path)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == path)
			return true;

	return false;
}

string Material::GetTexturePathByType(TextureType type)
{
	Texture* texture = GetTextureByType(type);
	if (texture)
		return texture->GetPath();

	return TEXTURE_PATH_UNKNOWN;
}

vector<string> Material::GetTexturePaths()
{
	vector<string> paths;
	for (auto i = 0; i < m_textures.size(); i++)
		paths.push_back(m_textures[i]->GetPath());

	return paths;
}
//==============================================================================

//= SHADER =====================================================================
void Material::AcquireShader()
{
	// Add a shader to the pool based on this material, if a 
	// matching shader already exists, it will be returned instead.
	m_shader = m_shaderPool->CreateShaderBasedOnMaterial(
		HasTextureOfType(Albedo),
		HasTextureOfType(Roughness),
		HasTextureOfType(Metallic),
		HasTextureOfType(Normal),
		HasTextureOfType(Height),
		HasTextureOfType(Occlusion),
		HasTextureOfType(Emission),
		HasTextureOfType(Mask),
		HasTextureOfType(CubeMap)
	);
}

ShaderVariation* Material::GetShader()
{
	return m_shader;
}

bool Material::HasShader()
{
	if (GetShader())
		return true;

	return false;
}

ID3D11ShaderResourceView* Material::GetShaderResourceViewByTextureType(TextureType type)
{
	Texture* texture = GetTextureByType(type);

	if (texture)
		return texture->GetID3D11ShaderResourceView();

	return nullptr;
}
//==============================================================================

//= PROPERTIES =================================================================
void Material::SetID(string id)
{
	m_ID = id;
}

string Material::GetID()
{
	return m_ID;
}

void Material::SetName(string name)
{
	m_name = name;
}

string Material::GetName()
{
	return m_name;
}

void Material::SetModelID(string id)
{
	m_modelID = id;
}

string Material::GetModelID()
{
	return m_modelID;
}

void Material::SetFaceCullMode(CullMode cullMode)
{
	this->m_cullMode = cullMode;
}

CullMode Material::GetFaceCullMode()
{
	return m_cullMode;
}

void Material::SetOpacity(float opacity)
{
	this->m_opacity = opacity;

	if (opacity != 1.0f)
		m_alphaBlending = true;
	else
		m_alphaBlending = false;
}

float Material::GetOpacity()
{
	return m_opacity;
}

void Material::SetAlphaBlending(bool alphaBlending)
{
	this->m_alphaBlending = alphaBlending;
}

bool Material::GetAlphaBlending()
{
	return m_alphaBlending;
}

void Material::SetRoughnessMultiplier(float roughness)
{
	m_roughnessMultiplier = roughness;
}

float Material::GetRoughnessMultiplier()
{
	return m_roughnessMultiplier;
}

void Material::SetMetallicMultiplier(float metallic)
{
	m_metallicMultiplier = metallic;
}

float Material::GetMetallicMultiplier()
{
	return m_metallicMultiplier;
}

void Material::SetOcclusionMultiplier(float occlusion)
{
	m_occlusionMultiplier = occlusion;
}

float Material::GetOcclusionMultiplier()
{
	return m_occlusionMultiplier;
}

void Material::SetNormalMultiplier(float intensity)
{
	m_normalMultiplier = intensity;
}

float Material::GetNormalMultiplier()
{
	return m_normalMultiplier;
}

void Material::SetHeightMultiplier(float height)
{
	m_heightMultiplier = height;
}

float Material::GetHeightMultiplier()
{
	return m_heightMultiplier;
}

void Material::SetSpecularMultiplier(float specular)
{
	m_specularMultiplier = specular;
}

float Material::GetSpecularMultiplier()
{
	return m_specularMultiplier;
}

void Material::SetShadingMode(ShadingMode shadingMode)
{
	this->m_shadingMode = shadingMode;
}

ShadingMode Material::GetShadingMode()
{
	return m_shadingMode;
}

void Material::SetColorAlbedo(Vector4 color)
{
	m_colorAlbedo = color;
}

Vector4 Material::GetColorAlbedo()
{
	return m_colorAlbedo;
}

void Material::SetTiling(Vector2 tiling)
{
	this->m_tiling = tiling;
}

Vector2 Material::GetTiling()
{
	return m_tiling;
}
//==============================================================================

//= HELPER FUNCTIONS ===========================================================
int Material::GetTextureIndexByType(TextureType type)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetType() == type)
			return i;

	return -1;
}

void Material::TextureBasedMultiplierAdjustment()
{
	if (HasTextureOfType(Roughness))
		SetRoughnessMultiplier(1.0f);

	if (HasTextureOfType(Metallic))
		SetMetallicMultiplier(1.0f);

	if (HasTextureOfType(Normal))
		SetNormalMultiplier(1.0f);

	if (HasTextureOfType(Height))
		SetHeightMultiplier(1.0f);
}
//==============================================================================