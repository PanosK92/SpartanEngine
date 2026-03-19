/*
Copyright(c) 2015-2026 Panos Karabelas
*/

//= INCLUDES ===========================
#include "pch.h"
#include "SkeletonWriter.h"
#include "AnimationAssetValidation.h"
#include "AnimationFileFormat.h"
#include "BinaryIO.h"
//======================================

namespace
{
    using namespace spartan::BinaryIO;
    using namespace spartan::animation_format;
}

namespace spartan
{
    bool SkeletonWriter::WriteToFile(const Skeleton& skeleton, const std::string& path)
    {
        if (!ValidateSkeleton(skeleton, nullptr))
            return false;

        std::ofstream stream(path, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
            return false;

        SkeletonHeader header = {};
        header.joint_count = skeleton.joint_count;

        if (!write_pod(stream, header))
            return false;

        return write_array(stream, skeleton.parent_indices)
            && write_array(stream, skeleton.bind_positions)
            && write_array(stream, skeleton.bind_rotations)
            && write_array(stream, skeleton.bind_scales);
    }
}
