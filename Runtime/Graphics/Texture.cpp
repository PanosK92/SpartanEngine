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

	static const DXGI_FORMAT apiTextureFormat[]
	{
		DXGI_FORMAT_R32G32B32_FLOAT,
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		DXGI_FORMAT_R8G8B8A8_UNORM,
		DXGI_FORMAT_R8_UNORM
	};

	Texture::Texture(Context* context)
	{
		// Resource
		InitializeResource(Resource_Texture);

		// Texture
		m_context = context;
		m_isUsingMipmaps = true;
		m_textureAPI = make_shared<D3D11Texture>(m_context->GetSubsystem<Graphics>());
		m_isDirty = false;
		m_hasShaderResource = true;
	}

	Texture::Texture(Context* context, unsigned int width, unsigned int height)
	{
		// Resource
		InitializeResource(Resource_Texture);

		m_width = width;
		m_height = height;
		m_isUsingMipmaps = false;	
		m_hasShaderResource = false;
		m_context = context;
		m_isDirty = true;
	}

	Texture::~Texture()
	{
		Clear();
	}

	//= RESOURCE INTERFACE =====================================================================
	bool Texture::SaveToFile(const string& filePath)
	{
		if (!m_isDirty)
			return false;

		m_isDirty = false;
		Serialize(filePath);
		Clear();

		// Serialize data in another thread so it doesn't stall the engine
		/*m_context->GetSubsystem<Threading>()->AddTask([this, &filePath]()
		{
			m_textureInfo->Serialize(filePath);
			m_textureInfo->Clear();
		});*/

		return true;
	}

	bool Texture::LoadFromFile(const string& filePath)
	{
		bool loaded;
		// engine format (binary)
		if (FileSystem::GetExtensionFromFilePath(filePath) == TEXTURE_EXTENSION) 
		{
			loaded = Deserialize(filePath);
		}
		// foreign format (most known image formats)
		else 
		{
			loaded = LoadFromForeignFormat(filePath);
		}

		if (!loaded)
		{
			LOG_ERROR("Texture: Failed to load \"" + filePath + "\".");
			return false;
		}

		// DDS textures load directly as a shader resource, no need to do it here
		if (m_hasShaderResource && FileSystem::GetExtensionFromFilePath(filePath) != ".dds")
		{
			CreateShaderResource();
		}

		m_memoryUsageKB = ComputeMemoryUsageKB();
		m_isDirty = true;
		return true;
	}
	//=====================================================================================

	//= PROPERTIES ========================================================================
	void Texture::SetWidth(unsigned int width)
	{
		m_width = width;
		m_isDirty = true;
	}

	void Texture::SetHeight(unsigned int height)
	{
		m_height = height;
		m_isDirty = true;
	}

	void Texture::SetTextureType(TextureType type)
	{
		m_type = type;

		// Some models (or Assimp) pass a normal map as a height map
		// and others pass a height map as a normal map, we try to fix that.
		if (m_type == TextureType_Height && !GetGrayscale())
		{
			m_type = TextureType_Normal;
		}

		if (m_type == TextureType_Normal && GetGrayscale())
		{
			m_type = TextureType_Height;
		}

		m_isDirty = true;
	}

	void Texture::SetGrayscale(bool grayscale)
	{
		m_isGrayscale = grayscale;
		m_isDirty = true;
	}

	void Texture::SetTransparency(bool transparency)
	{
		m_isTransparent = transparency;
		m_isDirty = true;
	}

	void Texture::SetBPP(unsigned int bpp)
	{
		m_bpp = bpp;
		m_isDirty = true;
	}

	void Texture::SetChannels(unsigned int channels)
	{
		m_channels = channels;
		m_isDirty = true;
	}

	void Texture::SetRGBA(const vector<vector<unsigned char>>& rgba)
	{
		m_rgba = rgba;
		m_isDirty = true;
	}

	void Texture::EnableMimaps(bool enable)
	{
		m_isUsingMipmaps = enable;
		m_isDirty = true;
	}
	//=====================================================================================

	//= SHADER RESOURCE ===================================================================
	void** Texture::GetShaderResource()
	{
		if (!m_textureAPI)
			return nullptr;

		return (void**)m_textureAPI->GetShaderResourceView();
	}

	bool Texture::CreateShaderResource(unsigned int width, unsigned int height, unsigned int channels, vector<unsigned char> rgba, TextureFormat format)
	{
		if (!m_textureAPI->Create(width, height, channels, rgba, apiTextureFormat[format]))
		{
			LOG_ERROR("Texture: Failed to create shader resource for \"" + m_resourceFilePath + "\".");
			return false;
		}

		return true;
	}

	bool Texture::CreateShaderResource()
	{
		if (!m_textureAPI)
		{
			LOG_ERROR("Texture: Failed to create shader resource. API texture not initialized.");
			return false;
		}

		if (!m_isUsingMipmaps)
		{
			if (!m_textureAPI->Create(m_width, m_height, m_channels, m_rgba[0], apiTextureFormat[m_format]))
			{
				LOG_ERROR("Texture: Failed to create shader resource for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}
		else
		{
			if (!m_textureAPI->Create(m_width, m_height, m_channels, m_rgba, apiTextureFormat[m_format]))
			{
				LOG_ERROR("Texture: Failed to create shader resource with mipmaps for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}

		return true;
	}
	//=====================================================================================

	bool Texture::LoadFromForeignFormat(const string& filePath)
	{
		if (filePath == NOT_ASSIGNED)
		{
			LOG_WARNING("Texture: Can't load texture, filepath is unassigned.");
			return false;
		}

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
			m_isDirty = true;
			return true;
		}

		// Load texture
		weak_ptr<ImageImporter> imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter();	
		if (!imageImp._Get()->Load(filePath, this))
		{
			return false;
		}

		// Change texture extension to an engine texture
		SetResourceFilePath(FileSystem::GetFilePathWithoutExtension(filePath) + TEXTURE_EXTENSION);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

		m_isDirty = true;
		return true;
	}

	TextureType Texture::TextureTypeFromString(const string& type)
	{
		if (type == "Albedo") return TextureType_Albedo;
		if (type == "Roughness") return TextureType_Roughness;
		if (type == "Metallic") return TextureType_Metallic;
		if (type == "Normal") return TextureType_Normal;
		if (type == "Height") return TextureType_Height;
		if (type == "Occlusion") return TextureType_Occlusion;
		if (type == "Emission") return TextureType_Emission;
		if (type == "Mask") return TextureType_Mask;
		if (type == "CubeMap") return TextureType_CubeMap;

		return TextureType_Unknown;
	}

	unsigned Texture::ComputeMemoryUsageKB()
	{
		unsigned int memoryKB = 0;

		for (const auto& mip : m_rgba)
		{
			memoryKB += mip.size();
		}

		return memoryKB / 1000;
	}

	bool Texture::Serialize(const string& filePath)
	{
		auto file = make_unique<StreamIO>(filePath, Mode_Write);
		if (!file->IsCreated())
			return false;

		file->Write((int)m_type);
		file->Write(m_bpp);
		file->Write(m_width);
		file->Write(m_height);
		file->Write(m_channels);
		file->Write(m_isGrayscale);
		file->Write(m_isTransparent);
		file->Write(m_isUsingMipmaps);

		file->Write((unsigned int)m_rgba.size());
		for (auto& mip : m_rgba)
		{
			file->Write(mip);
		}

		return true;
	}

	bool Texture::Deserialize(const string& filePath)
	{
		int requestedWidth = m_width;
		int requestedHeight = m_height;
		bool rescaleRequest = (m_width != 0 && m_height != 0);

		SetAsyncState(Async_Started);
		auto file = make_unique<StreamIO>(filePath, Mode_Read);
		if (!file->IsCreated())
		{
			SetAsyncState(Async_Failed);
			return false;
		}

		Clear();

		m_type = (TextureType)file->ReadInt();
		file->Read(m_bpp);
		file->Read(m_width);
		file->Read(m_height);
		file->Read(m_channels);
		file->Read(m_isGrayscale);
		file->Read(m_isTransparent);
		file->Read(m_isUsingMipmaps);

		unsigned int mipCount = file->ReadUInt();
		for (unsigned int i = 0; i < mipCount; i++)
		{
			m_rgba.emplace_back(vector<unsigned char>());
			file->Read(m_rgba[i]);
		}

		// If a size was defined before deserialization, and this texture won't be used for rendering, we rescale here.
		// This can be requested by the editor whenever it needs to quickly inspect some texture.
		if (rescaleRequest && !m_hasShaderResource && !m_rgba.empty() && !m_rgba[0].empty())
		{
			weak_ptr<ImageImporter> imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter();
			imageImp._Get()->RescaleBits(m_rgba[0], m_width, m_height, requestedWidth, requestedHeight);
		}

		SetAsyncState(Async_Completed);
		return true;
	}

	void Texture::Clear()
	{
		m_rgba.clear();
		m_rgba.shrink_to_fit();
	}
}
