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
#include "../IO/StreamIO.h"
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
		m_textureInfo = make_unique<TextureInfo>();
		m_textureInfo->isUsingMipmaps = true;
		m_textureAPI = make_unique<D3D11Texture>(m_context->GetSubsystem<Graphics>());
	}

	Texture::~Texture()
	{

	}

	//= RESOURCE INTERFACE =====================================================================
	bool Texture::SaveToFile(const string& filePath)
	{
		SetResourceFilePath(filePath);
		return Serialize(filePath);
	}

	bool Texture::LoadFromFile(const string& filePath)
	{
		bool loaded = false;
		bool engineFormat = FileSystem::GetExtensionFromFilePath(filePath) == TEXTURE_EXTENSION;
		if (engineFormat)
		{
			loaded = Deserialize(filePath);
		}
		else
		{
			loaded = LoadFromForeignFormat(filePath);
		}

		if (!loaded)
		{
			LOG_ERROR("Texture: Failed to load \"" + filePath + "\".");
			return false;
		}

		CreateShaderResource(m_textureInfo.get());
		return true;
	}
	//==========================================================================================

	void Texture::SetTextureType(TextureType type)
	{
		m_type = type;

		// Some models (or Assimp) pass a normal map as a height map
		// and others pass a height map as a normal map, we try to fix that.
		if (m_type == Height_Texture && !GetGrayscale())
		{
			m_type = Normal_Texture;
		}

		if (m_type == Normal_Texture && GetGrayscale())
		{
			m_type = Height_Texture;
		}
	}

	void** Texture::GetShaderResource()
	{
		if (!m_textureAPI)
			return nullptr;

		return (void**)m_textureAPI->GetShaderResourceView();
	}

	bool Texture::CreateShaderResource(unsigned width, unsigned height, unsigned channels, vector<unsigned char> rgba, TextureFormat format)
	{
		return m_textureAPI->Create(width, height, channels, rgba, (DXGI_FORMAT)ToAPIFormat(format));
	}

	bool Texture::CreateShaderResource(TextureInfo* texInfo)
	{
		if (!m_textureAPI)
		{
			LOG_ERROR("Texture: Failed to create shader resource. API texture not initialized.");
			return false;
		}

		if (!texInfo->isUsingMipmaps)
		{
			if (!m_textureAPI->Create(texInfo->width, texInfo->height, texInfo->channels, texInfo->rgba, (DXGI_FORMAT)ToAPIFormat(m_format)))
			{
				LOG_ERROR("Texture: Failed to create shader resource for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}
		else
		{
			if (!m_textureAPI->CreateWithMipmaps(texInfo->width, texInfo->height, texInfo->channels, texInfo->rgba_mimaps, (DXGI_FORMAT)ToAPIFormat(m_format)))
			{
				LOG_ERROR("Texture: Failed to create shader resource with mipmaps for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}

		return true;
	}

	bool Texture::Serialize(const string& filePath)
	{
		auto file = make_unique<StreamIO>(filePath, Mode_Write);
		if (!file->IsCreated())
			return false;

		file->Write((int)m_type);
		file->Write(m_textureInfo->bpp);
		file->Write(m_textureInfo->width);
		file->Write(m_textureInfo->height);
		file->Write(m_textureInfo->channels);
		file->Write(m_textureInfo->isGrayscale);
		file->Write(m_textureInfo->isTransparent);
		file->Write(m_textureInfo->isUsingMipmaps);
		if (!m_textureInfo->isUsingMipmaps)
		{
			file->Write(m_textureInfo->rgba);
		}
		else
		{
			file->Write((unsigned int)m_textureInfo->rgba_mimaps.size());
			for (auto& mip : m_textureInfo->rgba_mimaps)
			{
				file->Write(mip);
			}
		}

		return true;
	}

	bool Texture::Deserialize(const string& filePath)
	{
		auto file = make_unique<StreamIO>(filePath, Mode_Read);
		if (!file->IsCreated())
			return false;

		m_type = (TextureType)file->ReadInt();
		file->Read(m_textureInfo->bpp);
		file->Read(m_textureInfo->width);
		file->Read(m_textureInfo->height);
		file->Read(m_textureInfo->channels);
		file->Read(m_textureInfo->isGrayscale);
		file->Read(m_textureInfo->isTransparent);
		file->Read(m_textureInfo->isUsingMipmaps);
		if (!m_textureInfo->isUsingMipmaps)
		{
			file->Read(m_textureInfo->rgba);
		}
		else
		{
			int mipCount = file->ReadUInt();
			for (int i = 0; i < mipCount; i++)
			{
				m_textureInfo->rgba_mimaps.emplace_back(vector<unsigned char>());
				file->Read(m_textureInfo->rgba_mimaps[i]);
			}
		}

		return true;
	}

	bool Texture::LoadFromForeignFormat(const string& filePath)
	{
		// Load DDS (too bored to implement dds cubemap support in the ImageImporter)
		if (FileSystem::GetExtensionFromFilePath(filePath) == ".dds")
		{
			auto graphicsDevice = m_context->GetSubsystem<Graphics>()->GetDevice();
			if (!graphicsDevice)
				return false;

			ID3D11ShaderResourceView* ddsTex = nullptr;
			wstring widestr = wstring(filePath.begin(), filePath.end());
			auto hresult = DirectX::CreateDDSTextureFromFile(graphicsDevice, widestr.c_str(), nullptr, &ddsTex);
			if (FAILED(hresult))
			{
				return false;
			}

			m_textureAPI->SetShaderResourceView(ddsTex);
			return true;
		}

		// Load texture
		weak_ptr<ImageImporter> imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter();	
		if (!imageImp._Get()->Load(filePath, *m_textureInfo.get()))
		{
			return false;
		}

		// Extract any metadata we can from the ImageImporter
		SetResourceFilePath(filePath);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

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
