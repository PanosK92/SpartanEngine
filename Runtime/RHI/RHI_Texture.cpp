/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =========================
#include "RHI_Texture.h"
#include "../IO/FileStream.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
//====================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan
{
	RHI_Texture::RHI_Texture(Context* context) : IResource(context, Resource_Texture)
	{
		m_rhi_device = context->GetSubsystem<Renderer>()->GetRhiDevice();
	}

	RHI_Texture::~RHI_Texture()
	{
		m_data.clear();
		m_data.shrink_to_fit();
	}

	bool RHI_Texture::SaveToFile(const string& file_path)
	{
		// Check to see if the file already exists (if so, get the byte count)
		uint32_t byte_count = 0;
		{
			if (FileSystem::FileExists(file_path))
			{
				auto file = make_unique<FileStream>(file_path, FileStream_Read);
				if (file->IsOpen())
				{
					file->Read(&byte_count);
				}
			}
		}

		auto append = true;
		auto file = make_unique<FileStream>(file_path, FileStream_Write | FileStream_Append);
		if (!file->IsOpen())
			return false;

		// If the existing file has a byte count but we 
		// hold no data, don't overwrite the file's bytes.
		if (byte_count != 0 && m_data.empty())
		{
			file->Skip
			(
				sizeof(uint32_t) +	// byte count
				sizeof(uint32_t) +	// mipmap count
				byte_count			// bytes
			);
		}
		else
		{
			byte_count = GetByteCount();

			// Write byte count
			file->Write(byte_count);
			// Write mipmap count
			file->Write(static_cast<uint32_t>(m_data.size()));
			// Write bytes
			for (auto& mip : m_data)
			{
				file->Write(mip);
			}

			// The bytes have been saved, so we can now free some memory
			m_data.clear();
			m_data.shrink_to_fit();
		}

		// Write properties
		file->Write(m_bpp);
		file->Write(m_width);
		file->Write(m_height);
		file->Write(m_channels);
		file->Write(m_is_grayscale);
		file->Write(m_is_transparent);
		file->Write(GetId());
		file->Write(GetResourceName());
		file->Write(GetResourceFilePath());

		return true;
	}

	bool RHI_Texture::LoadFromFile(const string& file_path)
	{
		// Make the path relative to the engine
		const auto file_path_relative = FileSystem::GetRelativeFilePath(file_path);

		// Validate file path
		if (!FileSystem::FileExists(file_path_relative))
		{
			LOGF_ERROR("Path \"%s\" is invalid.", file_path_relative.c_str());
			return false;
		}

		m_data.clear();
		m_data.shrink_to_fit();
		m_load_state = LoadState_Started;

		// Load from disk
		auto texture_data_loaded = false;		
		if (FileSystem::IsEngineTextureFile(file_path_relative)) // engine format (binary)
		{
			texture_data_loaded = LoadFromFile_NativeFormat(file_path_relative);
		}	
		else if (FileSystem::IsSupportedImageFile(file_path_relative)) // foreign format (most known image formats)
		{
			texture_data_loaded = LoadFromFile_ForeignFormat(file_path_relative, m_generate_mipmaps_when_loading);
		}

		if (!texture_data_loaded)
		{
			LOGF_ERROR("Failed to load \"%s\".", file_path_relative.c_str());
			m_load_state = LoadState_Failed;
			return false;
		}

		// Create GPU resource
		if (!CreateResourceGpu())
		{
			LOGF_ERROR("Failed to create shader resource for \"%s\".", GetResourceFilePath().c_str());
			m_load_state = LoadState_Failed;
			return false;
		}

		// Only clear texture bytes if that's an engine texture, if not, it's not serialized yet.
		if (FileSystem::IsEngineTextureFile(file_path_relative)) 
		{
			m_data.clear();
			m_data.shrink_to_fit();
		}
		m_load_state = LoadState_Completed;
		return true;
	}

	vector<std::byte>* RHI_Texture::GetData(const uint32_t index)
	{
		if (index >= m_data.size())
		{
			LOG_WARNING("Index out of range");
			return nullptr;
		}

		return &m_data[index];
	}

    std::vector<std::byte> RHI_Texture::GetMipmap(uint32_t index)
    {
        std::vector<std::byte> data;

        // Use existing data, if it's there
        if (index < m_data.size())
        {
            data = m_data[index];
        }
        // Else attempt to load the data
        else
        {
            auto file = make_unique<FileStream>(GetResourceFilePath(), FileStream_Read);
            if (file->IsOpen())
            {
                auto byte_count = file->ReadAs<uint32_t>();
                auto mip_count  = file->ReadAs<uint32_t>();

                if (index < mip_count)
                {
                    for (uint32_t i = 0; i <= index; i++)
                    {
                        file->Read(&data);
                    }
                }
                else
                {
                    LOG_ERROR("Invalid index");
                }
                file->Close();
            }
            else
            {
                LOG_ERROR("Unable to retreive data");
            }
        }

        return data;
    }

    bool RHI_Texture::LoadFromFile_ForeignFormat(const string& file_path, const bool generate_mipmaps)
	{
		// Load texture
		auto imageImp = m_context->GetSubsystem<ResourceCache>()->GetImageImporter();	
		if (!imageImp->Load(file_path, this, generate_mipmaps))
			return false;

		// Change texture extension to an engine texture
		SetResourceFilePath(FileSystem::GetFilePathWithoutExtension(file_path) + EXTENSION_TEXTURE);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

		return true;
	}

	bool RHI_Texture::LoadFromFile_NativeFormat(const string& file_path)
	{
		auto file = make_unique<FileStream>(file_path, FileStream_Read);
		if (!file->IsOpen())
			return false;

		m_data.clear();
		m_data.shrink_to_fit();

		// Read byte and mipmap count
		auto byte_count = file->ReadAs<uint32_t>();
        auto mip_count  = file->ReadAs<uint32_t>();

		// Read bytes
		m_data.resize(mip_count);
		for (auto& mip : m_data)
		{
			file->Read(&mip);
		}

		// Read properties
		file->Read(&m_bpp);
		file->Read(&m_width);
		file->Read(&m_height);
		file->Read(&m_channels);
		file->Read(&m_is_grayscale);
		file->Read(&m_is_transparent);
		SetId(file->ReadAs<uint32_t>());
		SetResourceName(file->ReadAs<string>());
		SetResourceFilePath(file->ReadAs<string>());

		return true;
	}

	uint32_t RHI_Texture::GetChannelCountFromFormat(const RHI_Format format)
	{
		switch (format)
		{
			case Format_R8_UNORM:			return 1;
			case Format_R16_UINT:			return 1;
			case Format_R16_FLOAT:			return 1;
			case Format_R32_UINT:			return 1;
			case Format_R32_FLOAT:			return 1;
			case Format_D32_FLOAT:			return 1;
			case Format_R32_FLOAT_TYPELESS:	return 1;
			case Format_R8G8_UNORM:			return 2;
			case Format_R16G16_FLOAT:		return 2;
			case Format_R32G32_FLOAT:		return 2;
			case Format_R32G32B32_FLOAT:	return 3;
			case Format_R8G8B8A8_UNORM:		return 4;
			case Format_R16G16B16A16_FLOAT:	return 4;
			case Format_R32G32B32A32_FLOAT:	return 4;
			default:						return 0;
		}
	}

	uint32_t RHI_Texture::GetByteCount()
	{
		uint32_t byte_count = 0;

		for (auto& mip : m_data)
		{
			byte_count += static_cast<uint32_t>(mip.size());
		}

		return byte_count;
	}
}
