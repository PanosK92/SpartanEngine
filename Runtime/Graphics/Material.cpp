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

//= INCLUDES ===============================
#include "Material.h"
#include "../FileSystem/FileSystem.h"
#include "../Core/Context.h"
#include "../Resource/ResourceManager.h"
#include "DeferredShaders/ShaderVariation.h"
#include "../Logging/Log.h"
#include "../IO/XmlDocument.h"
//==========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Material::Material(Context* context)
	{
		// Resource
		RegisterResource(Resource_Material);

		// Material
		m_context				= context;
		m_modelID				= NOT_ASSIGNED_HASH;
		m_cullMode				= CullBack;
		m_opacity				= 1.0f;
		m_alphaBlending			= false;
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

	//= RESOURCE INTERFACE =====================================
	bool Material::LoadFromFile(const string& filePath)
	{
		// Make sure the path is relative
		SetResourceFilePath(FileSystem::GetRelativeFilePath(filePath));

		unique_ptr<XmlDocument> xml = make_unique<XmlDocument>();
		if (!xml->Load(GetResourceFilePath()))
			return false;

		xml->GetAttribute("Material", "Name", GetResourceName());
		SetResourceFilePath(xml->GetAttributeAsStr("Material", "Path"));
		xml->GetAttribute("Material", "Model_ID", m_modelID);
		m_cullMode = CullMode(xml->GetAttributeAsInt("Material", "Cull_Mode"));
		xml->GetAttribute("Material", "Opacity", m_opacity);
		xml->GetAttribute("Material", "Alpha_Blending", m_alphaBlending);
		m_shadingMode = ShadingMode(xml->GetAttributeAsInt("Material", "Shading_Mode"));
		m_colorAlbedo = xml->GetAttributeAsVector4("Material", "Color");
		xml->GetAttribute("Material", "Roughness_Multiplier", m_roughnessMultiplier);
		xml->GetAttribute("Material", "Metallic_Multiplier", m_metallicMultiplier);
		xml->GetAttribute("Material", "Normal_Multiplier", m_normalMultiplier);
		xml->GetAttribute("Material", "Height_Multiplier", m_heightMultiplier);
		m_uvTiling = xml->GetAttributeAsVector2("Material", "UV_Tiling");
		m_uvOffset = xml->GetAttributeAsVector2("Material", "UV_Offset");
		xml->GetAttribute("Material", "IsEditable", m_isEditable);

		int textureCount = xml->GetAttributeAsInt("Textures", "Count");
		for (int i = 0; i < textureCount; i++)
		{
			string nodeName = "Texture_" + to_string(i);
			TextureType texType = (TextureType)xml->GetAttributeAsInt(nodeName, "Texture_Type");
			string texName = xml->GetAttributeAsStr(nodeName, "Texture_Name");

			// If the texture happens to be loaded, get a reference to it
			auto texture = weak_ptr<Texture>();
			if (m_context)
			{
				texture = m_context->GetSubsystem<ResourceManager>()->GetResourceByName<Texture>(texName);
			}

			m_textures.insert(make_pair(texType, make_pair(texture, texName)));
		}

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

	bool Material::SaveToFile(const string& filePath)
	{
		// Make sure the path is relative
		SetResourceFilePath(FileSystem::GetRelativeFilePath(filePath));

		// Add material extension if not present
		if (FileSystem::GetExtensionFromFilePath(GetResourceFilePath()) != MATERIAL_EXTENSION)
		{
			SetResourceFilePath(GetResourceFilePath() + MATERIAL_EXTENSION);
		}

		unique_ptr<XmlDocument> xml = make_unique<XmlDocument>();
		xml->AddNode("Material");
		xml->AddAttribute("Material", "Name", GetResourceName());
		xml->AddAttribute("Material", "Path", GetResourceFilePath());
		xml->AddAttribute("Material", "Model_ID", m_modelID);
		xml->AddAttribute("Material", "Cull_Mode", int(m_cullMode));
		xml->AddAttribute("Material", "Opacity", m_opacity);
		xml->AddAttribute("Material", "Alpha_Blending", m_alphaBlending);
		xml->AddAttribute("Material", "Shading_Mode", int(m_shadingMode));
		xml->AddAttribute("Material", "Color", m_colorAlbedo);
		xml->AddAttribute("Material", "Roughness_Multiplier", m_roughnessMultiplier);
		xml->AddAttribute("Material", "Metallic_Multiplier", m_metallicMultiplier);
		xml->AddAttribute("Material", "Normal_Multiplier", m_normalMultiplier);
		xml->AddAttribute("Material", "Height_Multiplier", m_heightMultiplier);
		xml->AddAttribute("Material", "UV_Tiling", m_uvTiling);
		xml->AddAttribute("Material", "UV_Offset", m_uvOffset);
		xml->AddAttribute("Material", "IsEditable", m_isEditable);

		xml->AddChildNode("Material", "Textures");
		xml->AddAttribute("Textures", "Count", (int)m_textures.size());
		int i = 0;
		for (const auto& texture : m_textures)
		{
			string texNode = "Texture_" + to_string(i);
			xml->AddChildNode("Textures", texNode);
			xml->AddAttribute(texNode, "Texture_Type", (int)texture.first);
			xml->AddAttribute(texNode, "Texture_Name", texture.second.second);
			i++;
		}

		if (!xml->Save(GetResourceFilePath()))
			return false;

		// If this material is using a shader, save it
		if (!m_shader.expired())
		{
			m_shader._Get()->SetResourceFilePath(FileSystem::GetFilePathWithoutExtension(filePath) + SHADER_EXTENSION);
			m_shader._Get()->SaveToFile(m_shader._Get()->GetResourceFilePath());
		}

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

		TextureType texType = texture._Get()->GetType();
		string texName = texture._Get()->GetResourceName();

		// Check if a texture of that type already exists and replace it
		auto it = m_textures.find(texType);
		if (it != m_textures.end())
		{
			it->second.first = texture;
			it->second.second = texName;
		}
		else
		{
			// If that's a new texture type, simply add it
			m_textures.insert(make_pair(texType, make_pair(texture, texName)));
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

		return (string)NOT_ASSIGNED;
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
		unsigned long shaderFlags = 0;

		if (HasTextureOfType(TextureType_Albedo)) shaderFlags |= Variaton_Albedo;
		if (HasTextureOfType(TextureType_Roughness)) shaderFlags |= Variaton_Roughness;
		if (HasTextureOfType(TextureType_Metallic)) shaderFlags |= Variaton_Metallic;
		if (HasTextureOfType(TextureType_Normal)) shaderFlags |= Variaton_Normal;
		if (HasTextureOfType(TextureType_Height)) shaderFlags |= Variaton_Height;
		if (HasTextureOfType(TextureType_Occlusion)) shaderFlags |= Variaton_Occlusion;
		if (HasTextureOfType(TextureType_Emission)) shaderFlags |= Variaton_Emission;
		if (HasTextureOfType(TextureType_Mask)) shaderFlags |= Variaton_Mask;
		if (HasTextureOfType(TextureType_CubeMap)) shaderFlags |= Variaton_Cubemap;

		m_shader = CreateShaderBasedOnMaterial(shaderFlags);
	}

	weak_ptr<ShaderVariation> Material::FindMatchingShader(unsigned long shaderFlags)
	{
		auto shaders = m_context->GetSubsystem<ResourceManager>()->GetResourcesByType<ShaderVariation>();
		for (const auto& shader : shaders)
		{
			if (shader._Get()->GetShaderFlags() == shaderFlags)
				return shader;
		}
		return weak_ptr<ShaderVariation>();
	}

	weak_ptr<ShaderVariation> Material::CreateShaderBasedOnMaterial(unsigned long shaderFlags)
	{
		// If an appropriate shader already exists, return it's ID
		auto existingShader = FindMatchingShader(shaderFlags);

		if (!existingShader.expired())
			return existingShader;

		// If not, create a new one 
		auto resourceMng = m_context->GetSubsystem<ResourceManager>();
		string shaderDirectory = resourceMng->GetStandardResourceDirectory(Resource_Shader); // Get standard shader directory

		// Create and initialize shader
		auto shader = make_shared<ShaderVariation>();
		shader->SetResourceFilePath(shaderDirectory + "GBuffer.hlsl");
		shader->Initialize(m_context, shaderFlags);

		// A GBuffer shader can exist multiple times in memory because it can have multiple variations.
		// In order to avoid conflicts where the engine thinks it's the same shader, we randomize the
		// path which will automatically create a resource ID based on that path. Hence we make sure that
		// there are no conflicts. A more elegant way to handle this would be nice...
		shader->SetResourceFilePath(FileSystem::GetFilePathWithoutExtension(GetResourceFilePath()) + "_" + GUIDGenerator::GenerateAsStr() + SHADER_EXTENSION);
		shader->SetResourceName("GBuffer_" + to_string(shader->GetResourceID()) + ".hlsl");

		// Add the shader to the pool and return it
		return m_context->GetSubsystem<ResourceManager>()->Add<ShaderVariation>(shader);
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