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

#pragma once

//= INCLUDES ===========================================
#include "../Resource/Resource.h"
#include "../Threading/Threading.h"
#include "../Graphics/Texture.h"
#include "../Font/Font.h"
#include "../Graphics/Animation.h"
#include "../Components/Script.h"
#include "../Graphics/Model.h"
#include "../Graphics/Material.h"
#include "../Graphics/Mesh.h"
#include "../Graphics/DeferredShaders/ShaderVariation.h"
#include "../Audio/Audio.h"
//======================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

string Resource::GetResourceFileName()
{
	return FileSystem::GetFileNameNoExtensionFromFilePath(m_resourceFilePath);
}

string Resource::GetResourceDirectory()
{
	return FileSystem::GetDirectoryFromFilePath(m_resourceFilePath);
}

void Resource::SaveToFileAsync(const string& filePath)
{
	m_context->GetSubsystem<Threading>()->AddTask([this, &filePath]()
	{
		SaveToFile(filePath);
	});
}

void Resource::LoadFromFileAsync(const string& filePath)
{
	m_context->GetSubsystem<Threading>()->AddTask([this, &filePath]()
	{
		LoadFromFile(filePath);
	});
}

template <typename T>
ResourceType Resource::ToResourceType()
{
	if (typeid(T) == typeid(Texture))
		return Resource_Texture;

	if (typeid(T) == typeid(Audio))
		return Resource_Audio;

	if (typeid(T) == typeid(Material))
		return Resource_Material;

	if (typeid(T) == typeid(ShaderVariation))
		return Resource_Shader;

	if (typeid(T) == typeid(Mesh))
		return Resource_Mesh;

	if (typeid(T) == typeid(Model))
		return Resource_Model;

	if (typeid(T) == typeid(Script))
		return Resource_Script;

	if (typeid(T) == typeid(Animation))
		return Resource_Animation;

	if (typeid(T) == typeid(Font))
		return Resource_Font;

	return Resource_Unknown;
}

#define INSTANTIATE(T) template ENGINE_API ResourceType Resource::ToResourceType<T>()
// Explicit template instantiation
INSTANTIATE(Texture);
INSTANTIATE(Audio);
INSTANTIATE(Material);
INSTANTIATE(ShaderVariation);
INSTANTIATE(Mesh);
INSTANTIATE(Model);
INSTANTIATE(Script);
INSTANTIATE(Animation);
INSTANTIATE(Font);