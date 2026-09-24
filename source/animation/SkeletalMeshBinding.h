/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==============
#include "../math/Matrix.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>
//=========================

namespace spartan
{
    struct SkeletalVertexInfluence
    {
        uint16_t bone_indices[4] = { 0, 0, 0, 0 };
        float bone_weights[4]    = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct SkeletalMeshSection
    {
        uint32_t sub_mesh_index      = 0;
        uint32_t vertex_input_offset = 0;
        uint32_t vertex_count        = 0;
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
                {
                    return false;
                }
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
                {
                    return &section;
                }
            }

            return nullptr;
        }

        std::vector<SkeletalMeshSection> m_sections;
    };
}
