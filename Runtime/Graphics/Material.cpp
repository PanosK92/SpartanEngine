/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ============================
#include "Material.h"
#include "../Core/GUIDGenerator.h"
#include "../FileSystem/FileSystem.h"
#include "../Core/Context.h"
#include "../Resource/ResourceManager.h"
#include "Shaders/ShaderVariation.h"
#include "../Logging/Log.h"
#include "../IO/XmlDocument.h"
//======================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Material::Material(Context* context)
	{
		// Resource
		m_resourceID = GENERATE_GUID;
		m_resourceType = Material_Resource;

		// Material
		m_context = context;
		m_modelID = DATA_NOT_ASSIGNED;
		m_cullMode = CullBack;
		m_opacity = 1.0f;
		m_alphaBlending = false;
		m_shadingMode = Shading_PBR;
		m_colorAlbedo = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
		m_roughnessMultiplier = 1.0f;
		m_metallicMultiplier = 0.0f;
		m_occlusionMultiplier = 0.0f;
		m_normalMultiplier = 0.0f;
		m_heightMultiplier = 0.0f;
		m_specularMultiplier = 0.5f;
		m_uvTiling = Vector2(1.0f, 1.0f);
		m_uvOffset = Vector2(0.0f, 0.0f);
		m_isEditable = true;

		AcquireShader();
	}

	Material::~Material()
	{

	}

	//= I/O ============================================================
	bool Material::Save(const string& filePath, bool overwrite)
	{
		// Make sure the path is relative
		m_resourceFilePath = FileSystem::GetRelativeFilePath(filePath);

		// Add material extension if not present
		if (FileSystem::GetExtensionFromFilePath(m_resourceFilePath) != MATERIAL_EXTENSION)
		{
			m_resourceFilePath = m_resourceFilePath + MATERIAL_EXTENSION;
		}

		// If the user doesn't want to override and a material
		// indeed happens to exists, there is nothing to do
		if (!overwrite && FileSystem::FileExists(m_resourceFilePath))
			return true;

		XmlDocument::Create();
		XmlDocument::AddNode("Material");
		XmlDocument::AddAttribute("Material", "ID", m_resourceID);
		XmlDocument::AddAttribute("Material", "Name", m_resourceName);
		XmlDocument::AddAttribute("Material", "Path", m_resourceFilePath);
		XmlDocument::AddAttribute("Material", "Model_ID", m_modelID);
		XmlDocument::AddAttribute("Material", "Cull_Mode", int(m_cullMode));
		XmlDocument::AddAttribute("Material", "Opacity", m_opacity);
		XmlDocument::AddAttribute("Material", "Alpha_Blending", m_alphaBlending);
		XmlDocument::AddAttribute("Material", "Shading_Mode", int(m_shadingMode));
		XmlDocument::AddAttribute("Material", "Color", m_colorAlbedo);
		XmlDocument::AddAttribute("Material", "Roughness_Multiplier", m_roughnessMultiplier);
		XmlDocument::AddAttribute("Material", "Metallic_Multiplier", m_metallicMultiplier);
		XmlDocument::AddAttribute("Material", "Normal_Multiplier", m_normalMultiplier);
		XmlDocument::AddAttribute("Material", "Height_Multiplier", m_heightMultiplier);
		XmlDocument::AddAttribute("Material", "Occlusion_Multiplier", m_occlusionMultiplier);
		XmlDocument::AddAttribute("Material", "Specular_Multiplier", m_specularMultiplier);
		XmlDocument::AddAttribute("Material", "UV_Tiling", m_uvTiling);
		XmlDocument::AddAttribute("Material", "UV_Offset", m_uvOffset);
		XmlDocument::AddAttribute("Material", "IsEditable", m_isEditable);

		XmlDocument::AddChildNode("Material", "Textures");
		XmlDocument::AddAttribute("Textures", "Count", (int)m_textures.size());
		int i = 0;
		for (const auto& texture : m_textures)
		{
			string texNode = "Texture_" + to_string(i);
			XmlDocument::AddChildNode("Textures", texNode);
			XmlDocument::AddAttribute(texNode, "Texture_Type", (int)texture.first);
			XmlDocument::AddAttribute(texNode, "Texture_Path", texture.second.second);
			i++;
		}

		if (!XmlDocument::Save(m_resourceFilePath))
			return false;

		XmlDocument::Release();

		return true;
	}

	bool Material::SaveToExistingDirectory()
	{
		if (m_resourceFilePath == DATA_NOT_ASSIGNED)
			return false;

		return Save(FileSystem::GetFilePathWithoutExtension(m_resourceFilePath), true);
	}
	//==================================================================

	//= RESOURCE INTERFACE =====================================
	bool Material::LoadFromFile(const string& filePath)
	{
		// Make sure the path is relative
		m_resourceFilePath = FileSystem::GetRelativeFilePath(filePath);

		if (!XmlDocument::Load(m_resourceFilePath))
			return false;

		XmlDocument::GetAttribute("Material", "ID", m_resourceID);
		XmlDocument::GetAttribute("Material", "Name", m_resourceName);
		XmlDocument::GetAttribute("Material", "Path", m_resourceFilePath);
		XmlDocument::GetAttribute("Material", "Model_ID", m_modelID);
		m_cullMode = CullMode(XmlDocument::GetAttributeAsInt("Material", "Cull_Mode"));
		XmlDocument::GetAttribute("Material", "Opacity", m_opacity);
		XmlDocument::GetAttribute("Material", "Alpha_Blending", m_alphaBlending);
		m_shadingMode = ShadingMode(XmlDocument::GetAttributeAsInt("Material", "Shading_Mode"));
		m_colorAlbedo = XmlDocument::GetAttributeAsVector4("Material", "Color");
		XmlDocument::GetAttribute("Material", "Roughness_Multiplier", m_roughnessMultiplier);
		XmlDocument::GetAttribute("Material", "Metallic_Multiplier", m_metallicMultiplier);
		XmlDocument::GetAttribute("Material", "Normal_Multiplier", m_normalMultiplier);
		XmlDocument::GetAttribute("Material", "Height_Multiplier", m_heightMultiplier);
		XmlDocument::GetAttribute("Material", "Occlusion_Multiplier", m_occlusionMultiplier);
		XmlDocument::GetAttribute("Material", "Specular_Multiplier", m_specularMultiplier);
		m_uvTiling = XmlDocument::GetAttributeAsVector2("Material", "UV_Tiling");
		m_uvOffset = XmlDocument::GetAttributeAsVector2("Material", "UV_Offset");
		XmlDocument::GetAttribute("Material", "IsEditable", m_isEditable);

		int textureCount = XmlDocument::GetAttributeAsInt("Textures", "Count");
		for (int i = 0; i < textureCount; i++)
		{
			string nodeName = "Texture_" + to_string(i);
			TextureType texType = (TextureType)XmlDocument::GetAttributeAsInt(nodeName, "Texture_Type");
			string texPath = XmlDocument::GetAttributeAsStr(nodeName, "Texture_Path");

			// If the texture happens to be loaded, get a reference to it
			auto texture = weak_ptr<Texture>();
			if (m_context)
			{
				texture = m_context->GetSubsystem<ResourceManager>()->GetResourceByPath<Texture>(texPath);
			}

			m_textures.insert(make_pair(texType, make_pair(texture, texPath)));
		}
		XmlDocument::Release();

		// Load unloaded textures
		for (auto& it : m_textures)
		{
			if (it.second.first.expired())
			{
				it.second.first = m_context->GetSubsystem<ResourceManager>()->Load<Texture>(it.second.second);
			}
		}

		AcquireShader();

		return true;
	}
	//==========================================================

	//= TEXTURES ===================================================================
	// Set texture from an existing texture
	void Material::SetTexture(weak_ptr<Texture> texture)
	{
		// Make sure this texture exists
		if (texture.expired())
		{
			LOG_ERROR("Can't set uninitialized material texture.");
			return;
		}

		TextureType type = texture._Get()->GetTextureType();
		string filePath = texture._Get()->GetFilePathTexture();

		// Check if a texture of that type already exists and replace it
		auto it = m_textures.find(type);
		if (it != m_textures.end())
		{
			it->second.first = texture;
			it->second.second = filePath;
		}
		else
		{
			// If that's a new texture type, simply add it
			m_textures.insert(make_pair(type, make_pair(texture, filePath)));
		}

		// Adjust texture multipliers
		TextureBasedMultiplierAdjustment();

		// Acquire and appropriate shader
		AcquireShader();
	}

	weak_ptr<Texture> Material::GetTextureByType(TextureType type)
	{
		for (const auto& it : m_textures)
		{
			if (it.first == type)
			{
				return it.second.first;
			}
		}

		return weak_ptr<Texture>();
	}

	bool Material::HasTextureOfType(TextureType type)
	{
		for (const auto& it : m_textures)
		{
			if (it.first == type)
			{
				return true;
			}
		}

		return false;
	}

	bool Material::HasTexture(const string& path)
	{
		for (const auto& it : m_textures)
		{
			if (it.second.second == path)
			{
				return true;
			}
		}

		return false;
	}

	string Material::GetTexturePathByType(TextureType type)
	{
		for (const auto& it : m_textures)
		{
			if (it.first == type)
			{
				return it.second.second;
			}
		}

		return (string)DATA_NOT_ASSIGNED;
	}

	vector<string> Material::GetTexturePaths()
	{
		vector<string> paths;
		for (auto it : m_textures)
		{
			paths.push_back(it.second.second);
		}

		return paths;
	}
	//==============================================================================

	//= SHADER =====================================================================
	void Material::AcquireShader()
	{
		if (!m_context)
			return;

		// Add a shader to the pool based on this material, if a 
		// matching shader already exists, it will be returned.
		m_shader = CreateShaderBasedOnMaterial(
			HasTextureOfType(Albedo_Texture),
			HasTextureOfType(Roughness_Texture),
			HasTextureOfType(Metallic_Texture),
			HasTextureOfType(Normal_Texture),
			HasTextureOfType(Height_Texture),
			HasTextureOfType(Occlusion_Texture),
			HasTextureOfType(Emission_Texture),
			HasTextureOfType(Mask_Texture),
			HasTextureOfType(CubeMap_Texture)
		);
	}

	weak_ptr<ShaderVariation> Material::FindMatchingShader(
		bool albedo, bool roughness, bool metallic,
		bool normal, bool height, bool occlusion,
		bool emission, bool mask, bool cubemap
	)
	{
		auto shaders = m_context->GetSubsystem<ResourceManager>()->GetResourcesByType<ShaderVariation>();
		for (const auto& shader : shaders)
		{
			if (shader._Get()->HasAlbedoTexture() != albedo) continue;
			if (shader._Get()->HasRoughnessTexture() != roughness) continue;
			if (shader._Get()->HasMetallicTexture() != metallic) continue;
			if (shader._Get()->HasNormalTexture() != normal) continue;
			if (shader._Get()->HasHeightTexture() != height) continue;
			if (shader._Get()->HasOcclusionTexture() != occlusion) continue;
			if (shader._Get()->HasEmissionTexture() != emission) continue;
			if (shader._Get()->HasMaskTexture() != mask) continue;
			if (shader._Get()->HasCubeMapTexture() != cubemap) continue;

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
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		string shaderDirectory = resourceMng->GetResourceDirectory(Shader_Resource); // Get standard shader directory

		// Create and initialize shader
		auto shader = make_shared<ShaderVariation>();
		shader->SetResourceFilePath(shaderDirectory + "GBuffer.hlsl");
		shader->SetResourceName(shader->GetResourceName() + "_" + shader->GetResourceID());
		shader->Initialize(m_context, albedo, roughness, metallic, normal, height, occlusion, emission, mask, cubemap);

		// Add the shader to the pool and return it
		return m_context->GetSubsystem<ResourceManager>()->Add(shader);
	}

	void** Material::GetShaderResource(TextureType type)
	{
		auto texture = GetTextureByType(type);

		if (!texture.expired())
		{
			return texture._Get()->GetShaderResource();
		}

		return nullptr;
	}
	//==============================================================================

	void Material::TextureBasedMultiplierAdjustment()
	{
		if (HasTextureOfType(Roughness_Texture))
		{
			SetRoughnessMultiplier(1.0f);
		}

		if (HasTextureOfType(Metallic_Texture))
		{
			SetMetallicMultiplier(1.0f);
		}

		if (HasTextureOfType(Normal_Texture))
		{
			SetNormalMultiplier(1.0f);
		}

		if (HasTextureOfType(Height_Texture))
		{
			SetHeightMultiplier(1.0f);
		}
	}
}