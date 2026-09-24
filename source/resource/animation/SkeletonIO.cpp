/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===========================
#include "pch.h"
#include "SkeletonIO.h"
#include "AnimationAssetValidation.h"
#include "AnimationFileFormat.h"
#include "BinaryIO.h"
//======================================

//= NAMESPACES ================================
using namespace spartan::BinaryIO;
using namespace spartan::animation_format;
using namespace spartan::animation_limits;
//=============================================

namespace spartan
{
    bool SkeletonReader::ReadFromFile(const std::string& path, Skeleton& skeleton)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
        {
            return false;
        }

        uint64_t file_size = 0;
        if (!try_get_file_size(stream, file_size))
        {
            return false;
        }

        BoundedReader reader(stream, file_size);

        SkeletonHeader header = {};
        if (!read_pod(reader, header))
        {
            return false;
        }

        if (header.magic != skeleton_magic || header.version != skeleton_version)
        {
            return false;
        }

        if (header.joint_count == 0 || header.joint_count > animation_limits::joint_count)
        {
            return false;
        }

        ByteBudget budget = {};
        budget.limit = animation_limits::max_skeleton_bytes;

        if (!budget.AddArray(header.joint_count, sizeof(int16_t)) ||
            !budget.AddArray(header.joint_count, sizeof(math::Vector3)) ||
            !budget.AddArray(header.joint_count, sizeof(math::Quaternion)) ||
            !budget.AddArray(header.joint_count, sizeof(math::Vector3)))
        {
            return false;
        }

        skeleton.Allocate(header.joint_count);

        if (!read_array(reader, skeleton.m_mutable_parents, header.joint_count) ||
            !read_array(reader, skeleton.m_mutable_positions, header.joint_count) ||
            !read_array(reader, skeleton.m_mutable_rotations, header.joint_count) ||
            !read_array(reader, skeleton.m_mutable_scales, header.joint_count))
        {
            return false;
        }

        if (reader.remaining != 0)
        {
            return false;
        }

        if (!ValidateSkeleton(skeleton, nullptr))
        {
            return false;
        }

        for (uint16_t i = 0; i < skeleton.joint_count; i++)
        {
            skeleton.bind_local_matrices[i] =
                math::Matrix(
                    skeleton.bind_positions[i],
                    skeleton.bind_rotations[i],
                    skeleton.bind_scales[i]
                );
        }
        skeleton.FinalizeBindPose();

        return true;
    }

    bool SkeletonWriter::WriteToFile(const Skeleton& skeleton, const std::string& path)
    {
        if (!ValidateSkeleton(skeleton, nullptr))
        {
            return false;
        }

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            return false;
        }

        SkeletonHeader header = {};
        header.joint_count = skeleton.joint_count;

        if (!write_pod(stream, header))
        {
            return false;
        }

        return write_array(stream, skeleton.parent_indices)
            && write_array(stream, skeleton.bind_positions)
            && write_array(stream, skeleton.bind_rotations)
            && write_array(stream, skeleton.bind_scales);
    }
}
