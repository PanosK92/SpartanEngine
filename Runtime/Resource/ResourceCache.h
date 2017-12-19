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

//= INCLUDES ==============
#include <vector>
#include <memory>
#include "Resource.h"
#include "../Logging/Log.h"
//========================

namespace Directus
{
	class ENGINE_API ResourceCache
	{
	public:
		ResourceCache() {}
		~ResourceCache() { Clear(); }

		// Adds a resource
		void Add(std::shared_ptr<Resource> resource)
		{
			if (!resource)
				return;

			m_resourceGroups[resource->GetResourceType()].push_back(resource);
		}

		// Returns the file paths of all the resources
		void GetResourceFilePaths(std::vector<std::string>& filePaths)
		{
			for (const auto& resourceGroup : m_resourceGroups)
			{
				for (const auto& resource : resourceGroup.second)
				{
					filePaths.push_back(resource->GetResourceFilePath());
				}
			}
		}

		// Returns a resource by name
		std::shared_ptr<Resource> GetByName(const std::string& name)
		{
			for (const auto& resourceGroup : m_resourceGroups)
			{
				for (const auto& resource : resourceGroup.second)
				{
					if (resource->GetResourceName() == name)
						return resource;
				}
			}
			return std::shared_ptr<Resource>();
		}

		// Makes the resources save their metadata
		void SaveResourcesToFiles()
		{
			for (const auto& resourceGroup : m_resourceGroups)
			{
				for (const auto& resource : resourceGroup.second)
				{
					resource->SaveToFile(resource->GetResourceFilePath());
				}
			}
		}

		// Returns all the resources
		std::vector<std::shared_ptr<Resource>> GetAll()
		{
			std::vector<std::shared_ptr<Resource>> resources;
			for (const auto& resourceGroup : m_resourceGroups)
			{
				resources.insert(resources.end(), resourceGroup.second.begin(), resourceGroup.second.end());
			}

			return resources;
		}

		// Checks whether a resource is already cached
		bool IsCached(const std::string& filePath)
		{
			if (filePath == NOT_ASSIGNED)
			{
				LOG_WARNING("ResourceCache:IsCached: Can't check if resource \"" + filePath + "\" is cached as the filepath is unassigned.");
				return false;
			}

			ResourceType type = GetResourceTypeFromFilePath(filePath);
			if (type == Resource_Unknown)
			{
				LOG_WARNING("ResourceCache:IsCached: Unable to determine resource type from path \"" + filePath + "\".");
				return false;
			}

			std::vector<std::shared_ptr<Resource>>& vector = m_resourceGroups[type];
			std::string name = FileSystem::GetFileNameNoExtensionFromFilePath(filePath);
			for (const auto& resource : vector)
			{
				if (resource->GetResourceName() == name)
				{
					return true;
				}
			}

			return false;
		}

		unsigned int GetMemoryUsageKB(ResourceType type)
		{
			unsigned int sizeKB = 0;
			for (const auto& resource : m_resourceGroups[type])
			{
				sizeKB += resource->GetMemoryUsageKB();
			}

			return sizeKB;
		}

		// Returns all resources of a given type
		const std::vector<std::shared_ptr<Resource>>& GetByType(ResourceType type) { return m_resourceGroups[type]; }
		// Unloads all resources
		void Clear() { m_resourceGroups.clear(); }

	private:
		ResourceType GetResourceTypeFromFilePath(const std::string& filePath)
		{
			ResourceType type = Resource_Unknown;
			if (FileSystem::IsSupportedImageFile(filePath) || FileSystem::IsEngineTextureFile(filePath))
			{
				type = Resource_Texture;
			}
			else if (FileSystem::IsEngineMeshFile(filePath))
			{
				type = Resource_Mesh;
			}
			else if (FileSystem::IsSupportedModelFile(filePath) || FileSystem::IsEngineModelFile(filePath))
			{
				type = Resource_Model;
			}
			else if (FileSystem::IsSupportedAudioFile(filePath)) // engine doesn't use custom audio files
			{
				type = Resource_Audio;
			}				
			else if (FileSystem::IsSupportedFontFile(filePath)) // engine doesn't use custom font files
			{
				type = Resource_Font;
			}
			else if (FileSystem::IsSupportedShaderFile(filePath) || FileSystem::IsEngineShaderFile(filePath))
			{
				type = Resource_Shader;
			}
			else if (FileSystem::IsEngineMaterialFile(filePath))
			{
				type = Resource_Material;
			}
			else if (FileSystem::IsEngineScriptFile(filePath))
			{
				type = Resource_Script;
			}

			return type;
		}

		std::map<ResourceType, std::vector<std::shared_ptr<Resource>>> m_resourceGroups;
	};
}