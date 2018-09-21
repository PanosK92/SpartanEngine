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

//= INCLUDES =====================
#include "RHI_Texture.h"
#include "RHI_Device.h"
#include "../IO/FileStream.h"
#include "../Rendering/Renderer.h"
//================================

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

	RHI_Texture::RHI_Texture(Context* context) : IResource(context)
	{
		//= IResource ==================
		RegisterResource<RHI_Texture>();
		//==============================

		m_isUsingMipmaps	= true;
		m_format			= Texture_Format_R8G8B8A8_UNORM;
		m_rhiDevice			= context->GetSubsystem<Renderer>()->GetRHIDevice();
		m_shaderResource	= nullptr;
		m_memoryUsage		= 0;
	}

	//= RESOURCE INTERFACE =====================================================================
	bool RHI_Texture::SaveToFile(const string& filePath)
	{
		return Serialize(filePath);
	}

	bool RHI_Texture::LoadFromFile(const string& rawFilePath)
	{
		bool loaded = false;
		m_dataRGBA.clear();
		m_dataRGBA.shrink_to_fit();
		GetLoadState(LoadState_Started);

		// Make the path, relative to the engine
		auto filePath = FileSystem::GetRelativeFilePath(rawFilePath);

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
			LOGF_ERROR("RI_Texture::LoadFromFile: Failed to load \"%s\".", filePath.c_str());
			GetLoadState(LoadState_Failed);
			return false;
		}

		bool generateMipmaps = !m_isUsingMipmaps;
		if (ShaderResource_Create2D(m_width, m_height, m_channels, m_format, m_dataRGBA, generateMipmaps))
		{
			// If the texture was loaded from an image file, it's not 
			// saved yet, hence we have to maintain it's texture bits.
			// However, if the texture was deserialized (engine format) 
			// then we no longer need the texture bits. 
			// We free them here to free up some memory.
			if (FileSystem::IsEngineTextureFile(filePath))
			{
				ClearTextureBytes();
			}
		}
		else
		{
			LOGF_ERROR("RHI_Texture::LoadFromFile: Failed to create shader resource for \"%s\".", m_resourceFilePath.c_str());
		}

		GetLoadState(LoadState_Completed);
		return true;
	}

	unsigned int RHI_Texture::GetMemoryUsage()
	{
		// Compute texture bits (in case they are loaded)
		unsigned int size = 0;
		for (const auto& mip : m_dataRGBA)
		{
			size += (unsigned int)mip.size();
		}

		return size;
	}
	//=====================================================================================

	//= PROPERTIES =========================================================================
	void RHI_Texture::SetTextureType(TextureType type)
	{
		// Some models (or Assimp) pass a normal map as a height map
		// and others pass a height map as a normal map, we try to fix that.
		m_type = (type == TextureType_Normal && GetGrayscale()) ? TextureType_Height : 
				(type == TextureType_Height && !GetGrayscale()) ? TextureType_Normal : type;
	}
	//======================================================================================

	//= TEXTURE BITS =================================================================
	void RHI_Texture::ClearTextureBytes()
	{
		for (auto& mip : m_dataRGBA)
		{
			mip.clear();
			mip.shrink_to_fit();
		}
		m_dataRGBA.clear();
		m_dataRGBA.shrink_to_fit();
	}

	void RHI_Texture::GetTextureBytes(vector<vector<std::byte>>* textureBytes)
	{
		if (!m_dataRGBA.empty())
		{
			textureBytes = &m_dataRGBA;
			return;
		}

		auto file = make_unique<FileStream>(m_resourceFilePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return;

		unsigned int mipCount = file->ReadUInt();
		for (unsigned int i = 0; i < mipCount; i++)
		{
			textureBytes->emplace_back(vector<std::byte>());
			file->Read(&m_dataRGBA[i]);
		}
	}
	//================================================================================

	//=====================================================================================
	bool RHI_Texture::LoadFromForeignFormat(const string& filePath)
	{
		if (filePath == NOT_ASSIGNED)
		{
			LOG_WARNING("RHI_Texture::LoadFromForeignFormat: Can't load texture, filepath is unassigned.");
			return false;
		}

		// Load texture
		weak_ptr<ImageImporter> imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter();	
		if (!imageImp.lock()->Load(filePath, (RHI_Texture*)this))
		{
			return false;
		}

		// Change texture extension to an engine texture
		SetResourceFilePath(FileSystem::GetFilePathWithoutExtension(filePath) + EXTENSION_TEXTURE);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

		return true;
	}

	TextureType RHI_Texture::TextureTypeFromString(const string& type)
	{
		if (type == "Albedo")		return TextureType_Albedo;
		if (type == "Roughness")	return TextureType_Roughness;
		if (type == "Metallic")		return TextureType_Metallic;
		if (type == "Normal")		return TextureType_Normal;
		if (type == "Height")		return TextureType_Height;
		if (type == "Occlusion")	return TextureType_Occlusion;
		if (type == "Emission")		return TextureType_Emission;
		if (type == "Mask")			return TextureType_Mask;
		if (type == "CubeMap")		return TextureType_CubeMap;

		return TextureType_Unknown;
	}

	bool RHI_Texture::Serialize(const string& filePath)
	{
		// If the texture bits has been cleared, load it again
		// as we don't want to replaced existing data with nothing.
		// If the texture bits are not cleared, no loading will take place.
		GetTextureBytes(&m_dataRGBA);

		auto file = make_unique<FileStream>(filePath, FileStreamMode_Write);
		if (!file->IsOpen())
			return false;

		// Write texture bits
		file->Write((unsigned int)m_dataRGBA.size());
		for (auto& mip : m_dataRGBA)
		{
			file->Write(mip);
		}

		// Write properties
		file->Write((int)m_type);
		file->Write(m_bpp);
		file->Write(m_width);
		file->Write(m_height);
		file->Write(m_channels);
		file->Write(m_isGrayscale);
		file->Write(m_isTransparent);
		file->Write(m_isUsingMipmaps);
		file->Write(m_resourceID);
		file->Write(m_resourceName);
		file->Write(m_resourceFilePath);

		ClearTextureBytes();

		return true;
	}

	bool RHI_Texture::Deserialize(const string& filePath)
	{
		auto file = make_unique<FileStream>(filePath, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		// Read texture bits
		ClearTextureBytes();
		unsigned int mipCount = file->ReadUInt();
		for (unsigned int i = 0; i < mipCount; i++)
		{
			m_dataRGBA.emplace_back(vector<std::byte>());
			file->Read(&m_dataRGBA[i]);
		}

		// Read properties
		m_type = (TextureType)file->ReadInt();
		file->Read(&m_bpp);
		file->Read(&m_width);
		file->Read(&m_height);
		file->Read(&m_channels);
		file->Read(&m_isGrayscale);
		file->Read(&m_isTransparent);
		file->Read(&m_isUsingMipmaps);
		file->Read(&m_resourceID);
		file->Read(&m_resourceName);
		file->Read(&m_resourceFilePath);

		return true;
	}
}
