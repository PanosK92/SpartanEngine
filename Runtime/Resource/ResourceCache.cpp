/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ===================
#include "ResourceCache.h"
#include "../World/Entity.h"
#include "../Core/EventSystem.h"
//==============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	ResourceCache::ResourceCache(Context* context) : ISubsystem(context)
	{
		// Add engine standard resource directories
		AddStandardResourceDirectory(Resource_Texture, "Standard Assets//Textures//");
		AddStandardResourceDirectory(Resource_Font,		"Standard Assets//Fonts//");
		AddStandardResourceDirectory(Resource_Shader,	"Standard Assets//Shaders//");
		AddStandardResourceDirectory(Resource_Cubemap,	"Standard Assets//Cubemaps//");
		AddStandardResourceDirectory(Resource_Script,	"Standard Assets//Scripts//");
		AddStandardResourceDirectory(Resource_Model,	"Standard Assets//Models//");
		AddStandardResourceDirectory(Resource_Material, "Standard Assets//Materials//");

		// Add project directory
		SetProjectDirectory("Project//");

		// Subscribe to events
		SUBSCRIBE_TO_EVENT(Event_World_Unload, EVENT_HANDLER(Clear));
	}

	ResourceCache::~ResourceCache()
	{
		// Unsubscribe from event
		UNSUBSCRIBE_FROM_EVENT(Event_World_Unload, EVENT_HANDLER(Clear));
		Clear();
	}

	bool ResourceCache::Initialize()
	{
		// Importers
		m_imageImporter = make_shared<ImageImporter>(m_context);
		m_modelImporter = make_shared<ModelImporter>(m_context);
		m_fontImporter	= make_shared<FontImporter>(m_context);
		return true;
	}

	bool ResourceCache::IsCached(const string& resourceName, Resource_Type resourceType /*= Resource_Unknown*/)
	{
		if (resourceName == NOT_ASSIGNED)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		for (const auto& resource : m_resourceGroups[resourceType])
		{
			if (resourceName == resource->GetResourceName())
				return true;
		}

		return false;
	}

	shared_ptr<IResource>& ResourceCache::GetByName(const string& name, Resource_Type type)
	{
		for (auto& resource : m_resourceGroups[type])
		{
			if (name == resource->GetResourceName())
				return resource;
		}

		return m_emptyResource;
	}

	vector<shared_ptr<IResource>> ResourceCache::GetByType(Resource_Type type /*= Resource_Unknown*/)
	{
		vector<shared_ptr<IResource>> resources;

		if (type == Resource_Unknown)
		{
			for (const auto& resourceGroup : m_resourceGroups)
			{
				resources.insert(resources.end(), resourceGroup.second.begin(), resourceGroup.second.end());
			}
		}
		else
		{
			resources = m_resourceGroups[type];
		}

		return resources;
	}

	unsigned int ResourceCache::GetMemoryUsage(Resource_Type type /*= Resource_Unknown*/)
	{
		unsigned int size = 0;

		if (type = Resource_Unknown)
		{
			for (const auto& group : m_resourceGroups)
			{
				for (const auto& resource : group.second)
				{
					if (!resource)
						continue;

					size += resource->GetMemoryUsage();
				}
			}
		}
		else
		{
			for (const auto& resource : m_resourceGroups[type])
			{
				size += resource->GetMemoryUsage();
			}
		}

		return size;
	}

	void ResourceCache::GetResourceFilePaths(std::vector<std::string>& filePaths)
	{
		for (const auto& resourceGroup : m_resourceGroups)
		{
			for (const auto& resource : resourceGroup.second)
			{
				filePaths.emplace_back(resource->GetResourceFilePath());
			}
		}
	}

	void ResourceCache::SaveResourcesToFiles()
	{
		for (const auto& resourceGroup : m_resourceGroups)
		{
			for (const auto& resource : resourceGroup.second)
			{
				if (!resource->HasFilePath())
					continue;

				resource->SaveToFile(resource->GetResourceFilePath());
			}
		}
	}

	unsigned int ResourceCache::GetResourceCountByType(Resource_Type type)
	{
		return (unsigned int)GetByType(type).size();
	}

	void ResourceCache::AddStandardResourceDirectory(Resource_Type type, const string& directory)
	{
		m_standardResourceDirectories[type] = directory;
	}

	const string& ResourceCache::GetStandardResourceDirectory(Resource_Type type)
	{
		for (auto& directory : m_standardResourceDirectories)
		{
			if (directory.first == type)
				return directory.second;
		}

		return NOT_ASSIGNED;
	}

	void ResourceCache::SetProjectDirectory(const string& directory)
	{
		if (!FileSystem::DirectoryExists(directory))
		{
			FileSystem::CreateDirectory_(directory);
		}

		m_projectDirectory = directory;
	}

	string ResourceCache::GetProjectDirectoryAbsolute()
	{
		return FileSystem::GetWorkingDirectory() + m_projectDirectory;
	}
}
