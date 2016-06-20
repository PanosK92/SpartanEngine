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

void TexturePool::Serialize()
{
	Serializer::SaveInt(int(m_textures.size()));
	for (int i = 0; i < m_textures.size(); i++)
		m_textures[i]->Serialize();
}

void TexturePool::Deserialize()
{
	int textureCount = Serializer::LoadInt();
	for (int i = 0; i < textureCount; i++)
	{
		shared_ptr<Texture> texture(new Texture());
		texture->Deserialize();

		m_textures.push_back(texture);
	}
}

shared_ptr<Texture> TexturePool::Add(shared_ptr<Texture> texture)
{
	if (!texture)
		return nullptr;

	// If loaded, return the already loaded one
	shared_ptr<Texture> loaded = GetTextureByPath(texture->GetPath());
	if (loaded)
	{
		texture.reset();
		return loaded;
	}

	// If not, save it and return it
	m_textures.push_back(texture);
	return texture;
}

shared_ptr<Texture> TexturePool::GetTextureByName(string name)
{
	for (auto i = 0; i < m_textures.size(); i++)
	{
		if (m_textures[i]->GetName() == name)
			return m_textures[i];
	}

	return nullptr;
}

shared_ptr<Texture> TexturePool::GetTextureByID(string ID)
{
	for (auto i = 0; i < m_textures.size(); i++)
	{
		if (m_textures[i]->GetID() == ID)
			return m_textures[i];
	}

	return nullptr;
}

shared_ptr<Texture> TexturePool::GetTextureByPath(string path)
{
	for (unsigned int i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == path)
			return m_textures[i];

	return nullptr;
}

void TexturePool::RemoveTextureByPath(string path)
{
	vector<shared_ptr<Texture>>::iterator it;
	for (it = m_textures.begin(); it < m_textures.end();)
	{
		shared_ptr<Texture> tex = *it;
		if (tex->GetPath() == path)
		{
			it = m_textures.erase(it);
			return;
		}
		++it;
	}
}

void TexturePool::Clear()
{
	m_textures.clear();
	m_textures.shrink_to_fit();
}

/*------------------------------------------------------------------------------
						[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
int TexturePool::GetTextureIndex(shared_ptr<Texture> texture)
{
	for (unsigned int i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == texture->GetPath())
			return i;

	return -1;
}