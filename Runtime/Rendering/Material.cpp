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
#include "ShaderVariation.h"
#include "../Resource/ResourceCache.h"
#include "../IO/XmlDocument.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_TextureCube.h"
#include "../World/World.h"
//====================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
	Material::Material(Context* context) : IResource(context, Resource_Material)
	{
		m_rhi_device = context->GetSubsystem<Renderer>()->GetRhiDevice();

		// Initialize properties
		SetMultiplier(TextureType_Roughness, 0.9f);
		SetMultiplier(TextureType_Metallic, 0.0f);
		SetMultiplier(TextureType_Normal, 0.0f);
		SetMultiplier(TextureType_Height, 0.0f);
		AcquireShader();
		UpdateResourceArray();
	}

	Material::~Material()
	{
		m_textures.clear();
	}

	//= IResource ==============================================
	bool Material::LoadFromFile(const string& file_path)
	{
		auto xml = make_unique<XmlDocument>();
		if (!xml->Load(file_path))
			return false;

		SetResourceFilePath(file_path);

        xml->GetAttribute("Material", "Cull_Mode",              reinterpret_cast<uint32_t*>(&m_cull_mode));
        xml->GetAttribute("Material", "Shading_Mode",           reinterpret_cast<uint32_t*>(&m_shading_mode));
        xml->GetAttribute("Material", "Color",                  &m_color_albedo);
		xml->GetAttribute("Material", "Roughness_Multiplier",	&GetMultiplier(TextureType_Roughness));
		xml->GetAttribute("Material", "Metallic_Multiplier",	&GetMultiplier(TextureType_Metallic));
		xml->GetAttribute("Material", "Normal_Multiplier",		&GetMultiplier(TextureType_Normal));
		xml->GetAttribute("Material", "Height_Multiplier",		&GetMultiplier(TextureType_Height));
		xml->GetAttribute("Material", "IsEditable",				&m_is_editable);
		xml->GetAttribute("Material", "UV_Tiling",				&m_uv_tiling);
		xml->GetAttribute("Material", "UV_Offset",				&m_uv_offset);

		const auto texture_count = xml->GetAttributeAs<int>("Textures", "Count");
		for (auto i = 0; i < texture_count; i++)
		{
			auto node_name		= "Texture_" + to_string(i);
			const auto tex_type	= static_cast<TextureType>(xml->GetAttributeAs<uint32_t>(node_name, "Texture_Type"));
			auto tex_name		= xml->GetAttributeAs<string>(node_name, "Texture_Name");
			auto tex_path		= xml->GetAttributeAs<string>(node_name, "Texture_Path");

			// If the texture happens to be loaded, get a reference to it
			auto texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture2D>(tex_name);
			// If there is not texture (it's not loaded yet), load it
			if (!texture)
			{
				texture = m_context->GetSubsystem<ResourceCache>()->Load<RHI_Texture2D>(tex_path);
			}
			SetTextureSlot(tex_type, texture);
		}

		AcquireShader();

		return true;
	}

	bool Material::SaveToFile(const string& file_path)
	{
        SetResourceFilePath(file_path);

		auto xml = make_unique<XmlDocument>();
		xml->AddNode("Material");
		xml->AddAttribute("Material", "Cull_Mode",				uint32_t(m_cull_mode));	
		xml->AddAttribute("Material", "Shading_Mode",			uint32_t(m_shading_mode));
		xml->AddAttribute("Material", "Color",					m_color_albedo);
		xml->AddAttribute("Material", "Roughness_Multiplier",	GetMultiplier(TextureType_Roughness));
		xml->AddAttribute("Material", "Metallic_Multiplier",	GetMultiplier(TextureType_Metallic));
		xml->AddAttribute("Material", "Normal_Multiplier",		GetMultiplier(TextureType_Normal));
		xml->AddAttribute("Material", "Height_Multiplier",		GetMultiplier(TextureType_Height));
		xml->AddAttribute("Material", "UV_Tiling",				m_uv_tiling);
		xml->AddAttribute("Material", "UV_Offset",				m_uv_offset);
		xml->AddAttribute("Material", "IsEditable",				m_is_editable);

		xml->AddChildNode("Material", "Textures");
		xml->AddAttribute("Textures", "Count", static_cast<uint32_t>(m_textures.size()));
		auto i = 0;
		for (const auto& texture : m_textures)
		{
			auto tex_node = "Texture_" + to_string(i);
			xml->AddChildNode("Textures", tex_node);
			xml->AddAttribute(tex_node, "Texture_Type", static_cast<uint32_t>(texture.first));
			xml->AddAttribute(tex_node, "Texture_Name", texture.second ? texture.second->GetResourceName() : "");
			xml->AddAttribute(tex_node, "Texture_Path", texture.second ? texture.second->GetResourceFilePathNative() : "");
			i++;
		}

		return xml->Save(GetResourceFilePathNative());
	}

	void Material::SetTextureSlot(const TextureType type, const shared_ptr<RHI_Texture>& texture)
	{
		if (texture)
		{
            // In order for the material to guarantee serialization/deserialization we cache the texture
            auto texture_cached = m_context->GetSubsystem<ResourceCache>()->Cache(texture);
			m_textures[type] = texture_cached != nullptr ? texture_cached : texture;
		}
		else
		{
			m_textures.erase(type);
		}

		SetMultiplier(type, 1.0f);
		AcquireShader();
		UpdateResourceArray();
	}

	void Material::SetTextureSlot(const TextureType type, const std::shared_ptr<RHI_Texture2D>& texture)
	{
		SetTextureSlot(type, static_pointer_cast<RHI_Texture>(texture));
	}

	void Material::SetTextureSlot(const TextureType type, const std::shared_ptr<RHI_TextureCube>& texture)
	{
		SetTextureSlot(type, static_pointer_cast<RHI_Texture>(texture));
	}

	bool Material::HasTexture(const string& path)
	{
		for (const auto& texture : m_textures)
		{
			if (!texture.second)
				continue;

			if (texture.second->GetResourceFilePathNative() == path)
				return true;
		}

		return false;
	}

    bool Material::HasTexture(const TextureType type)
    {
        if (m_textures.empty())
            return false;

        auto pair = m_textures.find(type);
        return pair != m_textures.end() && pair->second != nullptr;
    }

    string Material::GetTexturePathByType(const TextureType type)
	{
		if (!HasTexture(type))
			return "";

		return m_textures[type]->GetResourceFilePathNative();
	}

	vector<string> Material::GetTexturePaths()
	{
		vector<string> paths;
		for (const auto& texture : m_textures)
		{
			if (!texture.second)
				continue;

			paths.emplace_back(texture.second->GetResourceFilePathNative());
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

	std::shared_ptr<ShaderVariation> Material::GetOrCreateShader(const unsigned long shader_flags)
	{
		if (!m_context)
		{
			LOG_ERROR("Context is null, can't execute function");
			static shared_ptr<ShaderVariation> empty;
			return empty;
		}

		// If an appropriate shader already exists, return it instead
		if (const auto& existing_shader = ShaderVariation::GetMatchingShader(shader_flags))
			return existing_shader;

		// Create and compile shader
		auto shader = make_shared<ShaderVariation>(m_rhi_device, m_context);
		const auto dir_shaders = m_context->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Shaders) + "/";
		shader->Compile(dir_shaders + "GBuffer.hlsl", shader_flags);

		return shader;
	}

    void Material::SetColorAlbedo(const Math::Vector4& color)
    {
        // If an object switches from opaque to transparent or vice versa, make the world update so that the renderer
        // goes through the entities and makes the ones that use this material, render in the correct mode.
        if ((m_color_albedo.w != 1.0f && color.w == 1.0f) || (m_color_albedo.w == 1.0f && color.w != 1.0f))
        {
            m_context->GetSubsystem<World>()->MakeDirty();
        }

        m_color_albedo = color;
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

	void Material::UpdateResourceArray()
	{
		// They must match the order with which the GBuffer shader defines them
		m_resources[0] = HasTexture(TextureType_Albedo)		? m_textures[TextureType_Albedo]->GetResource_Texture()		: nullptr;
		m_resources[1] = HasTexture(TextureType_Roughness)	? m_textures[TextureType_Roughness]->GetResource_Texture()	: nullptr;
		m_resources[2] = HasTexture(TextureType_Metallic)	? m_textures[TextureType_Metallic]->GetResource_Texture()	: nullptr;
		m_resources[3] = HasTexture(TextureType_Normal)		? m_textures[TextureType_Normal]->GetResource_Texture()		: nullptr;
		m_resources[4] = HasTexture(TextureType_Height)		? m_textures[TextureType_Height]->GetResource_Texture()		: nullptr;
		m_resources[5] = HasTexture(TextureType_Occlusion)	? m_textures[TextureType_Occlusion]->GetResource_Texture()	: nullptr;
		m_resources[6] = HasTexture(TextureType_Emission)	? m_textures[TextureType_Emission]->GetResource_Texture()	: nullptr;
		m_resources[7] = HasTexture(TextureType_Mask)		? m_textures[TextureType_Mask]->GetResource_Texture()		: nullptr;
	}
}
