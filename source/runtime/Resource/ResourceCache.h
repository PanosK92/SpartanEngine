/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "../Math/Vector2.h"
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
        Pause,
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
        Particle,
        Physics,
        Compressed,
        ArrowLeft,
        ArrowRight,
        ArrowUp,
        Refresh,
        Logo,
        Mcp,
        Snap,
        Max
    };

    // an icon resolved inside the gui icon atlas, texture plus the uv sub rect it occupies
    struct Icon
    {
        RHI_Texture* texture = nullptr;
        math::Vector2 uv_min = math::Vector2(0.0f, 0.0f);
        math::Vector2 uv_max = math::Vector2(1.0f, 1.0f);
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
            return std::static_pointer_cast<T>(
                GetByPathInternal(path)
            );
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

            return std::static_pointer_cast<T>(
                CacheInternal(resource)
            );
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

            // fast path, already cached
            if (std::shared_ptr<T> existing = GetByPath<T>(file_path))
                return existing;

            // serialize concurrent loads of the same path so we don't decode the same file twice
            std::lock_guard<std::mutex> in_flight_guard(GetInFlightMutex(file_path));

            // re-check after taking the per-path lock, another thread may have completed the load while we waited
            if (std::shared_ptr<T> existing = GetByPath<T>(file_path))
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
            {
                return;
            }

            RemoveInternal(resource.get());
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
        static std::recursive_mutex& GetMutex();
        static std::mutex& GetInFlightMutex(const std::string& path);
        static bool GetUseRootShaderDirectory();
        static void SetUseRootShaderDirectory(const bool use_root_shader_directory);
        static const Icon& GetIcon(IconType type);

    private:
        static std::shared_ptr<IResource> GetByPathInternal(
            const std::string& path
        );
        static std::shared_ptr<IResource> CacheInternal(
            const std::shared_ptr<IResource>& resource
        );
        static void RemoveInternal(IResource* resource);
    };
}
