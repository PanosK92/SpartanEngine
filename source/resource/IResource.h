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

        void SetResourceFilePath(const std::string& path);
        void SetResourceName(const std::string& name);
        void SetObjectName(const std::string& name);

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
        // True means CPU input was loaded; GPU preparation may still be pending.
        virtual bool LoadFromFile(const std::string& file_path) { return false; }

        // type
        template <typename T>
        static ResourceType TypeToEnum();

        ResourceState GetResourceState() const { return m_resource_state; }

    protected:
        ResourceType m_resource_type                = ResourceType::Max;
        std::atomic<ResourceState> m_resource_state = ResourceState::Max;
        uint32_t m_flags                            = 0;

    private:
        friend class ResourceCache;
        std::string m_resource_file_path;
        bool m_persistent = true;
    };
}
