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

//= INCLUDES ======================
#include "Texture.h"
#include "../Core/GUIDGenerator.h"
#include "../Core/Helper.h"
#include "../IO/Serializer.h"
#include "../IO/Log.h"
#include "../AssetImporting/ImageImporter.h"
#include "../IO/FileSystem.h"
//=================================

//= NAMESPACES =====
using namespace std;
//==================

Texture::Texture()
{
	m_ID = GENERATE_GUID;
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

void Texture::Serialize() const
{
	Serializer::SaveSTR(m_ID);
	Serializer::SaveSTR(m_name);
	Serializer::SaveSTR(m_filePath);
	Serializer::SaveInt(m_width);
	Serializer::SaveInt(m_height);
	Serializer::SaveInt(int(m_type));
	Serializer::SaveBool(m_grayscale);
	Serializer::SaveBool(m_transparency);
}

void Texture::Deserialize()
{
	m_ID = Serializer::LoadSTR();
	m_name = Serializer::LoadSTR();
	m_filePath = Serializer::LoadSTR();
	m_width = Serializer::LoadInt();
	m_height = Serializer::LoadInt();
	m_type = TextureType(Serializer::LoadInt());
	m_grayscale = Serializer::LoadBool();
	m_transparency = Serializer::LoadBool();

	LoadFromFile(m_filePath, m_type);
}

void Texture::SaveToFile(string filePath)
{
	filePath += GetName() + ".tex";

	Serializer::StartWriting(filePath);
	Serialize();
	Serializer::StopWriting();
}

bool Texture::LoadFromFile(const string& filePath)
{
	if (!FileSystem::FileExists(filePath))
		return false;

	Serializer::StartReading(filePath);
	Deserialize();
	Serializer::StopReading();

	return true;
}

ID3D11ShaderResourceView* Texture::GetID3D11ShaderResourceView() const
{
	return m_shaderResourceView;
}

void Texture::SetID3D11ShaderResourceView(ID3D11ShaderResourceView* srv)
{
	m_shaderResourceView = srv;
}

bool Texture::LoadFromFile(string path, TextureType type)
{
	// load it
	bool result = ImageImporter::GetInstance().Load(path);

	if (!result)
	{
		LOG("Failed to load texture \"" + path + "\".", Log::Error);
		ImageImporter::GetInstance().Clear();
		return false;
	}

	// Fill the texture with data
	SetFilePath(ImageImporter::GetInstance().GetPath());
	SetName(FileSystem::GetFileNameNoExtensionFromPath(GetFilePath()));
	SetWidth(ImageImporter::GetInstance().GetWidth());
	SetHeight(ImageImporter::GetInstance().GetHeight());
	SetGrayscale(ImageImporter::GetInstance().IsGrayscale());
	SetTransparency(ImageImporter::GetInstance().IsTransparent());
	m_shaderResourceView = ImageImporter::GetInstance().GetAsD3D11ShaderResourceView();

	// Determine texture type
	// FIX: some models pass a normal map as a height map
	// and others pass a height map as a normal map...
	SetType(type);
	if (GetType() == Height && !GetGrayscale())
		SetType(Normal);
	if (GetType() == Normal && GetGrayscale())
		SetType(Height);

	// Clear any memory allocated by the image loader
	ImageImporter::GetInstance().Clear();

	return true;
}

string Texture::GetID() const
{
	return m_ID;
}

void Texture::SetName(string name)
{
	m_name = name;
}

string Texture::GetName()
{
	return m_name;
}

void Texture::SetFilePath(string filepath)
{
	m_filePath = filepath;
}

string Texture::GetFilePath()
{
	return m_filePath;
}

void Texture::SetWidth(int width)
{
	m_width = width;
}

int Texture::GetWidth() const
{
	return m_width;
}

void Texture::SetHeight(int height)
{
	m_height = height;
}

int Texture::GetHeight() const
{
	return m_height;
}

TextureType Texture::GetType() const
{
	return m_type;
}

void Texture::SetType(TextureType type)
{
	m_type = type;
}

void Texture::SetGrayscale(bool isGrayscale)
{
	m_grayscale = isGrayscale;
}

bool Texture::GetGrayscale() const
{
	return m_grayscale;
}

void Texture::SetTransparency(bool transparency)
{
	m_transparency = transparency;
}

bool Texture::GetTransparency()
{
	return m_transparency;
}