/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ===============================
#include "Texture.h"
#include "../Core/GUIDGenerator.h"
#include "../IO/Serializer.h"
#include "../IO/Log.h"
#include "../AssetImporting/ImageImporter.h"
#include "../IO/FileSystem.h"
#include "../Core/Helper.h"
//==========================================

//= NAMESPACES =====
using namespace std;
//==================

Texture::Texture()
{
	m_ID = GENERATE_GUID;
	m_name = DATA_NOT_ASSIGNED;
	m_filePathTexture = DATA_NOT_ASSIGNED;
	m_filePathMetadata = DATA_NOT_ASSIGNED;
	m_width = 0;
	m_height = 0;
	m_shaderResourceView = nullptr;
	m_type = Albedo;
	m_grayscale = false;
	m_transparency = false;
	m_alphaIsTransparency = false;
}

Texture::~Texture()
{
	SafeRelease(m_shaderResourceView);
}

//===
// IO
//===
void Texture::Serialize()
{
	Serializer::WriteSTR(m_ID);
	Serializer::WriteSTR(m_name);
	Serializer::WriteSTR(m_filePathTexture);
	Serializer::WriteSTR(m_filePathMetadata);
	Serializer::WriteInt(m_width);
	Serializer::WriteInt(m_height);
	Serializer::WriteInt(int(m_type));
	Serializer::WriteBool(m_grayscale);
	Serializer::WriteBool(m_transparency);
}

void Texture::Deserialize()
{
	m_ID = Serializer::ReadSTR();
	m_name = Serializer::ReadSTR();
	m_filePathTexture = Serializer::ReadSTR();
	m_filePathMetadata = Serializer::ReadSTR();
	m_width = Serializer::ReadInt();
	m_height = Serializer::ReadInt();
	m_type = TextureType(Serializer::ReadInt());
	m_grayscale = Serializer::ReadBool();
	m_transparency = Serializer::ReadBool();
}

bool Texture::SaveMetadata()
{
	if (!FileSystem::IsSupportedTextureMetadata(m_filePathMetadata))
		return false;

	Serializer::StartWriting(m_filePathMetadata);
	Serialize();
	Serializer::StopWriting();

	return true;
}

bool Texture::LoadMetadata()
{
	if (!FileSystem::FileExists(m_filePathMetadata) || !FileSystem::IsSupportedTextureMetadata(m_filePathMetadata))
		return false;

	Serializer::StartReading(m_filePathMetadata);
	Deserialize();
	Serializer::StopReading();

	return true;
}

// Loads a texture from an image file (.jpg, .png and so on)
bool Texture::LoadFromFile(const string& filePath)
{
	// load it
	if (!ImageImporter::GetInstance().Load(filePath))
	{
		LOG_ERROR("Failed to load texture \"" + filePath + "\".");
		ImageImporter::GetInstance().Clear();
		return false;
	}

	// Get metadata from texture
	m_filePathTexture = ImageImporter::GetInstance().GetPath();
	m_name = FileSystem::GetFileNameNoExtensionFromPath(GetFilePathTexture());
	m_filePathMetadata = FileSystem::GetPathWithoutFileNameExtension(m_filePathTexture) + TEXTURE_METADATA_EXTENSION;
	m_width = ImageImporter::GetInstance().GetWidth();
	m_height = ImageImporter::GetInstance().GetHeight();
	m_grayscale = ImageImporter::GetInstance().IsGrayscale();
	m_transparency = ImageImporter::GetInstance().IsTransparent();
	m_shaderResourceView = ImageImporter::GetInstance().GetAsD3D11ShaderResourceView();

	// Free any memory allocated by the image importer
	ImageImporter::GetInstance().Clear();

	if (!LoadMetadata()) // load metadata file
		if (!SaveMetadata()) // if a metadata file doesn't exist, create one
			return false; // if that failed too, it means the filepath is invalid

	return true;
}