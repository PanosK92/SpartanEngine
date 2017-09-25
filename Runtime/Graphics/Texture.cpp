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
		m_generateMipmaps = true;
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

		XmlDocument::Create();
		XmlDocument::AddNode("Metadata");
		XmlDocument::AddAttribute("Metadata", "Type", "Texture");
		XmlDocument::AddChildNode("Metadata", "Texture");
		XmlDocument::AddAttribute("Texture", "Name", GetResourceName());
		XmlDocument::AddAttribute("Texture", "Path", GetResourceFilePath());
		XmlDocument::AddAttribute("Texture", "Width", m_width);
		XmlDocument::AddAttribute("Texture", "Height", m_height);
		XmlDocument::AddAttribute("Texture", "Channels", m_channels);
		XmlDocument::AddAttribute("Texture", "Type", textureTypeChar[(int)m_textureType]);
		XmlDocument::AddAttribute("Texture", "Greyscale", m_grayscale);
		XmlDocument::AddAttribute("Texture", "Transparency", m_transparency);
		XmlDocument::AddAttribute("Texture", "Mipmaps", m_generateMipmaps);

		if (!XmlDocument::Save(savePath))
			return false;

		XmlDocument::Release();

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
		return (void**)m_texture->GetShaderResourceView();
	}

	bool Texture::CreateFromMemory(int width, int height, int channels, unsigned char* buffer, TextureFormat format)
	{
		if (!m_texture)
			return false;

		m_width = width;
		m_height = height;
		m_channels = channels;

		if (!m_texture->Create(m_width, m_height, m_channels, buffer, (DXGI_FORMAT)ToAPIFormat(format)))
		{
			LOG_ERROR("Texture: Failed to create from memory.");
			return false;
		}

		return true;
	}

	bool Texture::CreateFromMemory(int width, int height, int channels, const vector<vector<unsigned char>>& buffer, TextureFormat format)
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
		bool loaded = imageImp._Get()->Load(filePath, m_generateMipmaps);
		if (!loaded)
		{
			LOG_WARNING("Failed to load texture \"" + filePath + "\".");
			imageImp._Get()->Clear();
			return false;
		}

		// Extract any metadata we can from the ImageImporter
		SetResourceFilePath(imageImp._Get()->GetPath());
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));
		m_grayscale = imageImp._Get()->IsGrayscale();
		m_transparency = imageImp._Get()->IsTransparent();

		// Create the texture
		if (!m_generateMipmaps)
		{
			CreateFromMemory(imageImp._Get()->GetWidth(), imageImp._Get()->GetHeight(), imageImp._Get()->GetChannels(), imageImp._Get()->GetRGBA(), RGBA_8_UNORM);
		}
		else
		{
			CreateFromMemory(imageImp._Get()->GetWidth(), imageImp._Get()->GetHeight(), imageImp._Get()->GetChannels(), imageImp._Get()->GetRGBAMipChain(), RGBA_8_UNORM);
		}

		// Free any memory allocated by the ImageImporter
		imageImp._Get()->Clear();

		// Save metadata file
		if (!SaveToFile(GetResourceFilePath() + METADATA_EXTENSION))
			return false;

		return true;
	}

	bool Texture::LoadMetadata(const string& filePath)
	{
		if (!XmlDocument::Load(filePath))
			return false;

		XmlDocument::GetAttribute("Texture", "Name", GetResourceName());
		SetResourceFilePath(XmlDocument::GetAttributeAsStr("Texture", "Path"));
		XmlDocument::GetAttribute("Texture", "Width", m_width);
		XmlDocument::GetAttribute("Texture", "Height", m_height);
		XmlDocument::GetAttribute("Texture", "Channels", m_channels);

		string type;
		XmlDocument::GetAttribute("Texture", "Type", type);
		m_textureType = TextureTypeFromString(type);

		XmlDocument::GetAttribute("Texture", "Greyscale", m_grayscale);
		XmlDocument::GetAttribute("Texture", "Transparency", m_transparency);
		XmlDocument::GetAttribute("Texture", "Mipmaps", m_generateMipmaps);

		XmlDocument::Release();

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
