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

        if (!read_array(reader, skeleton.m_mutable_parents, header.joint_count) ||
            !read_array(reader, skeleton.m_mutable_positions, header.joint_count) ||
            !read_array(reader, skeleton.m_mutable_rotations, header.joint_count) ||
            !read_array(reader, skeleton.m_mutable_scales, header.joint_count))
        {
            return false;
        }

        if (reader.remaining != 0)
            return false;

        if (!ValidateSkeleton(skeleton, nullptr))
            return false;

        return true;
    }

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
