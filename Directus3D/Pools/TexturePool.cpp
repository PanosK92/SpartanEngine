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
#include "../IO/Serializer.h"
#include "../IO/Log.h"
//===========================

using namespace std;

TexturePool::TexturePool(ImageLoader* imageLoader)
{
	m_imageLoader = imageLoader;
}

TexturePool::~TexturePool()
{
	Clear();
}

void TexturePool::Save()
{
	Serializer::SaveInt((int)m_textures.size());
	for (int i = 0; i < m_textures.size(); i++)
		m_textures[i]->Save();
}

void TexturePool::Load()
{
	int textureCount = Serializer::LoadInt();
	for (int i = 0; i < textureCount; i++)
	{
		Texture* texture = new Texture();
		texture->Load();
		m_imageLoader->Load(texture->GetPath());
		texture->SetShaderResourceView(m_imageLoader->GetAsD3D11ShaderResourceView());

		m_textures.push_back(texture);
	}
}

Texture* TexturePool::Add(string path, TextureType type)
{
	// make sure it's not already loaded
	if (!IsTextureLoadedByPath(path))
	{
		// load it
		bool loadedSuccessfully = m_imageLoader->Load(path);

		if (!loadedSuccessfully)
		{
			LOG("Failed to load texture \"" + path +"\".", Log::Error);
			return nullptr;
		}

		Texture* texture = m_imageLoader->GetAsTexture();
		texture->SetType(type);

		// FIX: some models pass a normal map as a height map...
		if (texture->GetType() == Height && !texture->IsGrayscale())
			texture->SetType(Normal);

		// FIX: and others pass a height map as a normal map...
		if (texture->GetType() == Normal && texture->IsGrayscale())
			texture->SetType(Height);

		// save it
		m_textures.push_back(texture);

		return texture;
	}

	// if it's loaded, return the existing one
	return GetTextureByPath(path);
}

Texture* TexturePool::GetTextureByID(string ID)
{
	for (auto i = 0; i < m_textures.size(); i++)
	{
		if (ID == m_textures[i]->GetID())
			return m_textures[i];
	}

	return nullptr;
}

Texture* TexturePool::GetTextureByPath(string path)
{
	for (unsigned int i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == path)
			return m_textures[i];

	return nullptr;
}

void TexturePool::RemoveTextureByPath(string path)
{
	vector<Texture*>::iterator it;
	for (it = m_textures.begin(); it < m_textures.end();)
	{
		Texture* tex = *it;
		if (tex->GetPath() == path)
		{
			delete tex;
			it = m_textures.erase(it);
			return;
		}
		++it;
	}
}

void TexturePool::Clear()
{
	vector<Texture*>::iterator it;
	for (it = m_textures.begin(); it < m_textures.end(); ++it)
		delete *it;

	m_textures.clear();
	m_textures.shrink_to_fit();
}

/*------------------------------------------------------------------------------
						[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
int TexturePool::GetTextureIndex(Texture* texture)
{
	for (unsigned int i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == texture->GetPath())
			return i;

	return -1;
}

bool TexturePool::IsTextureLoadedByPath(string path)
{
	for (unsigned int i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == path)
			return true;

	return false;
}
