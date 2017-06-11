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


//= INCLUDES ======================================
#include "Texture.h"
#include "../Core/GUIDGenerator.h"
#include "../IO/Serializer.h"
#include "../Logging/Log.h"
#include "../Core/Helper.h"
#include "../Resource/Import/ImageImporter.h"
#include "../Resource/Import/DDSTextureImporter.h"
#include "../Resource/ResourceManager.h"
#include "D3D11/D3D11Texture.h"
//================================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus
{
	Texture::Texture(Context* context)
	{
		//= RESOURCE INTERFACE ===========
		m_resourceID = GENERATE_GUID;
		m_resourceType = Texture_Resource;
		//================================

		// Texture
		m_context = context;
		m_width = 0;
		m_height = 0;
		m_textureType = Albedo_Texture;
		m_grayscale = false;
		m_transparency = false;
		m_alphaIsTransparency = false;
		m_generateMipchain = true;
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
			savePath = GetFilePathMetadata();
		}

		if (!Serializer::StartWriting(savePath))
			return false;

		Serializer::WriteSTR(METADATA_TYPE_TEXTURE);
		Serializer::WriteSTR(m_resourceID);
		Serializer::WriteSTR(m_resourceName);
		Serializer::WriteSTR(m_resourceFilePath);
		Serializer::WriteInt(m_width);
		Serializer::WriteInt(m_height);
		Serializer::WriteInt(int(m_textureType));
		Serializer::WriteBool(m_grayscale);
		Serializer::WriteBool(m_transparency);
		Serializer::WriteBool(m_generateMipchain);

		Serializer::StopWriting();

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

	bool Texture::LoadFromForeignFormat(const std::string& filePath)
	{
		auto graphicsDevice = m_context->GetSubsystem<Graphics>()->GetDevice();
		if (!graphicsDevice)
			return false;

		// Load DDS (too bored to implement dds cubemap support in the ImageImporter)
		if (FileSystem::GetExtensionFromFilePath(filePath) == ".dds")
		{
			ID3D11ShaderResourceView* ddsTex = nullptr;
			wstring widestr = wstring(filePath.begin(), filePath.end());
			HRESULT hr = DirectX::CreateDDSTextureFromFile(graphicsDevice, widestr.c_str(), nullptr, &ddsTex);
			if (FAILED(hr))
			{
				LOG_ERROR("Failed to load texture \"" + filePath + "\".");
				return false;
			}

			m_texture->SetShaderResourceView(ddsTex);
			return true;
		}

		// Load texture
		auto imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter().lock();
		bool loaded = m_generateMipchain ? imageImp->LoadAndCreateMipchain(filePath) : imageImp->Load(filePath);
		if (!loaded)
		{
			LOG_ERROR("Failed to load texture \"" + filePath + "\".");
			imageImp->Clear();
			return false;
		}

		// Extract any metadata we can from the ImageImporter
		m_resourceFilePath = imageImp->GetPath();
		m_resourceName = FileSystem::GetFileNameNoExtensionFromFilePath(GetFilePathTexture());
		m_width = imageImp->GetWidth();
		m_height = imageImp->GetHeight();
		m_grayscale = imageImp->IsGrayscale();
		m_transparency = imageImp->IsTransparent();

		if (!CreateShaderResource())
			return false;

		// Free any memory allocated by the ImageImporter
		imageImp->Clear();

		if (!LoadFromFile(GetFilePathMetadata())) // Load metadata file
		{
			if (!SaveToFile(GetFilePathMetadata())) // If a metadata file doesn't exist, create one
			{
				return false; // if that failed too, well at least get the file path right mate
			}
		}

		return true;
	}

	bool Texture::LoadMetadata(const std::string& filePath)
	{
		if (!Serializer::StartReading(filePath))
			return false;

		if (Serializer::ReadSTR() == METADATA_TYPE_TEXTURE)
		{
			m_resourceID = Serializer::ReadSTR();
			m_resourceName = Serializer::ReadSTR();
			m_resourceFilePath = Serializer::ReadSTR();
			m_width = Serializer::ReadInt();
			m_height = Serializer::ReadInt();
			m_textureType = TextureType(Serializer::ReadInt());
			m_grayscale = Serializer::ReadBool();
			m_transparency = Serializer::ReadBool();
			m_generateMipchain = Serializer::ReadBool();
		}

		Serializer::StopReading();

		return true;
	}

	bool Texture::CreateShaderResource()
	{
		if (!m_context)
			return false;

		auto imageImp = m_context->GetSubsystem<ResourceManager>()->GetImageImporter().lock();
		if (m_generateMipchain)
		{
			if (!m_texture->CreateFromMipchain(m_width, m_height, imageImp->GetChannels(), imageImp->GetRGBAMipchain()))
			{
				LOG_ERROR("Failed to create texture from loaded image \"" + imageImp->GetPath() + "\".");
				return false;
			}
		}
		else
		{
			if (!m_texture->Create(m_width, m_height, imageImp->GetChannels(), imageImp->GetRGBA()))
			{
				LOG_ERROR("Failed to create texture from loaded image \"" + imageImp->GetPath() + "\".");
				return false;
			}
		}

		return true;
	}
}