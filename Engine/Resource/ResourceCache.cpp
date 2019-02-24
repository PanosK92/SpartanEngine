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
		AddStandardResourceDirectory(Resource_Texture,	"Standard Assets//Textures//");
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
		m_importer_image = make_shared<ImageImporter>(m_context);
		m_importer_model = make_shared<ModelImporter>(m_context);
		m_importer_font	= make_shared<FontImporter>(m_context);
		return true;
	}

	bool ResourceCache::IsCached(const string& resource_name, const Resource_Type resource_type /*= Resource_Unknown*/)
	{
		if (resource_name == NOT_ASSIGNED)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return false;
		}

		for (const auto& resource : m_resource_groups[resource_type])
		{
			if (resource_name == resource->GetResourceName())
				return true;
		}

		return false;
	}

	shared_ptr<IResource>& ResourceCache::GetByName(const string& name, const Resource_Type type)
	{
		for (auto& resource : m_resource_groups[type])
		{
			if (name == resource->GetResourceName())
				return resource;
		}

		return m_empty_resource;
	}

	vector<shared_ptr<IResource>> ResourceCache::GetByType(const Resource_Type type /*= Resource_Unknown*/)
	{
		vector<shared_ptr<IResource>> resources;

		if (type == Resource_Unknown)
		{
			for (const auto& resource_group : m_resource_groups)
			{
				resources.insert(resources.end(), resource_group.second.begin(), resource_group.second.end());
			}
		}
		else
		{
			resources = m_resource_groups[type];
		}

		return resources;
	}

	unsigned int ResourceCache::GetMemoryUsage(Resource_Type type /*= Resource_Unknown*/)
	{
		unsigned int size = 0;

		if (type = Resource_Unknown)
		{
			for (const auto& group : m_resource_groups)
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
			for (const auto& resource : m_resource_groups[type])
			{
				size += resource->GetMemoryUsage();
			}
		}

		return size;
	}

	void ResourceCache::GetResourceFilePaths(std::vector<std::string>& file_paths)
	{
		for (const auto& resource_group : m_resource_groups)
		{
			for (const auto& resource : resource_group.second)
			{
				file_paths.emplace_back(resource->GetResourceFilePath());
			}
		}
	}

	void ResourceCache::SaveResourcesToFiles()
	{
		for (const auto& resource_group : m_resource_groups)
		{
			for (const auto& resource : resource_group.second)
			{
				if (!resource->HasFilePath())
					continue;

				resource->SaveToFile(resource->GetResourceFilePath());
			}
		}
	}

	unsigned int ResourceCache::GetResourceCountByType(const Resource_Type type)
	{
		return static_cast<unsigned int>(GetByType(type).size());
	}

	void ResourceCache::AddStandardResourceDirectory(const Resource_Type type, const string& directory)
	{
		m_standard_resource_directories[type] = directory;
	}

	const string& ResourceCache::GetStandardResourceDirectory(const Resource_Type type)
	{
		for (auto& directory : m_standard_resource_directories)
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

		m_project_directory = directory;
	}

	string ResourceCache::GetProjectDirectoryAbsolute() const
	{
		return FileSystem::GetWorkingDirectory() + m_project_directory;
	}
}
