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
#include "Renderer.h"
#include "Deferred/ShaderVariation.h"
#include "../RHI/RHI_Implementation.h"
#include "../Resource/ResourceManager.h"
#include "../IO/XmlDocument.h"
#include "../RHI/RHI_Texture.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Material::Material(Context* context) : IResource(context, Resource_Material)
	{
		// Material
		m_modelID				= NOT_ASSIGNED_HASH;
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
		m_rhiDevice				= context->GetSubsystem<Renderer>()->GetRHIDevice();

		AcquireShader();
	}

	Material::~Material()
	{
		m_textureSlots.clear();
		m_textureSlots.shrink_to_fit();
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

		auto textureCount = xml->GetAttributeAs<int>("Textures", "Count");
		for (int i = 0; i < textureCount; i++)
		{
			string nodeName		= "Texture_" + to_string(i);
			TextureType texType	= (TextureType)xml->GetAttributeAs<unsigned int>(nodeName, "Texture_Type");
			auto texName		= xml->GetAttributeAs<string>(nodeName, "Texture_Name");
			auto texPath		= xml->GetAttributeAs<string>(nodeName, "Texture_Path");

			// If the texture happens to be loaded, get a reference to it
			auto texture = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<RHI_Texture>(texName);
			// If there is not texture (it's not loaded yet), load it
			if (texture.expired())
			{
				texture = m_context->GetSubsystem<ResourceManager>()->Load<RHI_Texture>(texPath);
			}
			SetTextureSlot(texType, texture);
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
		xml->AddAttribute("Textures", "Count", (unsigned int)m_textureSlots.size());
		int i = 0;
		for (const auto& textureSlot : m_textureSlots)
		{
			string texNode = "Texture_" + to_string(i);
			xml->AddChildNode("Textures", texNode);
			xml->AddAttribute(texNode, "Texture_Type", (unsigned int)textureSlot.type);
			xml->AddAttribute(texNode, "Texture_Name", !textureSlot.ptr_weak.expired() ? textureSlot.ptr_raw->GetResourceName() : NOT_ASSIGNED);
			xml->AddAttribute(texNode, "Texture_Path", !textureSlot.ptr_weak.expired() ? textureSlot.ptr_raw->GetResourceFilePath() : NOT_ASSIGNED);
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
		size += sizeof(TextureSlot) * (int)m_textureSlots.size();

		return size;
	}

	const TextureSlot& Material::GetTextureSlotByType(TextureType type)
	{
		for (const auto& textureSlot : m_textureSlots)
		{
			if (textureSlot.type == type)
				return textureSlot;
		}

		return m_emptyTextureSlot;
	}

	// Set texture from an existing texture
	void Material::SetTextureSlot(TextureType type, const weak_ptr<RHI_Texture>& textureWeak, bool autoCache /* true */)
	{
		// Validate texture
		if (textureWeak.expired())
		{
			LOG_WARNING("Material::SetTexture: Invalid parameter");
			return;
		}

		// Cache it or use the provided reference as is
		auto texRef = autoCache ? textureWeak.lock()->Cache<RHI_Texture>() : textureWeak;

		// Assign - As a replacement (if there is a previous one)
		bool replaced = false;
		for (auto& textureSlot : m_textureSlots)
		{
			if (textureSlot.type == type)
			{
				textureSlot.ptr_weak	= texRef;
				textureSlot.ptr_raw		= texRef.lock().get();
				replaced = true;
				break;
			}
		}
		// Assign - Add a new one (in case it's the first time the slot is assigned)
		if (!replaced)
		{
			m_textureSlots.emplace_back(type, texRef, texRef.lock().get());
		}

		TextureBasedMultiplierAdjustment();
		AcquireShader();
	}

	bool Material::HasTexture(TextureType type)
	{
		auto textureSlot = GetTextureSlotByType(type);
		return !textureSlot.ptr_weak.expired();
	}

	bool Material::HasTexture(const string& path)
	{
		for (const auto& textureSlot : m_textureSlots)
		{
			if (textureSlot.ptr_weak.expired())
				continue;

			if (textureSlot.ptr_raw->GetResourceFilePath() == path)
				return true;
		}

		return false;
	}

	string Material::GetTexturePathByType(TextureType type)
	{
		auto textureSlot = GetTextureSlotByType(type);
		return textureSlot.ptr_weak.expired() ? NOT_ASSIGNED : textureSlot.ptr_raw->GetResourceFilePath();
	}

	vector<string> Material::GetTexturePaths()
	{
		vector<string> paths;
		for (const auto& textureSlot : m_textureSlots)
		{
			if (textureSlot.ptr_weak.expired())
				continue;

			paths.emplace_back(textureSlot.ptr_raw->GetResourceFilePath());
		}

		return paths;
	}

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

		if (HasTexture(TextureType_Albedo))		shaderFlags	|= Variaton_Albedo;
		if (HasTexture(TextureType_Roughness))	shaderFlags	|= Variaton_Roughness;
		if (HasTexture(TextureType_Metallic))	shaderFlags	|= Variaton_Metallic;
		if (HasTexture(TextureType_Normal))		shaderFlags	|= Variaton_Normal;
		if (HasTexture(TextureType_Height))		shaderFlags	|= Variaton_Height;
		if (HasTexture(TextureType_Occlusion))	shaderFlags	|= Variaton_Occlusion;
		if (HasTexture(TextureType_Emission))	shaderFlags	|= Variaton_Emission;
		if (HasTexture(TextureType_Mask))		shaderFlags	|= Variaton_Mask;
		if (HasTexture(TextureType_CubeMap))	shaderFlags	|= Variaton_Cubemap;

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
		auto shader = make_shared<ShaderVariation>(m_rhiDevice, m_context);
		shader->Compile(m_context->GetSubsystem<ResourceManager>()->GetStandardResourceDirectory(Resource_Shader) + "GBuffer.hlsl", shaderFlags);
		shader->SetResourceName("ShaderVariation_" + to_string(shader->Resource_GetID())); // set a different name for it's shader the cache doesn't thing they are the same

		// Add the shader to the pool and return it
		return shader->Cache<ShaderVariation>();
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
}