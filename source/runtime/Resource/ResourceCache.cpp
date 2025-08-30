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

//= INCLUDES ==================
#include "pch.h"
#include "ResourceCache.h"
#include "../RHI/RHI_Texture.h"
#include <unordered_map>
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//=============================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan
{
    namespace
    {
        array<string, 6> m_standard_resource_directories;
        string m_project_directory;
        vector<shared_ptr<IResource>> m_resources;
        mutex m_mutex;
        bool use_root_shader_directory = false;
        unordered_map<IconType, shared_ptr<RHI_Texture>> m_default_icons;
    }

    void ResourceCache::Initialize()
    {
        // create project directory
        SetProjectDirectory("project\\");

        // add engine standard resource directories
        const string data_dir = GetDataDirectory() + "\\";
        AddResourceDirectory(ResourceDirectory::Environment, m_project_directory + "environment");
        AddResourceDirectory(ResourceDirectory::Fonts, data_dir + "fonts");
        AddResourceDirectory(ResourceDirectory::Icons, data_dir + "icons");
        AddResourceDirectory(ResourceDirectory::ShaderCompiler, data_dir + "shader_compiler");
        AddResourceDirectory(ResourceDirectory::Shaders, data_dir + "shaders");
        AddResourceDirectory(ResourceDirectory::Textures, data_dir + "textures");

        // subscribe to events
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldClear, SP_EVENT_HANDLER_STATIC(Shutdown));

    }

    void ResourceCache::Shutdown()
    {
        uint32_t resource_count = static_cast<uint32_t>(m_resources.size());
        m_resources.clear();
        if (resource_count != 0)
        {
            SP_LOG_INFO("%d resources have been cleared", resource_count);
        }
    }

    void ResourceCache::LoadDefaultResources()
    {
        const string data_dir = GetDataDirectory() + "\\";

        m_default_icons[IconType::Console]       = Load<RHI_Texture>(data_dir + "Icons\\console.png");
        m_default_icons[IconType::File]          = Load<RHI_Texture>(data_dir + "Icons\\file.png");
        m_default_icons[IconType::Folder]        = Load<RHI_Texture>(data_dir + "Icons\\folder.png");
        m_default_icons[IconType::Audio]         = Load<RHI_Texture>(data_dir + "Icons\\audio.png");
        m_default_icons[IconType::Model]         = Load<RHI_Texture>(data_dir + "Icons\\model.png");
        m_default_icons[IconType::World]         = Load<RHI_Texture>(data_dir + "Icons\\world.png");
        m_default_icons[IconType::Material]      = Load<RHI_Texture>(data_dir + "Icons\\material.png");
        m_default_icons[IconType::Shader]        = Load<RHI_Texture>(data_dir + "Icons\\shader.png");
        m_default_icons[IconType::Xml]           = Load<RHI_Texture>(data_dir + "Icons\\xml.png");
        m_default_icons[IconType::Dll]           = Load<RHI_Texture>(data_dir + "Icons\\dll.png");
        m_default_icons[IconType::Txt]           = Load<RHI_Texture>(data_dir + "Icons\\txt.png");
        m_default_icons[IconType::Ini]           = Load<RHI_Texture>(data_dir + "Icons\\ini.png");
        m_default_icons[IconType::Exe]           = Load<RHI_Texture>(data_dir + "Icons\\exe.png");
        m_default_icons[IconType::Font]          = Load<RHI_Texture>(data_dir + "Icons\\font.png");
        m_default_icons[IconType::Screenshot]    = Load<RHI_Texture>(data_dir + "Icons\\screenshot.png");
        m_default_icons[IconType::Gear]          = Load<RHI_Texture>(data_dir + "Icons\\gear.png");
        m_default_icons[IconType::Play]          = Load<RHI_Texture>(data_dir + "Icons\\play.png");
        m_default_icons[IconType::Profiler]      = Load<RHI_Texture>(data_dir + "Icons\\timer.png");
        m_default_icons[IconType::ResourceCache] = Load<RHI_Texture>(data_dir + "Icons\\resource_viewer.png");
        m_default_icons[IconType::RenderDoc]     = Load<RHI_Texture>(data_dir + "Icons\\capture.png");
        m_default_icons[IconType::Shader]        = Load<RHI_Texture>(data_dir + "Icons\\code.png");
        m_default_icons[IconType::Texture]       = Load<RHI_Texture>(data_dir + "Icons\\texture.png");
        m_default_icons[IconType::Minimize]      = Load<RHI_Texture>(data_dir + "Icons\\window_minimise.png");
        m_default_icons[IconType::Maximize]      = Load<RHI_Texture>(data_dir + "Icons\\window_maximise.png");
        m_default_icons[IconType::X]             = Load<RHI_Texture>(data_dir + "Icons\\window_close.png");
        m_default_icons[IconType::Hybrid]        = Load<RHI_Texture>(data_dir + "Icons\\hybrid.png");
        m_default_icons[IconType::Audio]         = Load<RHI_Texture>(data_dir + "Icons\\audio.png");
        m_default_icons[IconType::Terrain]       = Load<RHI_Texture>(data_dir + "Icons\\terrain.png");
        m_default_icons[IconType::Entity]        = Load<RHI_Texture>(data_dir + "Icons\\entity.png");
        m_default_icons[IconType::Light]         = Load<RHI_Texture>(data_dir + "Icons\\light.png");
        m_default_icons[IconType::Camera]        = Load<RHI_Texture>(data_dir + "Icons\\camera.png");
        m_default_icons[IconType::Physics]       = Load<RHI_Texture>(data_dir + "Icons\\physics.png");
        m_default_icons[IconType::Compressed]    = Load<RHI_Texture>(data_dir + "Icons\\compressed.png");
    }

    void ResourceCache::UnloadDefaultResources()
    {
        m_default_icons.clear();
    }

    shared_ptr<IResource>& ResourceCache::GetByName(const string& name, const ResourceType type)
    {
        lock_guard<mutex> guard(m_mutex);
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (name == resource->GetObjectName())
                return resource;
        }
        static shared_ptr<IResource> empty;
        return empty;
    }

    vector<shared_ptr<IResource>> ResourceCache::GetByType(const ResourceType type /*= ResourceType::Unknown*/)
    {
        lock_guard<mutex> guard(m_mutex);
        vector<shared_ptr<IResource>> resources;
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Max)
            {
                resources.emplace_back(resource);
            }
        }
        return resources;
    }

    uint64_t ResourceCache::GetMemoryUsage(ResourceType type /*= Resource_Unknown*/)
    {
        lock_guard<mutex> guard(m_mutex);
        uint64_t size = 0;
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Max)
            {
                if (SpartanObject* object = dynamic_cast<SpartanObject*>(resource.get()))
                {
                    size += object->GetObjectSize();
                }
            }
        }
        return size;
    }

    uint32_t ResourceCache::GetResourceCount(const ResourceType type)
    {
        return static_cast<uint32_t>(GetByType(type).size());
    }

    void ResourceCache::AddResourceDirectory(const ResourceDirectory type, const string& directory)
    {
        m_standard_resource_directories[static_cast<uint32_t>(type)] = directory;
    }

    string ResourceCache::GetResourceDirectory(const ResourceDirectory resource_directory_type)
    {
        string directory = m_standard_resource_directories[static_cast<uint32_t>(resource_directory_type)];
        if (use_root_shader_directory)
        {
            if (resource_directory_type == ResourceDirectory::Shaders)
            {
                directory = "..\\" + directory;
            }
        }
        return directory;
    }

    void ResourceCache::SetProjectDirectory(const string& directory)
    {
        if (!FileSystem::Exists(directory))
        {
            FileSystem::CreateDirectory_(directory);
        }
        m_project_directory = directory;
    }

    string ResourceCache::GetProjectDirectoryAbsolute()
    {
        return FileSystem::GetWorkingDirectory() + "/" + m_project_directory;
    }

    const string& ResourceCache::GetProjectDirectory()
    {
        return m_project_directory;
    }

    string ResourceCache::GetDataDirectory()
    {
        return "Data";
    }

    vector<shared_ptr<IResource>>& ResourceCache::GetResources()
    {
        return m_resources;
    }

    mutex& ResourceCache::GetMutex()
    {
        return m_mutex;
    }

    bool ResourceCache::GetUseRootShaderDirectory()
    {
        return use_root_shader_directory;
    }

    void ResourceCache::SetUseRootShaderDirectory(const bool _use_root_shader_directory)
    {
        use_root_shader_directory = _use_root_shader_directory;
    }

    RHI_Texture* ResourceCache::GetIcon(IconType type)
    {
        auto it = m_default_icons.find(type);

        if (it != m_default_icons.end())
            return it->second.get();
    
        return m_default_icons[IconType::File].get();
    }
}
