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

//= LINKING ==========================
// Required by DDSTextureLoader when using Windows 10 SDK
//#pragma comment(lib, "WindowsApp.lib")
//====================================


//= INCLUDES =====================================
#include "Texture.h"
#include "../Logging/Log.h"
#include "../Core/Helper.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Resource/Import/DDSTextureImporter.h"
#include "../Resource/ResourceManager.h"
#include "D3D11/D3D11Texture.h"
#include "../IO/XmlDocument.h"
//================================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	static const char* textureTypeChar[] =
	{
		"Unknown",
		"Albedo",
		"Roughness",
		"Metallic",
		"Normal",
		"Height",
		"Occlusion",
		"Emission",
		"Mask",
		"CubeMap",
	};

	Texture::Texture(Context* context)
	{
		//= RESOURCE INTERFACE ==============
		InitializeResource(Texture_Resource);
		//===================================

		// Texture
		m_context = context;
		m_width = 0;
		m_height = 0;
		m_channels = 0;
		m_textureType = Unknown_Texture;
		m_grayscale = false;
		m_transparency = false;
		m_alphaIsTransparency = false;
		m_mimaps = true;
		m_texture = make_unique<D3D11Texture>(m_context->GetSubsystem<Graphics>());
	}

	Texture::~Texture()
	{

	}

	//= RESOURCE INTERFACE =======================================================================
	bool Texture::SaveToFile(const string& filePath)
	{
		string savePath = filePath;
		if (filePath == RESOURCE_SAVE)
		{
			savePath = GetResourceFilePath() + METADATA_EXTENSION;
		}

		unique_ptr<XmlDocument> xml = make_unique<XmlDocument>();
		xml->AddNode("Metadata");
		xml->AddAttribute("Metadata", "Type", "Texture");
		xml->AddChildNode("Metadata", "Texture");
		xml->AddAttribute("Texture", "Name", GetResourceName());
		xml->AddAttribute("Texture", "Path", GetResourceFilePath());
		xml->AddAttribute("Texture", "Width", m_width);
		xml->AddAttribute("Texture", "Height", m_height);
		xml->AddAttribute("Texture", "Channels", m_channels);
		xml->AddAttribute("Texture", "Type", textureTypeChar[(int)m_textureType]);
		xml->AddAttribute("Texture", "Greyscale", m_grayscale);
		xml->AddAttribute("Texture", "Transparency", m_transparency);
		xml->AddAttribute("Texture", "Mipmaps", m_mimaps);

		if (!xml->Save(savePath))
			return false;

		return true;
	}

	bool Texture::LoadFromFile(const string& filePath)
	{
		bool engineFormat = FileSystem::GetExtensionFromFilePath(filePath) == METADATA_EXTENSION;
		return engineFormat ? LoadMetadata(filePath) : LoadFromForeignFormat(filePath);
	}
	//==============================================================================================

	void Texture::SetTextureType(TextureType type)
	{
		m_textureType = type;

		// Some models (or Assimp) pass a normal map as a height map
		// and others pass a height map as a normal map, we try to fix that.
		if (m_textureType == Height_Texture && !GetGrayscale())
		{
			m_textureType = Normal_Texture;
		}

		if (m_textureType == Normal_Texture && GetGrayscale())
		{
			m_textureType = Height_Texture;
		}
	}

	void** Texture::GetShaderResource()
	{
		if (!m_texture)
			return nullptr;

		return (void**)m_texture->GetShaderResourceView();
	}

	bool Texture::CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, vector<unsigned char>& buffer, TextureFormat format)
	{
		if (!m_texture)
			return false;

		m_width = width;
		m_height = height;
		m_channels = channels;

		if (!m_texture->Create(m_width, m_height, m_channels, &buffer[0], (DXGI_FORMAT)ToAPIFormat(format)))
		{
			LOG_ERROR("Texture: Failed to create from memory.");
			return false;
		}

		return true;
	}

	bool Texture::CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, const vector<vector<unsigned char>>& buffer, TextureFormat format)
	{
		if (!m_texture)
			return false;

		m_width = width;
		m_height = height;
		m_channels = channels;

		if (!m_texture->CreateFromMipmaps(m_width, m_height, m_channels, buffer, (DXGI_FORMAT)ToAPIFormat(format)))
		{
			LOG_ERROR("Texture: Failed to create from memory.");
			return false;
		}

		return true;
	}

	bool Texture::LoadFromForeignFormat(const string& filePath)
	{
		auto graphicsDevice = m_context->GetSubsystem<Graphics>()->GetDevice();
		if (!graphicsDevice)
			return false;

		// Load DDS (too bored to implement dds cubemap support in the ImageImporter)
		if (FileSystem::GetExtensionFromFilePath(filePath) == ".dds")
		{
			ID3D11ShaderResourceView* ddsTex = nullptr;
			wstring widestr = wstring(filePath.begin(), filePath.end());
			auto hresult = DirectX::CreateDDSTextureFromFile(graphicsDevice, widestr.c_str(), nullptr, &ddsTex);
			if (FAILED(hresult))
			{
				LOG_WARNING("Failed to load texture \"" + filePath + "\".");
				return false;
			}

			m_texture->SetShaderResourceView(ddsTex);
			return true;
		}

		// Load texture
		auto imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter();
		ImageData imageData = ImageData(filePath, m_mimaps);
		bool loaded = imageImp._Get()->Load(imageData);
		if (!loaded)
		{
			LOG_WARNING("Failed to load texture \"" + filePath + "\".");
			return false;
		}

		// Extract any metadata we can from the ImageImporter
		SetResourceFilePath(imageData.filePath);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));
		m_grayscale = imageData.isGrayscale;
		m_transparency = imageData.isTransparent;

		// Create the texture
		if (!m_mimaps)
		{
			CreateShaderResource(imageData.width, imageData.height, imageData.channels, imageData.rgba, RGBA_8_UNORM);
		}
		else
		{
			CreateShaderResource(imageData.width, imageData.height, imageData.channels, imageData.rgba_mimaps, RGBA_8_UNORM);
		}

		// Save metadata file
		if (!SaveToFile(GetResourceFilePath() + METADATA_EXTENSION))
			return false;

		return true;
	}

	bool Texture::LoadMetadata(const string& filePath)
	{
		unique_ptr<XmlDocument> xml = make_unique<XmlDocument>();

		if (!xml->Load(filePath))
			return false;

		xml->GetAttribute("Texture", "Name", GetResourceName());
		SetResourceFilePath(xml->GetAttributeAsStr("Texture", "Path"));
		xml->GetAttribute("Texture", "Width", m_width);
		xml->GetAttribute("Texture", "Height", m_height);
		xml->GetAttribute("Texture", "Channels", m_channels);

		string type;
		xml->GetAttribute("Texture", "Type", type);
		m_textureType = TextureTypeFromString(type);

		xml->GetAttribute("Texture", "Greyscale", m_grayscale);
		xml->GetAttribute("Texture", "Transparency", m_transparency);
		xml->GetAttribute("Texture", "Mipmaps", m_mimaps);

		return true;
	}

	TextureType Texture::TextureTypeFromString(const string& type)
	{
		if (type == "Albedo") return Albedo_Texture;
		if (type == "Roughness") return Roughness_Texture;
		if (type == "Metallic") return Metallic_Texture;
		if (type == "Normal") return Normal_Texture;
		if (type == "Height") return Height_Texture;
		if (type == "Occlusion") return Occlusion_Texture;
		if (type == "Emission") return Emission_Texture;
		if (type == "Mask") return Mask_Texture;
		if (type == "CubeMap") return CubeMap_Texture;

		return Unknown_Texture;
	}

	int Texture::ToAPIFormat(TextureFormat format)
	{
		if (format == RGBA_8_UNORM) return DXGI_FORMAT_R8G8B8A8_UNORM;
		if (format == RGBA_32_FLOAT) return DXGI_FORMAT_R32G32B32_FLOAT;
		if (format == RGBA_16_FLOAT) return DXGI_FORMAT_R16G16B16A16_FLOAT;
		if (format == R_8_UNORM) return DXGI_FORMAT_R8_UNORM;

		return DXGI_FORMAT_R8G8B8A8_UNORM;
	}
}
