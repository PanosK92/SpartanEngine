/*
Copyright(c) 2016-2019 Panos Karabelas

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
#include "Renderer.h"
#include "Deferred/ShaderVariation.h"
#include "../RHI/RHI_Implementation.h"
#include "../Resource/ResourceCache.h"
#include "../IO/XmlDocument.h"
#include "../RHI/RHI_Texture.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Material::Material(Context* context) : IResource(context, Resource_Material)
	{
		// Material
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
		m_rhiDevice				= context->GetSubsystem<Renderer>()->GetRhiDevice();

		AcquireShader();
	}

	Material::~Material()
	{
		m_textureSlots.clear();
		m_textureSlots.shrink_to_fit();
	}

	//= IResource ==============================================
	bool Material::LoadFromFile(const std::string& file_path)
	{
		// Make sure the path is relative
		SetResourceFilePath(FileSystem::GetRelativeFilePath(file_path));

		auto xml = make_unique<XmlDocument>();
		if (!xml->Load(GetResourceFilePath()))
			return false;

		SetResourceName(xml->GetAttributeAs<string>("Material",	"Name"));
		SetResourceFilePath(xml->GetAttributeAs<string>("Material",	"Path"));
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

		const auto textureCount = xml->GetAttributeAs<int>("Textures", "Count");
		for (int i = 0; i < textureCount; i++)
		{
			string nodeName				= "Texture_" + to_string(i);
			const TextureType tex_type	= static_cast<TextureType>(xml->GetAttributeAs<unsigned int>(nodeName, "Texture_Type"));
			auto texName				= xml->GetAttributeAs<string>(nodeName, "Texture_Name");
			auto texPath				= xml->GetAttributeAs<string>(nodeName, "Texture_Path");

			// If the texture happens to be loaded, get a reference to it
			auto texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture>(texName);
			// If there is not texture (it's not loaded yet), load it
			if (!texture)
			{
				texture = m_context->GetSubsystem<ResourceCache>()->Load<RHI_Texture>(texPath);
			}
			SetTextureSlot(tex_type, texture);
		}

		AcquireShader();

		return true;
	}

	bool Material::SaveToFile(const std::string& file_path)
	{
		// Make sure the path is relative
		SetResourceFilePath(FileSystem::GetRelativeFilePath(file_path));

		// Add material extension if not present
		if (FileSystem::GetExtensionFromFilePath(GetResourceFilePath()) != EXTENSION_MATERIAL)
		{
			SetResourceFilePath(GetResourceFilePath() + EXTENSION_MATERIAL);
		}

		auto xml = make_unique<XmlDocument>();
		xml->AddNode("Material");
		xml->AddAttribute("Material", "Name",					GetResourceName());
		xml->AddAttribute("Material", "Path",					GetResourceFilePath());
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
		xml->AddAttribute("Textures", "Count", static_cast<unsigned int>(m_textureSlots.size()));
		int i = 0;
		for (const auto& texture_slot : m_textureSlots)
		{
			string tex_node = "Texture_" + to_string(i);
			xml->AddChildNode("Textures", tex_node);
			xml->AddAttribute(tex_node, "Texture_Type", static_cast<unsigned int>(texture_slot.type));
			xml->AddAttribute(tex_node, "Texture_Name", texture_slot.ptr ? texture_slot.ptr->GetResourceName() : NOT_ASSIGNED);
			xml->AddAttribute(tex_node, "Texture_Path", texture_slot.ptr ? texture_slot.ptr->GetResourceFilePath() : NOT_ASSIGNED);
			i++;
		}

		return xml->Save(GetResourceFilePath());
	}

	const TextureSlot& Material::GetTextureSlotByType(const TextureType type)
	{
		for (const auto& texture_slot : m_textureSlots)
		{
			if (texture_slot.type == type)
				return texture_slot;
		}

		return m_emptyTextureSlot;
	}

	void Material::SetTextureSlot(TextureType type, const shared_ptr<RHI_Texture>& texture)
	{
		if (texture)
		{
			// Some models (or Assimp) pass a normal map as a height map
			// and others pass a height map as a normal map, we try to fix that.
			type =
				(type == TextureType_Normal && texture->GetGrayscale()) ? TextureType_Height :
				(type == TextureType_Height && !texture->GetGrayscale()) ? TextureType_Normal : type;

			// Assign - As a replacement (if there is a previous one)
			bool replaced = false;
			for (auto& textureSlot : m_textureSlots)
			{
				if (textureSlot.type == type)
				{
					textureSlot.ptr = texture;
					replaced = true;
					break;
				}
			}
			// Assign - Add a new one (in case it's the first time the slot is assigned)
			if (!replaced)
			{
				m_textureSlots.emplace_back(type, texture);
			}
		}
		else
		{
			for (auto it = m_textureSlots.begin(); it != m_textureSlots.end();)
			{
				if ((*it).type == type) 
				{
					it = m_textureSlots.erase(it);
				}
				else
				{
					++it;
				}
			}

		}

		TextureBasedMultiplierAdjustment();
		AcquireShader();
	}

	bool Material::HasTexture(TextureType type)
	{
		const auto texture_slot = GetTextureSlotByType(type);
		return texture_slot.ptr != nullptr;
	}

	bool Material::HasTexture(const string& path)
	{
		for (const auto& texture_slot : m_textureSlots)
		{
			if (!texture_slot.ptr)
				continue;

			if (texture_slot.ptr->GetResourceFilePath() == path)
				return true;
		}

		return false;
	}

	string Material::GetTexturePathByType(TextureType type)
	{
		auto textureSlot = GetTextureSlotByType(type);
		return textureSlot.ptr == nullptr ? NOT_ASSIGNED : textureSlot.ptr->GetResourceFilePath();
	}

	vector<string> Material::GetTexturePaths()
	{
		vector<string> paths;
		for (const auto& texture_slot : m_textureSlots)
		{
			if (texture_slot.ptr == nullptr)
				continue;

			paths.emplace_back(texture_slot.ptr->GetResourceFilePath());
		}

		return paths;
	}

	void Material::AcquireShader()
	{
		if (!m_context)
		{
			LOG_ERROR("Context is null, can't execute function");
			return;
		}

		// Add a shader to the pool based on this material, if a 
		// matching shader already exists, it will be returned.
		unsigned long shader_flags = 0;

		if (HasTexture(TextureType_Albedo))		shader_flags	|= Variation_Albedo;
		if (HasTexture(TextureType_Roughness))	shader_flags	|= Variation_Roughness;
		if (HasTexture(TextureType_Metallic))	shader_flags	|= Variation_Metallic;
		if (HasTexture(TextureType_Normal))		shader_flags	|= Variation_Normal;
		if (HasTexture(TextureType_Height))		shader_flags	|= Variation_Height;
		if (HasTexture(TextureType_Occlusion))	shader_flags	|= Variation_Occlusion;
		if (HasTexture(TextureType_Emission))	shader_flags	|= Variation_Emission;
		if (HasTexture(TextureType_Mask))		shader_flags	|= Variation_Mask;

		m_shader = GetOrCreateShader(shader_flags);
	}

	shared_ptr<ShaderVariation> Material::GetOrCreateShader(unsigned long shader_flags)
	{
		if (!m_context)
		{
			LOG_ERROR("Context is null, can't execute function");
			return nullptr;
		}

		// If an appropriate shader already exists, return it instead
		if (auto existingShader = ShaderVariation::GetMatchingShader(shader_flags))
			return existingShader;

		// Create and compile shader
		auto shader = make_shared<ShaderVariation>(m_rhiDevice, m_context);
		shader->Compile(m_context->GetSubsystem<ResourceCache>()->GetStandardResourceDirectory(Resource_Shader) + "GBuffer.hlsl", shader_flags);

		return shader;
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

	void Material::TextureBasedMultiplierAdjustment()
	{
		if (HasTexture(TextureType_Roughness))
		{
			SetRoughnessMultiplier(1.0f);
		}

		if (HasTexture(TextureType_Metallic))
		{
			SetMetallicMultiplier(1.0f);
		}

		if (HasTexture(TextureType_Normal))
		{
			SetNormalMultiplier(1.0f);
		}

		if (HasTexture(TextureType_Height))
		{
			SetHeightMultiplier(1.0f);
		}
	}

	TextureType Material::TextureTypeFromString(const string& type)
	{
		if (type == "Albedo")		return TextureType_Albedo;
		if (type == "Roughness")	return TextureType_Roughness;
		if (type == "Metallic")		return TextureType_Metallic;
		if (type == "Normal")		return TextureType_Normal;
		if (type == "Height")		return TextureType_Height;
		if (type == "Occlusion")	return TextureType_Occlusion;
		if (type == "Emission")		return TextureType_Emission;
		if (type == "Mask")			return TextureType_Mask;

		return TextureType_Unknown;
	}
}