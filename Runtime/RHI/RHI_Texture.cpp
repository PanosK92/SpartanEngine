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
		// If the texture bits has been cleared, load it again
		// as we don't want to replaced existing data with nothing.
		// If the texture bits are not cleared, no loading will take place.
		// TODO simply skip X bytes before witting, no need to load.
		GetTextureBytes(&m_data);

		auto file = make_unique<FileStream>(file_path, FileStreamMode_Write);
		if (!file->IsOpen())
			return false;

		// Write texture bits
		file->Write((unsigned int)m_data.size());
		for (auto& mip : m_data)
		{
			file->Write(mip);
		}

		// Write properties
		file->Write(m_bpp);
		file->Write(m_width);
		file->Write(m_height);
		file->Write(m_channels);
		file->Write(m_has_mipmaps);
		file->Write(m_is_grayscale);
		file->Write(m_is_transparent);
		file->Write(GetResourceId());
		file->Write(GetResourceName());
		file->Write(GetResourceFilePath());

		ClearTextureBytes();

		return true;
	}

	bool RHI_Texture::LoadFromFile(const string& rawFilePath)
	{
		// Make the path, relative to the engine and validate it
		auto filePath = FileSystem::GetRelativeFilePath(rawFilePath);
		if (!FileSystem::FileExists(filePath))
		{
			LOGF_ERROR("Path \"%s\" is invalid.", filePath.c_str());
			return false;
		}

		m_data.clear();
		m_data.shrink_to_fit();
		m_load_state = LoadState_Started;

		// Load from disk
		bool texture_data_loaded = false;		
		if (FileSystem::IsEngineTextureFile(filePath)) // engine format (binary)
		{
			texture_data_loaded = LoadFromFile_NativeFormat(filePath);
		}	
		else if (FileSystem::IsSupportedImageFile(filePath)) // foreign format (most known image formats)
		{
			texture_data_loaded = LoadFromFile_ForeignFormat(filePath, m_has_mipmaps);
		}

		if (!texture_data_loaded)
		{
			LOGF_ERROR("Failed to load \"%s\".", filePath.c_str());
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
		if (FileSystem::IsEngineTextureFile(filePath)) { ClearTextureBytes(); }
		m_load_state = LoadState_Completed;
		return true;
	}

	vector<std::byte>* RHI_Texture::GetData(unsigned int index)
	{
		if (index >= m_data.size())
		{
			LOG_WARNING("Index out of range");
			return nullptr;
		}

		return &m_data[index];
	}

	void RHI_Texture::ClearTextureBytes()
	{
		m_data.clear();
		m_data.shrink_to_fit();
	}

	void RHI_Texture::GetTextureBytes(vector<vector<std::byte>>* texture_bytes)
	{
		if (!m_data.empty())
		{
			texture_bytes = &m_data;
			return;
		}

		auto file = make_unique<FileStream>(GetResourceFilePath(), FileStreamMode_Read);
		if (!file->IsOpen())
			return;

		auto mipCount = file->ReadAs<unsigned int>();
		for (unsigned int i = 0; i < mipCount; i++)
		{
			texture_bytes->emplace_back(vector<std::byte>());
			file->Read(&m_data[i]);
		}
	}

	bool RHI_Texture::LoadFromFile_ForeignFormat(const string& file_path, bool generate_mipmaps)
	{
		// Load texture
		ImageImporter* imageImp = m_context->GetSubsystem<ResourceCache>()->GetImageImporter();	
		if (!imageImp->Load(file_path, this, generate_mipmaps))
			return false;

		// Change texture extension to an engine texture
		SetResourceFilePath(FileSystem::GetFilePathWithoutExtension(file_path) + EXTENSION_TEXTURE);
		SetResourceName(FileSystem::GetFileNameNoExtensionFromFilePath(GetResourceFilePath()));

		return true;
	}

	bool RHI_Texture::LoadFromFile_NativeFormat(const string& file_path)
	{
		auto file = make_unique<FileStream>(file_path, FileStreamMode_Read);
		if (!file->IsOpen())
			return false;

		// Read texture bits
		ClearTextureBytes();
		m_data.resize(file->ReadAs<unsigned int>());
		for (auto& mip : m_data)
		{
			file->Read(&mip);
		}

		// Read properties
		file->Read(&m_bpp);
		file->Read(&m_width);
		file->Read(&m_height);
		file->Read(&m_channels);
		file->Read(&m_has_mipmaps);
		file->Read(&m_is_grayscale);
		file->Read(&m_is_transparent);
		SetResourceID(file->ReadAs<unsigned int>());
		SetResourceName(file->ReadAs<string>());
		SetResourceFilePath(file->ReadAs<string>());

		return true;
	}
}
