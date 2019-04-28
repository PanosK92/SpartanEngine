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
#include "../Resource/ResourceCache.h"
#include "../IO/XmlDocument.h"
#include "../RHI/RHI_ConstantBuffer.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
	Material::Material(Context* context) : IResource(context, Resource_Material)
	{
		// Material
		m_cull_mode				= Cull_Back;
		m_shading_mode			= Shading_PBR;
		m_color_albedo			= Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		m_roughness_multiplier	= 1.0f;
		m_metallic_multiplier	= 0.0f;
		m_normal_multiplier		= 0.0f;
		m_height_multiplier		= 0.0f;
		m_uv_tiling				= Vector2(1.0f, 1.0f);
		m_uv_offset				= Vector2(0.0f, 0.0f);
		m_is_editable			= true;
		m_rhi_device			= context->GetSubsystem<Renderer>()->GetRhiDevice();

		AcquireShader();
	}

	Material::~Material()
	{
		m_texture_slots.clear();
		m_texture_slots.shrink_to_fit();
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
		xml->GetAttribute("Material", "Roughness_Multiplier",	&m_roughness_multiplier);
		xml->GetAttribute("Material", "Metallic_Multiplier",	&m_metallic_multiplier);
		xml->GetAttribute("Material", "Normal_Multiplier",		&m_normal_multiplier);
		xml->GetAttribute("Material", "Height_Multiplier",		&m_height_multiplier);
		xml->GetAttribute("Material", "IsEditable",				&m_is_editable);
		xml->GetAttribute("Material", "Cull_Mode",				(unsigned int*)&m_cull_mode);
		xml->GetAttribute("Material", "Shading_Mode",			(unsigned int*)&m_shading_mode);
		xml->GetAttribute("Material", "Color",					&m_color_albedo);
		xml->GetAttribute("Material", "UV_Tiling",				&m_uv_tiling);
		xml->GetAttribute("Material", "UV_Offset",				&m_uv_offset);

		const auto texture_count = xml->GetAttributeAs<int>("Textures", "Count");
		for (auto i = 0; i < texture_count; i++)
		{
			auto node_name		= "Texture_" + to_string(i);
			const auto tex_type	= static_cast<TextureType>(xml->GetAttributeAs<unsigned int>(node_name, "Texture_Type"));
			auto tex_name		= xml->GetAttributeAs<string>(node_name, "Texture_Name");
			auto tex_path		= xml->GetAttributeAs<string>(node_name, "Texture_Path");

			// If the texture happens to be loaded, get a reference to it
			auto texture = m_context->GetSubsystem<ResourceCache>()->GetByName<RHI_Texture>(tex_name);
			// If there is not texture (it's not loaded yet), load it
			if (!texture)
			{
				texture = m_context->GetSubsystem<ResourceCache>()->Load<RHI_Texture>(tex_path);
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
		xml->AddAttribute("Material", "Cull_Mode",				unsigned int(m_cull_mode));	
		xml->AddAttribute("Material", "Shading_Mode",			unsigned int(m_shading_mode));
		xml->AddAttribute("Material", "Color",					m_color_albedo);
		xml->AddAttribute("Material", "Roughness_Multiplier",	m_roughness_multiplier);
		xml->AddAttribute("Material", "Metallic_Multiplier",	m_metallic_multiplier);
		xml->AddAttribute("Material", "Normal_Multiplier",		m_normal_multiplier);
		xml->AddAttribute("Material", "Height_Multiplier",		m_height_multiplier);
		xml->AddAttribute("Material", "UV_Tiling",				m_uv_tiling);
		xml->AddAttribute("Material", "UV_Offset",				m_uv_offset);
		xml->AddAttribute("Material", "IsEditable",				m_is_editable);

		xml->AddChildNode("Material", "Textures");
		xml->AddAttribute("Textures", "Count", static_cast<unsigned int>(m_texture_slots.size()));
		auto i = 0;
		for (const auto& texture_slot : m_texture_slots)
		{
			auto tex_node = "Texture_" + to_string(i);
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
		for (const auto& texture_slot : m_texture_slots)
		{
			if (texture_slot.type == type)
				return texture_slot;
		}

		return m_empty_texture_slot;
	}

	void* Material::GetTextureShaderResourceByType(TextureType type)
	{
		for (const auto& texture_slot : m_texture_slots)
		{
			if (texture_slot.type == type)
				return texture_slot.ptr ? texture_slot.ptr->GetResource() : nullptr;
		}

		return nullptr;
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
			auto replaced = false;
			for (auto& texture_slot : m_texture_slots)
			{
				if (texture_slot.type == type)
				{
					texture_slot.ptr = texture;
					replaced = true;
					break;
				}
			}
			// Assign - Add a new one (in case it's the first time the slot is assigned)
			if (!replaced)
			{
				m_texture_slots.emplace_back(type, texture);
			}
		}
		else
		{
			for (auto it = m_texture_slots.begin(); it != m_texture_slots.end();)
			{
				if ((*it).type == type) 
				{
					it = m_texture_slots.erase(it);
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

	bool Material::HasTexture(const TextureType type)
	{
		const auto texture_slot = GetTextureSlotByType(type);
		return texture_slot.ptr != nullptr;
	}

	bool Material::HasTexture(const string& path)
	{
		for (const auto& texture_slot : m_texture_slots)
		{
			if (!texture_slot.ptr)
				continue;

			if (texture_slot.ptr->GetResourceFilePath() == path)
				return true;
		}

		return false;
	}

	string Material::GetTexturePathByType(const TextureType type)
	{
		const auto texture_slot = GetTextureSlotByType(type);
		return texture_slot.ptr == nullptr ? NOT_ASSIGNED : texture_slot.ptr->GetResourceFilePath();
	}

	vector<string> Material::GetTexturePaths()
	{
		vector<string> paths;
		for (const auto& texture_slot : m_texture_slots)
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

	shared_ptr<ShaderVariation> Material::GetOrCreateShader(const unsigned long shader_flags)
	{
		if (!m_context)
		{
			LOG_ERROR("Context is null, can't execute function");
			return nullptr;
		}

		// If an appropriate shader already exists, return it instead
		if (auto existing_shader = ShaderVariation::GetMatchingShader(shader_flags))
			return existing_shader;

		// Create and compile shader
		auto shader = make_shared<ShaderVariation>(m_rhi_device, m_context);
		const auto dir_shaders = m_context->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Shaders);
		shader->Compile(dir_shaders + "GBuffer.hlsl", shader_flags);

		return shader;
	}

	void Material::SetMultiplier(const TextureType type, const float value)
	{
		if (type == TextureType_Roughness)
		{
			m_roughness_multiplier = value;
		}
		else if (type == TextureType_Metallic)
		{
			m_metallic_multiplier = value;
		}
		else if (type == TextureType_Normal)
		{
			m_normal_multiplier = value;
		}
		else if (type == TextureType_Height)
		{
			m_height_multiplier = value;
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

	void Material::UpdateConstantBuffer()
	{
		// Has to match GBuffer.hlsl
		if (!m_constant_buffer_gpu)
		{
			m_constant_buffer_gpu = make_shared<RHI_ConstantBuffer>(m_rhi_device);
			m_constant_buffer_gpu->Create<ConstantBufferData>();
		}

		// Determine if the buffer needs to update
		auto update = false;
		update = m_constant_buffer_cpu.mat_albedo			!= GetColorAlbedo() 		? true : update;
		update = m_constant_buffer_cpu.mat_tiling_uv		!= GetTiling()				? true : update;
		update = m_constant_buffer_cpu.mat_offset_uv		!= GetOffset()				? true : update;
		update = m_constant_buffer_cpu.mat_roughness_mul	!= GetRoughnessMultiplier() ? true : update;
		update = m_constant_buffer_cpu.mat_metallic_mul		!= GetMetallicMultiplier()	? true : update;
		update = m_constant_buffer_cpu.mat_normal_mul		!= GetNormalMultiplier()	? true : update;
		update = m_constant_buffer_cpu.mat_shading_mode		!= float(GetShadingMode())	? true : update;

		if (!update)
			return;

		auto buffer = static_cast<ConstantBufferData*>(m_constant_buffer_gpu->Map());

		buffer->mat_albedo			= m_constant_buffer_cpu.mat_albedo			= GetColorAlbedo();
		buffer->mat_tiling_uv		= m_constant_buffer_cpu.mat_tiling_uv		= GetTiling();
		buffer->mat_offset_uv		= m_constant_buffer_cpu.mat_offset_uv		= GetOffset();
		buffer->mat_roughness_mul	= m_constant_buffer_cpu.mat_roughness_mul	= GetRoughnessMultiplier();
		buffer->mat_metallic_mul	= m_constant_buffer_cpu.mat_metallic_mul	= GetMetallicMultiplier();
		buffer->mat_normal_mul		= m_constant_buffer_cpu.mat_normal_mul		= GetNormalMultiplier();
		buffer->mat_height_mul		= m_constant_buffer_cpu.mat_normal_mul		= GetHeightMultiplier();
		buffer->mat_shading_mode	= m_constant_buffer_cpu.mat_shading_mode	= float(GetShadingMode());
		buffer->padding				= m_constant_buffer_cpu.padding				= Vector3::Zero;

		m_constant_buffer_gpu->Unmap();
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