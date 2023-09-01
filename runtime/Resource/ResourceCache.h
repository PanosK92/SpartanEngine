/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ===============
#include <algorithm>
#include "IResource.h"
#include "ProgressTracker.h"
//==========================

namespace Spartan
{
    class FontImporter;

    enum class ResourceDirectory
    {
        Environment,
        Fonts,
        Icons,
        ShaderCompiler,
        Shaders,
        Textures
    };

    class SP_CLASS ResourceCache
    {
    public:
        static void Initialize();
        static void Shutdown();

        // Get by name
        static std::shared_ptr<IResource>& GetByName(const std::string& name, ResourceType type);
        template <class T> 
        static std::shared_ptr<T> GetByName(const std::string& name) 
        { 
            return std::static_pointer_cast<T>(GetByName(name, IResource::TypeToEnum<T>()));
        }

        // Get by type
        static std::vector<std::shared_ptr<IResource>> GetByType(ResourceType type = ResourceType::Unknown);

        // Get by path
        template <class T>
        static std::shared_ptr<T> GetByPath(const std::string& path)
        {
            for (std::shared_ptr<IResource>& resource : GetResources())
            {
                if (path == resource->GetResourceFilePathNative())
                    return std::static_pointer_cast<T>(resource);
            }

            return nullptr;
        }

        // Caches resource, or replaces with existing cached resource
        template <class T>
        static std::shared_ptr<T> Cache(const std::shared_ptr<T> resource)
        {
            // Validate resource
            if (!resource)
                return nullptr;

            // Validate resource file path
            if (!resource->HasFilePathNative() && !FileSystem::IsDirectory(resource->GetResourceFilePathNative()))
            {
                SP_LOG_ERROR("A resource must have a valid file path in order to be cached");
                return nullptr;
            }

            // Validate resource file path
            if (!FileSystem::IsEngineFile(resource->GetResourceFilePathNative()))
            {
                SP_LOG_ERROR("A resource must have a native file format in order to be cached, provide format was %s", FileSystem::GetExtensionFromFilePath(resource->GetResourceFilePathNative()).c_str());
                return nullptr;
            }

            // Ensure that this resource is not already cached
            if (IsCached(resource->GetObjectName(), resource->GetResourceType()))
                return GetByName<T>(resource->GetObjectName());

            std::lock_guard<std::mutex> guard(GetMutex());

            // In order to guarantee deserialization, we save it now
            resource->SaveToFile(resource->GetResourceFilePathNative());

            // Cache it
            return std::static_pointer_cast<T>(GetResources().emplace_back(resource));
        }

        // Loads a resource and adds it to the resource cache
        template <class T>
        static std::shared_ptr<T> Load(const std::string& file_path, uint32_t flags = 0)
        {
            if (!FileSystem::Exists(file_path))
            {
                SP_LOG_ERROR("\"%s\" doesn't exist.", file_path.c_str());
                return nullptr;
            }

            // Check if the resource is already loaded
            const std::string name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
            if (IsCached(name, IResource::TypeToEnum<T>()))
                return GetByName<T>(name);

            // Create new resource
            std::shared_ptr<T> resource = std::make_shared<T>();

            if (flags != 0)
            {
                resource->SetFlags(flags);
            }

            // Set a default file path in case it's not overridden by LoadFromFile()
            resource->SetResourceFilePath(file_path);

            // Load
            if (!resource || !resource->LoadFromFile(file_path))
            {
                SP_LOG_ERROR("Failed to load \"%s\".", file_path.c_str());
                return nullptr;
            }

            // Returned cached reference which is guaranteed to be around after deserialization
            return Cache<T>(resource);
        }

        template <class T>
        static void Remove(std::shared_ptr<T>& resource)
        {
            if (!resource)
                return;

            if (!IsCached(resource->GetObjectName(), resource->GetResourceType()))
                return;

            GetResources().erase
            (
                std::remove_if
                (
                    GetResources().begin(),
                    GetResources().end(),
                    [](std::shared_ptr<IResource> resource) { return dynamic_cast<SP_Object*>(resource.get())->GetObjectId() == resource->GetObjectId(); }
                ),
                GetResources().end()
            );
        }

        // Memory
        static uint64_t GetMemoryUsageCpu(ResourceType type = ResourceType::Unknown);
        static uint64_t GetMemoryUsageGpu(ResourceType type = ResourceType::Unknown);
        static uint32_t GetResourceCount(ResourceType type = ResourceType::Unknown);

        // Directories
        static void AddResourceDirectory(ResourceDirectory type, const std::string& directory);
        static std::string GetResourceDirectory(ResourceDirectory type);
        static void SetProjectDirectory(const std::string& directory);
        static std::string GetProjectDirectoryAbsolute();
        static const std::string& GetProjectDirectory();
        static std::string GetDataDirectory();

        // Misc
        static std::vector<std::shared_ptr<IResource>>& GetResources();
        static std::mutex& GetMutex();

    private:
        static bool IsCached(const uint64_t resource_id);
        static bool IsCached(const std::string& resource_name, const ResourceType resource_type);

        // Event handlers
        static void SaveResourcesToFiles();
        static void LoadResourcesFromFiles();
    };
}
