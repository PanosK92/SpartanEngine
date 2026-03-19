/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:

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

//= INCLUDES ====================
#include "../../Math/Matrix.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>
//===============================

namespace spartan
{
    struct SkeletalVertexInfluence
    {
        uint16_t bone_indices[4] = { 0, 0, 0, 0 };
        float bone_weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct SkeletalMeshSection
    {
        uint32_t sub_mesh_index = 0;
        uint32_t vertex_input_offset = 0;
        uint32_t vertex_count = 0;
        std::vector<uint16_t> palette_bone_indices;
        std::vector<SkeletalVertexInfluence> influences;
        uint32_t gpu_influence_offset = std::numeric_limits<uint32_t>::max();
        std::vector<math::Matrix> inverse_bind_matrices;

        bool IsValid() const
        {
            return influences.size() == vertex_count;
        }
    };

    struct SkeletalMeshBinding
    {
        void Clear()
        {
            m_sections.clear();
        }

        bool IsValid() const
        {
            for (const SkeletalMeshSection& section : m_sections)
            {
                if (!section.IsValid())
                    return false;
            }

            return true;
        }

        SkeletalMeshSection* GetSectionBySubMeshIndex(const uint32_t sub_mesh_index)
        {
            return GetSectionBySubMeshIndexImpl(m_sections, sub_mesh_index);
        }

        const SkeletalMeshSection* GetSectionBySubMeshIndex(const uint32_t sub_mesh_index) const
        {
            return GetSectionBySubMeshIndexImpl(m_sections, sub_mesh_index);
        }

        void AddSection(SkeletalMeshSection section)
        {
            m_sections.push_back(std::move(section));
        }

        const std::vector<SkeletalMeshSection>& GetSections() const { return m_sections; }

    private:
        template <typename TSections>
        static auto GetSectionBySubMeshIndexImpl(TSections& sections, const uint32_t sub_mesh_index)
            -> std::conditional_t<
                std::is_const_v<std::remove_reference_t<TSections>>,
                const SkeletalMeshSection*,
                SkeletalMeshSection*>
        {
            for (auto& section : sections)
            {
                if (section.sub_mesh_index == sub_mesh_index)
                    return &section;
            }

            return nullptr;
        }

        std::vector<SkeletalMeshSection> m_sections;
    };
}
