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
#include "../IO/FileSystem.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

Material::Material(TexturePool* texturePool, ShaderPool* shaderPool)
{
	m_ID = GENERATE_GUID;
	m_name = "N/A";
	m_modelID = "N/A";
	m_filePath = "N/A";
	m_cullMode = CullBack;
	m_opacity = 1.0f;
	m_alphaBlending = false;
	m_shadingMode = Physically_Based;
	m_colorAlbedo = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	m_roughnessMultiplier = 1.0f;
	m_metallicMultiplier = 0.0f;
	m_occlusionMultiplier = 0.0f;
	m_normalMultiplier = 0.0f;
	m_heightMultiplier = 0.0f;
	m_specularMultiplier = 0.5f;
	m_tilingUV = Vector2(1.0f, 1.0f);
	m_offsetUV = Vector2(0.0f, 0.0f);

	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
	m_shader = nullptr;

	AcquireShader();
}

Material::~Material()
{

}

//= I/O ========================================================================
void Material::Serialize()
{
	Serializer::WriteSTR(m_ID);
	Serializer::WriteSTR(m_name);
	Serializer::WriteSTR(m_modelID);
	Serializer::WriteSTR(m_filePath);
	Serializer::WriteInt(m_cullMode);
	Serializer::WriteFloat(m_opacity);
	Serializer::WriteBool(m_alphaBlending);
	Serializer::WriteInt(m_shadingMode);
	Serializer::WriteVector4(m_colorAlbedo);
	Serializer::WriteFloat(m_roughnessMultiplier);
	Serializer::WriteFloat(m_metallicMultiplier);
	Serializer::WriteFloat(m_normalMultiplier);
	Serializer::WriteFloat(m_heightMultiplier);
	Serializer::WriteFloat(m_occlusionMultiplier);
	Serializer::WriteFloat(m_specularMultiplier);
	Serializer::WriteVector2(m_tilingUV);
	Serializer::WriteVector2(m_offsetUV);

	Serializer::WriteInt(int(m_textures.size()));
	for (auto i = 0; i < m_textures.size(); i++)
		Serializer::WriteSTR(m_textures[i]->GetID());
}

void Material::Deserialize()
{
	m_ID = Serializer::ReadSTR();
	m_name = Serializer::ReadSTR();
	m_modelID = Serializer::ReadSTR();
	m_filePath = Serializer::ReadSTR();
	m_cullMode = CullMode(Serializer::ReadInt());
	m_opacity = Serializer::ReadFloat();
	m_alphaBlending = Serializer::ReadBool();
	m_shadingMode = ShadingMode(Serializer::ReadInt());
	m_colorAlbedo = Serializer::ReadVector4();
	m_roughnessMultiplier = Serializer::ReadFloat();
	m_metallicMultiplier = Serializer::ReadFloat();
	m_normalMultiplier = Serializer::ReadFloat();
	m_heightMultiplier = Serializer::ReadFloat();
	m_occlusionMultiplier = Serializer::ReadFloat();
	m_specularMultiplier = Serializer::ReadFloat();
	m_tilingUV = Serializer::ReadVector2();
	m_offsetUV = Serializer::ReadVector2();

	int textureCount = Serializer::ReadInt();
	for (int i = 0; i < textureCount; i++)
	{
		string textureID = Serializer::ReadSTR(); // texture id
		Texture* texture = m_texturePool->GetTextureByID(textureID);

		if (texture)
			m_textures.push_back(texture);
	}

	AcquireShader();
}

void Material::SaveAsFile(const string& path)
{
	m_filePath = path + GetName() + MATERIAL_EXTENSION;

	Serializer::StartWriting(m_filePath);
	Serialize();
	Serializer::StopWriting();
}

bool Material::LoadFromFile(const string& filePath)
{
	if (!FileSystem::FileExists(filePath))
		return false;

	Serializer::StartReading(filePath);
	Deserialize();
	Serializer::StopReading();

	return true;
}
//==============================================================================

//= TEXTURES ===================================================================
void Material::SetTexture(Texture* texture)
{
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

void Material::SetTextureByID(const string& textureID)
{
	// Get the texture from the pool
	Texture* texture = m_texturePool->GetTextureByID(textureID);

	SetTexture(texture);
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

bool Material::HasTexture(const string& path)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetFilePathTexture() == path)
			return true;

	return false;
}

string Material::GetTexturePathByType(TextureType type)
{
	Texture* texture = GetTextureByType(type);
	if (texture)
		return texture->GetFilePathTexture();

	return TEXTURE_PATH_UNKNOWN;
}

vector<string> Material::GetTexturePaths()
{
	vector<string> paths;
	for (auto i = 0; i < m_textures.size(); i++)
		paths.push_back(m_textures[i]->GetFilePathTexture());

	return paths;
}
//==============================================================================

//= SHADER =====================================================================
void Material::AcquireShader()
{
	if (!m_shaderPool)
		return;

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