/*
Copyright(c) 2015-2026 Panos Karabelas
*/

//= INCLUDES ===========================
#include "pch.h"
#include "SkeletonReader.h"
#include "AnimationAssetValidation.h"
#include "AnimationFileFormat.h"
#include "AnimationLimits.h"
#include "BinaryIO.h"
//======================================

namespace
{
    using namespace spartan::BinaryIO;
    using namespace spartan::animation_format;
    using namespace spartan::animation_limits;
}

namespace spartan
{
    bool SkeletonReader::ReadFromFile(const std::string& path, Skeleton& skeleton)
    {
        std::ifstream stream(path, std::ios::binary);
        if (!stream.is_open())
            return false;

        uint64_t file_size = 0;
        if (!try_get_file_size(stream, file_size))
            return false;

        BoundedReader reader(stream, file_size);

        SkeletonHeader header = {};
        if (!read_pod(reader, header))
        {
            return false;
        }

        if (header.magic != skeleton_magic || header.version != skeleton_version)
            return false;

        if (header.joint_count == 0 || header.joint_count > animation_limits::joint_count)
            return false;

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

        if (!read_array(reader, skeleton.parent_indices, header.joint_count) ||
            !read_array(reader, skeleton.bind_positions, header.joint_count) ||
            !read_array(reader, skeleton.bind_rotations, header.joint_count) ||
            !read_array(reader, skeleton.bind_scales, header.joint_count))
        {
            return false;
        }

        if (reader.remaining != 0)
            return false;

        if (!ValidateSkeleton(skeleton, nullptr))
            return false;

        return true;
    }
}
