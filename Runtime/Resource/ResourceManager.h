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

#pragma once

//= INCLUDES =====================
#include <memory>
#include <map>
#include "ResourceCache.h"
#include "Import/ModelImporter.h"
#include "Import/ImageImporter.h"
#include "Import/FontImporter.h"
#include "../Core/SubSystem.h"
#include "../Audio/AudioClip.h"
#include "../RHI/RHI_Texture.h"
#include "../Rendering/Model.h"
#include "../Rendering/Material.h"
//================================

namespace Directus
{
	class ENGINE_CLASS ResourceManager : public Subsystem
	{
	public:
		ResourceManager(Context* context);
		~ResourceManager() { Clear(); }

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		// Unloads all resources
		void Clear() { m_resourceCache->Clear(); }

		// Loads a resource and adds it to the resource cache
		template <class T>
		std::shared_ptr<T> Load(const std::string& filePath)
		{
			if (filePath == NOT_ASSIGNED)
			{
				LOGF_WARNING("Can't load resource of type \"%s\", filepath \"%s\" is unassigned.", typeid(T).name(), filePath.c_str());
				return nullptr;
			}

			// Try to make the path relative to the engine (in case it isn't)
			std::string filePathRelative	= FileSystem::GetRelativeFilePath(filePath);
			std::string name				= FileSystem::GetFileNameNoExtensionFromFilePath(filePathRelative);

			// Check if the resource is already loaded
			if (m_resourceCache->IsCached(name, IResource::DeduceResourceType<T>()))
			{
				return GetResourceByName<T>(name);
			}

			// Create new resource
			auto typed = std::make_shared<T>(m_context);
			// Set a default name and a default filepath in case it's not overridden by LoadFromFile()
			typed->SetResourceName(name);
			typed->SetResourceFilePath(filePathRelative);

			// Cache it now so LoadFromFile() can safely pass around a reference to the resource from the ResourceManager
			Cache<T>(typed);

			// Load
			if (!typed->LoadFromFile(filePathRelative))
			{
				LOGF_WARNING("Resource \"%s\" failed to load", filePathRelative.c_str());
				return nullptr;
			}

			// Cache it and cast it
			return typed;
		}

		// Adds a resource into the cache and returns the derived resource as a weak reference
		template <class T>
		std::shared_ptr<T> Cache(std::shared_ptr<IResource> resource)
		{
			if (!resource)
				return nullptr;

			// If the resource is already loaded, return the existing one
			if (m_resourceCache->IsCached(resource))
			{
				return GetResourceByName<T>(FileSystem::GetFileNameNoExtensionFromFilePath(resource->GetResourceFilePath()));
			}

			Cache(resource);
			return std::dynamic_pointer_cast<T>(resource);
		}

		// Adds a resource into the cache (if it's not already cached)
		void Cache(std::shared_ptr<IResource> resource)
		{
			if (!resource || m_resourceCache->IsCached(resource))
				return;

			// Add the resource
			m_resourceCache->Cache(resource);
		}

		// Returns cached resource by name
		template <class T>
		std::shared_ptr<T> GetResourceByName(const std::string& name)
		{
			return std::dynamic_pointer_cast<T>(m_resourceCache->GetByName<T>(name));
		}

		// Returns cached resource by name
		std::shared_ptr<IResource> GetResourceByName(const std::string& name, Resource_Type type)
		{
			return m_resourceCache->GetByName(name, type);
		}

		// Checks if a resource exists
		bool ExistsByName(const std::string& name, Resource_Type type)
		{
			return m_resourceCache->GetByName(name, type) != nullptr;
		}

		// Returns cached resource by path
		template <class T>
		std::shared_ptr<T> GetResourceByPath(const std::string& path)
		{
			return std::dynamic_pointer_cast<T>(m_resourceCache->GetByPath<T>(path));
		}

		// Returns cached resource by Type
		template <class T>
		std::vector<std::shared_ptr<T>> GetResourcesByType()
		{
			std::vector<std::shared_ptr<T>> typedVec;
			for (const auto& resource : m_resourceCache->GetAll())
			{
				std::shared_ptr<T> typed = std::dynamic_pointer_cast<T>(resource);
				bool validCasting = typed != nullptr;

				if (validCasting)
				{
					typedVec.emplace_back(typed);
				}
			}
			return typedVec;
		}

		std::vector<std::shared_ptr<IResource>> GetResourcesByType(Resource_Type type)
		{
			std::vector<std::shared_ptr<IResource>> vec;
			for (const auto& resource : m_resourceCache->GetByType(type))
			{
				vec.emplace_back(resource);
			}
			return vec;
		}

		// Returns all resources of a given type
		unsigned int GetResourceCountByType(Resource_Type type)
		{
			return (unsigned int)m_resourceCache->GetByType(type).size();
		}

		auto GetResourceAll() 
		{
			return m_resourceCache->GetAll();
		}

		void SaveResourcesToFiles()
		{
			m_resourceCache->SaveResourcesToFiles();
		}

		void GetResourceFilePaths(std::vector<std::string>& filePaths)
		{
			m_resourceCache->GetResourceFilePaths(filePaths);
		}

		// Memory
		unsigned int GetMemoryUsage(Resource_Type type)	{ return m_resourceCache->GetMemoryUsage(type); }
		unsigned int GetMemoryUsage()					{ return m_resourceCache->GetMemoryUsage(); }

		// Directories
		void AddStandardResourceDirectory(Resource_Type type, const std::string& directory);
		const std::string& GetStandardResourceDirectory(Resource_Type type);
		void SetProjectDirectory(const std::string& directory);
		std::string GetProjectDirectoryAbsolute();
		const std::string& GetProjectDirectory()		{ return m_projectDirectory; }	
		std::string GetProjectStandardAssetsDirectory() { return m_projectDirectory + "Standard_Assets//"; }

		// Importers
		ModelImporter* GetModelImporter()	{ return m_modelImporter.get(); }
		ImageImporter* GetImageImporter()	{ return m_imageImporter.get(); }
		FontImporter* GetFontImporter()		{ return m_fontImporter.get(); }

	private:
		std::unique_ptr<ResourceCache> m_resourceCache;
		std::map<Resource_Type, std::string> m_standardResourceDirectories;
		std::string m_projectDirectory;

		// Importers
		std::shared_ptr<ModelImporter> m_modelImporter;
		std::shared_ptr<ImageImporter> m_imageImporter;
		std::shared_ptr<FontImporter> m_fontImporter;
	};
}