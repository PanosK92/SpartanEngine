/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ========================
#include <atomic>
#include "../FileSystem/FileSystem.h"
#include "../Core/SpartanObject.h"
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
        Max,
    };

    enum class ResourceState
    {
        LoadingFromDrive,
        PreparingForGpu,
        PreparedForGpu,
        Max
    };

    class IResource : public SpartanObject
    {
    public:
        IResource(ResourceType type);
        virtual ~IResource() = default;

        void SetResourceFilePath(const std::string& path)
        {
            m_resource_file_path = FileSystem::GetRelativePath(path);
            m_object_name        = FileSystem::GetFileNameWithoutExtensionFromFilePath(m_resource_file_path);
        }
        
        ResourceType GetResourceType()            const { return m_resource_type; }
        const char* GetResourceTypeCstr()         const { return typeid(*this).name(); }
        const std::string& GetResourceFilePath()  const { return m_resource_file_path; }
        const std::string GetResourceDirectory() const { return FileSystem::GetDirectoryFromFilePath(m_resource_file_path); }

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

        // io
        virtual void SaveToFile(const std::string& file_path)   { }
        virtual void LoadFromFile(const std::string& file_path) { }

        // type
        template <typename T>
        static constexpr ResourceType TypeToEnum();

        ResourceState GetResourceState() const { return m_resource_state; }

    protected:
        ResourceType m_resource_type                = ResourceType::Max;
        std::atomic<ResourceState> m_resource_state = ResourceState::Max;
        uint32_t m_flags                            = 0;

    private:
        std::string m_resource_file_path;
    };
}
