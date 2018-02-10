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

//= INCLUDES =====================================
#include "Texture.h"
#include "../Logging/Log.h"
#include "../Core/EngineDefs.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Resource/Import/DDSTextureImporter.h"
#include "../Resource/ResourceManager.h"
#include "D3D11/D3D11Texture.h"
#include "../IO/FileStream.h"
#include "../Core/Stopwatch.h"
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
		RegisterResource(Resource_Texture);

		// Texture
		m_context			= context;
		m_isUsingMipmaps	= true;
		m_textureAPI		= make_shared<D3D11Texture>(m_context->GetSubsystem<Graphics>());
	}

	Texture::~Texture()
	{
		ClearTextureBits();
	}

	//= RESOURCE INTERFACE =====================================================================
	bool Texture::SaveToFile(const string& filePath)
	{
		Serialize(filePath); // Async maybe?
		return true;
	}

	bool Texture::LoadFromFile(const string& filePath)
	{
		Stopwatch timer;
		bool loaded = false;
		SetAsyncState(Async_Started);

		// engine format (binary)
		if (FileSystem::IsEngineTextureFile(filePath)) 
		{
			loaded = Deserialize(filePath);
		}
		// foreign format (most known image formats)
		else if (FileSystem::IsSupportedImageFile(filePath))
		{
			loaded = LoadFromForeignFormat(filePath);
		}

		if (!loaded)
		{
			LOG_ERROR("Texture: Failed to load \"" + filePath + "\".");
			SetAsyncState(Async_Failed);
			return false;
		}

		// Create shader resource only if this texture is intended for internal use (by the engine)
		if (m_usage == TextureUsage_Internal)
		{
			// DDS textures load directly as a shader resource, no need to do it here
			if (FileSystem::GetExtensionFromFilePath(filePath) != ".dds")
			{
				if (CreateShaderResource())
				{
					// If the texture was loaded from an image file, it's not 
					// saved yet, hence we have to maintain it's texture bits.
					// However, if the texture was deserialized (engine format) 
					// then we no longer need the texture bits. 
					// We free them here to free up some memory.
					if (FileSystem::IsEngineTextureFile(filePath))
					{
						ClearTextureBits();
					}
				}
			}
		}

		SetAsyncState(Async_Completed);
		LOG_INFO("Texture: Loading \"" + FileSystem::GetFileNameFromFilePath(filePath) + "\" took " + to_string(timer.GetElapsedTimeMs()) + " ms");

		return true;
	}

	unsigned int Texture::GetMemoryUsageKB()
	{
		// Compute texture bits (in case they are loaded)
		unsigned int memoryKB = 0;
		for (const auto& mip : m_textureBits)
		{
			memoryKB += (unsigned int)mip.size();
		}

		// Compute shader resource (in case it's created)
		if (m_textureAPI->GetShaderResourceView())
		{
			GUID guid;
			unsigned int size = 0;
			void* data = nullptr;
			m_textureAPI->GetShaderResourceView()->GetPrivateData(guid, &size, &data);
			memoryKB += sizeof(&data);
		}

		return memoryKB / 1000;
	}

	//=====================================================================================

	//= PROPERTIES =========================================================================
	void Texture::SetType(TextureType type)
	{
		// Some models (or Assimp) pass a normal map as a height map
		// and others pass a height map as a normal map, we try to fix that.
		m_type = (type == TextureType_Normal && GetGrayscale()) ? TextureType_Height : 
				(type == TextureType_Height && !GetGrayscale()) ? TextureType_Normal : type;
	}
	//======================================================================================

	//= SHADER RESOURCE =====================================
	void** Texture::GetShaderResource()
	{
		if (!m_textureAPI)
			return nullptr;

		return (void**)m_textureAPI->GetShaderResourceView();
	}
	//=======================================================

	//= TEXTURE BITS =================================================================
	void Texture::ClearTextureBits()
	{
		for (auto& mip : m_textureBits)
		{
			mip.clear();
			mip.shrink_to_fit();
		}
		m_textureBits.clear();
		m_textureBits.shrink_to_fit();
	}

	void Texture::GetTextureBits(vector<vector<unsigned char>>* textureBits)
	{
		if (!m_textureBits.empty())
		{
			textureBits = &m_textureBits;
			return;
		}

		auto file = make_unique<FileStream>(m_resourceFilePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return;

		unsigned int mipCount = file->ReadUInt();
		for (unsigned int i = 0; i < mipCount; i++)
		{
			textureBits->emplace_back(vector<unsigned char>());
			file->Read(&m_textureBits[i]);
		}
	}
	//================================================================================

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
			if (!m_textureAPI->Create(m_width, m_height, m_channels, m_textureBits[0], apiTextureFormat[m_format]))
			{
				LOG_ERROR("Texture: Failed to create shader resource for \"" + m_resourceFilePath + "\".");
				return false;
			}
		}
		else
		{
			if (!m_textureAPI->Create(m_width, m_height, m_channels, m_textureBits, apiTextureFormat[m_format]))
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
			return true;
		}

		// Load texture
		weak_ptr<ImageImporter> imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter();	
		if (!imageImp.lock()->Load(filePath, this))
		{
			return false;
		}

		// Change texture extension to an engine texture
		SetResourceFilePath(FileSystem::GetFilePathWithoutExtension(filePath) + TEXTURE_EXTENSION);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

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

	bool Texture::Serialize(const string& filePath)
	{
		// If the texture bits has been cleared, load it again
		// as we don't want to replaced existing data with nothing.
		// If the texture bits are not cleared, no loading will take place.
		GetTextureBits(&m_textureBits);

		auto file = make_unique<FileStream>(filePath, FileStreamMode_Write);
		if (!file->IsOpen())
			return false;

		file->Write((unsigned int)m_textureBits.size());
		for (auto& mip : m_textureBits)
		{
			file->Write(mip);
		}
		file->Write((int)m_type);
		file->Write(m_bpp);
		file->Write(m_width);
		file->Write(m_height);
		file->Write(m_channels);
		file->Write(m_isGrayscale);
		file->Write(m_isTransparent);
		file->Write(m_isUsingMipmaps);

		return true;
	}

	bool Texture::Deserialize(const string& filePath)
	{
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		ClearTextureBits();

		unsigned int mipCount = file->ReadUInt();
		for (unsigned int i = 0; i < mipCount; i++)
		{
			m_textureBits.emplace_back(vector<unsigned char>());
			file->Read(&m_textureBits[i]);
		}
		m_type = (TextureType)file->ReadInt();
		file->Read(&m_bpp);
		file->Read(&m_width);
		file->Read(&m_height);
		file->Read(&m_channels);
		file->Read(&m_isGrayscale);
		file->Read(&m_isTransparent);
		file->Read(&m_isUsingMipmaps);

		return true;
	}
}
