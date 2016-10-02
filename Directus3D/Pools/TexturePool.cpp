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
Texture* TexturePool::Add(Texture* texture)
{
	if (!texture)
		return nullptr;

	m_textures.push_back(texture);
	return m_textures.back();
}

// Adds a texture to the pool by loading it from a file
Texture* TexturePool::Add(const string& texturePath, TextureType textureType)
{
	// If loaded, return the already loaded one
	Texture* loaded = GetTextureByPath(texturePath);
	if (loaded)
		return loaded;

	// If not, load it
	Texture* texture = new Texture();
	texture->LoadFromFile(texturePath, textureType);
	m_textures.push_back(texture);

	return m_textures.back();
}

// Adds multiple textures to the pool by reading them from files
void TexturePool::Add(vector<string> filePaths)
{
	for (auto i = 0; i < filePaths.size(); i++)
	{
		Texture* texture = new Texture();

		if (texture->LoadFromFile(filePaths[i]))
			m_textures.push_back(texture);
		else
			delete texture;
	}
}

Texture* TexturePool::GetTextureByName(const string&  name)
{
	for (auto i = 0; i < m_textures.size(); i++)
	{
		if (m_textures[i]->GetName() == name)
			return m_textures[i];
	}

	return nullptr;
}

Texture* TexturePool::GetTextureByID(const string&  ID)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetID() == ID)
			return m_textures[i];

	return nullptr;
}

Texture* TexturePool::GetTextureByPath(const string&  path)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetFilePath() == path)
			return m_textures[i];

	return nullptr;
}

void TexturePool::GetAllTextureFilePaths(vector<string>& paths)
{
	for (auto i = 0; i < m_textures.size(); i++)
		paths.push_back(m_textures[i]->GetFilePath());
}

void TexturePool::RemoveTextureByPath(const string&  path)
{
	for (auto it = m_textures.begin(); it < m_textures.end();)
	{
		Texture* texture = *it;
		if (texture->GetFilePath() == path)
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
	for (auto i = 0; i < m_textures.size(); i++)
		delete m_textures[i];

	m_textures.clear();
	m_textures.shrink_to_fit();
}

/*------------------------------------------------------------------------------
						[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
int TexturePool::GetTextureIndex(Texture* texture)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetFilePath() == texture->GetFilePath())
			return i;

	return -1;
}