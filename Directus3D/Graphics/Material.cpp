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

//= INCLUDES =========================
#include "Material.h"
#include "../IO/Serializer.h"
#include "../Core/GUIDGenerator.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceCache.h"
#include "../Core/Context.h"
//====================================

//= NAMESPACES ====================
using namespace std;
using namespace Directus::Math;
using namespace Directus::Resource;
//=================================

Material::Material(Context* context)
{
	m_context = context;
	m_ID = GENERATE_GUID;
	m_name = DATA_NOT_ASSIGNED;
	m_modelID = DATA_NOT_ASSIGNED;
	m_filePath = DATA_NOT_ASSIGNED;
	m_cullMode = CullBack;
	m_opacity = 1.0f;
	m_alphaBlending = false;
	m_shadingMode = Physically_Based;
	m_colorAlbedo = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	m_roughnessMultiplier = 0.0f;
	m_metallicMultiplier = 0.0f;
	m_occlusionMultiplier = 0.0f;
	m_normalMultiplier = 0.0f;
	m_heightMultiplier = 0.0f;
	m_specularMultiplier = 0.5f;
	m_tilingUV = Vector2(1.0f, 1.0f);
	m_offsetUV = Vector2(0.0f, 0.0f);
	m_isEditable = true;

	AcquireShader();
}

Material::~Material()
{

}

//= I/O ========================================================================
bool Material::SaveMetadata()
{
	return true;
}

bool Material::Save(const string& filePath, bool overwrite)
{
	m_filePath = filePath + MATERIAL_EXTENSION;

	// If the user doesn't want to override and a material
	// indeed happens to exists, there is nothing to do
	if (!overwrite && FileSystem::FileExists(m_filePath))
		return true;

	if (!Serializer::StartWriting(m_filePath))
		return false;

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
	Serializer::WriteBool(m_isEditable);

	Serializer::WriteInt(int(m_textures.size()));
	for (const auto& texture : m_textures)
	{
		Serializer::WriteSTR(texture.first.first); // Texture Path
		Serializer::WriteInt((int)texture.first.second); // Texture Type
	}

	Serializer::StopWriting();

	return true;
}

bool Material::SaveToExistingDirectory()
{
	if (m_filePath == DATA_NOT_ASSIGNED)
		return false;

	return Save(FileSystem::GetPathWithoutFileNameExtension(m_filePath), true);
}

bool Material::LoadFromFile(const string& filePath)
{
	if (!Serializer::StartReading(filePath))
		return false;

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
	m_isEditable = Serializer::ReadBool();

	int textureCount = Serializer::ReadInt();
	for (int i = 0; i < textureCount; i++)
	{
		string texPath = Serializer::ReadSTR();
		TextureType texType = (TextureType)Serializer::ReadInt();
		auto texture = weak_ptr<Texture>();

		// If the texture happens to be loaded, we might as well get a reference to it
		if (m_context)
			texture = m_context->GetSubsystem<ResourceCache>()->GetResourceByPath<Texture>(texPath);

		m_textures.insert(make_pair(make_pair(texPath, texType), texture));
	}

	Serializer::StopReading();

	// Load unloaded textures
	for (auto& it : m_textures)
		if (it.second.expired())
			it.second = m_context->GetSubsystem<ResourceCache>()->LoadResource<Texture>(it.first.first);

	AcquireShader();

	return true;
}
//==============================================================================

//= TEXTURES ===================================================================
// Set texture from an existing texture
void Material::SetTexture(weak_ptr<Texture> texture)
{
	// Make sure this texture exists
	if (texture.expired())
		return;

	// Add it
	m_textures.insert(make_pair(make_pair(texture.lock()->GetFilePathTexture(), texture.lock()->GetType()), texture));

	// Adjust texture multipliers
	TextureBasedMultiplierAdjustment();

	// Acquire and appropriate shader
	AcquireShader();
}

