/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ========================
#include <atomic>
#include <functional>
#include <memory>
#include "../file_system/FileSystem.h"
#include "../core/SpartanObject.h"
//===================================

namespace spartan
{
    enum class ResourceType
    {
        Unknown,
        Texture,
        Audio,
        Material,
        Mesh,
        Cubemap,
        Animation,
        Font,
        Shader,
        Max
    };

    enum class ResourceState
    {
        LoadingFromDrive,
        PreparingForGpu,
        PreparedForGpu,
        Max
    };

    class IResource : public SpartanObject, public std::enable_shared_from_this<IResource>
    {
    public:
        IResource(ResourceType type);
        virtual ~IResource() = default;

        void SetResourceFilePath(const std::string& path)
        {
            m_resource_file_path = FileSystem::GetRelativePath(path);
            m_object_name        = FileSystem::GetFileNameWithoutExtensionFromFilePath(m_resource_file_path);
        }

        void SetResourceName(const std::string& name)
        {
            const std::string file_name = FileSystem::GetFileNameFromFilePath(name);
            std::string object_name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_name);
            if (object_name.empty())
            {
                object_name = file_name;
            }
            m_object_name = object_name;

            // runtime resources keep a name only, do not invent a file under the texture library
            const std::string directory = FileSystem::GetDirectoryFromFilePath(m_resource_file_path);
            if (directory.empty())
            {
                return;
            }

            std::string extension = FileSystem::GetExtensionFromFilePath(file_name);
            if (extension.empty())
            {
                extension = FileSystem::GetExtensionFromFilePath(m_resource_file_path);
            }
            m_resource_file_path = directory + object_name + extension;
        }
        
        ResourceType GetResourceType()           const { return m_resource_type; }
        const char* GetResourceTypeCstr()        const
        {
            switch (m_resource_type)
            {
            case ResourceType::Texture:   return "texture";
            case ResourceType::Audio:     return "audio";
            case ResourceType::Material:  return "material";
            case ResourceType::Mesh:      return "mesh";
            case ResourceType::Cubemap:   return "cubemap";
            case ResourceType::Animation: return "animation";
            case ResourceType::Font:      return "font";
            case ResourceType::Shader:    return "shader";
            default:                      return "unknown";
            }
        }
        const std::string& GetResourceFilePath() const { return m_resource_file_path; }
        std::string GetResourceDirectory()       const { return FileSystem::GetDirectoryFromFilePath(m_resource_file_path); }

        // flags
        void SetFlag(const uint32_t flag, bool enabled = true)
        {
            if (enabled)
            {
                m_flags |= flag;
            }
            else
            {
                m_flags &= ~flag;
            }
        }
        uint32_t GetFlags()           const { return m_flags; }
        void SetFlags(const uint32_t flags) { m_flags = flags; }

        // runtime generated resources are rebuilt by code on load, world saves skip them
        void SetPersistent(const bool persistent) { m_persistent = persistent; }
        bool IsPersistent() const                 { return m_persistent; }

        // io
        // Capture on the owner thread; the returned task owns everything it writes.
        virtual std::function<void()> CreateSaveTask(const std::string& file_path) { return {}; }
        virtual void SaveToFile(const std::string& file_path)   { }
        virtual void LoadFromFile(const std::string& file_path) { }

        // type
        template <typename T>
        static ResourceType TypeToEnum();

        ResourceState GetResourceState() const { return m_resource_state; }

    protected:
        ResourceType m_resource_type                = ResourceType::Max;
        std::atomic<ResourceState> m_resource_state = ResourceState::Max;
        uint32_t m_flags                            = 0;

    private:
        std::string m_resource_file_path;
        bool m_persistent = true;
    };
}
