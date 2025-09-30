/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "IResource.h"
#include "../Logging/Log.h"
#include <mutex>
#include "../Rendering/Material.h"
#include "../RHI/RHI_Texture.h"
//================================

namespace pugi
{
    class xml_node;
}

namespace spartan
{
    enum class ResourceDirectory
    {
        Environment,
        Fonts,
        Icons,
        ShaderCompiler,
        Shaders,
        Textures
    };

    enum class IconType
    {
        Undefined,
        Console,
        File,
        Folder,
        Model,
        World,
        Material,
        Shader,
        Xml,
        Dll,
        Txt,
        Ini,
        Exe,
        Font,
        Screenshot,
        Gear,
        Play,
        Profiler,
        ResourceCache,
        RenderDoc,
        Texture,
        Minimize,
        Maximize,
        X,
        Entity,
        Hybrid,
        Audio,
        Terrain,
        Light,
        Camera,
        Physics,
        Compressed,
        Max
    };

    class ResourceCache
    {
    public:
        static void Initialize();
        static void Shutdown();

        // default resources
         static void LoadDefaultResources();
         static void UnloadDefaultResources();

        // get by name
        static std::shared_ptr<IResource>& GetByName(const std::string& name, ResourceType type);
        template <class T>
        static std::shared_ptr<T> GetByName(const std::string& name)
        {
            return std::static_pointer_cast<T>(GetByName(name, IResource::TypeToEnum<T>()));
        }

        // get by type
        static std::vector<std::shared_ptr<IResource>> GetByType(ResourceType type = ResourceType::Max);

        // get by path
        template <class T>
        static std::shared_ptr<T> GetByPath(const std::string& path)
        {
            for (std::shared_ptr<IResource>& resource : GetResources())
            {
                if (path == resource->GetResourceFilePath())
                    return std::static_pointer_cast<T>(resource);
            }
            return nullptr;
        }

        // caches resource, or replaces with existing cached resource
        template <class T>
        static std::shared_ptr<T> Cache(const std::shared_ptr<T> resource)
        {
            if (!resource)
                return nullptr;

            if (resource->GetResourceFilePath().empty())
            {
                SP_LOG_ERROR("Resource \"%s\" has an empty file path and cannot be cached.", resource->GetObjectName().c_str());
                return nullptr;
            }

            // return cached resource if it already exists
            std::shared_ptr<T> existing = GetByPath<T>(resource->GetResourceFilePath());
            if (existing.get() != nullptr)
                return existing;

            // if not, cache it and return the cached resource
            std::lock_guard<std::mutex> guard(GetMutex());
            return std::static_pointer_cast<T>(GetResources().emplace_back(resource));
        }

        // loads a resource and adds it to the resource cache
        template <class T>
        static std::shared_ptr<T> Load(const std::string& file_path, uint32_t flags = 0)
        {
            if (!FileSystem::Exists(file_path))
            {
                SP_LOG_ERROR("\"%s\" doesn't exist.", file_path.c_str());
                return nullptr;
            }

            // return cached resource if it already exists
            const std::string name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
            std::shared_ptr<T> existing = GetByPath<T>(file_path);
            if (existing.get() != nullptr)
                return existing;

            // create new resource
            std::shared_ptr<T> resource = std::make_shared<T>();
            if (flags != 0)
            {
                resource->SetFlags(flags);
            }
            resource->SetResourceFilePath(file_path);
            resource->LoadFromFile(file_path);
            return Cache<T>(resource); // cache and return
        }

        template <class T>
        static void Remove(std::shared_ptr<T>& resource)
        {
            if (!resource)
                return;
            GetResources().erase
            (
                std::remove_if
                (
                    GetResources().begin(),
                    GetResources().end(),
                    [](std::shared_ptr<IResource> resource) { return dynamic_cast<SpartanObject*>(resource.get())->GetObjectId() == resource->GetObjectId(); }
                ),
                GetResources().end()
            );
        }

        // memory
        static uint64_t GetMemoryUsage(ResourceType type = ResourceType::Max);
        static uint32_t GetResourceCount(ResourceType type = ResourceType::Max);

        // directories
        static void AddResourceDirectory(ResourceDirectory type, const std::string& directory);
        static std::string GetResourceDirectory(ResourceDirectory type);
        static void SetProjectDirectory(const char* directory);
        static std::string GetProjectDirectoryAbsolute();
        static const char* GetProjectDirectory();
        static const char* GetDataDirectory();

        // misc
        static std::vector<std::shared_ptr<IResource>>& GetResources();
        static std::mutex& GetMutex();
        static bool GetUseRootShaderDirectory();
        static void SetUseRootShaderDirectory(const bool use_root_shader_directory);
        static RHI_Texture* GetIcon(IconType type);
    };
}
