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
		unique_ptr<Texture> texture(new Texture());
		texture->Deserialize();

		m_textures.push_back(move(texture));
	}
}

Texture* TexturePool::CreateNewTexture()
{
	unique_ptr<Texture> texture(new Texture());
	m_textures.push_back(move(texture));

	return m_textures.back().get();
}

Texture* TexturePool::AddFromFile(string texturePath, TextureType textureType)
{
	// If loaded, return the already loaded one
	Texture* loaded = GetTextureByPath(texturePath);
	if (loaded)
		return loaded;

	// If not, load it
	unique_ptr<Texture> texture(new Texture());
	texture->LoadFromFile(texturePath, textureType);

	m_textures.push_back(move(texture));
	return m_textures.back().get();
}

Texture* TexturePool::GetTextureByName(string name)
{
	for (auto i = 0; i < m_textures.size(); i++)
	{
		if (m_textures[i]->GetName() == name)
			return m_textures[i].get();
	}

	return nullptr;
}

Texture* TexturePool::GetTextureByID(string ID)
{
	for (auto i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetID() == ID)
			return m_textures[i].get();

	return nullptr;
}

Texture* TexturePool::GetTextureByPath(string path)
{
	for (unsigned int i = 0; i < m_textures.size(); i++)
		if (m_textures[i]->GetPath() == path)
			return m_textures[i].get();

	return nullptr;
}

void TexturePool::RemoveTextureByPath(string path)
{
	vector<unique_ptr<Texture>>::iterator it;
	for (it = m_textures.begin(); it < m_textures.end();)
	{
		if (it->get()->GetPath() == path)
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