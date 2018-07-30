/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ===========================
#include "Material.h"
#include "Deferred/ShaderVariation.h"
#include "../RHI/IRHI_Implementation.h"
#include "../Resource/ResourceManager.h"
#include "../IO/XmlDocument.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Material::Material(Context* context) : IResource(context)
	{
		//= IResource ===============
		RegisterResource<Material>();
		//===========================

		// Material
		m_modelID				= NOT_ASSIGNED_HASH;
		m_opacity				= 1.0f;
		m_alphaBlending			= false;
		m_cullMode				= Cull_Back;
		m_shadingMode			= Shading_PBR;
		m_colorAlbedo			= Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		m_roughnessMultiplier	= 1.0f;
		m_metallicMultiplier	= 0.0f;
		m_normalMultiplier		= 0.0f;
		m_heightMultiplier		= 0.0f;
		m_uvTiling				= Vector2(1.0f, 1.0f);
		m_uvOffset				= Vector2(0.0f, 0.0f);
		m_isEditable			= true;

		AcquireShader();
	}

	Material::~Material()
	{

	}

	//= IResource ==============================================
	bool Material::LoadFromFile(const string& filePath)
	{
		// Make sure the path is relative
		SetResourceFilePath(FileSystem::GetRelativeFilePath(filePath));

		auto xml = make_unique<XmlDocument>();
		if (!xml->Load(GetResourceFilePath()))
			return false;

		SetResourceName(xml->GetAttributeAs<string>("Material", "Name"));
		SetResourceFilePath(xml->GetAttributeAs<string>("Material", "Path"));
		xml->GetAttribute("Material", "Model_ID",				&m_modelID);
		xml->GetAttribute("Material", "Opacity",				&m_opacity);
		xml->GetAttribute("Material", "Alpha_Blending",			&m_alphaBlending);	
		xml->GetAttribute("Material", "Roughness_Multiplier",	&m_roughnessMultiplier);
		xml->GetAttribute("Material", "Metallic_Multiplier",	&m_metallicMultiplier);
		xml->GetAttribute("Material", "Normal_Multiplier",		&m_normalMultiplier);
		xml->GetAttribute("Material", "Height_Multiplier",		&m_heightMultiplier);
		xml->GetAttribute("Material", "IsEditable",				&m_isEditable);
		xml->GetAttribute("Material", "Cull_Mode",				(unsigned int*)&m_cullMode);
		xml->GetAttribute("Material", "Shading_Mode",			(unsigned int*)&m_shadingMode);
		xml->GetAttribute("Material", "Color",					&m_colorAlbedo);
		xml->GetAttribute("Material", "UV_Tiling",				&m_uvTiling);
		xml->GetAttribute("Material", "UV_Offset",				&m_uvOffset);

		int textureCount = xml->GetAttributeAs<int>("Textures", "Count");
		for (int i = 0; i < textureCount; i++)
		{
			string nodeName		= "Texture_" + to_string(i);
			TextureType texType	= (TextureType)xml->GetAttributeAs<unsigned int>(nodeName, "Texture_Type");
			string texName		= xml->GetAttributeAs<string>(nodeName, "Texture_Name");
			string texPath		= xml->GetAttributeAs<string>(nodeName, "Texture_Path");

			// If the texture happens to be loaded, get a reference to it
			m_textures[texType] = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<RHI_Texture>(texName);
			// If there is not texture (it's not loaded yet), load it
			if (m_textures[texType].expired())
			{
				m_textures[texType] = m_context->GetSubsystem<ResourceManager>()->Load<RHI_Texture>(texPath);
			}
		}

		AcquireShader();

		return true;
	}

	bool Material::SaveToFile(const string& filePath)
	{
		// Make sure the path is relative
		SetResourceFilePath(FileSystem::GetRelativeFilePath(filePath));

		// Add material extension if not present
		if (FileSystem::GetExtensionFromFilePath(GetResourceFilePath()) != EXTENSION_MATERIAL)
		{
			SetResourceFilePath(GetResourceFilePath() + EXTENSION_MATERIAL);
		}

		auto xml = make_unique<XmlDocument>();
		xml->AddNode("Material");
		xml->AddAttribute("Material", "Name",					GetResourceName());
		xml->AddAttribute("Material", "Path",					GetResourceFilePath());
		xml->AddAttribute("Material", "Model_ID",				m_modelID);
		xml->AddAttribute("Material", "Opacity",				m_opacity);
		xml->AddAttribute("Material", "Alpha_Blending",			m_alphaBlending);
		xml->AddAttribute("Material", "Cull_Mode",				unsigned int(m_cullMode));	
		xml->AddAttribute("Material", "Shading_Mode",			unsigned int(m_shadingMode));
		xml->AddAttribute("Material", "Color",					m_colorAlbedo);
		xml->AddAttribute("Material", "Roughness_Multiplier",	m_roughnessMultiplier);
		xml->AddAttribute("Material", "Metallic_Multiplier",	m_metallicMultiplier);
		xml->AddAttribute("Material", "Normal_Multiplier",		m_normalMultiplier);
		xml->AddAttribute("Material", "Height_Multiplier",		m_heightMultiplier);
		xml->AddAttribute("Material", "UV_Tiling",				m_uvTiling);
		xml->AddAttribute("Material", "UV_Offset",				m_uvOffset);
		xml->AddAttribute("Material", "IsEditable",				m_isEditable);

		xml->AddChildNode("Material", "Textures");
		xml->AddAttribute("Textures", "Count", (unsigned int)m_textures.size());
		int i = 0;
		for (const auto& texture : m_textures)
		{
			string texNode = "Texture_" + to_string(i);
			xml->AddChildNode("Textures", texNode);
			xml->AddAttribute(texNode, "Texture_Type", (unsigned int)texture.first);
			xml->AddAttribute(texNode, "Texture_Name", !texture.second.expired() ? texture.second.lock()->GetResourceName() : NOT_ASSIGNED);
			xml->AddAttribute(texNode, "Texture_Path", !texture.second.expired() ? texture.second.lock()->GetResourceFilePath() : NOT_ASSIGNED);
			i++;
		}

		return xml->Save(GetResourceFilePath());
	}

	unsigned int Material::GetMemoryUsage()
	{
		// Doesn't have to be spot on, just representative
		unsigned int size = 0;
		size += sizeof(bool) * 2;
		size += sizeof(int) * 3;
		size += sizeof(float) * 5;
		size += sizeof(Vector2) * 2;
		size += sizeof(Vector4);
		size += (unsigned int)(sizeof(std::map<TextureType, std::weak_ptr<RHI_Texture>>) + (sizeof(TextureType) + sizeof(std::weak_ptr<RHI_Texture>)) * m_textures.size());

		return size;
	}

	//==========================================================

	//= TEXTURES ===================================================================
	// Set texture from an existing texture
	void Material::SetTexture(const weak_ptr<RHI_Texture>& textureWeak, bool autoCache /* true */)
	{
		// Make sure this texture exists
		auto texture = textureWeak.lock();
		if (!texture)
		{
			LOG_WARNING("Material::SetTexture(): Provided texture is null, can't execute function");
			return;
		}

		// Cache it or use the provided reference as is
		auto texRef = autoCache ? textureWeak.lock()->Cache<RHI_Texture>() : textureWeak;
		// Save a reference
		m_textures[texture->GetType()] = texRef;

		TextureBasedMultiplierAdjustment();
		AcquireShader();
	}

	bool Material::HasTextureOfType(TextureType type)
	{
		return !m_textures[type].expired();
	}

	bool Material::HasTexture(const string& path)
	{
		for (const auto& it : m_textures)
		{
			if (it.second.expired())
				continue;

			if (it.second.lock()->GetResourceFilePath() == path)
				return true;
		}

		return false;
	}

	std::string Material::GetTexturePathByType(TextureType type)
	{
		if (m_textures[type].expired())
			return NOT_ASSIGNED;

		return m_textures[type].lock()->GetResourceFilePath();
	}

	vector<string> Material::GetTexturePaths()
	{
		vector<string> paths;
		for (const auto& it : m_textures)
		{
			if (it.second.expired())
				continue;

			paths.push_back(it.second.lock()->GetResourceFilePath());
		}

		return paths;
	}
	//==============================================================================

	//= SHADER =====================================================================
	void Material::AcquireShader()
	{
		if (!m_context)
		{
			LOG_ERROR("Material::AcquireShader(): Context is null, can't execute function");
			return;
		}

		// Add a shader to the pool based on this material, if a 
		// matching shader already exists, it will be returned.
		unsigned long shaderFlags = 0;

		if (HasTextureOfType(TextureType_Albedo))		shaderFlags	|= Variaton_Albedo;
		if (HasTextureOfType(TextureType_Roughness))	shaderFlags	|= Variaton_Roughness;
		if (HasTextureOfType(TextureType_Metallic))		shaderFlags	|= Variaton_Metallic;
		if (HasTextureOfType(TextureType_Normal))		shaderFlags	|= Variaton_Normal;
		if (HasTextureOfType(TextureType_Height))		shaderFlags	|= Variaton_Height;
		if (HasTextureOfType(TextureType_Occlusion))	shaderFlags	|= Variaton_Occlusion;
		if (HasTextureOfType(TextureType_Emission))		shaderFlags	|= Variaton_Emission;
		if (HasTextureOfType(TextureType_Mask))			shaderFlags	|= Variaton_Mask;
		if (HasTextureOfType(TextureType_CubeMap))		shaderFlags	|= Variaton_Cubemap;

		m_shader = GetOrCreateShader(shaderFlags);
	}

	weak_ptr<ShaderVariation> Material::FindMatchingShader(unsigned long shaderFlags)
	{
		auto shaders = m_context->GetSubsystem<ResourceManager>()->GetResourcesByType<ShaderVariation>();
		for (const auto& shader : shaders)
		{
			if (shader.lock()->GetShaderFlags() == shaderFlags)
				return shader;
		}
		return weak_ptr<ShaderVariation>();
	}

	weak_ptr<ShaderVariation> Material::GetOrCreateShader(unsigned long shaderFlags)
	{
		if (!m_context)
		{
			LOG_ERROR("Material::GetOrCreateShader(): Context is null, can't execute function");
			return weak_ptr<ShaderVariation>();
		}

		// If an appropriate shader already exists, return it's ID
		auto existingShader = FindMatchingShader(shaderFlags);
		if (!existingShader.expired())
			return existingShader;

		// Create and initialize shader
		auto shader = make_shared<ShaderVariation>(m_context);
		shader->Compile(m_context->GetSubsystem<ResourceManager>()->GetStandardResourceDirectory(Resource_Shader) + "GBuffer.hlsl", shaderFlags);
		shader->SetResourceName("ShaderVariation_" + to_string(shader->GetResourceID())); // set a different name for it's shader the cache doesn't thing they are the same

		// Add the shader to the pool and return it
		return shader->Cache<ShaderVariation>();
	}

	const std::vector<void*>& Material::GetShaderResources()
	{
		m_shaderResources.clear();
		m_shaderResources.shrink_to_fit();

		// Must maintain the same order as the way the G-Buffer stage expects them to
		// Note: this is not efficient, have to do it better
		#define GET_SR(type) !m_textures[type].expired() ? m_textures[type].lock()->GetShaderResource() : nullptr
		m_shaderResources.emplace_back(GET_SR(TextureType_Albedo));
		m_shaderResources.emplace_back(GET_SR(TextureType_Roughness));
		m_shaderResources.emplace_back(GET_SR(TextureType_Metallic));
		m_shaderResources.emplace_back(GET_SR(TextureType_Normal));
		m_shaderResources.emplace_back(GET_SR(TextureType_Height));
		m_shaderResources.emplace_back(GET_SR(TextureType_Occlusion));
		m_shaderResources.emplace_back(GET_SR(TextureType_Emission));
		m_shaderResources.emplace_back(GET_SR(TextureType_Mask));

		return m_shaderResources;
	}

	void Material::SetMultiplier(TextureType type, float value)
	{
		if (type == TextureType_Roughness)
		{
			m_roughnessMultiplier = value;
		}
		else if (type == TextureType_Metallic)
		{
			m_metallicMultiplier = value;
		}
		else if (type == TextureType_Normal)
		{
			m_normalMultiplier = value;
		}
		else if (type == TextureType_Height)
		{
			m_heightMultiplier = value;
		}
	}

	//==============================================================================
	void Material::TextureBasedMultiplierAdjustment()
	{
		if (HasTextureOfType(TextureType_Roughness))
		{
			SetRoughnessMultiplier(1.0f);
		}

		if (HasTextureOfType(TextureType_Metallic))
		{
			SetMetallicMultiplier(1.0f);
		}

		if (HasTextureOfType(TextureType_Normal))
		{
			SetNormalMultiplier(1.0f);
		}

		if (HasTextureOfType(TextureType_Height))
		{
			SetHeightMultiplier(1.0f);
		}
	}
}