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
#include "Import/ModelImporter.h"
#include "Import/ImageImporter.h"
#include "Import/FontImporter.h"
#include "../Core/ISubsystem.h"
#include "../Audio/AudioClip.h"
#include "../RHI/RHI_Texture.h"
#include "../Rendering/Model.h"
#include "../Rendering/Material.h"
//================================

namespace Directus
{
	#define ValidateResourceType(T) static_assert(std::is_base_of<IResource, T>::value, "Provided type does not implement IResource")

	class ENGINE_CLASS ResourceCache : public ISubsystem
	{
	public:
		ResourceCache(Context* context);
		~ResourceCache();

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		//= GET BY ==================================================================================
		// NAME
		std::shared_ptr<IResource>& GetByName(const std::string& name, Resource_Type type);
		template <class T> std::shared_ptr<T> GetByName(const std::string& name) 
		{ 
			ValidateResourceType(T);
			return std::static_pointer_cast<T>(GetByName(name, IResource::DeduceResourceType<T>()));
		}

		// TYPE
		std::vector<std::shared_ptr<IResource>> GetByType(Resource_Type type = Resource_Unknown);
		// PATH
		template <class T>
		std::shared_ptr<IResource>& GetByPath(const std::string& path)
		{
			ValidateResourceType(T);

			for (auto& resource : m_resourceGroups[IResource::DeduceResourceType<T>()])
			{
				if (path == resource->GetResourceFilePath())
					return resource;
			}

			return m_emptyResource;
		}
		//===========================================================================================
	
		//= LOADING/CACHING =============================================================================================
		// Caches resource, or replaces with existing cached resource
		template <class T>
		void Cache(std::shared_ptr<T>& resource)
		{
			ValidateResourceType(T);

			if (!resource)
				return;

			// If the resource is already loaded, replace it with the existing one, then early exit
			if (IsCached(resource->GetResourceName(), resource->GetResourceType()))
			{
				resource = GetByName<T>(FileSystem::GetFileNameNoExtensionFromFilePath(resource->GetResourceFilePath()));
				return;
			}

			// Cache the resource
			lock_guard<mutex> guard(m_mutex);
			m_resourceGroups[resource->GetResourceType()].emplace_back(resource);
		}
		bool IsCached(const std::string& resourceName, Resource_Type resourceType);

		// Loads a resource and adds it to the resource cache
		template <class T>
		std::shared_ptr<T> Load(const std::string& filePath)
		{
			ValidateResourceType(T);

			if (!FileSystem::FileExists(filePath))
			{
				LOGF_ERROR("Path \"%s\" is invalid.", filePath.c_str());
				return false;
			}

			// Try to make the path relative to the engine (in case it isn't)
			std::string filePathRelative	= FileSystem::GetRelativeFilePath(filePath);
			std::string name				= FileSystem::GetFileNameNoExtensionFromFilePath(filePathRelative);

			// Check if the resource is already loaded
			if (IsCached(name, IResource::DeduceResourceType<T>()))
			{
				return GetByName<T>(name);
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
				LOGF_ERROR("Failed to load \"%s\".", filePathRelative.c_str());
				return nullptr;
			}

			// Cache it and cast it
			return typed;
		}
		//===============================================================================================================

		//= I/O =======================================================
		void GetResourceFilePaths(std::vector<std::string>& filePaths);
		void SaveResourcesToFiles();
		//=============================================================

		//= MISC ==========================================================
		// Memory
		unsigned int GetMemoryUsage(Resource_Type type = Resource_Unknown);
		// Unloads all resources
		void Clear() { m_resourceGroups.clear(); }
		// Returns all resources of a given type
		unsigned int GetResourceCountByType(Resource_Type type);
		//=================================================================

		//= DIRECTORIES ====================================================================================
		void AddStandardResourceDirectory(Resource_Type type, const std::string& directory);
		const std::string& GetStandardResourceDirectory(Resource_Type type);
		void SetProjectDirectory(const std::string& directory);
		std::string GetProjectDirectoryAbsolute();
		const std::string& GetProjectDirectory()		{ return m_projectDirectory; }	
		std::string GetProjectStandardAssetsDirectory() { return m_projectDirectory + "Standard_Assets//"; }
		//==================================================================================================

		// Importers
		ModelImporter* GetModelImporter()	{ return m_modelImporter.get(); }
		ImageImporter* GetImageImporter()	{ return m_imageImporter.get(); }
		FontImporter* GetFontImporter()		{ return m_fontImporter.get(); }

	private:
		// Cache
		std::map<Resource_Type, std::vector<std::shared_ptr<IResource>>> m_resourceGroups;
		std::mutex m_mutex;

		// Directories
		std::map<Resource_Type, std::string> m_standardResourceDirectories;
		std::string m_projectDirectory;

		// Importers
		std::shared_ptr<ModelImporter> m_modelImporter;
		std::shared_ptr<ImageImporter> m_imageImporter;
		std::shared_ptr<FontImporter> m_fontImporter;

		std::shared_ptr<IResource> m_emptyResource = nullptr;
	};
}