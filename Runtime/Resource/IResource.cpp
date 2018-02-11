/*
Copyright(c) 2016-2018 Panos Karabelas

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
#include "../Resource/IResource.h"
#include "../Threading/Threading.h"
#include "../Graphics/Texture.h"
#include "../Font/Font.h"
#include "../Graphics/Animation.h"
#include "../Graphics/Model.h"
#include "../Graphics/Material.h"
#include "../Graphics/Mesh.h"
#include "../Graphics/DeferredShaders/ShaderVariation.h"
#include "../Audio/Audio.h"
#include "ResourceManager.h"
//======================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

string IResource::GetResourceFileName()
{
	return FileSystem::GetFileNameNoExtensionFromFilePath(m_resourceFilePath);
}

string IResource::GetResourceDirectory()
{
	return FileSystem::GetDirectoryFromFilePath(m_resourceFilePath);
}

template <typename T>
void IResource::RegisterResource()
{
	m_resourceType	= ToResourceType<T>();
	m_resourceID	= GENERATE_GUID;
	m_asyncState	= Async_Idle;
}

template <typename T>
bool IResource::IsCached()
{
	if (!m_context)
	{
		LOG_ERROR(string(typeid(T).name()) + "::IsCached(): Context is null, can't execute function");
		return weak_ptr<T>();
	}

	auto resourceManager	= m_context->GetSubsystem<ResourceManager>();
	auto resource			= resourceManager->GetResourceByName<T>(GetResourceName());
	return !resource.expired();
}

template <typename T>
weak_ptr<T> IResource::Cache()
{
	if (!m_context)
	{
		LOG_ERROR(string(typeid(T).name()) + "::Cache(): Context is null, can't execute function");
		return weak_ptr<T>();
	}

	auto resourceManager	= m_context->GetSubsystem<ResourceManager>();
	auto resource			= resourceManager->GetResourceByName<T>(GetResourceName());
	if (resource.expired())
	{
		return resourceManager->Add<T>(GetSharedPtr());
	}

	return resource;
}

void IResource::SaveToFileAsync(const string& filePath)
{
	if (!m_context)
	{
		LOG_ERROR("Resource::SaveToFileAsync(): Context is null, can't execute function");
		return;
	}

	m_context->GetSubsystem<Threading>()->AddTask([this, &filePath]()
	{
		SaveToFile(filePath);
	});
}

void IResource::LoadFromFileAsync(const string& filePath)
{
	if (!m_context)
	{
		LOG_ERROR("Resource::LoadFromFileAsync(): Context is null, can't execute function");
		return;
	}

	m_context->GetSubsystem<Threading>()->AddTask([this, &filePath]()
	{
		LoadFromFile(filePath);
	});
}

ResourceType IResource::ToResourceType()
{
	if (typeid(*this) == typeid(Texture))
		return Resource_Texture;

	if (typeid(*this) == typeid(Audio))
		return Resource_Audio;

	if (typeid(*this) == typeid(Material))
		return Resource_Material;

	if (typeid(*this) == typeid(ShaderVariation))
		return Resource_Shader;

	if (typeid(*this) == typeid(Mesh))
		return Resource_Mesh;

	if (typeid(*this) == typeid(Model))
		return Resource_Model;

	if (typeid(*this) == typeid(Animation))
		return Resource_Animation;

	if (typeid(*this) == typeid(Font))
		return Resource_Font;

	return Resource_Unknown;
}

template <typename T>
ResourceType IResource::ToResourceType()
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

	if (typeid(T) == typeid(Animation))
		return Resource_Animation;

	if (typeid(T) == typeid(Font))
		return Resource_Font;

	return Resource_Unknown;
}

// Explicit template instantiation
#define INSTANTIATE_RegisterResource(T) template ENGINE_CLASS void IResource::RegisterResource<T>()
INSTANTIATE_RegisterResource(Texture);
INSTANTIATE_RegisterResource(AudioClip);
INSTANTIATE_RegisterResource(Material);
INSTANTIATE_RegisterResource(ShaderVariation);
INSTANTIATE_RegisterResource(Mesh);
INSTANTIATE_RegisterResource(Model);
INSTANTIATE_RegisterResource(Animation);
INSTANTIATE_RegisterResource(Font);

// Explicit template instantiation
#define INSTANTIATE_ToResourceType(T) template ENGINE_CLASS ResourceType IResource::ToResourceType<T>()
INSTANTIATE_ToResourceType(Texture);
INSTANTIATE_ToResourceType(AudioClip);
INSTANTIATE_ToResourceType(Material);
INSTANTIATE_ToResourceType(ShaderVariation);
INSTANTIATE_ToResourceType(Mesh);
INSTANTIATE_ToResourceType(Model);
INSTANTIATE_ToResourceType(Animation);
INSTANTIATE_ToResourceType(Font);

// Explicit template instantiation
#define INSTANTIATE_Cache(T) template ENGINE_CLASS weak_ptr<T> IResource::Cache<T>()
INSTANTIATE_Cache(Texture);
INSTANTIATE_Cache(AudioClip);
INSTANTIATE_Cache(Material);
INSTANTIATE_Cache(ShaderVariation);
INSTANTIATE_Cache(Mesh);
INSTANTIATE_Cache(Model);
INSTANTIATE_Cache(Animation);
INSTANTIATE_Cache(Font);