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

//= INCLUDES ================
#include "TexturePool.h"
#include <filesystem>
#include "../IO/Log.h"
#include "../IO/FileSystem.h"
//===========================

//= NAMESPACES =====
using namespace std;
//==================

TexturePool::TexturePool()
{

}

TexturePool::~TexturePool()
{
	Clear();
}

// Adds a texture to the pool directly from memory
Texture* TexturePool::Add(Texture* textureIn)
{
	if (!textureIn)
		return nullptr;

	for (Texture* texture : m_textures)
		if (textureIn->GetID() == texture->GetID())
			return texture;

	m_textures.push_back(textureIn);
	return m_textures.back();
}

// Adds a texture to the pool by loading it from an image file
Texture* TexturePool::Add(const string& texturePath)
{
	if (!FileSystem::FileExists(texturePath) || !FileSystem::IsSupportedImage(texturePath))
		return nullptr;

	// If the texture alrady exists, return it
	Texture* loaded = GetTextureByPath(texturePath);
	if (loaded)
		return loaded;

	// If the texture doesn't exist, create and load it
	Texture* texture = new Texture();
	texture->LoadFromFile(texturePath);
	m_textures.push_back(texture);

	return m_textures.back();
}

// Adds multiple textures to the pool by reading them from image files
void TexturePool::Add(const vector<string>& imagePaths)
{
	for (const string& imagePath : imagePaths)
		Add(imagePath);
}

Texture* TexturePool::GetTextureByName(const string&  name)
{
	for (Texture* texture : m_textures)
		if (texture->GetName() == name)
			return texture;

	return nullptr;
}

Texture* TexturePool::GetTextureByID(const string&  ID)
{
	for (Texture* texture : m_textures)
		if (texture->GetID() == ID)
			return texture;

	return nullptr;
}

Texture* TexturePool::GetTextureByPath(const string&  path)
{
	for (Texture* texture : m_textures)
		if (texture->GetFilePathTexture() == path)
			return texture;

	return nullptr;
}

vector<string> TexturePool::GetAllTextureFilePaths()
{
	vector<string> paths;

	for (Texture* texture : m_textures)
		paths.push_back(texture->GetFilePathTexture());

	return paths;
}

void TexturePool::Remove(Texture* texture)
{
	for (auto it = m_textures.begin(); it < m_textures.end();)
	{
		Texture* currentTex = *it;
		if (currentTex->GetID() == texture->GetID())
		{
			delete currentTex;
			it = m_textures.erase(it);
			return;
		}
		++it;
	}
}

void TexturePool::RemoveTextureByPath(const string&  path)
{
	for (auto it = m_textures.begin(); it < m_textures.end();)
	{
		Texture* texture = *it;
		if (texture->GetFilePathTexture() == path)
		{
			delete texture;
			it = m_textures.erase(it);
			return;
		}
		++it;
	}
}

void TexturePool::Clear()
{
	for (Texture* texture : m_textures)
		delete texture;

	m_textures.clear();
	m_textures.shrink_to_fit();
}