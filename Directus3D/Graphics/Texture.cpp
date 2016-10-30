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
#include "../Logging/Log.h"
#include "../FileSystem/ImageImporter.h"
#include "../FileSystem/FileSystem.h"
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
bool Texture::LoadFromFile(const string& filePath, Graphics* graphics)
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
	m_shaderResourceView = CreateID3D11ShaderResourceView(graphics);

	// Free any memory allocated by the image importer
	ImageImporter::GetInstance().Clear();

	if (!LoadMetadata()) // load metadata file
		if (!SaveMetadata()) // if a metadata file doesn't exist, create one
			return false; // if that failed too, it means the filepath is invalid

	return true;
}

ID3D11ShaderResourceView* Texture::CreateID3D11ShaderResourceView(Graphics* graphics)
{
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM; // texture format
	unsigned int mipLevels = 7; // 0 for a full mip chain. The mip chain will extend to 1x1 at the lowest level, even if the dimensions aren't square.

	// texture description
	D3D11_TEXTURE2D_DESC textureDesc{};
	textureDesc.Width = m_width;
	textureDesc.Height = m_height;
	textureDesc.MipLevels = mipLevels;
	textureDesc.ArraySize = 1;
	textureDesc.SampleDesc.Count = 1;
	textureDesc.SampleDesc.Quality = 0;
	textureDesc.Usage = D3D11_USAGE_DEFAULT;
	textureDesc.Format = format;
	textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	textureDesc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

	// Create 2D texture from texture description
	ID3D11Texture2D* texture = nullptr;
	HRESULT hResult = graphics->GetDevice()->CreateTexture2D(&textureDesc, nullptr, &texture);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create ID3D11Texture2D from imported image data while trying to load " + ImageImporter::GetInstance().GetPath() + ".");
		return nullptr;
	}

	// Resource view description
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;

	// Create shader resource view from resource view description
	ID3D11ShaderResourceView* shaderResourceView = nullptr;
	hResult = graphics->GetDevice()->CreateShaderResourceView(texture, &srvDesc, &shaderResourceView);
	if (FAILED(hResult))
	{
		LOG_ERROR("Failed to create the shader resource view.");
		return nullptr;
	}

	// Resource data description
	D3D11_SUBRESOURCE_DATA mapResource{};
	mapResource.pSysMem = ImageImporter::GetInstance().GetRGBA();
	mapResource.SysMemPitch = sizeof(unsigned char) * m_width * ImageImporter::GetInstance().GetChannels();

	// Copy data from memory to the subresource created in non-mappable memory
	graphics->GetDeviceContext()->UpdateSubresource(texture, 0, nullptr, mapResource.pSysMem, mapResource.SysMemPitch, 0);

	// Generate mip chain
	graphics->GetDeviceContext()->GenerateMips(shaderResourceView);

	return shaderResourceView;
}