// Set texture by searching it by it's ID
void Material::SetTexture(const string& textureID)
{
	if (!m_context)
		return;

	SetTexture(m_context->GetSubsystem<ResourceCache>()->GetResourceByID<Texture>(textureID));
}

// Set texture by loading it from a file
void Material::SetTexture(const string& filePath, TextureType type)
{
	if (!m_context)
		return;

	auto texture = m_context->GetSubsystem<ResourceCache>()->LoadResource<Texture>(filePath);
	if (!texture.expired())
	{
		texture.lock()->SetType(type);
		SetTexture(texture);
	}
}

weak_ptr<Texture> Material::GetTextureByType(TextureType type)
{
	for (const auto& it : m_textures)
		if (it.first.second == type)
			return it.second;

	return weak_ptr<Texture>();
}

bool Material::HasTextureOfType(TextureType type)
{
	for (const auto& it : m_textures)
		if (it.first.second == type)
			return true;

	return false;
}

bool Material::HasTexture(const string& path)
{
	for (const auto& it : m_textures)
		if (it.first.first == path)
			return true;

	return false;
}

string Material::GetTexturePathByType(TextureType type)
{
	for (const auto& it : m_textures)
		if (it.first.second == type)
			return it.first.first;

	return (string)DATA_NOT_ASSIGNED;
}

vector<string> Material::GetTexturePaths()
{
	vector<string> paths;

	for (auto it : m_textures)
		paths.push_back(it.first.first);

	return paths;
}
//==============================================================================

//= SHADER =====================================================================
void Material::AcquireShader()
{
	if (!m_context)
		return;

	// Add a shader to the pool based on this material, if a 
	// matching shader already exists, it will be returned instead.
	m_shader = CreateShaderBasedOnMaterial(
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

weak_ptr<ShaderVariation> Material::FindMatchingShader(bool albedo, bool roughness, bool metallic, bool normal, bool height, bool occlusion, bool emission, bool mask, bool cubemap)
{
	auto shaders = m_context->GetSubsystem<ResourceCache>()->GetResourcesOfType<ShaderVariation>();
	for (const auto shaderTemp : shaders)
	{
		auto shader = shaderTemp.lock();
		if (shader->HasAlbedoTexture() != albedo) continue;
		if (shader->HasRoughnessTexture() != roughness) continue;
		if (shader->HasMetallicTexture() != metallic) continue;
		if (shader->HasNormalTexture() != normal) continue;
		if (shader->HasHeightTexture() != height) continue;
		if (shader->HasOcclusionTexture() != occlusion) continue;
		if (shader->HasEmissionTexture() != emission) continue;
		if (shader->HasMaskTexture() != mask) continue;
		if (shader->HasCubeMapTexture() != cubemap) continue;

		return shader;
	}

	return weak_ptr<ShaderVariation>();
}

weak_ptr<ShaderVariation> Material::CreateShaderBasedOnMaterial(bool albedo, bool roughness, bool metallic, bool normal, bool height, bool occlusion, bool emission, bool mask, bool cubemap)
{
	// If an appropriate shader already exists, return it's ID
	auto existingShader = FindMatchingShader(albedo, roughness, metallic, normal, height, occlusion, emission, mask, cubemap);

	if (!existingShader.expired())
		return existingShader;

	// If not, create a new one
	auto shader = make_shared<ShaderVariation>();
	shader->Initialize(albedo, roughness, metallic, normal, height, occlusion, emission, mask, cubemap, m_context->GetSubsystem<Graphics>());

	// Add the shader to the pool and return it
	return m_context->GetSubsystem<ResourceCache>()->AddResource(shader);
}

ID3D11ShaderResourceView* Material::GetShaderResourceViewByTextureType(TextureType type)
{
	auto texture = GetTextureByType(type);

	if (!texture.expired())
		return texture.lock()->GetID3D11ShaderResourceView();

	return nullptr;
}
//==============================================================================

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
