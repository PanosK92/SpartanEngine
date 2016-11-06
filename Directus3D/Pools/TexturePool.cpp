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
#include "../FileSystem/FileSystem.h"
#include "../Core/Context.h"
#include "../Logging/Log.h"
//===========================

//= NAMESPACES =====
using namespace std;
//==================

TexturePool::TexturePool(Context* context) : Object(context) 
{

}

TexturePool::~TexturePool()
{
	Clear();
}

// Adds a texture to the pool directly from memory
weak_ptr<Texture> TexturePool::Add(shared_ptr<Texture> textureIn)
{
	if (!textureIn)
		return weak_ptr<Texture>();

	for (const auto texture : m_textures)
		if (textureIn->GetID() == texture->GetID())
			return texture;

	m_textures.push_back(textureIn);
	return m_textures.back();
}

// Adds a texture to the pool by loading it from an image file
weak_ptr<Texture> TexturePool::Add(const string& texturePath)
{
	if (!FileSystem::FileExists(texturePath) || !FileSystem::IsSupportedImage(texturePath))
		return weak_ptr<Texture>();

	// If the texture alrady exists, return it
	auto existingTexture = GetTextureByPath(texturePath);
	if (!existingTexture.expired())
		return existingTexture;

	// If the texture doesn't exist, create and load it
	auto texture = make_shared<Texture>();
	texture->LoadFromFile(texturePath, g_context->GetSubsystem<Graphics>());
	m_textures.push_back(texture);

	return m_textures.back();
}

// Adds multiple textures to the pool by reading them from image files
void TexturePool::Add(const vector<string>& imagePaths)
{
	for (const string& imagePath : imagePaths)
		Add(imagePath);
}

void TexturePool::SaveTextureMetadata()
{
	for (const auto texture : m_textures)
		texture->SaveMetadata();
}

weak_ptr<Texture> TexturePool::GetTextureByName(const string&  name)
{
	for (const auto texture : m_textures)
		if (texture->GetName() == name)
			return texture;

	return weak_ptr<Texture>();
}

weak_ptr<Texture> TexturePool::GetTextureByID(const string&  ID)
{
	for (const auto texture : m_textures)
		if (texture->GetID() == ID)
			return texture;

	return weak_ptr<Texture>();
}

weak_ptr<Texture> TexturePool::GetTextureByPath(const string&  path)
{
	for (const auto texture : m_textures)
		if (texture->GetFilePathTexture() == path)
			return texture;

	return weak_ptr<Texture>();
}

vector<string> TexturePool::GetAllTextureFilePaths()
{
	vector<string> paths;
	for (const auto texture : m_textures)
		paths.push_back(texture->GetFilePathTexture());

	return paths;
}

void TexturePool::Clear()
{
	m_textures.clear();
	m_textures.shrink_to_fit();
}