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
		InitializeResource(Resource_Texture);
		//===================================

		// Texture
		m_context = context;
		m_isUsingMipmaps = true;
		m_textureAPI = make_shared<D3D11Texture>(m_context->GetSubsystem<Graphics>());
		m_isDirty = false;
	}

	Texture::~Texture()
	{

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

		if (FileSystem::GetExtensionFromFilePath(filePath) == TEXTURE_EXTENSION) // engine format
		{
			loaded = Deserialize(filePath);
		}
		else // foreign format
		{
			loaded = LoadFromForeignFormat(filePath);
		}

		if (!loaded)
		{
			LOG_ERROR("Texture: Failed to load \"" + filePath + "\".");
			return false;
		}

		m_memoryUsageKB = ComputeMemoryUsageKB();
		m_isDirty = true;
		return true;
	}
	//=====================================================================================

	//= SHADER RESOURCE CREATION ==========================================================
	bool Texture::Serialize(const string& filePath)
	{
		auto file = std::make_unique<StreamIO>(filePath, Mode_Write);
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
		auto file = std::make_unique<StreamIO>(filePath, Mode_Read);
		if (!file->IsCreated())
			return false;

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
		m_rgba.reserve(mipCount);
		for (unsigned int i = 0; i < mipCount; i++)
		{
			m_rgba.emplace_back(std::vector<unsigned char>());
			file->Read(m_rgba[i]);
		}

		return true;
	}

	void Texture::Clear()
	{
		m_rgba.clear();
		m_rgba.shrink_to_fit();
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
		if (!m_textureAPI->Create(width, height, channels, rgba, (DXGI_FORMAT)ToAPIFormat(format)))
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
			if (!m_textureAPI->Create(m_width, m_height, m_channels, m_rgba[0], (DXGI_FORMAT)ToAPIFormat(m_format)))
			{
				LOG_ERROR("Texture: Failed to create shader resource for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}
		else
		{
			if (!m_textureAPI->CreateWithMipmaps(m_width, m_height, m_channels, m_rgba, (DXGI_FORMAT)ToAPIFormat(m_format)))
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

		// Extract any metadata we can from the ImageImporter
		SetResourceFilePath(filePath);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

		m_isDirty = true;

		return CreateShaderResource();
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

	int Texture::ToAPIFormat(TextureFormat format)
	{
		if (format == RGBA_8_UNORM) return DXGI_FORMAT_R8G8B8A8_UNORM;
		if (format == RGBA_32_FLOAT) return DXGI_FORMAT_R32G32B32_FLOAT;
		if (format == RGBA_16_FLOAT) return DXGI_FORMAT_R16G16B16A16_FLOAT;
		if (format == R_8_UNORM) return DXGI_FORMAT_R8_UNORM;

		return DXGI_FORMAT_R8G8B8A8_UNORM;
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
}
